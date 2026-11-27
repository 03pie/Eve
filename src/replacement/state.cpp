/** @file state.cpp
 * @brief 停止前保存独立原值，实际创建时交给原 Core 成员准入；恢复使用相同原值。
 */
#include "../core/instance_state_p.hpp"
#include "../core/runtime_state_p.hpp"
#include "../module/json_p.hpp"
#include "operation_p.hpp"

namespace eve::detail {
namespace {
ErrorInfo stateError(ErrorInfo error, OperationId operation, InstanceId instance)
{
    error.operation = operation;
    error.instance = instance;
    return error;
}
} // namespace
Result<void> ReplacementOperationControl::captureCheckpoint() const
{
    if (delivery->blocked.load())
        return Result<void>::failure(replacementError(ErrorCode::SchedulerClosed, "state capture delivery blocked"));
    if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
        return Result<void>::failure(replacementError(ErrorCode::ContextClosing, "Kernel closed during state capture"));
    if (cancelled || sessionClosing())
        return Result<void>::failure(replacementError(ErrorCode::OperationCancelled, "state capture cancelled"));
    auto current = execution->scope->snapshot();
    if (!current.hasValue())
        return Result<void>::failure(current.error());
    if (current.value().state != ReplacementScopeState::Frozen)
        return Result<void>::failure(
            replacementError(ErrorCode::ContextClosing, "state capture lost its frozen scope"));
    for (const bool restoring : {false, true}) {
        auto healthy = module::detail::ReplacementAccess::checkSources(execution->reservation, restoring);
        if (!healthy.hasValue())
            return healthy;
    }
    return Result<void>::success();
}
Result<void> ReplacementOperationControl::captureState()
{
    auto current = captureCheckpoint();
    if (!current.hasValue())
        return current;
    auto& work = *execution;
    for (const auto& member : work.original.members) {
        const auto schema = work.state.declared.find(member.input.instance);
        if (schema == work.state.declared.end())
            continue;
        auto captured = InstanceStateAccess::capture(*work.scope, member.instance);
        auto checkpoint = captureCheckpoint();
        if (!captured.hasValue())
            return Result<void>::failure(stateError(captured.error(), report.operation, member.input.instance));
        if (!checkpoint.hasValue())
            return checkpoint;
        if (!captured.value() || captured.value()->schema.id != schema->second.id ||
            captured.value()->schema.version != schema->second.version)
            return Result<void>::failure(
                stateError(replacementError(ErrorCode::ConfigConflict, "actual member state contract changed"),
                           report.operation, member.input.instance));
        auto owned = ownStateSnapshot(std::move(*captured.value()), work.state.budget, module::detail::validAbiJson);
        if (!owned.hasValue())
            return Result<void>::failure(stateError(owned.error(), report.operation, member.input.instance));
        if (!work.state.original.emplace(member.input.instance, std::move(owned.value())).second)
            return Result<void>::failure(
                replacementError(ErrorCode::ConfigConflict, "state captured twice for actual member"));
    }
    if (!work.state.original.empty()) {
        // 转换至少需要一字节；原值已占满预算时，两条路线都必然失败，不能先停止旧树。
        auto minimum = work.state.budget->hold(1);
        if (!minimum.hasValue())
            return Result<void>::failure(minimum.error());
    }
    // 两条重建路线都提前检查已知工厂；转换值只在本次校验中持有，原始快照保持不变。
    for (const bool restoring : {false, true})
        for (const auto& unit : work.units)
            for (const auto& item : (restoring ? unit.recovery : unit.candidate).members) {
                if (!item.previous || !item.factory)
                    continue;
                const auto source = work.state.original.find(item.previous->instance);
                if (source == work.state.original.end())
                    continue;
                const auto original = source->second;
                auto converted = RuntimeStateAccess::validate(*item.factory, original->value);
                auto checkpoint = captureCheckpoint();
                if (!converted.hasValue())
                    return Result<void>::failure(
                        stateError(converted.error(), report.operation, item.previous->instance));
                if (!checkpoint.hasValue())
                    return checkpoint;
                auto checked = ownStateSnapshot(std::move(converted.value()), work.state.budget, original->format);
                if (!checked.hasValue())
                    return Result<void>::failure(
                        stateError(checked.error(), report.operation, item.previous->instance));
            }
    return captureCheckpoint();
}
void ReplacementOperationControl::rejectCapture(ErrorInfo error)
{
    if (delivery->terminal.load() || delivery->blocked.load())
        return;
    error.operation = report.operation;
    report.errors.push_back(std::move(error));
    if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
        return;
    cancelled = cancelled || sessionClosing();
    auto oldHealthy = module::detail::ReplacementAccess::checkSources(execution->reservation, true);
    if (oldHealthy.hasValue()) {
        settle(cancelled ? ReplacementOutcome::Cancelled : ReplacementOutcome::Failed, false);
        return;
    }
    // 旧来源已经失败，不能把原树当作健康业务重新开放；沿原实际停止屏障保留失败。
    report.errors.push_back(oldHealthy.error());
    phase = ReplacementPhase::Stopping;
    ++sequence;
    auto stopped = execution->scope->stop();
    watch(stopped, [self = shared_from_this()](const Result<CleanupReport>& result) {
        if (!result.hasValue())
            self->report.errors.push_back(result.error());
        else
            self->report.errors.insert(self->report.errors.end(), result.value().errors.begin(),
                                       result.value().errors.end());
        self->settle(ReplacementOutcome::Failed, true);
    });
}
Result<void> ReplacementOperationControl::configureState()
{
    auto& work = *execution;
    std::vector<StateConstruction> members;
    for (const auto& unit : work.units)
        for (const auto& item : (recovery ? unit.recovery : unit.candidate).members) {
            std::shared_ptr<const OwnedStateSnapshot> input;
            if (item.previous) {
                const auto source = work.state.original.find(item.previous->instance);
                if (source != work.state.original.end())
                    input = source->second;
                else if (work.state.declared.count(item.previous->instance))
                    return Result<void>::failure(
                        replacementError(ErrorCode::ProvisionIncomplete, "stateful member snapshot is missing"));
            }
            members.push_back({item.key, item.parent, std::move(input)});
        }
    return CoreReplacementAccess::statePlan(*work.scope, *work.request.kernel, std::move(members), work.state.budget,
                                            !work.state.declared.empty());
}
} // namespace eve::detail
