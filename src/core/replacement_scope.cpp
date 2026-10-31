/** @file replacement_scope.cpp
 * @brief 原子占用现有闭包、冻结旧业务并等待有序停止；不切换模块定义。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cordis::detail {
namespace {
ErrorInfo scopeError(ErrorCode code, const char* message, OperationId operation = {})
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.scope";
    error.operation = operation;
    return error;
}
Task<CleanupReport> failedStop(ErrorInfo error)
{
    Promise<CleanupReport> result;
    result.reject(std::move(error));
    return result.task();
}
bool sameSelection(const ReplacementSelection& a, const ReplacementSelection& b)
{
    if (a.closure.stopOrder != b.closure.stopOrder || a.closure.runtimes != b.closure.runtimes ||
        a.outputs.size() != b.outputs.size() || a.closure.members.size() != b.closure.members.size())
        return false;
    for (std::size_t i = 0; i != a.outputs.size(); ++i)
        if (a.outputs[i] < b.outputs[i] || b.outputs[i] < a.outputs[i])
            return false;
    for (std::size_t i = 0; i != a.closure.members.size(); ++i) {
        const auto& x = a.closure.members[i];
        const auto& y = b.closure.members[i];
        if (x.instance != y.instance || x.runtime != y.runtime || x.parent != y.parent || x.epoch != y.epoch ||
            x.revision != y.revision || x.state != y.state)
            return false;
    }
    return true;
}
void abandonScope(std::shared_ptr<ReplacementScopeControl> control) noexcept
{
    if (!control)
        return;
    control->abandoned.store(true);
    auto undo = [control] {
        if (control->state == ReplacementScopeState::Reserved || control->state == ReplacementScopeState::Frozen)
            (void)control->release();
    };
    if (control->scheduler->isCurrentThread()) {
        try {
            undo();
        } catch (...) {
            control->blocked.store(true);
        }
        return;
    }
    try {
        if (control->scheduler->post({std::move(undo), [control] { control->blocked.store(true); }}))
            return;
    } catch (...) {
    }
    control->blocked.store(true);
}
} // namespace

bool replacementAllowsActivation(const std::weak_ptr<ReplacementScopeControl>& weak)
{
    const auto control = weak.lock();
    return !control || control->state == ReplacementScopeState::Released;
}
bool replacementAllowsBusiness(const std::weak_ptr<ReplacementScopeControl>& weak)
{
    const auto control = weak.lock();
    return !control || control->state == ReplacementScopeState::Released ||
           (control->state == ReplacementScopeState::Reserved && !control->cancelled && !control->blocked.load() &&
            control->kernel && !control->kernel->closing_);
}
Result<void> replacementMutationAccess(const std::weak_ptr<ReplacementScopeControl>& weak)
{
    const auto control = weak.lock();
    return !control || control->state == ReplacementScopeState::Released
               ? Result<void>::success()
               : Result<void>::failure(
                     scopeError(ErrorCode::ConfigConflict, "instance reserved by replacement", control->operation));
}
Result<void> replacementOutputAccess(const std::shared_ptr<ServiceRegistryControl>& registry, const ServiceKey& key)
{
    if (!registry->scheduler->isCurrentThread())
        return Result<void>::failure(scopeError(ErrorCode::WrongThread, "service admission requires owner thread"));
    const auto found = registry->replacementOutputs.find(key);
    return found == registry->replacementOutputs.end()
               ? Result<void>::success()
               : Result<void>::failure(
                     scopeError(ErrorCode::ConfigConflict, "service key reserved by replacement", found->second));
}

Result<ReplacementScope> KernelControl::reserveReplacement(const std::vector<RuntimeId>& requested)
{
    using Output = Result<ReplacementScope>;
    auto selected = selectReplacement(requested);
    if (!selected.hasValue())
        return Output::failure(selected.error());
    const auto registry = services_->control_;
    const auto& selection = selected.value();
    auto conflict = [](OperationId operation) {
        return Output::failure(
            scopeError(ErrorCode::ConfigConflict, "replacement closure already reserved", operation));
    };
    for (const auto id : selection.closure.runtimes)
        if (auto at = registry->replacementRuntimes.find(id); at != registry->replacementRuntimes.end())
            return conflict(at->second);
    for (const auto& member : selection.closure.members)
        if (auto at = registry->replacementInstances.find(member.instance); at != registry->replacementInstances.end())
            return conflict(at->second);
    for (const auto& key : selection.outputs)
        if (auto at = registry->replacementOutputs.find(key); at != registry->replacementOutputs.end())
            return conflict(at->second);
    auto control = std::make_shared<ReplacementScopeControl>();
    try {
        control->operation = nextOperationId();
    } catch (const std::overflow_error&) {
        return Output::failure(scopeError(ErrorCode::InvalidArgument, "operation identities exhausted"));
    }
    control->kernel = shared_from_this();
    control->scheduler = scheduler_;
    control->selection = std::move(selected).value();
    // 所有可分配操作先在本地完成；发布四张索引及实例标记期间不调用外部代码。
    auto operations = registry->replacements;
    auto runtimes = registry->replacementRuntimes;
    auto instances = registry->replacementInstances;
    auto outputs = registry->replacementOutputs;
    operations.emplace(control->operation, control);
    for (const auto id : control->selection.closure.runtimes)
        runtimes.emplace(id, control->operation);
    for (const auto& member : control->selection.closure.members)
        instances.emplace(member.instance, control->operation);
    for (const auto& key : control->selection.outputs)
        outputs.emplace(key, control->operation);
    registry->replacements.swap(operations);
    registry->replacementRuntimes.swap(runtimes);
    registry->replacementInstances.swap(instances);
    registry->replacementOutputs.swap(outputs);
    for (const auto& member : control->selection.instances)
        member.second.setReplacement(control);
    for (const auto& entry : registry->bindings)
        if (entry.second->managedProvider && control->selection.instances.count(entry.second->provider))
            entry.second->replacement = control;
    return Output::success(ReplacementScope(std::move(control)));
}
Result<std::vector<ReplacementScopeSnapshot>> KernelControl::replacements() const
{
    using Output = Result<std::vector<ReplacementScopeSnapshot>>;
    if (!scheduler_->isCurrentThread())
        return Output::failure(scopeError(ErrorCode::WrongThread, "replacement diagnostics require owner thread"));
    std::vector<ReplacementScopeSnapshot> result;
    for (const auto& entry : services_->control_->replacements) {
        auto snapshot = entry.second->snapshot();
        if (!snapshot.hasValue())
            return Output::failure(snapshot.error());
        result.push_back(std::move(snapshot).value());
    }
    return Output::success(std::move(result));
}
Result<void> KernelControl::checkMount(const Runtime& runtime, const RealmMap& realms,
                                       const std::vector<ServiceRequirement>& additionalDependencies) const
{
    if (!scheduler_->isCurrentThread())
        return Result<void>::failure(scopeError(ErrorCode::WrongThread, "mount check requires owner thread"));
    if (closing_)
        return Result<void>::failure(scopeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen())
        return Result<void>::failure(scopeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    for (const auto& entry : runtimes_)
        if (entry.second.id() == runtime.id())
            return services_->replacementMountAccess(runtime, realms, additionalDependencies);
    return Result<void>::failure(scopeError(ErrorCode::InvalidArgument, "runtime not registered here"));
}
Task<void> KernelControl::whenReplacementReleased(OperationId operation) const
{
    if (!scheduler_->isCurrentThread())
        return failedTask(scopeError(ErrorCode::WrongThread, "replacement observation requires owner thread"));
    const auto at = services_->control_->replacements.find(operation);
    if (!operation.valid() || at == services_->control_->replacements.end())
        return failedTask(scopeError(ErrorCode::InvalidArgument, "replacement operation is not owned here"));
    return at->second->released.task();
}

Result<void> ReplacementScopeControl::check() const
{
    if (!scheduler->isCurrentThread())
        return Result<void>::failure(
            scopeError(ErrorCode::WrongThread, "replacement requires owner thread", operation));
    if (blocked.load() || !scheduler->isOpen())
        return Result<void>::failure(
            scopeError(ErrorCode::SchedulerClosed, "replacement delivery unavailable", operation));
    if (cancelled || !kernel || kernel->closing_)
        return Result<void>::failure(
            scopeError(ErrorCode::ContextClosing, "replacement cancelled by kernel close", operation));
    return Result<void>::success();
}
bool ReplacementScopeControl::deliveryBlocked() const
{
    if (blocked.load())
        return true;
    if (releasing)
        return true;
    if (state != ReplacementScopeState::Released && !scheduler->isOpen())
        return true;
    for (const auto& entry : selection.instances) {
        const auto snapshot = entry.second.snapshot();
        if (!snapshot.hasValue() || snapshot.value().deliveryBlocked || snapshot.value().resources.deliveryBlocked)
            return true;
    }
    if (kernel)
        for (const auto& entry : kernel->services_->control_->bindings)
            if (entry.second->replacement.lock().get() == this && entry.second->deliveryBlocked.load())
                return true;
    return false;
}
Result<ReplacementScopeSnapshot> ReplacementScopeControl::snapshot() const
{
    if (!scheduler->isCurrentThread())
        return Result<ReplacementScopeSnapshot>::failure(
            scopeError(ErrorCode::WrongThread, "replacement snapshot requires owner thread", operation));
    const bool unavailable = deliveryBlocked();
    ReplacementScopeSnapshot result{
        operation,
        unavailable && state != ReplacementScopeState::Released ? ReplacementScopeState::Failed : state,
        selection.closure,
        selection.outputs,
        abandoned.load(),
        unavailable,
        cancelled,
        {},
        {}};
    if (auto completed = stopped.task().tryResult()) {
        if (completed->hasValue())
            result.cleanup = completed->value();
        else
            result.failure = completed->error();
    }
    if (unavailable && !result.failure)
        result.failure = scopeError(ErrorCode::SchedulerClosed, "replacement resource delivery blocked", operation);
    if (!result.cleanup && ((state == ReplacementScopeState::Failed && !report.quiescent) ||
                            (cancelled && state == ReplacementScopeState::Released)))
        result.cleanup = report;
    return Result<ReplacementScopeSnapshot>::success(std::move(result));
}
Result<void> ReplacementScopeControl::freeze()
{
    auto admitted = check();
    if (!admitted.hasValue())
        return admitted;
    if (state != ReplacementScopeState::Reserved && state != ReplacementScopeState::Frozen)
        return Result<void>::failure(
            scopeError(ErrorCode::ContextClosing, "replacement cannot freeze in this phase", operation));
    auto latest = kernel->selectReplacement(selection.closure.runtimes);
    if (!latest.hasValue())
        return Result<void>::failure(latest.error());
    if (!sameSelection(selection, latest.value()))
        return Result<void>::failure(
            scopeError(ErrorCode::ConfigConflict, "replacement closure changed before freeze", operation));
    state = ReplacementScopeState::Frozen;
    return Result<void>::success();
}
Task<CleanupReport> ReplacementScopeControl::stop()
{
    if (!scheduler->isCurrentThread())
        return failedStop(scopeError(ErrorCode::WrongThread, "replacement stop requires owner thread", operation));
    if (state == ReplacementScopeState::Released && !stopped.task().isReady())
        return failedStop(scopeError(ErrorCode::OperationCancelled, "replacement released before stop", operation));
    if (state == ReplacementScopeState::Released || cancelled || state == ReplacementScopeState::Stopping ||
        state == ReplacementScopeState::Stopped || state == ReplacementScopeState::Failed)
        return stopped.task();
    auto admitted = check();
    if (!admitted.hasValue())
        return failedStop(admitted.error());
    if (state == ReplacementScopeState::Reserved) {
        auto frozen = freeze();
        if (!frozen.hasValue())
            return failedStop(frozen.error());
    }
    if (deliveryBlocked()) {
        blocked.store(true);
        state = ReplacementScopeState::Failed;
        stopped.reject(scopeError(ErrorCode::SchedulerClosed, "replacement resource delivery blocked", operation));
        return stopped.task();
    }
    state = ReplacementScopeState::Stopping;
    advanceStop();
    return stopped.task();
}
void ReplacementScopeControl::advanceStop()
{
    if (state != ReplacementScopeState::Stopping || cancelled)
        return;
    if (next == selection.closure.stopOrder.size()) {
        state = ReplacementScopeState::Stopped;
        stopped.resolve(report);
        return;
    }
    const auto self = shared_from_this();
    const auto id = selection.closure.stopOrder[next];
    const auto instance = selection.instances.at(id);
    auto source = instance.stopForReplacement(self);
    if (cancelled || state == ReplacementScopeState::Released)
        return;
    auto receipt = source.thenOn(scheduler, [self](const Result<CleanupReport>& outcome) {
        if (self->cancelled || self->state != ReplacementScopeState::Stopping)
            return;
        if (!outcome.hasValue()) {
            self->state = ReplacementScopeState::Failed;
            self->stopped.reject(outcome.error());
            return;
        }
        self->report.errors.insert(self->report.errors.end(), outcome.value().errors.begin(),
                                   outcome.value().errors.end());
        if (!outcome.value().quiescent) {
            self->report.quiescent = false;
            self->state = ReplacementScopeState::Failed;
            self->stopped.resolve(self->report);
            return;
        }
        ++self->next;
        self->advanceStop();
    });
    TaskAccess::observe(receipt, [self](Task<void>::Snapshot outcome) {
        if (!outcome->hasValue()) {
            self->blocked.store(true);
            self->stopped.reject(outcome->error());
        }
    });
}
Result<void> ReplacementScopeControl::release()
{
    if (!scheduler->isCurrentThread())
        return Result<void>::failure(
            scopeError(ErrorCode::WrongThread, "replacement release requires owner thread", operation));
    if (state == ReplacementScopeState::Released)
        return blocked.load() || releasing
                   ? Result<void>::failure(scopeError(ErrorCode::SchedulerClosed,
                                                      "replacement release delivery pending or failed", operation))
                   : Result<void>::success();
    auto admitted = check();
    if (!admitted.hasValue())
        return admitted;
    if (deliveryBlocked())
        return Result<void>::failure(
            scopeError(ErrorCode::SchedulerClosed, "replacement resource delivery blocked", operation));
    if (state != ReplacementScopeState::Reserved && state != ReplacementScopeState::Frozen &&
        state != ReplacementScopeState::Stopped)
        return Result<void>::failure(scopeError(ErrorCode::CleanupFailed, "replacement is not quiescent", operation));
    removeClaims(state != ReplacementScopeState::Stopped);
    if (blocked.load())
        return Result<void>::failure(
            scopeError(ErrorCode::SchedulerClosed, "replacement resume delivery failed", operation));
    return Result<void>::success();
}
void ReplacementScopeControl::removeClaims(bool resume)
{
    const auto self = shared_from_this();
    const auto owner = std::move(kernel);
    const auto registry = owner->services_->control_;
    auto instances = std::move(selection.instances);
    const auto previous = state;
    state = ReplacementScopeState::Released;
    releasing = true;
    struct Finish {
        ReplacementScopeControl& control;
        bool completed = false;
        ~Finish()
        {
            if (!completed)
                control.blocked.store(true);
            control.releasing = false;
        }
    } finish{*this};
    for (const auto& entry : instances)
        entry.second.clearReplacement(self);
    for (const auto& entry : registry->bindings)
        if (entry.second->replacement.lock() == self)
            entry.second->replacement.reset();
    for (const auto id : selection.closure.runtimes)
        if (auto at = registry->replacementRuntimes.find(id);
            at != registry->replacementRuntimes.end() && at->second == operation)
            registry->replacementRuntimes.erase(at);
    for (const auto& entry : instances)
        if (auto at = registry->replacementInstances.find(entry.first);
            at != registry->replacementInstances.end() && at->second == operation)
            registry->replacementInstances.erase(at);
    for (const auto& key : selection.outputs)
        if (auto at = registry->replacementOutputs.find(key);
            at != registry->replacementOutputs.end() && at->second == operation)
            registry->replacementOutputs.erase(at);
    registry->replacements.erase(operation);
    released.resolve();
    if (cancelled)
        stopped.resolve(report);
    else if (previous == ReplacementScopeState::Reserved || previous == ReplacementScopeState::Frozen)
        stopped.reject(scopeError(ErrorCode::OperationCancelled, "replacement released before stop", operation));
    if (resume)
        for (const auto& entry : instances)
            entry.second.resumeReplacement();
    if (!notifyRegistry(registry))
        blocked.store(true);
    for (const auto& entry : instances) {
        auto snapshot = entry.second.snapshot();
        if (!snapshot.hasValue() || snapshot.value().deliveryBlocked || snapshot.value().resources.deliveryBlocked)
            blocked.store(true);
    }
    finish.completed = true;
}
void ReplacementScopeControl::shutdown(bool finished, const CleanupReport& outcome)
{
    if (state == ReplacementScopeState::Released)
        return;
    cancelled = true;
    if (!finished) {
        stopped.reject(scopeError(ErrorCode::OperationCancelled, "replacement cancelled by kernel close", operation));
        return;
    }
    report = outcome;
    if (outcome.quiescent)
        removeClaims(false);
    else {
        state = ReplacementScopeState::Failed;
        stopped.resolve(report);
    }
}
void shutdownReplacementScopes(const std::shared_ptr<ServiceRegistryControl>& registry, bool finished,
                               const CleanupReport& outcome)
{
    std::vector<std::shared_ptr<ReplacementScopeControl>> controls;
    for (const auto& entry : registry->replacements)
        controls.push_back(entry.second);
    for (const auto& control : controls)
        control->shutdown(finished, outcome);
}
} // namespace cordis::detail

namespace cordis {
Result<void>
ServiceRegistry::replacementMountAccess(const Runtime& runtime, const RealmMap& realms,
                                        const std::vector<ServiceRequirement>& additionalDependencies) const
{
    auto own = replacementContractAccess(runtime.id(), runtime.requirements(), runtime.outputs(), realms);
    if (!own.hasValue())
        return own;
    for (const auto& dependency : additionalDependencies) {
        auto allowed = detail::replacementOutputAccess(control_, realms.resolve(dependency.key));
        if (!allowed.hasValue())
            return allowed;
    }
    return Result<void>::success();
}
Result<void> ServiceRegistry::replacementContractAccess(RuntimeId runtime,
                                                        const std::vector<ServiceRequirement>& dependencies,
                                                        const std::vector<ServiceOutput>& outputs,
                                                        const RealmMap& realms) const
{
    if (!control_->scheduler->isCurrentThread())
        return Result<void>::failure(
            detail::scopeError(ErrorCode::WrongThread, "mount admission requires owner thread"));
    if (auto at = control_->replacementRuntimes.find(runtime); at != control_->replacementRuntimes.end())
        return Result<void>::failure(
            detail::scopeError(ErrorCode::ConfigConflict, "runtime reserved by replacement", at->second));
    for (const auto& output : outputs) {
        auto allowed = detail::replacementOutputAccess(control_, realms.resolve(output.key));
        if (!allowed.hasValue())
            return allowed;
    }
    for (const auto& dependency : dependencies) {
        auto allowed = detail::replacementOutputAccess(control_, realms.resolve(dependency.key));
        if (!allowed.hasValue())
            return allowed;
    }
    return Result<void>::success();
}
ReplacementScope& ReplacementScope::operator=(ReplacementScope&& other) noexcept
{
    if (this != &other) {
        auto previous = std::exchange(control_, std::move(other.control_));
        detail::abandonScope(std::move(previous));
    }
    return *this;
}
ReplacementScope::~ReplacementScope()
{
    detail::abandonScope(std::move(control_));
}
Result<ReplacementScopeSnapshot> ReplacementScope::snapshot() const
{
    const auto control = control_;
    return control ? control->snapshot()
                   : Result<ReplacementScopeSnapshot>::failure(
                         detail::scopeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Result<void> ReplacementScope::freeze() const
{
    const auto control = control_;
    return control
               ? control->freeze()
               : Result<void>::failure(detail::scopeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Task<CleanupReport> ReplacementScope::stop() const
{
    const auto control = control_;
    return control ? control->stop()
                   : detail::failedStop(detail::scopeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Result<void> ReplacementScope::release() const
{
    const auto control = control_;
    return control
               ? control->release()
               : Result<void>::failure(detail::scopeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
} // namespace cordis
