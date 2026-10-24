/** @file configuration.hpp
 * @brief 提供与文本解析器无关的配置结构校验及无副作用差异预览。
 */
#pragma once
#include <cordis/result.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cordis::loader {
/** @brief 配置中附加或收紧的依赖声明；接口类型由后续插件定义校验确定。 */
struct InjectSpec {
    std::string name;
    std::uint32_t major = 1;
    std::uint32_t minimumMinor = 0;
    std::vector<std::string> capabilities;
};
/** @brief 一条配置及其显式子树；config 暂按 Runtime 的不透明字符串协议保存。 */
struct EntrySpec {
    std::string id;
    std::string plugin;
    std::string module = "builtin";
    bool disabled = false;
    std::string config;
    std::map<std::string, std::string> realms;
    std::vector<InjectSpec> inject;
    std::vector<EntrySpec> children;
};
/** @brief 文本解析器输出或宿主构造的结构；此类型不表示结构、schema 或插件校验已经通过。 */
struct Document {
    std::uint32_t formatVersion = 1;
    std::map<std::string, std::string> modules;
    std::vector<EntrySpec> plugins;
};
/** @brief 结构模型的资源预算；原始文本大小、YAML 别名和 tag 另由解析器限制。 */
struct ConfigurationLimits {
    std::size_t maximumEntries = 2048;
    std::size_t maximumDepth = 32;
    std::size_t maximumModules = 256;
    std::size_t maximumBytes = 1024 * 1024;
    std::size_t maximumInjects = 128;
    std::size_t maximumCapabilities = 64;
};
/** @brief 扁平化条目的宿主值，不包含 Runtime、实例、回调或解析器节点。 */
struct EntryValue {
    std::string plugin;
    std::string module;
    bool disabled = false;
    bool effectiveDisabled = false;
    std::string config;
    std::map<std::string, std::string> realms;
    std::vector<InjectSpec> inject;
};
/** @brief 仅结构校验通过的候选；尚未完成插件定义、schema、路径或模块产物校验。 */
class DocumentCandidate {
public:
    /** @brief 复制完整候选值，不与来源文档共享可变节点。 */
    DocumentCandidate(const DocumentCandidate&) = default;
    /** @brief 转移完整候选的所有值。 */
    DocumentCandidate(DocumentCandidate&&) noexcept = default;
    /** @brief 禁止部分成员赋值失败后破坏已校验候选的不变量。 */
    DocumentCandidate& operator=(const DocumentCandidate&) = delete;
    /** @brief 候选发布后不原地替换，新的文本产生新的候选值。 */
    DocumentCandidate& operator=(DocumentCandidate&&) = delete;
    /** @brief 读取声明模块与 manifest 字符串；builtin 是隐含模块，不在映射中。 */
    const std::map<std::string, std::string>& modules() const noexcept { return modules_; }
    /** @brief 按规范 EntryPath 排序读取条目；路径由不含斜杠的稳定 id 组成。 */
    const std::map<std::string, EntryValue>& entries() const noexcept { return entries_; }
private:
    friend Result<DocumentCandidate> validateDocument(const Document&, ConfigurationLimits);
    /** @brief 禁止绕过结构校验直接构造候选；空文档也经过同一入口。 */
    DocumentCandidate() = default;
    std::map<std::string, std::string> modules_;
    std::map<std::string, EntryValue> entries_;
};
/** @brief 校验版本、标识、模块引用、兄弟 id、依赖及限额；不读文件、不调用插件。 */
Result<DocumentCandidate> validateDocument(const Document& document, ConfigurationLimits limits = {});
/** @brief 差异预览的运行意图；NoOp 仍可能包含禁用条目的目标数据变化。 */
enum class ChangeKind { NoOp, Add, Remove, Disable, Restart, Recreate };
/** @brief 预览中的独立条目行；desiredChanged 区分目标变化与祖先重建带来的动作。 */
struct EntryChange {
    std::string path;
    ChangeKind kind = ChangeKind::NoOp;
    bool desiredChanged = false;
};
/** @brief 两份目标结构的预览，不是可以直接执行的计划或真实运行状态。 */
struct PlanPreview {
    std::uint64_t baseDesiredRevision = 0;
    bool modulesChanged = false;
    bool desiredChanged = false;
    std::vector<EntryChange> entries;
};
/** @brief 生成稳定排序的目标差异；父代重启会使启用子树重建，不更改任何实例或目标修订。 */
PlanPreview previewChanges(const DocumentCandidate& base, const DocumentCandidate& candidate,
                           std::uint64_t baseDesiredRevision = 0);
} // namespace cordis::loader
