/** @file yaml.cpp
 * @brief 从 LibYAML 事件构造有界配置树，拒绝引用/标记并生成确定性的 JSON/YAML 文本。
 */
#include <cordis/loader/yaml.hpp>
#include <yaml.h>
#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <system_error>

namespace cordis::loader {
namespace {
/** @brief 只用于内部展开错误；跨公共入口转为 ErrorInfo。 */
struct ParseFailure { ErrorInfo error; };
/** @brief 统一生成带位置及可选条目路径的文本错误。 */
[[noreturn]] void fail(std::string message, SourcePosition position, const std::string& path = {})
{
    auto error = makeError(ErrorCode::ConfigInvalid, "line " + std::to_string(position.line) + ", column " +
                           std::to_string(position.column) + ": " + std::move(message));
    error.phase = "loader.yaml";
    if (!path.empty()) error.entryPath = path;
    throw ParseFailure{std::move(error)};
}
/** @brief LibYAML 以零为基的位置转为面向用户的一基位置。 */
SourcePosition position(yaml_mark_t mark) { return {mark.line + 1, mark.column + 1}; }
/** @brief 解析器独占其 C 资源，所有提前返回与异常都释放。 */
class Parser {
public:
    /** @brief 输入只借用调用期间存活的 UTF-8 字节。 */
    explicit Parser(const std::string& text)
    {
        if (!yaml_parser_initialize(&value)) fail("cannot initialize YAML parser", {});
        yaml_parser_set_input_string(&value, reinterpret_cast<const unsigned char*>(text.data()), text.size());
        yaml_parser_set_encoding(&value, YAML_UTF8_ENCODING);
    }
    /** @brief 释放解析器缓冲、token 和内部状态。 */
    ~Parser() { yaml_parser_delete(&value); }
    /** @brief 解析器 C 状态不能浅拷贝。 */
    Parser(const Parser&) = delete;
    /** @brief 已初始化资源不允许覆盖。 */
    Parser& operator=(const Parser&) = delete;
    yaml_parser_t value{};
};
/** @brief 每次事件均由独立守卫释放，不在宿主树中保留库分配的指针。 */
struct Event {
    yaml_event_t value{};
    /** @brief 即使事件尚为空，也可安全销毁。 */
    ~Event() { yaml_event_delete(&value); }
    /** @brief 初始事件为空，由下一次 parse 填充。 */
    Event() = default;
    /** @brief 事件内容只能由一个守卫删除。 */
    Event(const Event&) = delete;
    /** @brief 防止覆盖尚未删除的事件。 */
    Event& operator=(const Event&) = delete;
};
/** @brief 宿主树只含值；唯一指针使扩容不影响当前容器栈指向的节点。 */
struct Node {
    enum class Kind { Scalar, Sequence, Mapping } kind = Kind::Scalar;
    std::string scalar;
    bool plain = true;
    SourcePosition source;
    std::vector<std::unique_ptr<Node>> sequence;
    std::map<std::string, std::unique_ptr<Node>> mapping;
};
/** @brief 配置负载采用 JSON 标量类别，其余普通文本按字符串处理。 */
enum class ScalarKind { String, Null, Boolean, Number };
/** @brief 识别 JSON 数字文法，不把非有限值、前导零或 YAML 1.1 特殊格式转为数字。 */
bool jsonNumber(std::string_view value)
{
    std::size_t i = 0;
    if (i < value.size() && value[i] == '-') ++i;
    if (i == value.size()) return false;
    if (value[i] == '0') ++i;
    else {
        if (value[i] < '1' || value[i] > '9') return false;
        do { ++i; } while (i < value.size() && value[i] >= '0' && value[i] <= '9');
    }
    if (i < value.size() && value[i] == '.') {
        const auto start = ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
        if (i == start) return false;
    }
    if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
        ++i;
        if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
        const auto start = i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
        if (i == start) return false;
    }
    return i == value.size();
}
/** @brief 引号和块文本保持字符串，只有受支持的普通标量有隐式类别。 */
ScalarKind scalarKind(const Node& node)
{
    if (!node.plain) return ScalarKind::String;
    if (node.scalar.empty() || node.scalar == "~" || node.scalar == "null" || node.scalar == "Null" || node.scalar == "NULL")
        return ScalarKind::Null;
    if (node.scalar == "true" || node.scalar == "false") return ScalarKind::Boolean;
    return jsonNumber(node.scalar) ? ScalarKind::Number : ScalarKind::String;
}
/** @brief 事件树容器维护键值交替状态，允许配置负载的空字符串键。 */
struct Frame {
    Node* node;
    std::string key;
    bool needsKey = true;
};
/** @brief 逐事件限额，不使用库的文档组合器或展开 alias。 */
std::unique_ptr<Node> readTree(const std::string& text, const YamlLimits& limits)
{
    Parser parser(text);
    std::unique_ptr<Node> root;
    std::vector<Frame> stack;
    std::size_t nodes = 0, scalarBytes = 0;
    bool document = false, ended = false;
    for (;;) {
        Event event;
        if (!yaml_parser_parse(&parser.value, &event.value))
            fail(parser.value.problem ? parser.value.problem : "YAML parse failed", position(parser.value.problem_mark));
        const auto& value = event.value;
        const auto at = position(value.start_mark);
        if (value.type == YAML_STREAM_START_EVENT) continue;
        if (value.type == YAML_STREAM_END_EVENT) break;
        if (value.type == YAML_DOCUMENT_START_EVENT) {
            if (document) fail("multiple YAML documents are not supported", at);
            document = true;
            const auto& start = value.data.document_start;
            if (start.tag_directives.start != start.tag_directives.end) fail("tag directives are not supported", at);
            if (start.version_directive && (start.version_directive->major != 1 || start.version_directive->minor != 2))
                fail("only YAML 1.2 directives are supported", at);
            continue;
        }
        if (value.type == YAML_DOCUMENT_END_EVENT) { ended = true; continue; }
        if (!document || ended) fail("unexpected YAML event", at);
        if (value.type == YAML_ALIAS_EVENT) fail("aliases are not supported", at);
        if (value.type == YAML_SEQUENCE_END_EVENT || value.type == YAML_MAPPING_END_EVENT) {
            if (stack.empty() || (stack.back().node->kind == Node::Kind::Mapping && !stack.back().needsKey))
                fail("incomplete YAML container", at);
            stack.pop_back();
            continue;
        }
        if (nodes >= limits.maximumNodes || stack.size() >= limits.maximumDepth) fail("YAML node/depth limit exceeded", at);
        ++nodes;
        auto node = std::make_unique<Node>();
        node->source = at;
        const yaml_char_t* anchor = nullptr;
        const yaml_char_t* tag = nullptr;
        if (value.type == YAML_SCALAR_EVENT) {
            anchor = value.data.scalar.anchor; tag = value.data.scalar.tag;
            if (value.data.scalar.length > limits.maximumScalarBytes - scalarBytes) fail("YAML scalar byte limit exceeded", at);
            scalarBytes += value.data.scalar.length;
            node->scalar.assign(reinterpret_cast<const char*>(value.data.scalar.value), value.data.scalar.length);
            node->plain = value.data.scalar.style == YAML_PLAIN_SCALAR_STYLE;
        } else if (value.type == YAML_SEQUENCE_START_EVENT) {
            node->kind = Node::Kind::Sequence;
            anchor = value.data.sequence_start.anchor; tag = value.data.sequence_start.tag;
        } else if (value.type == YAML_MAPPING_START_EVENT) {
            node->kind = Node::Kind::Mapping;
            anchor = value.data.mapping_start.anchor; tag = value.data.mapping_start.tag;
        } else fail("unsupported YAML event", at);
        if (anchor || tag) fail("anchors and explicit tags are not supported", at);
        auto current = node.get();
        if (stack.empty()) {
            if (root) fail("multiple root nodes", at);
            root = std::move(node);
        } else {
            auto& frame = stack.back();
            if (frame.node->kind == Node::Kind::Sequence) frame.node->sequence.push_back(std::move(node));
            else if (frame.needsKey) {
                if (node->kind != Node::Kind::Scalar || scalarKind(*node) != ScalarKind::String)
                    fail("mapping keys must be strings", at);
                if (node->scalar == "<<") fail("merge keys are not supported", at);
                if (frame.node->mapping.count(node->scalar)) fail("duplicate mapping key: " + node->scalar, at);
                frame.key = std::move(node->scalar);
                frame.needsKey = false;
                continue;
            } else {
                frame.node->mapping.emplace(std::move(frame.key), std::move(node));
                frame.needsKey = true;
            }
        }
        if (current->kind != Node::Kind::Scalar) stack.push_back({current, {}, true});
    }
    if (!document || !root || !stack.empty()) fail("empty or incomplete YAML document", {});
    return root;
}
/** @brief 追加前检查规范文本预算，控制转义及重复生成的输出长度。 */
void append(std::string& output, std::string_view text, std::size_t maximum)
{
    if (text.size() > maximum - output.size()) fail("normalized text byte limit exceeded", {});
    output.append(text.data(), text.size());
}
/** @brief JSON 引号转义控制字符，保留经 LibYAML 验证的 UTF-8 字节。 */
void quote(std::string& output, const std::string& value, std::size_t maximum)
{
    append(output, "\"", maximum);
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            const char escaped[] = {'\\', static_cast<char>(ch)};
            append(output, std::string_view(escaped, 2), maximum);
        } else if (ch < 0x20) {
            const char escaped[] = {'\\', 'u', '0', '0', digits[ch >> 4], digits[ch & 15]};
            append(output, std::string_view(escaped, 6), maximum);
        } else {
            const char byte = static_cast<char>(ch);
            append(output, std::string_view(&byte, 1), maximum);
        }
    }
    append(output, "\"", maximum);
}
/** @brief 容器深度已受限，递归输出按键排序的 JSON 兼容 YAML。 */
void encode(const Node& node, std::string& output, std::size_t maximum)
{
    if (node.kind == Node::Kind::Scalar) {
        switch (scalarKind(node)) {
        case ScalarKind::String: quote(output, node.scalar, maximum); break;
        case ScalarKind::Null: append(output, "null", maximum); break;
        default: append(output, node.scalar, maximum); break;
        }
        return;
    }
    const bool sequence = node.kind == Node::Kind::Sequence;
    append(output, sequence ? "[" : "{", maximum);
    bool first = true;
    if (sequence) for (const auto& child : node.sequence) {
        if (!first) append(output, ",", maximum);
        first = false;
        encode(*child, output, maximum);
    } else for (const auto& item : node.mapping) {
        if (!first) append(output, ",", maximum);
        first = false;
        quote(output, item.first, maximum);
        append(output, ":", maximum);
        encode(*item.second, output, maximum);
    }
    append(output, sequence ? "]" : "}", maximum);
}
/** @brief 固定结构只接受已知字段，插件 config 子树另由插件 schema 检查。 */
void fields(const Node& node, std::initializer_list<std::string_view> allowed, const std::string& path = {})
{
    if (node.kind != Node::Kind::Mapping) fail("expected mapping", node.source, path);
    for (const auto& item : node.mapping)
        if (std::find(allowed.begin(), allowed.end(), item.first) == allowed.end())
            fail("unknown field: " + item.first, item.second->source, path);
}
/** @brief 可选字段仅在映射结构确认后读取。 */
const Node* optional(const Node& node, const std::string& key)
{
    auto found = node.mapping.find(key);
    return found == node.mapping.end() ? nullptr : found->second.get();
}
/** @brief 缺少必填字段时明确失败，空文档不能意外表达删除所有条目。 */
const Node& required(const Node& node, std::string key, std::string path = {})
{
    auto value = optional(node, key);
    if (!value) fail("missing field: " + key, node.source, path);
    return *value;
}
/** @brief 只接受字符串标量，不隐式把数字、null 或布尔转为标识。 */
const std::string& string(const Node& node, const std::string& path = {})
{
    if (node.kind != Node::Kind::Scalar || scalarKind(node) != ScalarKind::String) fail("expected string", node.source, path);
    return node.scalar;
}
/** @brief 只接受十进制非负整数，严格检查范围及完整消费。 */
std::uint32_t unsignedValue(std::string_view text, SourcePosition at, const std::string& path)
{
    std::uint32_t value = 0;
    if (text.empty() || (text.size() > 1 && text.front() == '0')) fail("expected unsigned decimal integer", at, path);
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) fail("invalid unsigned integer", at, path);
    return value;
}
/** @brief 结构整数要求未加引号的数值，禁止字符串到数字的宽松转换。 */
std::uint32_t number(const Node& node, const std::string& path = {})
{
    if (node.kind != Node::Kind::Scalar || scalarKind(node) != ScalarKind::Number) fail("expected integer", node.source, path);
    return unsignedValue(node.scalar, node.source, path);
}
/** @brief 显式检查序列，null 和空映射均不能代替空列表。 */
void sequence(const Node& node, const std::string& path = {})
{
    if (node.kind != Node::Kind::Sequence) fail("expected sequence", node.source, path);
}
/** @brief 依赖支持 name/vN 简写及完整映射，类型和规范化由结构阶段继续检查。 */
InjectSpec inject(const Node& node, const std::string& path)
{
    InjectSpec value;
    if (node.kind == Node::Kind::Scalar) {
        const auto& text = string(node, path);
        const auto slash = text.find('/');
        if (slash == std::string::npos || text.substr(slash, 2) != "/v") fail("inject shorthand requires name/vN", node.source, path);
        value.name = text.substr(0, slash);
        value.major = unsignedValue(std::string_view(text).substr(slash + 2), node.source, path);
        return value;
    }
    fields(node, {"name", "major", "minimumMinor", "capabilities"}, path);
    value.name = string(required(node, "name", path), path);
    if (auto item = optional(node, "major")) value.major = number(*item, path);
    if (auto item = optional(node, "minimumMinor")) value.minimumMinor = number(*item, path);
    if (auto items = optional(node, "capabilities")) {
        sequence(*items, path);
        for (const auto& item : items->sequence) value.capabilities.push_back(string(*item, path));
    }
    return value;
}
/** @brief 构造条目前检查结构深度与数量，来源位置按最终路径记录。 */
EntrySpec entry(const Node& node, const std::string& parent, std::size_t depth, std::size_t& count,
                const YamlLimits& limits, std::map<std::string, SourcePosition>& positions)
{
    if (depth > limits.structure.maximumDepth || count >= limits.structure.maximumEntries)
        fail("configuration entry/depth limit exceeded", node.source, parent);
    ++count;
    fields(node, {"id", "plugin", "module", "disabled", "config", "realms", "inject", "children"}, parent);
    EntrySpec value;
    value.id = string(required(node, "id", parent), parent);
    const auto path = parent.empty() ? value.id : parent + "/" + value.id;
    positions.emplace(path, node.source);
    value.plugin = string(required(node, "plugin", path), path);
    if (auto item = optional(node, "module")) value.module = string(*item, path);
    if (auto item = optional(node, "disabled")) {
        if (item->kind != Node::Kind::Scalar || scalarKind(*item) != ScalarKind::Boolean) fail("disabled requires true or false", item->source, path);
        value.disabled = item->scalar == "true";
    }
    value.config = "{}";
    if (auto item = optional(node, "config")) {
        value.config.clear();
        encode(*item, value.config, limits.maximumNormalizedBytes);
    }
    if (auto item = optional(node, "realms")) {
        if (item->kind != Node::Kind::Mapping) fail("realms requires mapping", item->source, path);
        for (const auto& realm : item->mapping) value.realms.emplace(realm.first, string(*realm.second, path));
    }
    if (auto items = optional(node, "inject")) {
        sequence(*items, path);
        for (const auto& item : items->sequence) value.inject.push_back(inject(*item, path));
    }
    if (auto items = optional(node, "children")) {
        sequence(*items, path);
        for (const auto& item : items->sequence) value.children.push_back(entry(*item, path, depth + 1, count, limits, positions));
    }
    return value;
}
} // namespace

/** @brief 先解析为受限树，再提取结构并复用 Q4a 校验；成功结果不保留库节点。 */
Result<ParsedConfiguration> parseConfiguration(const std::string& text, YamlLimits limits)
{
    try {
        if (!limits.maximumInputBytes || text.size() > limits.maximumInputBytes || !limits.maximumNodes ||
            !limits.maximumDepth || limits.maximumDepth > 128 || !limits.maximumScalarBytes || !limits.maximumNormalizedBytes)
            fail("invalid YAML limits or input byte limit exceeded", {});
        if (!limits.structure.maximumEntries || !limits.structure.maximumDepth || limits.structure.maximumDepth > 128 || !limits.structure.maximumBytes)
            fail("invalid structure limits", {});
        auto root = readTree(text, limits);
        fields(*root, {"formatVersion", "modules", "plugins"});
        Document document;
        document.formatVersion = number(required(*root, "formatVersion"));
        if (auto modules = optional(*root, "modules")) {
            if (modules->kind != Node::Kind::Mapping) fail("modules requires mapping", modules->source);
            for (const auto& module : modules->mapping) {
                fields(*module.second, {"manifest"});
                document.modules.emplace(module.first, string(required(*module.second, "manifest")));
            }
        }
        std::map<std::string, SourcePosition> positions;
        const auto plugins = &required(*root, "plugins");
        sequence(*plugins);
        std::size_t count = 0;
        for (const auto& item : plugins->sequence) document.plugins.push_back(entry(*item, {}, 1, count, limits, positions));
        auto candidate = validateDocument(document, limits.structure);
        if (!candidate.hasValue()) {
            auto error = candidate.error();
            const auto found = error.entryPath ? positions.find(*error.entryPath) : positions.end();
            const auto at = found == positions.end() ? root->source : found->second;
            error.message = "line " + std::to_string(at.line) + ", column " + std::to_string(at.column) + ": " + error.message;
            return Result<ParsedConfiguration>::failure(std::move(error));
        }
        std::string normalized;
        encode(*root, normalized, limits.maximumNormalizedBytes);
        return Result<ParsedConfiguration>::success(ParsedConfiguration(std::move(candidate).value(), std::move(normalized), std::move(positions)));
    } catch (const ParseFailure& failure) {
        return Result<ParsedConfiguration>::failure(failure.error);
    }
}
/** @brief 在受限值树中精确定位路径，不用文本替换误改配置负载中的 id/disabled。 */
Result<ParsedConfiguration> setEntryEnabled(const ParsedConfiguration& source, const std::string& path, bool enabled,
                                            YamlLimits limits)
{
    if (!source.isValid() || !source.structure().entries().count(path))
        return Result<ParsedConfiguration>::failure(
            makeError(ErrorCode::InvalidArgument, "unknown configuration path"));
    try {
        limits.maximumInputBytes = limits.maximumNormalizedBytes;
        if (!limits.maximumInputBytes || source.normalizedYaml().size() > limits.maximumInputBytes)
            fail("normalized text byte limit exceeded", {}, path);
        auto root = readTree(source.normalizedYaml(), limits);
        Node* sequenceNode = root->mapping.at("plugins").get();
        Node* selected = nullptr;
        std::size_t begin = 0;
        while (begin < path.size()) {
            const auto end = path.find('/', begin);
            const auto id = path.substr(begin, end == std::string::npos ? end : end - begin);
            selected = nullptr;
            for (auto& child : sequenceNode->sequence)
                if (child->mapping.at("id")->scalar == id) {
                    selected = child.get();
                    break;
                }
            if (!selected)
                fail("configuration path missing from source", {}, path);
            if (end == std::string::npos)
                break;
            sequenceNode = selected->mapping.at("children").get();
            begin = end + 1;
        }
        auto disabled = std::make_unique<Node>();
        disabled->scalar = enabled ? "false" : "true";
        selected->mapping.insert_or_assign("disabled", std::move(disabled));
        std::string normalized;
        encode(*root, normalized, limits.maximumNormalizedBytes);
        return parseConfiguration(normalized, limits);
    } catch (const ParseFailure& failure) {
        return Result<ParsedConfiguration>::failure(failure.error);
    }
}
namespace {
/** @brief 受限值树修改不经过浮点 JSON DOM，其他负载中的大整数与小数原文保持。 */
Result<ParsedConfiguration> changeEntryConfiguration(const ParsedConfiguration& source, const std::string& path,
                                                     const std::string& id, const std::string& plugin,
                                                     const std::string& configuration, YamlLimits limits)
{
    if (!source.isValid() || (!path.empty() && !source.structure().entries().count(path)))
        return Result<ParsedConfiguration>::failure(
            makeError(ErrorCode::InvalidArgument, "unknown configuration path"));
    try {
        if (!limits.maximumInputBytes || configuration.size() > limits.maximumInputBytes || !limits.maximumNodes ||
            !limits.maximumDepth || limits.maximumDepth > 128 || !limits.maximumScalarBytes ||
            !limits.maximumNormalizedBytes)
            fail("configuration fragment exceeds input budget", {}, path);
        auto payload = readTree(configuration, limits);
        limits.maximumInputBytes = limits.maximumNormalizedBytes;
        if (source.normalizedYaml().size() > limits.maximumInputBytes)
            fail("normalized text byte limit exceeded", {}, path);
        auto root = readTree(source.normalizedYaml(), limits);
        auto sequenceNode = root->mapping.at("plugins").get();
        Node* selected = nullptr;
        if (path.empty()) {
            auto entry = std::make_unique<Node>();
            entry->kind = Node::Kind::Mapping;
            for (const auto& value : {std::make_pair("id", id), std::make_pair("plugin", plugin)}) {
                auto text = std::make_unique<Node>();
                text->plain = false;
                text->scalar = value.second;
                entry->mapping.emplace(value.first, std::move(text));
            }
            selected = entry.get();
            sequenceNode->sequence.push_back(std::move(entry));
        } else {
            std::size_t begin = 0;
            while (begin < path.size()) {
                const auto end = path.find('/', begin);
                const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
                selected = nullptr;
                for (auto& child : sequenceNode->sequence)
                    if (child->mapping.at("id")->scalar == part) {
                        selected = child.get();
                        break;
                    }
                if (!selected)
                    fail("configuration path missing from source", {}, path);
                if (end == std::string::npos)
                    break;
                sequenceNode = selected->mapping.at("children").get();
                begin = end + 1;
            }
        }
        selected->mapping.insert_or_assign("config", std::move(payload));
        std::string normalized;
        encode(*root, normalized, limits.maximumNormalizedBytes);
        return parseConfiguration(normalized, limits);
    } catch (const ParseFailure& failure) {
        return Result<ParsedConfiguration>::failure(failure.error);
    }
}
} // namespace
/** @brief 路径必须存在，不能借空路径把编辑误作追加。 */
Result<ParsedConfiguration> setEntryConfiguration(const ParsedConfiguration& source, const std::string& path,
                                                  const std::string& configuration, YamlLimits limits)
{
    if (path.empty())
        return Result<ParsedConfiguration>::failure(
            makeError(ErrorCode::InvalidArgument, "configuration path is empty"));
    return changeEntryConfiguration(source, path, {}, {}, configuration, limits);
}
/** @brief 追加后复用完整解析、结构和调用方的 schema/CAS 校验。 */
Result<ParsedConfiguration> addConfigurationEntry(const ParsedConfiguration& source, const std::string& id,
                                                  const std::string& plugin, const std::string& configuration,
                                                  YamlLimits limits)
{
    return changeEntryConfiguration(source, {}, id, plugin, configuration, limits);
}
} // namespace cordis::loader
