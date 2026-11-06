/** @file replacement_inputs.cpp
 * @brief 实际绑定的事务读意图，不伪造实例租约或另建运行图。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
namespace cordis::detail {
namespace {
ErrorInfo inputError(ErrorCode code, const char* message, OperationId operation)
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.inputs";
    error.operation = operation;
    return error;
}
} // namespace
bool ReplacementScopeControl::inputsAvailable(bool recovery) const noexcept
{
    for (const auto& entry : readBindings) {
        const auto& read = entry.second;
        if (!(recovery ? read.recovery : read.candidate))
            continue;
        if (!kernel || read.binding->deliveryBlocked.load() || !availableBinding(*read.binding, {}))
            return false;
        const auto& registry = kernel->services_->control_;
        const auto current = registry->bindings.find(entry.first);
        if (current == registry->bindings.end() || current->second != read.binding)
            return false;
    }
    return true;
}
Result<void> ReplacementScopeControl::checkInputs(bool recovery) const
{
    return inputsAvailable(recovery)
               ? Result<void>::success()
               : Result<void>::failure(inputError(ErrorCode::DependencyMissing,
                                                  "replacement input binding is no longer available", operation));
}
Result<void> replacementBindingReadAccess(const std::shared_ptr<BindingControl>& binding)
{
    if (binding->state == BindingState::Retiring || binding->state == BindingState::Released)
        return Result<void>::success();
    if (const auto registry = binding->owner.lock())
        for (const auto& entry : registry->replacements) {
            const auto& scope = entry.second;
            const auto read = scope->readBindings.find(binding->key);
            if (read != scope->readBindings.end() && read->second.binding == binding)
                return Result<void>::failure(
                    inputError(ErrorCode::ConfigConflict, "service binding is read by replacement", entry.first));
        }
    return Result<void>::success();
}
Result<void> replacementProviderAccess(const std::shared_ptr<ServiceRegistryControl>& registry, InstanceId instance)
{
    for (const auto& entry : registry->replacements)
        for (const auto& input : entry.second->readBindings) {
            const auto& binding = input.second.binding;
            if (!binding->managedProvider || !availableBinding(*binding, {}))
                continue;
            auto current = binding->provider;
            while (current.valid()) {
                if (current == instance)
                    return Result<void>::failure(
                        inputError(ErrorCode::ConfigConflict, "provider is read by replacement", entry.first));
                const auto parent = registry->parents.find(current);
                current = parent == registry->parents.end() ? InstanceId{} : parent->second;
            }
        }
    return Result<void>::success();
}
void invalidateReplacementInputs(const std::shared_ptr<BindingControl>& binding)
{
    const auto registry = binding->owner.lock();
    if (!registry)
        return;
    // Promise 结算可能同步唤醒观察器，先保存实际操作，再离开原目录遍历。
    std::vector<std::shared_ptr<ReplacementScopeControl>> readers;
    for (const auto& entry : registry->replacements) {
        const auto found = entry.second->readBindings.find(binding->key);
        if (found != entry.second->readBindings.end() && found->second.binding == binding)
            readers.push_back(entry.second);
    }
    for (const auto& scope : readers) {
        const auto found = scope->readBindings.find(binding->key);
        if (found == scope->readBindings.end() || found->second.binding != binding)
            continue;
        const auto read = found->second;
        const auto error =
            inputError(ErrorCode::DependencyMissing, "replacement input binding retired", scope->operation);
        if (read.candidate)
            scope->candidateInputsLost.reject(error);
        if (read.recovery)
            scope->recoveryInputsLost.reject(error);
    }
}
Result<void> CoreReplacementAccess::activateInputs(const ReplacementScope& scope, const Kernel& kernel, bool recovery)
{
    if (!scope.control_ || scope.control_->kernel != kernel.control_)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "input phase requires actual scope Kernel"));
    auto allowed = scope.control_->check();
    if (!allowed.hasValue())
        return allowed;
    if (scope.control_->state != ReplacementScopeState::Stopped)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "input phase requires stopped closure"));
    auto inputs = scope.control_->checkInputs(recovery);
    if (!inputs.hasValue())
        return inputs;
    scope.control_->recoveringInputs = recovery;
    return Result<void>::success();
}
Task<void> CoreReplacementAccess::whenInputsLost(const ReplacementScope& scope, const Kernel& kernel, bool recovery)
{
    if (!scope.control_ || scope.control_->kernel != kernel.control_)
        return failedTask(makeError(ErrorCode::ConfigConflict, "input observation requires actual scope Kernel"));
    return recovery ? scope.control_->recoveryInputsLost.task() : scope.control_->candidateInputsLost.task();
}
Result<std::vector<ReplacementReadSource>> CoreReplacementAccess::inputSources(const ReplacementScope& scope,
                                                                               const Kernel& kernel)
{
    using Output = Result<std::vector<ReplacementReadSource>>;
    const auto control = scope.control_;
    if (!control || control->kernel != kernel.control_)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "input sources require actual scope Kernel"));
    auto admitted = control->check();
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (control->state != ReplacementScopeState::Reserved || !control->planSealed)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "input sources require sealed reserved plan"));
    auto ownership = kernel.control_->inspectOwnership();
    if (!ownership.hasValue())
        return Output::failure(ownership.error());
    std::map<InstanceId, ReplacementReadSource> sources;
    for (const auto& entry : control->readBindings) {
        const auto& read = entry.second;
        if (!read.binding->managedProvider)
            continue;
        auto current = read.binding->provider;
        while (current.valid()) {
            const auto found = ownership.value().find(current);
            if (found == ownership.value().end() || found->second.blocked ||
                found->second.member.state != InstanceState::Active)
                return Output::failure(inputError(ErrorCode::ConfigConflict,
                                                  "input provider ancestry is no longer active", control->operation));
            const auto& member = found->second.member;
            auto source = sources.emplace(current, ReplacementReadSource{current, member.runtime, member.epoch}).first;
            source->second.candidate = source->second.candidate || read.candidate;
            source->second.recovery = source->second.recovery || read.recovery;
            current = member.parent;
        }
    }
    for (const bool recovery : {false, true}) {
        auto available = control->checkInputs(recovery);
        if (!available.hasValue())
            return Output::failure(available.error());
    }
    std::vector<ReplacementReadSource> result;
    for (const auto& source : sources)
        result.push_back(source.second);
    return Output::success(std::move(result));
}
Result<std::vector<ReplacementInputBinding>> CoreReplacementAccess::inputBindings(const ReplacementScope& scope,
                                                                                  const Kernel& kernel)
{
    using Output = Result<std::vector<ReplacementInputBinding>>;
    const auto control = scope.control_;
    if (!control || control->kernel != kernel.control_)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "input bindings require actual scope Kernel"));
    auto admitted = control->check();
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (control->state != ReplacementScopeState::Reserved || !control->planSealed)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "input bindings require sealed reserved plan"));
    for (const bool recovery : {false, true}) {
        auto available = control->checkInputs(recovery);
        if (!available.hasValue())
            return Output::failure(available.error());
    }
    std::vector<ReplacementInputBinding> bindings;
    for (const auto& entry : control->readBindings) {
        const auto& read = entry.second;
        if (!read.binding->managedProvider)
            bindings.push_back(
                {read.binding->object, read.binding->type, read.binding->provider, read.candidate, read.recovery});
    }
    return Output::success(std::move(bindings));
}
} // namespace cordis::detail
namespace cordis {
Result<void> ServiceRegistry::replacementProviderAccess(InstanceId instance) const
{
    return detail::replacementProviderAccess(control_, instance);
}
} // namespace cordis
