/** @file configuration.cpp
 * @brief 以有界结构校验和稳定 EntryPath 比较构建配置预览，保持与内核和 Qt 隔离。
 */
#include <cordis/loader/configuration.hpp>
#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace cordis::loader {
namespace {
/** @brief 标识只允许稳定 ASCII token，排除分隔符及 .、.. 路径歧义。 */
bool identifier(const std::string& value)
{
    if (value.empty() || value.size() > 256 || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    });
}
/** @brief 结构错误始终附带阶段，条目错误另附规范路径。 */
Result<DocumentCandidate> invalid(std::string message, const std::string& path = {})
{
    auto error = makeError(ErrorCode::ConfigInvalid, std::move(message));
    error.phase = "loader.structure";
    if (!path.empty()) error.entryPath = path;
    return Result<DocumentCandidate>::failure(std::move(error));
}
/** @brief 先检查剩余预算再相加，避免整数溢出使大小限额失效。 */
bool charge(std::size_t amount, std::size_t& used, std::size_t maximum)
{
    if (amount > maximum - used) return false;
    used += amount;
    return true;
}
/** @brief 用显式栈遍历配置树，校验器不沿输入递归调用。 */
struct PendingEntry {
    const EntrySpec* source;
    std::string parent;
    std::size_t depth;
    bool parentDisabled;
};
/** @brief 依赖已规范化排序后比较其业务字段，不受输入排列影响。 */
bool sameInject(const std::vector<InjectSpec>& left, const std::vector<InjectSpec>& right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (std::tie(a.name, a.major, a.minimumMinor, a.capabilities) !=
            std::tie(b.name, b.major, b.minimumMinor, b.capabilities)) return false;
    }
    return true;
}
/** @brief 类型、模块、realm 或依赖变化要求重建；普通 config 字符串变化只要求重启。 */
bool sameDefinition(const EntryValue& left, const EntryValue& right)
{
    return left.plugin == right.plugin && left.module == right.module && left.realms == right.realms &&
           sameInject(left.inject, right.inject);
}
/** @brief 模块 manifest 变化保守地使其条目重建；builtin 不依赖 manifest。 */
bool sameManifest(const DocumentCandidate& base, const DocumentCandidate& candidate, const std::string& module)
{
    return module == "builtin" || base.modules().at(module) == candidate.modules().at(module);
}
/** @brief 仅检查由斜杠划分的真实祖先，避免 a 和 ab 被误判为同一子树。 */
bool ancestorRebuilt(const std::set<std::string>& rebuilt, const std::string& path)
{
    auto slash = path.find('/');
    while (slash != std::string::npos) {
        if (rebuilt.count(path.substr(0, slash))) return true;
        slash = path.find('/', slash + 1);
    }
    return false;
}
} // namespace

/** @brief 所有检查先于候选返回；任何失败都不产生可使用的部分候选。 */
Result<DocumentCandidate> validateDocument(const Document& document, ConfigurationLimits limits)
{
    if (document.formatVersion != 1) return invalid("unsupported formatVersion");
    if (!limits.maximumEntries || !limits.maximumDepth || limits.maximumDepth > 128 || !limits.maximumBytes)
        return invalid("invalid configuration limits (maximumDepth must be 1..128)");
    if (document.modules.size() > limits.maximumModules || document.plugins.size() > limits.maximumEntries)
        return invalid("configuration count limit exceeded");
    DocumentCandidate result;
    std::size_t bytes = 0;
    for (const auto& module : document.modules) {
        if (!identifier(module.first) || module.first == "builtin" || module.second.empty() ||
            module.second.size() > 4096 || module.second.find('\0') != std::string::npos)
            return invalid("invalid module declaration");
        if (!charge(module.first.size(), bytes, limits.maximumBytes) ||
            !charge(module.second.size(), bytes, limits.maximumBytes)) return invalid("configuration byte limit exceeded");
        result.modules_.emplace(module);
    }
    std::vector<PendingEntry> pending;
    for (const auto& entry : document.plugins) pending.push_back({&entry, {}, 1, false});
    std::size_t count = 0;
    while (!pending.empty()) {
        auto next = std::move(pending.back());
        pending.pop_back();
        const auto& entry = *next.source;
        if (!identifier(entry.id)) return invalid("invalid entry id", next.parent);
        const auto path = next.parent.empty() ? entry.id : next.parent + "/" + entry.id;
        if (next.depth > limits.maximumDepth || count >= limits.maximumEntries)
            return invalid("configuration tree limit exceeded", path);
        ++count;
        if (!identifier(entry.plugin) || !identifier(entry.module)) return invalid("invalid plugin or module id", path);
        if (entry.module != "builtin" && !result.modules_.count(entry.module)) return invalid("unknown module", path);
        if (entry.inject.size() > limits.maximumInjects) return invalid("inject limit exceeded", path);
        for (const auto* text : {&entry.id, &entry.plugin, &entry.module, &entry.config})
            if (!charge(text->size(), bytes, limits.maximumBytes)) return invalid("configuration byte limit exceeded", path);
        for (const auto& realm : entry.realms) {
            if (!identifier(realm.first) || !identifier(realm.second)) return invalid("invalid realm mapping", path);
            if (!charge(realm.first.size(), bytes, limits.maximumBytes) ||
                !charge(realm.second.size(), bytes, limits.maximumBytes)) return invalid("configuration byte limit exceeded", path);
        }
        for (const auto& inject : entry.inject) {
            if (!identifier(inject.name) || inject.major == 0 || inject.capabilities.size() > limits.maximumCapabilities)
                return invalid("invalid inject declaration", path);
            if (!charge(inject.name.size(), bytes, limits.maximumBytes)) return invalid("configuration byte limit exceeded", path);
            for (const auto& capability : inject.capabilities) {
                if (!identifier(capability)) return invalid("invalid capability", path);
                if (!charge(capability.size(), bytes, limits.maximumBytes)) return invalid("configuration byte limit exceeded", path);
            }
        }
        EntryValue value{entry.plugin, entry.module, entry.disabled, next.parentDisabled || entry.disabled,
                         entry.config, entry.realms, entry.inject};
        for (auto& inject : value.inject) {
            std::sort(inject.capabilities.begin(), inject.capabilities.end());
            inject.capabilities.erase(std::unique(inject.capabilities.begin(), inject.capabilities.end()), inject.capabilities.end());
        }
        std::sort(value.inject.begin(), value.inject.end(), [](const InjectSpec& left, const InjectSpec& right) {
            return std::tie(left.name, left.major) < std::tie(right.name, right.major);
        });
        for (std::size_t i = 1; i < value.inject.size(); ++i)
            if (value.inject[i - 1].name == value.inject[i].name && value.inject[i - 1].major == value.inject[i].major)
                return invalid("duplicate inject key", path);
        if (!result.entries_.emplace(path, std::move(value)).second) return invalid("duplicate entry path", path);
        // 将已处理、已排队和待加入条目一起计入预算，避免先分配一个超大工作栈。
        const auto remaining = limits.maximumEntries - count;
        if (pending.size() > remaining || entry.children.size() > remaining - pending.size())
            return invalid("configuration entry limit exceeded", path);
        for (const auto& child : entry.children)
            pending.push_back({&child, path, next.depth + 1, next.parentDisabled || entry.disabled});
    }
    return Result<DocumentCandidate>::success(std::move(result));
}

/** @brief 预览只比较目标值；执行排序、依赖闭包、运行差异及 CAS 留给真正的执行计划。 */
PlanPreview previewChanges(const DocumentCandidate& base, const DocumentCandidate& candidate, std::uint64_t revision)
{
    PlanPreview preview;
    preview.baseDesiredRevision = revision;
    preview.modulesChanged = base.modules() != candidate.modules();
    preview.desiredChanged = preview.modulesChanged;
    std::set<std::string> paths;
    for (const auto& entry : base.entries()) paths.insert(entry.first);
    for (const auto& entry : candidate.entries()) paths.insert(entry.first);
    std::set<std::string> rebuilt;
    for (const auto& path : paths) {
        auto old = base.entries().find(path);
        auto next = candidate.entries().find(path);
        const bool had = old != base.entries().end();
        const bool has = next != candidate.entries().end();
        const bool wasEnabled = had && !old->second.effectiveDisabled;
        const bool nowEnabled = has && !next->second.effectiveDisabled;
        const bool same = had && has && sameDefinition(old->second, next->second) &&
                    sameManifest(base, candidate, next->second.module);
        const bool changed = !had || !has || !same || old->second.disabled != next->second.disabled ||
                             old->second.effectiveDisabled != next->second.effectiveDisabled || old->second.config != next->second.config;
        auto kind = ChangeKind::NoOp;
        if (wasEnabled && !has) kind = ChangeKind::Remove;
        else if (wasEnabled && !nowEnabled) kind = ChangeKind::Disable;
        else if (!wasEnabled && nowEnabled) kind = ChangeKind::Add;
        else if (wasEnabled && nowEnabled) {
            if (!same || ancestorRebuilt(rebuilt, path)) kind = ChangeKind::Recreate;
            else if (old->second.config != next->second.config) kind = ChangeKind::Restart;
        }
        if (kind == ChangeKind::Restart || kind == ChangeKind::Recreate) rebuilt.insert(path);
        preview.desiredChanged = preview.desiredChanged || changed;
        preview.entries.push_back({path, kind, changed});
    }
    return preview;
}
} // namespace cordis::loader
