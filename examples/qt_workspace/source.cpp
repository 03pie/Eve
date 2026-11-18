/** @file source.cpp
 * @brief 复核固定业务来源和宿主冻结名单；所选候选不能授予显式卸载。
 */
#include "contract.hpp"
#include "source_p.hpp"
#include <algorithm>
#include <cordis/module/qt_manifest.hpp>

namespace notebook::detail {
using namespace cordis;
using namespace cordis::module;
std::string businessPlugin(const std::string& alias)
{
    if (alias != "counter" && alias != "reporter" && alias != "notebook")
        throw InitializationFailure{makeError(ErrorCode::InvalidArgument, "unknown business module alias")};
    return "example." + alias;
}
PreflightArtifact reviewBusinessArtifact(const std::string& alias, const std::string& root, const std::string& path)
{
    const auto plugin = businessPlugin(alias);
    const bool qt = alias == "notebook";
    auto artifact = qt ? require(inspectQtModule(root, path)).artifact : require(preflightModule(root, path));
    const auto& value = artifact.manifest();
    if (value.moduleId != "org.cordis.example." + alias || value.plugins != std::vector<std::string>{plugin} ||
        value.loader != (qt ? ModuleLoader::QtUi : ModuleLoader::CAbi) ||
        (qt && value.unloadPolicy != ModuleUnloadPolicy::Resident))
        throw InitializationFailure{makeError(ErrorCode::AbiMismatch, "business artifact identity differs")};
    return artifact;
}
std::vector<BusinessArtifact> businessArtifacts(const std::string& root)
{
    std::vector<BusinessArtifact> result;
    for (const auto* alias : {"counter", "reporter", "notebook"})
        for (const auto* variant : {"a", "b"}) {
            const bool qt = std::string(alias) == "notebook";
            const auto path =
                std::string(qt ? "qt-upgrade/" : "native-upgrade/") + alias + "-" + variant + "/module.json";
            const auto artifact = reviewBusinessArtifact(alias, root, path);
            if (artifact.manifest().moduleVersion != (std::string(variant) == "a" ? "1.1.0" : "1.2.0") ||
                artifact.manifest().unloadPolicy != (qt ? ModuleUnloadPolicy::Resident : ModuleUnloadPolicy::Explicit))
                throw InitializationFailure{
                    makeError(ErrorCode::AbiMismatch, "fixed business artifact identity differs")};
            result.push_back({alias, path, artifact.manifest()});
        }
    return result;
}
void requireApproved(const PreflightArtifact& artifact, const NativeUnloadWhitelist& approvals)
{
    const auto& value = artifact.manifest();
    if (value.loader != ModuleLoader::CAbi || value.unloadPolicy != ModuleUnloadPolicy::Explicit)
        return;
    if (std::none_of(approvals.begin(), approvals.end(), [&](const auto& approved) {
            return approved.moduleId == value.moduleId && approved.sha256 == value.sha256;
        }))
        throw InitializationFailure{
            makeError(ErrorCode::ModulePinned, "explicit artifact requires exact frozen host approval")};
}
loader::ConfiguredPlugin prepareNotebook(const std::shared_ptr<NotebookModule>& record,
                                         const std::shared_ptr<ModuleManager>& manager,
                                         const std::shared_ptr<qt::QtScheduler>& scheduler, loader::PluginModule source,
                                         bool stateful)
{
    record->runtime = require(qtRuntime(record->generation,
                                        {"example.notebook",
                                         {ServiceRequirement::of<ui::UiPagesService>({"ui.pages"}),
                                          ServiceRequirement::of<ui::CommandService>({"ui.commands"}),
                                          ServiceRequirement::of<ui::OutputService>({"ui.output"})},
                                         {}},
                                        scheduler));
    const auto& schema = record->runtime->stateSchema();
    if (stateful && (!schema || schema->id != "org.cordis.example.notebook.draft" || schema->version != 1))
        throw InitializationFailure{makeError(ErrorCode::AbiMismatch, "stateful notebook state schema differs")};
    const std::weak_ptr<ModuleManager> weak = manager;
    return {{"example.notebook",
             {{"ui.pages"}, {"ui.commands"}, {"ui.output"}},
             [](const std::string& text) {
                 const auto checked = configuration(text);
                 return checked.hasValue() ? Result<void>::success() : Result<void>::failure(checked.error());
             },
             std::move(source)},
            *record->runtime,
            {},
            [weak, id = record->generation] {
                const auto manager = weak.lock();
                if (manager) {
                    auto rows = manager->snapshot();
                    if (!rows.hasValue())
                        return Result<void>::failure(rows.error());
                    for (const auto& row : rows.value())
                        if (row.generation == id && !row.failure && !row.businessFailure &&
                            (row.state == ModuleState::Loaded || row.state == ModuleState::Serving))
                            return Result<void>::success();
                }
                return Result<void>::failure(makeError(ErrorCode::ModulePinned, "notebook module unavailable"));
            }};
}
} // namespace notebook::detail
