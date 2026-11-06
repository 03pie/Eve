/** @file sources.cpp
 * @brief 宿主合成并兑现纯工厂来源；实际代次权限仍由 Module 预约持有。
 */
#include "../module/host_services_p.hpp"
#include "operation_p.hpp"
namespace cordis::detail {
Result<void> ReplacementOperationControl::borrowInputs()
{
    auto& work = *execution;
    std::vector<module::detail::ModuleInputSource> sources;
    auto owned = CoreReplacementAccess::inputSources(*work.scope, *work.request.kernel);
    if (!owned.hasValue())
        return Result<void>::failure(owned.error());
    for (const auto& input : owned.value()) {
        auto origin = work.request.modules->runtimeOrigin(input.runtime);
        if (!origin.hasValue())
            return Result<void>::failure(origin.error());
        if (origin.value())
            sources.push_back({origin.value()->generation, input.instance, input.epoch, input.runtime, input.candidate,
                               input.recovery});
    }
    auto bindings = CoreReplacementAccess::inputBindings(*work.scope, *work.request.kernel);
    if (!bindings.hasValue())
        return Result<void>::failure(bindings.error());
    for (const auto& binding : bindings.value()) {
        if (binding.type != typeid(module::detail::NativeServiceEndpoint))
            continue;
        const auto endpoint = std::static_pointer_cast<module::detail::NativeServiceEndpoint>(binding.object);
        const auto provider = endpoint->owner.lock();
        if (!provider)
            return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "native input owner expired"));
        {
            std::lock_guard<std::mutex> member(provider->mutex);
            if (provider->managed || provider->instance != binding.provider || provider->uncertain ||
                provider->closing || provider->state != module::NativeInstanceState::Started)
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "native input is not actual active raw business"));
        }
        {
            std::lock_guard<std::mutex> root(provider->root->mutex);
            if (provider->root->closing || provider->root->closed || provider->root->faulted)
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "native input module unavailable"));
        }
        sources.push_back({provider->root->source.generation,
                           provider->instance,
                           provider->epoch,
                           {},
                           binding.candidate,
                           binding.recovery});
    }
    return module::detail::ReplacementAccess::borrowInputs(work.reservation, *work.scope, sources);
}
Result<std::optional<module::ModuleFactorySource>> factorySource(const module::ModuleManager& modules,
                                                                 RuntimeId runtime)
{
    using Output = Result<std::optional<module::ModuleFactorySource>>;
    auto origin = modules.runtimeOrigin(runtime);
    if (!origin.hasValue())
        return Output::failure(origin.error());
    if (!origin.value())
        return Output::success({});
    return Output::success(module::ModuleFactorySource{origin.value()->generation, origin.value()->pluginId});
}
Result<void> ReplacementOperationControl::describeSources(ReplacementBuildPlan& plan) const
{
    const auto& request = execution->request;
    for (auto& member : plan.members) {
        if (!member.factory && !member.previous)
            continue;
        auto source = factorySource(*request.modules, member.factory ? member.factory->id() : member.previous->runtime);
        if (!source.hasValue())
            return Result<void>::failure(source.error());
        member.source = std::move(source).value();
        if (!member.factory && !plan.recovery && member.source && member.source->generation == request.previous)
            member.source->generation = request.candidate;
    }
    return Result<void>::success();
}
Result<void> ReplacementOperationControl::sealSources()
{
    std::vector<module::ModuleFactorySource> candidateSources, recoverySources;
    for (const auto& unit : execution->units)
        for (const auto* plan : {&unit.candidate, &unit.recovery})
            for (const auto& member : plan->members) {
                if (member.factory || (plan->recovery && member.previous)) {
                    auto original = factorySource(*execution->request.modules,
                                                  member.factory ? member.factory->id() : member.previous->runtime);
                    if (!original.hasValue())
                        return Result<void>::failure(original.error());
                    if (member.source != original.value())
                        return Result<void>::failure(
                            replacementError(ErrorCode::ConfigConflict,
                                             "recovery or canonical factory source differs from actual input"));
                }
                if (member.source)
                    (plan->recovery ? recoverySources : candidateSources).push_back(*member.source);
            }
    auto sealed = module::detail::ReplacementAccess::sealSources(execution->reservation, *execution->scope,
                                                                 candidateSources, recoverySources);
    if (!sealed.hasValue())
        return sealed;
    auto events = module::detail::ReplacementAccess::watchSources(execution->reservation);
    if (!events.hasValue())
        return Result<void>::failure(events.error());
    execution->sourceEvents = std::move(events).value();
    return Result<void>::success();
}
} // namespace cordis::detail
