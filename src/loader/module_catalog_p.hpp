/** @file module_catalog_p.hpp
 * @brief 只读核对宿主已验证的模块目录，不执行文件或原生代码。
 */
#pragma once
#include <cordis/loader/configuration.hpp>

namespace cordis::loader::detail {
inline Result<void> checkModuleCatalog(const std::map<std::string, PluginModule>& catalog)
{
    std::map<std::string, std::string> aliases;
    for (const auto& item : catalog) {
        Document probe;
        const auto& module = item.second;
        if (module.alias == "builtin") {
            if (!module.manifest.empty())
                return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "builtin manifest must be empty"));
        } else {
            const auto found = aliases.find(module.alias);
            if (found != aliases.end() && found->second != module.manifest)
                return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "conflicting host module alias"));
            aliases.emplace(module.alias, module.manifest);
            probe.modules.emplace(module.alias, module.manifest);
        }
        EntrySpec entry;
        entry.id = entry.plugin = item.first;
        entry.module = module.alias;
        probe.plugins.push_back(std::move(entry));
        auto checked = validateDocument(probe);
        if (!checked.hasValue())
            return Result<void>::failure(checked.error());
    }
    return Result<void>::success();
}
inline Result<void> checkModuleBindings(const DocumentCandidate& source,
                                        const std::map<std::string, PluginModule>& catalog)
{
    std::map<std::string, std::string> known;
    for (const auto& item : catalog)
        if (item.second.alias != "builtin")
            known.emplace(item.second.alias, item.second.manifest);
    for (const auto& item : source.modules()) {
        const auto found = known.find(item.first);
        if (found == known.end() || found->second != item.second)
            return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "module differs from host catalog"));
    }
    for (const auto& item : source.entries()) {
        const auto found = catalog.find(item.second.plugin);
        if (found == catalog.end() || item.second.module != found->second.alias) {
            auto error = makeError(ErrorCode::ConfigInvalid, "plugin module differs from host catalog");
            error.entryPath = item.first;
            return Result<void>::failure(std::move(error));
        }
    }
    return Result<void>::success();
}
} // namespace cordis::loader::detail
