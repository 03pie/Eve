/** @file validation.cpp
 * @brief 以内置定义和无业务副作用 schema 校验配置，合并依赖时保留定义要求。
 */
#include "definition_validation_p.hpp"
#include "module_catalog_p.hpp"
#include <algorithm>
#include <cordis/loader/yaml.hpp>
#include <exception>

namespace cordis::loader {
namespace {
/** @brief 定义、schema 错误保留条目位置，并用配置错误包裹原始原因。 */
ErrorInfo errorAt(const ParsedConfiguration& source, std::string phase, std::string message, const std::string& path = {})
{
    auto found = source.positions().find(path);
    if (found != source.positions().end()) message = "line " + std::to_string(found->second.line) + ", column " +
        std::to_string(found->second.column) + ": " + message;
    auto error = makeError(ErrorCode::ConfigInvalid, std::move(message));
    error.phase = std::move(phase);
    if (!path.empty()) error.entryPath = path;
    return error;
}
/** @brief 依赖按接口键联合，minor 取较大值、capability 取并集，不能削弱定义要求。 */
std::vector<InjectSpec> mergeInject(const std::vector<InjectSpec>& definition, const std::vector<InjectSpec>& requested)
{
    std::map<std::pair<std::string, std::uint32_t>, InjectSpec> merged;
    for (const auto& list : {&definition, &requested}) for (const auto& value : *list) {
        auto found = merged.find({value.name, value.major});
        if (found == merged.end()) merged.emplace(std::make_pair(value.name, value.major), value);
        else {
            found->second.minimumMinor = std::max(found->second.minimumMinor, value.minimumMinor);
            auto& capabilities = found->second.capabilities;
            capabilities.insert(capabilities.end(), value.capabilities.begin(), value.capabilities.end());
            std::sort(capabilities.begin(), capabilities.end());
            capabilities.erase(std::unique(capabilities.begin(), capabilities.end()), capabilities.end());
        }
    }
    std::vector<InjectSpec> result;
    for (auto& value : merged) result.push_back(std::move(value.second));
    return result;
}
} // namespace
Result<std::map<std::string, PluginDefinition>> detail::normalizeDefinitions(std::vector<PluginDefinition> definitions)
{
    using Output = Result<std::map<std::string, PluginDefinition>>;
    std::map<std::string, PluginDefinition> catalog;
    std::map<std::string, PluginModule> modules;
    for (auto& definition : definitions) {
        Document probe;
        EntrySpec value;
        value.id = definition.id; value.plugin = definition.id; value.inject = definition.requiredInject;
        probe.plugins.push_back(std::move(value));
        auto checked = validateDocument(probe);
        if (!checked.hasValue() || !definition.validateConfig || catalog.count(definition.id))
            return Output::failure(
                makeError(ErrorCode::ConfigInvalid, "invalid, duplicate or schema-less plugin definition"));
        definition.requiredInject = checked.value().entries().begin()->second.inject;
        const auto id = definition.id;
        modules.emplace(id, definition.module);
        catalog.emplace(id, std::move(definition));
    }
    auto declarations = detail::checkModuleCatalog(modules);
    if (!declarations.hasValue())
        return Output::failure(declarations.error());
    return Output::success(std::move(catalog));
}
/** @brief 参数按值保留来源及 schema 生命周期，结果不持有定义回调。 */
Result<ValidatedCandidate> validateCandidate(ParsedConfiguration source, std::vector<PluginDefinition> definitions)
{
    if (!source.isValid())
        return Result<ValidatedCandidate>::failure(
            errorAt(source, "loader.definition", "parsed configuration was moved from"));
    auto normalized = detail::normalizeDefinitions(std::move(definitions));
    if (!normalized.hasValue())
        return Result<ValidatedCandidate>::failure(errorAt(source, "loader.definition", normalized.error().message));
    auto catalog = std::move(normalized).value();
    std::map<std::string, PluginModule> modules;
    for (const auto& entry : catalog)
        modules.emplace(entry.first, entry.second.module);
    auto bindings = detail::checkModuleBindings(source.structure(), modules);
    if (!bindings.hasValue())
        return Result<ValidatedCandidate>::failure(errorAt(source, "loader.definition", bindings.error().message,
                                                           bindings.error().entryPath.value_or(std::string{})));
    std::map<std::string, std::vector<InjectSpec>> inject;
    for (const auto& entry : source.structure().entries()) {
        const auto& definition = catalog.at(entry.second.plugin);
        try {
            auto checked = definition.validateConfig(entry.second.config);
            if (!checked.hasValue()) {
                auto error = errorAt(source, "loader.schema", "plugin configuration rejected", entry.first);
                error.causes.push_back(checked.error());
                return Result<ValidatedCandidate>::failure(std::move(error));
            }
        } catch (const std::exception& exception) {
            return Result<ValidatedCandidate>::failure(errorAt(source, "loader.schema", exception.what(), entry.first));
        } catch (...) {
            return Result<ValidatedCandidate>::failure(errorAt(source, "loader.schema", "plugin schema threw", entry.first));
        }
        inject.emplace(entry.first, mergeInject(definition.requiredInject, entry.second.inject));
    }
    return Result<ValidatedCandidate>::success(ValidatedCandidate(std::move(source), std::move(inject)));
}
} // namespace cordis::loader
