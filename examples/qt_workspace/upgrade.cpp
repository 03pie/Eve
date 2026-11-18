/** @file upgrade.cpp
 * @brief 纯预览、重新准入与当前槽查询；不在 getter 中运行插件或清理。
 */
#include "upgrade_p.hpp"
#include <QDir>
#include <algorithm>

namespace notebook::detail {
using namespace cordis;
bool sameArtifact(const module::PreflightArtifact& a, const module::PreflightArtifact& b)
{
    const auto& x = a.manifest();
    const auto& y = b.manifest();
    return a.isValid() && b.isValid() && a.manifestPath() == b.manifestPath() && a.libraryPath() == b.libraryPath() &&
           a.libraryBytes() == b.libraryBytes() && x.moduleId == y.moduleId && x.moduleVersion == y.moduleVersion &&
           x.library == y.library && x.sha256 == y.sha256 && x.plugins == y.plugins && x.loader == y.loader &&
           x.unloadPolicy == y.unloadPolicy;
}
bool sameImpact(const ReplacementClosure& a, const ReplacementClosure& b)
{
    if (a.runtimes != b.runtimes || a.stopOrder != b.stopOrder || a.startOrder != b.startOrder ||
        a.members.size() != b.members.size())
        return false;
    for (std::size_t i = 0; i != a.members.size(); ++i) {
        const auto& x = a.members[i];
        const auto& y = b.members[i];
        if (x.instance != y.instance || x.runtime != y.runtime || x.parent != y.parent || x.epoch != y.epoch ||
            x.revision != y.revision || x.state != y.state)
            return false;
    }
    return true;
}
} // namespace notebook::detail
namespace notebook {
using namespace cordis;
Result<void> Workspace::synchronizeUpgrade()
{
    if (!scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "workspace query requires owner"));
    return upgrade_ ? upgrade_->project() : Result<void>::success();
}
Result<GenerationId> Workspace::generation()
{
    auto current = synchronizeUpgrade();
    if (!current.hasValue())
        return Result<GenerationId>::failure(current.error());
    return Result<GenerationId>::success(notebook_ ? notebook_->generation : GenerationId{});
}
Result<Runtime> Workspace::notebookRuntime()
{
    auto current = synchronizeUpgrade();
    if (!current.hasValue())
        return Result<Runtime>::failure(current.error());
    if (!notebook_ || !notebook_->runtime)
        return Result<Runtime>::failure(makeError(ErrorCode::ContextClosing, "notebook factory released"));
    return Result<Runtime>::success(*notebook_->runtime);
}
Result<std::shared_ptr<const native_services::CurrentModule>> Workspace::counter()
{
    auto current = synchronizeUpgrade();
    if (!current.hasValue())
        return Result<std::shared_ptr<const native_services::CurrentModule>>::failure(current.error());
    return Result<std::shared_ptr<const native_services::CurrentModule>>::success(counter_);
}
Result<std::shared_ptr<const native_services::CurrentModule>> Workspace::reporter()
{
    auto current = synchronizeUpgrade();
    if (!current.hasValue())
        return Result<std::shared_ptr<const native_services::CurrentModule>>::failure(current.error());
    return Result<std::shared_ptr<const native_services::CurrentModule>>::success(reporter_);
}
Result<UpgradePreview> Workspace::previewUpgrade(const std::string& alias, const QString& sourceRoot,
                                                 const std::string& manifestPath)
{
    using Output = Result<UpgradePreview>;
    try {
        detail::require(synchronizeUpgrade());
        if (closeState_->closing || closeState_->closed || !session || !driver || !kernel || !scheduler->isOpen())
            return Output::failure(makeError(ErrorCode::ContextClosing, "workspace upgrade closed"));
        if (upgrade_)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "workspace upgrade already active"));
        const auto plugin = detail::businessPlugin(alias);
        const auto native = alias == "counter" ? counter_ : alias == "reporter" ? reporter_ : nullptr;
        const auto runtime = native                             ? native->runtime
                             : alias == "notebook" && notebook_ ? notebook_->runtime
                                                                : std::optional<Runtime>{};
        if (!runtime)
            return Output::failure(makeError(ErrorCode::InvalidArgument, "business module is not configured"));
        if (alias == "notebook" &&
            (!runtime->stateSchema() || runtime->stateSchema()->id != "org.cordis.example.notebook.draft"))
            return Output::failure(makeError(ErrorCode::ConfigInvalid, "legacy notebook has no draft state contract"));
        const auto generation = native ? native->generation : notebook_->generation;
        const auto origin = detail::require(manager->runtimeOrigin(runtime->id()));
        if (!origin || origin->generation != generation || origin->pluginId != plugin)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "current business factory source differs"));
        const auto state = detail::require(session->snapshot());
        if (!state.coordinator.desired || state.coordinator.activeRevision || state.closing || state.deliveryBlocked)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "configuration is not settled"));
        const auto root = QDir(sourceRoot).absolutePath().toUtf8().toStdString();
        auto artifact = detail::reviewBusinessArtifact(alias, root, manifestPath);
        detail::requireApproved(artifact, approvals_);
        const auto rows = detail::require(manager->snapshot());
        const auto previous =
            std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.generation == generation; });
        if (previous == rows.end() || previous->role != module::GenerationRole::Current || previous->failure ||
            previous->businessFailure)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "current module is not available"));
        if (previous->manifest.sha256 == artifact.manifest().sha256)
            return Output::failure(makeError(ErrorCode::InvalidArgument, "candidate is already current content"));
        auto impact = detail::require(kernel->replacementClosure({runtime->id()}));
        return Output::success({alias, root, generation, runtime->id(), state.coordinator.desiredRevision,
                                std::move(artifact), std::move(impact)});
    } catch (const detail::InitializationFailure& failure) {
        return Output::failure(failure.error);
    } catch (const std::exception& failure) {
        return Output::failure(makeError(ErrorCode::CallbackFailed, failure.what()));
    }
}
Result<Task<WorkspaceUpgradeReport>> Workspace::upgrade(const UpgradePreview& preview)
{
    using Output = Result<Task<WorkspaceUpgradeReport>>;
    auto checked =
        previewUpgrade(preview.alias, QString::fromUtf8(preview.sourceRoot.c_str()), preview.artifact.manifestPath());
    if (!checked.hasValue())
        return Output::failure(checked.error());
    const auto& current = checked.value();
    if (current.previous != preview.previous || current.previousRuntime != preview.previousRuntime ||
        current.desiredRevision != preview.desiredRevision || current.sourceRoot != preview.sourceRoot ||
        !detail::sameArtifact(current.artifact, preview.artifact) ||
        !detail::sameImpact(current.impact, preview.impact))
        return Output::failure(makeError(ErrorCode::ConfigConflict, "upgrade preview is stale or source changed"));
    auto execution = std::make_shared<detail::WorkspaceUpgradeExecution>(shared_from_this(), current);
    upgrade_ = execution;
    try {
        execution->start(current);
    } catch (const detail::InitializationFailure& failure) {
        execution->preparationFailed(failure.error);
    } catch (const std::exception& failure) {
        execution->preparationFailed(makeError(ErrorCode::CallbackFailed, failure.what()));
    } catch (...) {
        execution->preparationFailed(makeError(ErrorCode::CallbackFailed, "upgrade preparation threw"));
    }
    return Output::success(execution->completed.task());
}
Result<WorkspaceUpgradeSnapshot> Workspace::upgradeSnapshot()
{
    if (!scheduler->isCurrentThread())
        return Result<WorkspaceUpgradeSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "workspace upgrade snapshot requires owner"));
    const auto synced = synchronizeUpgrade();
    WorkspaceUpgradeSnapshot value{!upgrade_ && !closeState_->closing && !closeState_->closed && scheduler->isOpen(),
                                   false,
                                   {},
                                   {},
                                   std::vector<WorkspaceUpgradeReport>(upgradeHistory_.begin(), upgradeHistory_.end())};
    if (upgrade_) {
        auto report = upgrade_->report;
        if (!synced.hasValue()) {
            report.failure.emplace(synced.error());
            report.retained = true;
        }
        value.active.emplace(std::move(report));
        value.deliveryBlocked = upgrade_->blocked->load();
        value.active->deliveryBlocked = value.deliveryBlocked;
        value.active->retained = value.active->retained || value.deliveryBlocked;
        if (upgrade_->operation) {
            const auto operation = upgrade_->operation->snapshot();
            if (operation.hasValue())
                value.phase.emplace(operation.value().phase);
        }
    }
    return Result<WorkspaceUpgradeSnapshot>::success(std::move(value));
}
} // namespace notebook
