/** @file replacement_outputs.cpp
 * @brief 完整未来计划在原 Scope 键账目封存权限，检查不创建绑定或调用外部代码。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
namespace cordis::detail {
namespace {
ErrorInfo outputError(ErrorCode code, const char* message, OperationId operation)
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.outputs";
    error.operation = operation;
    return error;
}
} // namespace
Result<void> ReplacementScopeControl::checkOutputs(const std::set<ServiceKey>& keys,
                                                   const std::shared_ptr<PublicationControl>& publication) const
{
    const auto registry = kernel->services_->control_;
    for (const auto& key : keys) {
        if (key.name.empty() || key.interfaceMajor == 0 || !registry->realms.count(key.realm))
            return Result<void>::failure(
                outputError(ErrorCode::InvalidArgument, "output has an invalid key or realm", operation));
        if (publication && planSealed && !stagedOutputs.count(key))
            return Result<void>::failure(
                outputError(ErrorCode::ConfigConflict, "output is outside the sealed plan", operation));
        const auto claimed = registry->replacementOutputs.find(key);
        if (claimed != registry->replacementOutputs.end() && claimed->second != operation)
            return Result<void>::failure(
                outputError(ErrorCode::ConfigConflict, "output belongs to another replacement", claimed->second));
        const auto binding = registry->bindings.find(key);
        if (binding == registry->bindings.end())
            continue;
        const bool owned =
            publication ? binding->second->publication == publication
                        : binding->second->managedProvider && selection.instances.count(binding->second->provider);
        if (!owned)
            return Result<void>::failure(
                outputError(ErrorCode::ServiceConflict, "output binding is still occupied", operation));
    }
    return Result<void>::success();
}
Result<void> ReplacementScopeControl::sealPlan(const std::vector<ServiceKey>& keys,
                                               const CoreReplacementAccess::InputBindings& candidate,
                                               const CoreReplacementAccess::InputBindings& recovery)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if (state != ReplacementScopeState::Reserved || planSealed)
        return Result<void>::failure(
            outputError(ErrorCode::ConfigConflict, "output plan requires an unsealed reserved scope", operation));
    const std::set<ServiceKey> planned(keys.begin(), keys.end());
    auto checked = checkOutputs(planned, {});
    if (!checked.hasValue())
        return checked;
    const auto registry = kernel->services_->control_;
    std::map<ServiceKey, BindingRead> reads;
    for (const auto* phase : {&candidate, &recovery})
        for (const auto& input : *phase) {
            const auto actual = registry->bindings.find(input.first);
            if (actual == registry->bindings.end() || actual->second->id != input.second ||
                !availableBinding(*actual->second, {}) || actual->second->deliveryBlocked.load() ||
                !actual->second->replacement.expired() || registry->replacementOutputs.count(input.first))
                return Result<void>::failure(
                    outputError(ErrorCode::ConfigConflict, "planned input binding changed or is reserved", operation));
            auto& read = reads[input.first];
            read.binding = actual->second;
            (phase == &candidate ? read.candidate : read.recovery) = true;
        }
    auto claims = registry->replacementOutputs;
    auto owned = stagedOutputs;
    for (const auto& key : planned) {
        claims.emplace(key, operation);
        owned.insert(key);
    }
    registry->replacementOutputs.swap(claims);
    stagedOutputs.swap(owned);
    readBindings.swap(reads);
    planSealed = true;
    return Result<void>::success();
}
Result<void> CoreReplacementAccess::sealPlan(const ReplacementScope& scope, const Kernel& kernel,
                                             const std::vector<ServiceKey>& keys, const InputBindings& candidate,
                                             const InputBindings& recovery)
{
    if (!scope.control_ || scope.control_->kernel != kernel.control_)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "output plan requires actual scope Kernel"));
    return scope.control_->sealPlan(keys, candidate, recovery);
}
} // namespace cordis::detail
