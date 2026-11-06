/** @file replacement_stage.cpp
 * @brief 同一作用域与发布视图授权候选；真实清理完成前不解除运行占用。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace cordis::detail {
void registerReplacementStageInstance(const std::shared_ptr<PublicationControl>& publication, const Instance& instance)
{
    if (replacementViewAllowsStage(publication))
        publication->replacement.lock()->registerStage(instance);
}
namespace {
ErrorInfo stageError(ErrorCode code, const char* message, OperationId operation = {})
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.stage";
    error.operation = operation;
    return error;
}
Task<CleanupReport> rejected(ErrorInfo error)
{
    Promise<CleanupReport> result;
    result.reject(std::move(error));
    return result.task();
}
} // namespace
Result<void> ReplacementScopeControl::beginStage()
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if ((state != ReplacementScopeState::Stopped && state != ReplacementScopeState::Staging) || deliveryBlocked())
        return Result<void>::failure(stageError(ErrorCode::ContextClosing, "old closure is not quiescent", operation));
    if (state == ReplacementScopeState::Stopped) {
        auto group = kernel->holdPublication();
        if (!group.hasValue())
            return Result<void>::failure(group.error());
        if (state != ReplacementScopeState::Stopped || blocked.load() || cancelled || kernel->closing_) {
            (void)group.value().abort();
            return Result<void>::failure(
                stageError(ErrorCode::ContextClosing, "stage invalidated while opening group", operation));
        }
        staging.emplace(std::move(group).value());
        staging->control_->replacement = shared_from_this();
        discarded = Promise<CleanupReport>();
        stageReport = {true, {}};
        state = ReplacementScopeState::Staging;
    }
    return Result<void>::success();
}
Result<ReplacementBuilder> ReplacementScopeControl::builder()
{
    auto opened = beginStage();
    if (!opened.hasValue())
        return Result<ReplacementBuilder>::failure(opened.error());
    if (!stageOpen(staging->control_))
        return Result<ReplacementBuilder>::failure(
            stageError(ErrorCode::ContextClosing, "construction group is not open", operation));
    return Result<ReplacementBuilder>::success(ReplacementBuilder(shared_from_this(), staging->control_));
}
Result<Instance> ReplacementScopeControl::stage(const Runtime& runtime, std::string config, RealmMap realms,
                                                std::vector<ServiceRequirement> additionalDependencies,
                                                const std::shared_ptr<PublicationControl>& permission)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return Result<Instance>::failure(allowed.error());
    if (permission && !stageOpen(permission))
        return Result<Instance>::failure(
            stageError(ErrorCode::ContextClosing, "construction permission expired", operation));
    if ((state != ReplacementScopeState::Stopped && state != ReplacementScopeState::Staging) || deliveryBlocked())
        return Result<Instance>::failure(
            stageError(ErrorCode::ContextClosing, "old closure is not quiescent", operation));
    if (std::none_of(kernel->runtimes_.begin(), kernel->runtimes_.end(),
                     [&](const auto& entry) { return entry.second.id() == runtime.id(); }) &&
        !ownsDefinition(runtime.id()))
        return Result<Instance>::failure(
            stageError(ErrorCode::InvalidArgument, "candidate runtime is not registered here", operation));
    auto opened = beginStage();
    if (!opened.hasValue())
        return Result<Instance>::failure(opened.error());
    if (permission && !stageOpen(permission))
        return Result<Instance>::failure(
            stageError(ErrorCode::ContextClosing, "construction permission changed", operation));
    const auto group = *staging;
    auto claimed = claimStage(group.control_, runtime, realms, additionalDependencies, true);
    if (!claimed.hasValue())
        return Result<Instance>::failure(claimed.error());
    return kernel->mountHeld(group, runtime, std::move(config), std::move(realms), std::move(additionalDependencies));
}
Result<void> ReplacementScopeControl::claimStage(const std::shared_ptr<PublicationControl>& permission,
                                                 const Runtime& runtime, const RealmMap& realms,
                                                 const std::vector<ServiceRequirement>& additionalDependencies,
                                                 bool root)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if (!stageOpen(permission) || !runtime.id().valid())
        return Result<void>::failure(
            stageError(ErrorCode::ContextClosing, "candidate creation is not open", operation));
    const auto registry = kernel->services_->control_;
    std::set<ServiceKey> keys;
    for (const auto& output : runtime.outputs())
        keys.insert(realms.resolve(output.key));
    auto outputsAllowed = checkOutputs(keys, staging->control_);
    if (!outputsAllowed.hasValue())
        return outputsAllowed;
    if (const auto at = registry->replacementRuntimes.find(runtime.id());
        at != registry->replacementRuntimes.end() && at->second != operation)
        return Result<void>::failure(
            stageError(ErrorCode::ConfigConflict, "candidate runtime belongs to another replacement", operation));
    for (const auto* requirements : {&runtime.requirements(), &additionalDependencies})
        for (const auto& requirement : *requirements) {
            const auto key = realms.resolve(requirement.key);
            if (!kernel->services_->containsRealm(key.realm))
                return Result<void>::failure(
                    stageError(ErrorCode::InvalidArgument, "candidate input has foreign realm", operation));
            if (const auto at = registry->replacementOutputs.find(key);
                at != registry->replacementOutputs.end() && at->second != operation)
                return Result<void>::failure(
                    stageError(ErrorCode::ConfigConflict, "candidate input belongs to another replacement", operation));
        }
    if (!stageOpen(permission))
        return Result<void>::failure(
            stageError(ErrorCode::ContextClosing, "construction permission changed during checks", operation));
    // 所有分配先完成，随后只交换宿主索引；不在权限发布中调用插件或调度器。
    auto runtimes = registry->replacementRuntimes;
    auto outputs = registry->replacementOutputs;
    auto ownedRuntimes = stagedRuntimes;
    auto ownedOutputs = stagedOutputs;
    // 新顶层工厂占用 RuntimeId；恢复的消费者按真实服务键隔离，不能锁住其他 realm 的同类消费者。
    const bool replacingRuntime = root && !readsDefinition(runtime.id());
    if (replacingRuntime) {
        for (const auto& entry : registry->replacements)
            if (entry.first != operation && entry.second->readsDefinition(runtime.id()))
                return Result<void>::failure(
                    stageError(ErrorCode::ConfigConflict, "candidate factory has other readers", entry.first));
        runtimes.emplace(runtime.id(), operation);
        ownedRuntimes.insert(runtime.id());
    }
    for (const auto& key : keys) {
        outputs.emplace(key, operation);
        ownedOutputs.insert(key);
    }
    registry->replacementRuntimes.swap(runtimes);
    registry->replacementOutputs.swap(outputs);
    stagedRuntimes.swap(ownedRuntimes);
    stagedOutputs.swap(ownedOutputs);
    return Result<void>::success();
}
void ReplacementScopeControl::registerStage(const Instance& instance)
{
    if (state != ReplacementScopeState::Staging || cancelled)
        throw std::invalid_argument("candidate creation closed during preparation");
    const auto id = instance.snapshot().value().instance;
    auto instances = staged;
    auto claims = kernel->services_->control_->replacementInstances;
    instances.emplace(id, instance);
    claims.emplace(id, operation);
    staged.swap(instances);
    kernel->services_->control_->replacementInstances.swap(claims);
    instance.setReplacement(shared_from_this());
}
Task<CleanupReport> ReplacementScopeControl::discard()
{
    if (!scheduler->isCurrentThread())
        return rejected(stageError(ErrorCode::WrongThread, "candidate cleanup requires owner thread", operation));
    if ((staging || state == ReplacementScopeState::Stopped) &&
        (state == ReplacementScopeState::Discarding || discarded.task().isReady()))
        return discarded.task();
    auto allowed = check();
    if (!allowed.hasValue())
        return rejected(allowed.error());
    if (state == ReplacementScopeState::Prepared && !staging) {
        state = ReplacementScopeState::Stopped;
        discarded.resolve(stageReport);
        return discarded.task();
    }
    if ((state != ReplacementScopeState::Staging && state != ReplacementScopeState::Prepared) || !staging)
        return rejected(stageError(ErrorCode::ContextClosing, "no candidate publication to discard", operation));
    const auto self = shared_from_this();
    state = ReplacementScopeState::Discarding;
    pendingStage = staged.size();
    try {
        auto aborted = staging->abort();
        if (!aborted.hasValue()) {
            blocked.store(true);
            discarded.reject(aborted.error());
            return discarded.task();
        }
    } catch (const std::exception& error) {
        blocked.store(true);
        discarded.reject(makeError(ErrorCode::CleanupFailed, error.what()));
        return discarded.task();
    } catch (...) {
        blocked.store(true);
        discarded.reject(stageError(ErrorCode::CleanupFailed, "candidate abort threw", operation));
        return discarded.task();
    }
    if (cancelled)
        return discarded.task();
    if (staged.empty()) {
        state = ReplacementScopeState::Stopped;
        discarded.resolve(stageReport);
    }
    for (const auto& entry : staged) {
        auto receipt = entry.second.stopForReplacement(self).thenOn(
            scheduler, [self](const Result<CleanupReport>& result) { self->finishStage(result); });
        TaskAccess::observe(receipt, [self](Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                self->blocked.store(true);
                self->discarded.reject(result->error());
            }
        });
    }
    return discarded.task();
}
void ReplacementScopeControl::finishStage(const Result<CleanupReport>& result)
{
    if (cancelled || state != ReplacementScopeState::Discarding)
        return;
    if (!result.hasValue()) {
        stageReport.quiescent = false;
        stageReport.errors.push_back(result.error());
    } else {
        stageReport.quiescent = stageReport.quiescent && result.value().quiescent;
        stageReport.errors.insert(stageReport.errors.end(), result.value().errors.begin(), result.value().errors.end());
    }
    if (--pendingStage != 0)
        return;
    state = stageReport.quiescent ? ReplacementScopeState::Stopped : ReplacementScopeState::Failed;
    discarded.resolve(stageReport);
}
} // namespace cordis::detail
namespace cordis {
Result<Instance> ReplacementBuilder::stage(const Runtime& runtime, std::string config, RealmMap realms,
                                           std::vector<ServiceRequirement> dependencies) const
{
    const auto owner = owner_.lock();
    const auto permission = publication_.lock();
    if (!owner || !permission)
        return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "construction permission expired"));
    return owner->stage(runtime, std::move(config), std::move(realms), std::move(dependencies), permission);
}
Result<Instance> ReplacementScope::stage(const Runtime& runtime, std::string config, RealmMap realms,
                                         std::vector<ServiceRequirement> additionalDependencies) const
{
    const auto control = control_;
    return control ? control->stage(runtime, std::move(config), std::move(realms), std::move(additionalDependencies))
                   : Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Task<CleanupReport> ReplacementScope::discard() const
{
    const auto control = control_;
    return control ? control->discard()
                   : detail::rejected(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
} // namespace cordis
