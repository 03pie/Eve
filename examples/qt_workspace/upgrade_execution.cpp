/** @file upgrade_execution.cpp
 * @brief 实际候选准备与原整体事务；当前投影只核对原权威值。
 */
#include "upgrade_p.hpp"
#include <algorithm>

namespace notebook::detail {
using namespace cordis;
using namespace cordis::module;
WorkspaceUpgradeExecution::WorkspaceUpgradeExecution(std::shared_ptr<Workspace> workspace,
                                                     const UpgradePreview& preview)
    : owner(std::move(workspace)), previousRuntime(preview.previousRuntime)
{
    report.alias = preview.alias;
    report.previous = preview.previous;
    if (preview.alias == "notebook")
        previousNotebook = owner->notebook_;
    else
        previousNative = preview.alias == "counter" ? owner->counter_ : owner->reporter_;
}
void WorkspaceUpgradeExecution::start(const UpgradePreview& preview)
{
    const auto pluginId = businessPlugin(report.alias);
    const auto baseline = owner->driver->configurationDefinitions();
    if (!baseline)
        throw InitializationFailure{makeError(ErrorCode::ConfigConflict, "configuration definitions unavailable")};
    previousLocator = require(baseline->module(pluginId)).manifest;
    installed.emplace(
        require(installModuleArtifact(preview.sourceRoot, preview.artifact.manifestPath(), owner->pluginRoot_)));
    const auto& copied = installed->artifact().manifest();
    const auto& selected = preview.artifact.manifest();
    if (!sameManifest(copied, selected))
        throw InitializationFailure{
            makeError(ErrorCode::ConfigConflict, "candidate source changed during installation")};
    candidateLocator = copied.sha256 + "/module.json";
    const auto staged =
        report.alias == "notebook"
            ? stageQtModule(owner->pluginRoot_, candidateLocator, report.previous)
            : owner->manager->stage(owner->pluginRoot_, candidateLocator, report.previous, {}, owner->approvals_);
    if (!staged.hasValue()) {
        auto failure = staged.error();
        const auto rows = owner->manager->snapshot();
        if (!rows.hasValue()) {
            failure.causes.push_back(rows.error());
            report.retained = true;
            throw InitializationFailure{std::move(failure)};
        }
        const auto& expected = installed->artifact();
        const ModuleGenerationSnapshot* matched = nullptr;
        for (const auto& row : rows.value()) {
            if (row.role != GenerationRole::Candidate || row.predecessor != report.previous ||
                !sameManifest(row.manifest, expected.manifest()) || row.manifestPath != expected.manifestPath() ||
                row.libraryPath != expected.libraryPath())
                continue;
            if (matched) {
                failure.causes.push_back(
                    makeError(ErrorCode::ConfigConflict, "failed stage has multiple matching candidate generations"));
                report.retained = true;
                throw InitializationFailure{std::move(failure)};
            }
            matched = &row;
        }
        if (matched)
            report.candidate = matched->generation;
        throw InitializationFailure{std::move(failure)};
    }
    report.candidate = staged.value().generation;
    std::optional<loader::ConfiguredPlugin> plugin;
    if (report.alias == "notebook") {
        candidateNotebook = std::make_shared<NotebookModule>(report.candidate);
        plugin.emplace(prepareNotebook(candidateNotebook, owner->manager, owner->scheduler,
                                       {report.alias, candidateLocator}, true));
        owner->registerQtOutputSource(plugin->runtime.id(), report.candidate);
    } else {
        candidateNative = std::make_shared<native_services::CurrentModule>(
            report.alias == "counter" ? native_services::ServiceKind::Counter : native_services::ServiceKind::Reporter,
            report.candidate);
        require(native_services::prepareServiceModule(candidateNative, *owner->manager, owner->scheduler,
                                                      owner->kernel->services(), owner->nativeLogSink_));
        plugin.emplace(
            require(native_services::configuredServicePlugin(candidateNative, {report.alias, candidateLocator})));
    }
    candidateRuntime = plugin->runtime.id();
    const auto state = require(owner->session->snapshot());
    if (!state.coordinator.desired || state.coordinator.desiredRevision != preview.desiredRevision || cancelRequested ||
        owner->closeState_->closing)
        throw InitializationFailure{makeError(
            cancelRequested || owner->closeState_->closing ? ErrorCode::OperationCancelled : ErrorCode::ConfigConflict,
            "configuration changed or workspace closed during preparation")};
    auto definitions = require(owner->driver->configurationDefinitions()->replacing({*plugin}));
    auto source =
        require(loader::setModuleManifest(state.coordinator.desired->source(), report.alias, candidateLocator));
    auto target = std::make_shared<const loader::ValidatedCandidate>(require(definitions->validate(std::move(source))));
    ReplacementRequest request;
    request.scheduler = owner->scheduler;
    request.kernel = owner->kernel;
    request.modules = owner->manager;
    request.previous = report.previous;
    request.candidate = report.candidate;
    request.pluginRoot = owner->pluginRoot_;
    request.definitions.push_back({previousRuntime, plugin->runtime});
    request.configurations.push_back({owner->session, owner->driver, definitions, target, preview.desiredRevision});
    auto begun = ReplacementOperation::begin(std::move(request));
    if (!begun.hasValue())
        throw InitializationFailure{begun.error()};
    operation.emplace(std::move(begun).value());
    if (cancelRequested || owner->closeState_->closing)
        (void)cancel();
    const auto self = shared_from_this();
    watch(operation->settled(), [self](const Result<ReplacementReport>& result) { self->settled(result); });
}
Result<void> WorkspaceUpgradeExecution::project()
{
    if (!owner->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "upgrade projection requires owner"));
    if (previousLocator.empty())
        return Result<void>::failure(
            makeError(ErrorCode::ConfigConflict, "upgrade preparation authority is not fixed"));
    if (operation) {
        auto current = operation->snapshot();
        if (!current.hasValue())
            return Result<void>::failure(current.error());
        report.replacement.emplace(current.value().report);
        if (current.value().deliveryBlocked)
            blocked->store(true);
    }
    report.deliveryBlocked = blocked->load();
    report.retained =
        report.retained || report.deliveryBlocked || (report.replacement && report.replacement->resourcesRetained);
    if (!owner->driver || !owner->kernel)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "upgrade authority released"));
    const bool committed = report.replacement && report.replacement->committed;
    const auto expectedGeneration = committed ? report.candidate : report.previous;
    const auto expectedRuntime = committed ? candidateRuntime : previousRuntime;
    const auto rows = owner->manager->snapshot();
    if (!rows.hasValue())
        return Result<void>::failure(rows.error());
    const auto current = std::find_if(rows.value().begin(), rows.value().end(),
                                      [&](const auto& row) { return row.generation == expectedGeneration; });
    if (current == rows.value().end() || current->role != GenerationRole::Current)
        return Result<void>::failure(
            makeError(ErrorCode::ConfigConflict, "module role does not match actual transaction"));
    const auto core = owner->kernel->snapshot();
    if (!core.hasValue())
        return Result<void>::failure(core.error());
    const auto name = businessPlugin(report.alias);
    const auto canonical = std::find_if(core.value().names.begin(), core.value().names.end(),
                                        [&](const auto& row) { return row.name == name; });
    const auto definitions = owner->driver->configurationDefinitions();
    if (canonical == core.value().names.end() || canonical->id != expectedRuntime || !definitions)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "canonical business factory differs"));
    const auto source = definitions->module(name);
    const auto origin = owner->manager->runtimeOrigin(expectedRuntime);
    if (!source.hasValue())
        return Result<void>::failure(source.error());
    if (!origin.hasValue())
        return Result<void>::failure(origin.error());
    if (!origin.value() || origin.value()->generation != expectedGeneration || origin.value()->pluginId != name ||
        source.value().alias != report.alias ||
        source.value().manifest != (committed ? candidateLocator : previousLocator))
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "driver and actual factory sources differ"));
    // 原执行仍保留两代，指针交换不运行析构清理、插件或再次共同提交。
    if (report.alias == "notebook")
        owner->notebook_ = committed ? candidateNotebook : previousNotebook;
    else if (report.alias == "counter")
        owner->counter_ = committed ? candidateNative : previousNative;
    else
        owner->reporter_ = committed ? candidateNative : previousNative;
    owner->definitions_ = definitions;
    return Result<void>::success();
}
Result<void> WorkspaceUpgradeExecution::cancel()
{
    if (operation)
        return operation->cancel();
    cancelRequested = true;
    return Result<void>::success();
}
void WorkspaceUpgradeExecution::retain(ErrorInfo failure)
{
    if (!report.failure)
        report.failure.emplace(std::move(failure));
    else if (report.failure->code != failure.code || report.failure->message != failure.message)
        report.failure->causes.push_back(std::move(failure));
    report.retained = true;
    report.deliveryBlocked = blocked->load();
    completed.resolve(report);
}
void WorkspaceUpgradeExecution::preparationFailed(ErrorInfo failure)
{
    if (report.retained) {
        retain(std::move(failure));
        return;
    }
    report.failure.emplace(failure);
    const auto claims = owner->kernel->replacements();
    const auto modules = owner->manager->snapshot();
    if (!claims.hasValue() || !modules.hasValue()) {
        retain(!claims.hasValue() ? claims.error() : modules.error());
        return;
    }
    for (const auto& claim : claims.value())
        if (std::find(claim.closure.runtimes.begin(), claim.closure.runtimes.end(), previousRuntime) !=
            claim.closure.runtimes.end()) {
            retain(std::move(failure));
            return;
        }
    for (const auto& row : modules.value())
        if ((row.generation == report.previous || row.generation == report.candidate) &&
            (row.replacement || (row.generation == report.previous && row.role != GenerationRole::Current))) {
            retain(std::move(failure));
            return;
        }
    retire();
}
void WorkspaceUpgradeExecution::settled(const Result<ReplacementReport>& result)
{
    if (result.hasValue())
        report.replacement.emplace(result.value());
    else
        report.failure.emplace(result.error());
    const auto projected = project();
    if (!projected.hasValue()) {
        retain(projected.error());
        return;
    }
    if (!result.hasValue() || report.retained) {
        retain(!result.hasValue() ? result.error()
                                  : makeError(ErrorCode::CleanupFailed, "replacement retained its original execution"));
        return;
    }
    // 正常 settled 已由原操作释放 Execution；这里只保留无工厂的门面供进度查询。
    retire();
}
} // namespace notebook::detail
