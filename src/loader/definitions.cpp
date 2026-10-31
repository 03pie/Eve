/** @file definitions.cpp
 * @brief 规范化完整定义，保持 schema/来源/实际工厂为同一只读拥有单元。
 */
#include "definitions_p.hpp"
#include "module_catalog_p.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace cordis::loader {
namespace {
ErrorInfo error(const char* message)
{
    auto value = makeError(ErrorCode::ConfigInvalid, message);
    value.phase = "loader.definitions";
    return value;
}
bool satisfies(const InjectSpec& spec, const ServiceRequirement& requirement)
{
    return spec.name == requirement.key.name && spec.major == requirement.key.interfaceMajor &&
           spec.minimumMinor >= requirement.minimumMinor &&
           std::all_of(requirement.capabilities.begin(), requirement.capabilities.end(), [&](const auto& cap) {
               return std::find(spec.capabilities.begin(), spec.capabilities.end(), cap) != spec.capabilities.end();
           });
}
Result<std::shared_ptr<const detail::DefinitionsData>> prepare(std::vector<ConfiguredPlugin> plugins, bool complete)
{
    using Output = Result<std::shared_ptr<const detail::DefinitionsData>>;
    auto data = std::make_shared<detail::DefinitionsData>();
    if (complete)
        data->identity = std::make_shared<const detail::DefinitionIdentity>();
    std::map<std::string, PluginDefinition> schemas;
    if (complete) {
        std::vector<PluginDefinition> definitions;
        for (const auto& plugin : plugins)
            definitions.push_back(plugin.configuration);
        auto checked = detail::normalizeDefinitions(std::move(definitions));
        if (!checked.hasValue())
            return Output::failure(checked.error());
        schemas = std::move(checked).value();
    }
    for (auto& plugin : plugins) {
        const auto id = plugin.configuration.id;
        if (id.empty() || !plugin.runtime.id().valid() || data->entries.count(id))
            return Output::failure(error("duplicate, invalid or moved runtime definition"));
        if (complete)
            plugin.configuration = std::move(schemas.at(id));
        std::set<std::pair<std::string, std::uint32_t>> known;
        for (const auto& requirement : plugin.runtime.requirements())
            known.emplace(requirement.key.name, requirement.key.interfaceMajor);
        for (const auto& optional : plugin.configurableInject) {
            std::set<std::string> capabilities;
            if (optional.key.name.empty() || optional.key.interfaceMajor == 0 || !optional.key.realm.valid() ||
                optional.type == typeid(void) || !known.emplace(optional.key.name, optional.key.interfaceMajor).second)
                return Output::failure(error("invalid, duplicate or fixed configurable inject interface"));
            for (const auto& cap : optional.capabilities)
                if (cap.empty() || !capabilities.insert(cap).second)
                    return Output::failure(error("invalid configurable inject capabilities"));
        }
        if (complete) {
            const auto& inject = plugin.configuration.requiredInject;
            for (const auto& fixed : plugin.runtime.requirements())
                if (std::none_of(inject.begin(), inject.end(),
                                 [&](const auto& spec) { return satisfies(spec, fixed); }))
                    return Output::failure(error("schema weakens fixed runtime requirements"));
            for (const auto& spec : inject) {
                bool typed = false;
                const std::vector<ServiceRequirement>* lists[] = {&plugin.runtime.requirements(),
                                                                  &plugin.configurableInject};
                for (const auto* contracts : lists)
                    for (const auto& requirement : *contracts)
                        if (satisfies(spec, requirement))
                            typed = true;
                if (!typed)
                    return Output::failure(error("schema requirement has no compatible typed runtime contract"));
            }
        }
        data->modules.emplace(id, plugin.configuration.module);
        data->entries.emplace(id, std::move(plugin));
    }
    auto checked = detail::checkModuleCatalog(data->modules);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    return Output::success(std::move(data));
}
} // namespace
Result<std::shared_ptr<const ConfigurationDefinitions>>
ConfigurationDefinitions::create(std::vector<ConfiguredPlugin> plugins)
{
    using Output = Result<std::shared_ptr<const ConfigurationDefinitions>>;
    auto data = prepare(std::move(plugins), true);
    if (!data.hasValue())
        return Output::failure(data.error());
    return Output::success(
        std::shared_ptr<const ConfigurationDefinitions>(new ConfigurationDefinitions(std::move(data).value())));
}
std::shared_ptr<const ConfigurationDefinitions> ConfigurationDefinitions::runtimeOnly(std::vector<KernelPlugin> plugins)
{
    std::vector<ConfiguredPlugin> entries;
    for (auto& plugin : plugins)
        entries.push_back({{std::move(plugin.id), {}, {}, std::move(plugin.module)},
                           std::move(plugin.runtime),
                           std::move(plugin.configurableInject),
                           std::move(plugin.available),
                           plugin.maximumDependencies});
    auto data = prepare(std::move(entries), false);
    if (!data.hasValue())
        throw std::invalid_argument(data.error().message);
    return std::shared_ptr<const ConfigurationDefinitions>(new ConfigurationDefinitions(std::move(data).value()));
}
Result<ValidatedCandidate> ConfigurationDefinitions::validate(ParsedConfiguration source) const
{
    const auto data = data_;
    if (!data->identity)
        return Result<ValidatedCandidate>::failure(error("runtime-only definitions cannot validate configuration"));
    std::vector<PluginDefinition> definitions;
    for (const auto& entry : data->entries)
        definitions.push_back(entry.second.configuration);
    auto result = validateCandidate(std::move(source), std::move(definitions));
    if (result.hasValue())
        result.value().definitions_ = data->identity;
    return result;
}
bool ConfigurationDefinitions::owns(const ValidatedCandidate& candidate) const noexcept
{
    return data_->identity == candidate.definitions_;
}
std::vector<std::string> ConfigurationDefinitions::plugins() const
{
    std::vector<std::string> result;
    for (const auto& entry : data_->entries)
        result.push_back(entry.first);
    return result;
}
Result<PluginModule> ConfigurationDefinitions::module(const std::string& plugin) const
{
    const auto at = data_->entries.find(plugin);
    return at == data_->entries.end() ? Result<PluginModule>::failure(error("unknown plugin definition"))
                                      : Result<PluginModule>::success(at->second.configuration.module);
}
} // namespace cordis::loader
