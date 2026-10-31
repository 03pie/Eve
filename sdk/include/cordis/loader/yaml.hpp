/** @file yaml.hpp
 * @brief 声明受限 YAML 文本解析及内置插件配置校验，不导出解析器类型。
 */
#pragma once
#include <cordis/loader/configuration.hpp>
#include <functional>
#include <memory>
#include <utility>

namespace cordis::loader {
class ConfigurationDefinitions;
namespace detail {
struct DefinitionIdentity;
}
/** @brief 配置来源的位置；行列从 1 开始，字节偏移不作为文件写入能力。 */
struct SourcePosition {
    std::size_t line = 1;
    std::size_t column = 1;
};
/** @brief 原始文本、事件树及规范文本的独立预算，不能以条目数代替解析预算。 */
struct YamlLimits {
    std::size_t maximumInputBytes = 1024 * 1024;
    std::size_t maximumNodes = 65536;
    std::size_t maximumDepth = 64;
    std::size_t maximumScalarBytes = 1024 * 1024;
    std::size_t maximumNormalizedBytes = 8 * 1024 * 1024;
    ConfigurationLimits structure;
};
/** @brief 解析及结构校验通过的不可变来源值；还未调用插件 schema。 */
class ParsedConfiguration {
public:
    /** @brief 复制完整来源值，不共享可变解析器节点。 */
    ParsedConfiguration(const ParsedConfiguration&) = default;
    /** @brief 转移完整来源值并使原值失效，不能把移动后的空对象接受为新目标。 */
    ParsedConfiguration(ParsedConfiguration&& other) noexcept
        : structure_(std::move(other.structure_)), normalized_(std::move(other.normalized_)),
          positions_(std::move(other.positions_)), valid_(std::exchange(other.valid_, false)) {}
    /** @brief 禁止不完整的原地替换。 */
    ParsedConfiguration& operator=(const ParsedConfiguration&) = delete;
    /** @brief 新文本必须生成新来源值。 */
    ParsedConfiguration& operator=(ParsedConfiguration&&) = delete;
    /** @brief 检查来源是否仍持有一次完整解析结果；移动后原值返回 false。 */
    bool isValid() const noexcept { return valid_; }
    /** @brief 读取已校验结构，config 为规范 JSON 字符串。 */
    const DocumentCandidate& structure() const noexcept { return structure_; }
    /** @brief 规范 JSON 是有效 YAML 子集；不保留注释及原有排版。 */
    const std::string& normalizedYaml() const noexcept { return normalized_; }
    /** @brief 按规范条目路径读取来源位置。 */
    const std::map<std::string, SourcePosition>& positions() const noexcept { return positions_; }
private:
    friend Result<ParsedConfiguration> parseConfiguration(const std::string&, YamlLimits);
    /** @brief 仅解析入口可以组合结构、规范文本和来源位置。 */
    ParsedConfiguration(DocumentCandidate structure, std::string normalized, std::map<std::string, SourcePosition> positions)
        : structure_(std::move(structure)), normalized_(std::move(normalized)), positions_(std::move(positions)) {}
    DocumentCandidate structure_;
    std::string normalized_;
    std::map<std::string, SourcePosition> positions_;
    bool valid_ = true;
};
/** @brief 解析单份 UTF-8 文档，拒绝 tag、锚点、别名、重复键和未知结构字段。 */
Result<ParsedConfiguration> parseConfiguration(const std::string& text, YamlLimits limits = {});
/** @brief 只改规范路径的本地启用标志，保留其他负载及子树，重新结构校验；不运行插件。 */
Result<ParsedConfiguration> setEntryEnabled(const ParsedConfiguration& source, const std::string& path, bool enabled,
                                            YamlLimits limits = {});
/** @brief 只替换指定条目的配置值，保留其余树及数字原文；新值接受 YAML/JSON 片段。 */
Result<ParsedConfiguration> setEntryConfiguration(const ParsedConfiguration& source, const std::string& path,
                                                  const std::string& configuration, YamlLimits limits = {});
/** @brief 追加顶层配置实例，标识/类型按字符串处理，完整结构校验拒绝重复或非法身份。 */
Result<ParsedConfiguration> addConfigurationEntry(const ParsedConfiguration& source, const std::string& id,
                                                  const std::string& plugin, const std::string& configuration,
                                                  YamlLimits limits = {}, PluginModule module = {});
/** @brief 宿主提供的内置插件定义；schema 必须无业务副作用，只检查规范 JSON config。 */
struct PluginDefinition {
    std::string id;
    std::vector<InjectSpec> requiredInject;
    std::function<Result<void>(const std::string&)> validateConfig;
    PluginModule module{};
};
/** @brief 内置插件定义/schema 校验完成的目标；不保留回调、Runtime 或实例。 */
class ValidatedCandidate {
public:
    /** @brief 复制完整的内置配置验证结果。 */
    ValidatedCandidate(const ValidatedCandidate&) = default;
    /** @brief 转移结果值。 */
    ValidatedCandidate(ValidatedCandidate&&) noexcept = default;
    /** @brief 禁止部分赋值破坏验证结果。 */
    ValidatedCandidate& operator=(const ValidatedCandidate&) = delete;
    /** @brief 目标修改必须重新解析和验证。 */
    ValidatedCandidate& operator=(ValidatedCandidate&&) = delete;
    /** @brief 读取原始目标的结构和规范文本。 */
    const ParsedConfiguration& source() const noexcept { return source_; }
    /** @brief 依赖是定义与条目声明的联合，只能增加或收紧。 */
    const std::map<std::string, std::vector<InjectSpec>>& effectiveInject() const noexcept { return inject_; }
    /** @brief 只比较校验来源，不授予 Runtime、schema 或目录修改能力。 */
    bool hasDefinitionIdentity() const noexcept { return static_cast<bool>(definitions_); }

private:
    friend class ConfigurationDefinitions;
    friend Result<ValidatedCandidate> validateCandidate(ParsedConfiguration, std::vector<PluginDefinition>);
    /** @brief 只有完整内置校验入口可发布结果。 */
    ValidatedCandidate(ParsedConfiguration source, std::map<std::string, std::vector<InjectSpec>> inject)
        : source_(std::move(source)), inject_(std::move(inject)) {}
    ParsedConfiguration source_;
    std::map<std::string, std::vector<InjectSpec>> inject_;
    std::shared_ptr<const detail::DefinitionIdentity> definitions_;
};
/** @brief 校验所有条目（含禁用项）的内置定义/schema；外部模块在 P4 接入前拒绝。 */
Result<ValidatedCandidate> validateCandidate(ParsedConfiguration source, std::vector<PluginDefinition> definitions);
} // namespace cordis::loader
