/** @file replacement_inspect.cpp
 * @brief 宿主读取真实原输入和候选身份；不承担替换执行或提交。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <algorithm>

namespace cordis::detail {
Result<CoreReplacementAccess::PlanningContext> ReplacementScopeControl::planningContext(bool recovery) const
{
    using Output = Result<CoreReplacementAccess::PlanningContext>;
    auto allowed = check();
    if (!allowed.hasValue())
        return Output::failure(allowed.error());
    auto inputs = checkInputs(recovery);
    if (!inputs.hasValue())
        return Output::failure(inputs.error());
    const auto registry = kernel->services_->control_;
    CoreReplacementAccess::PlanningContext result{{}, registry->realms, {}};
    std::set<InstanceId> old;
    for (const auto& member : selection.closure.members)
        old.insert(member.instance);
    for (const auto& row : registry->bindings) {
        const auto& binding = *row.second;
        if (old.count(binding.provider) || registry->replacementOutputs.count(row.first) ||
            !binding.replacement.expired() || binding.deliveryBlocked.load() || !availableBinding(binding, {}))
            continue;
        result.providers.push_back({binding.key, binding.type, true, binding.metadata});
        result.bindings.emplace(binding.key, binding.id);
    }
    return Output::success(std::move(result));
}
Result<CoreReplacementAccess::PlanningContext>
CoreReplacementAccess::planningContext(const ReplacementScope& scope, const Kernel& kernel, bool recovery)
{
    if (!scope.control_ || scope.control_->kernel != kernel.control_)
        return Result<PlanningContext>::failure(
            makeError(ErrorCode::ConfigConflict, "plan requires actual scope Kernel"));
    return scope.control_->planningContext(recovery);
}
Result<void> CoreReplacementAccess::release(const ReplacementScope& scope,
                                            const std::function<void()>& beforeNotifications)
{
    const auto control = scope.control_;
    if (!control)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "release requires actual scope"));
    return control->release(beforeNotifications);
}
Result<void> CoreReplacementAccess::borrow(const ReplacementScope& scope, const Kernel& kernel,
                                           const std::vector<Runtime>& factories)
{
    const auto control = scope.control_;
    if (!control || control->kernel != kernel.control_)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "borrow requires actual scope Kernel"));
    return control->borrow(factories);
}
Task<CleanupReport> CoreReplacementAccess::whenKernelClosed(const Kernel& kernel)
{
    return kernel.control_->closed_.task();
}
bool CoreReplacementAccess::kernelClosing(const Kernel& kernel) noexcept
{
    return kernel.control_->closing_;
}
Result<ReplacementBuilder> CoreReplacementAccess::builder(const ReplacementScope& scope, const Kernel& kernel)
{
    const auto control = scope.control_;
    if (!control || control->kernel != kernel.control_)
        return Result<ReplacementBuilder>::failure(
            makeError(ErrorCode::ConfigConflict, "builder requires actual scope Kernel"));
    return control->builder();
}
Result<ReplacementScope> CoreReplacementAccess::reserve(Kernel& kernel, const std::vector<RuntimeId>& runtimes,
                                                        const std::vector<InstanceId>& anchors)
{
    return kernel.control_->reserveReplacement(runtimes, anchors);
}
std::shared_ptr<std::atomic_bool> CoreReplacementAccess::failureSignal(const ReplacementScope& scope,
                                                                       const Kernel& kernel) noexcept
{
    const auto control = scope.control_;
    if (!control || control->ownerThread != std::this_thread::get_id() || control->kernel != kernel.control_)
        return {};
    return control->blocked.signal;
}
void CoreReplacementAccess::retainFailure(const ReplacementScope& scope) noexcept
{
    const auto control = scope.control_;
    if (control && control->ownerThread == std::this_thread::get_id() &&
        control->state != ReplacementScopeState::Released)
        control->state = ReplacementScopeState::Failed;
}
Result<PreparedReplacementView> ReplacementScopeControl::inspectPrepared() const
{
    using Output = Result<PreparedReplacementView>;
    auto inputs = checkInputs(preparedMode == ReplacementCommitMode::Recovery);
    if (!inputs.hasValue())
        return Output::failure(inputs.error());
    PreparedReplacementView view{preparedStamp, operation, preparedMode, selection.closure, {}, {}};
    for (const auto& entry : candidateDefinitions)
        view.definitions.push_back({entry.first, entry.second.id()});
    view.members.reserve(preparedMembers.size());
    for (const auto& member : preparedMembers) {
        if (!member.preparedForPublication())
            return Output::failure(makeError(ErrorCode::ProvisionIncomplete, "prepared member is no longer ready"));
        auto input = member.captureReplacementInput({});
        const auto parent = kernel->services_->control_->parents.find(input.instance);
        if (parent != kernel->services_->control_->parents.end())
            input.parent = parent->second;
        view.members.push_back({member, std::move(input)});
    }
    return Output::success(std::move(view));
}
Result<PreparedReplacementView> CoreReplacementAccess::inspect(const ReplacementScope& scope, const Kernel& kernel)
{
    using Output = Result<PreparedReplacementView>;
    const auto control = scope.control_;
    const auto owner = kernel.control_;
    if (!control)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement scope is empty"));
    auto admitted = control->check();
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (control->state != ReplacementScopeState::Prepared || control->kernel != owner)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement is not prepared"));
    return control->inspectPrepared();
}
Result<OriginalReplacementView> CoreReplacementAccess::original(const ReplacementScope& scope, const Kernel& kernel)
{
    using Output = Result<OriginalReplacementView>;
    const auto control = scope.control_;
    const auto owner = kernel.control_;
    if (!control)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement scope is empty"));
    auto admitted = control->check();
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (control->kernel != owner || control->state != ReplacementScopeState::Reserved ||
        control->candidateDefinitions.size() != control->selection.closure.runtimes.size())
        return Output::failure(makeError(ErrorCode::ConfigConflict, "replacement definitions are not reserved here"));
    OriginalReplacementView view{control, control->operation, control->selection.closure, {}, {}};
    for (const auto& definition : control->candidateDefinitions) {
        view.definitions.push_back({definition.first, definition.second.id()});
        const auto previous = std::find_if(control->kernel->runtimes_.begin(), control->kernel->runtimes_.end(),
                                           [&](const auto& item) { return item.second.id() == definition.first; });
        if (previous == control->kernel->runtimes_.end())
            return Output::failure(makeError(ErrorCode::ConfigConflict, "canonical runtime changed before inspection"));
        view.factories.push_back({previous->second, definition.second});
    }
    view.members.reserve(control->selection.inputs.size());
    for (const auto& input : control->selection.inputs)
        view.members.push_back({control->selection.instances.at(input.instance), input});
    return Output::success(std::move(view));
}
bool CoreReplacementAccess::current(const OriginalReplacementView& view) noexcept
{
    const auto control = view.owner.lock();
    return control && control->ownerThread == std::this_thread::get_id() && control->operation == view.operation &&
           control->state == ReplacementScopeState::Reserved && !control->cancelled && !control->blocked.load() &&
           !control->abandoned.load() && control->kernel && !control->kernel->closing_;
}
bool CoreReplacementAccess::current(const PreparedReplacementView& view) noexcept
{
    const auto control = view.stamp ? view.stamp->owner.lock() : nullptr;
    return control && control->ownerThread == std::this_thread::get_id() &&
           control->state == ReplacementScopeState::Prepared && control->preparedStamp == view.stamp &&
           !control->cancelled && !control->blocked.load() && !control->abandoned.load() && control->kernel &&
           !control->kernel->closing_ &&
           control->inputsAvailable(control->preparedMode == ReplacementCommitMode::Recovery);
}
} // namespace cordis::detail
