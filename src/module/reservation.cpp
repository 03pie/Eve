/** @file reservation.cpp
 * @brief 预约关联真实 Core 来源；目录锁内不调用 Core、调度器或插件。
 */
#include "reservation_p.hpp"
#include <cordis/module/manager.hpp>
#include <cordis/replacement_scope.hpp>
#include <utility>

namespace cordis::module {
Result<ReplacementReservation> ModuleManager::reserveReplacement(GenerationId current, GenerationId candidate)
{
    if (std::this_thread::get_id() != owner_) {
        auto error = makeError(ErrorCode::WrongThread, "module reservation requires manager owner");
        error.phase = "module.replacement";
        return Result<ReplacementReservation>::failure(std::move(error));
    }
    auto result = detail::reserveReplacementPair(current, candidate);
    return result.hasValue()
               ? Result<ReplacementReservation>::success(ReplacementReservation(std::move(result).value()))
               : Result<ReplacementReservation>::failure(result.error());
}
Result<ReplacementReservation> ModuleManager::reserveReplacement(GenerationId current, GenerationId candidate,
                                                                 const ReplacementScope& scope)
{
    using Output = Result<ReplacementReservation>;
    if (std::this_thread::get_id() != owner_)
        return Output::failure(makeError(ErrorCode::WrongThread, "module reservation requires manager owner"));
    auto snapshot = scope.snapshot();
    if (!snapshot.hasValue())
        return Output::failure(snapshot.error());
    const auto& value = snapshot.value();
    if (value.state != ReplacementScopeState::Reserved || value.cancelled || value.deliveryBlocked)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "module reservation requires a fresh Core scope"));
    for (const auto runtime : value.closure.runtimes) {
        auto origin = runtimeOrigin(runtime);
        if (!origin.hasValue())
            return Output::failure(origin.error());
        if (!origin.value() || origin.value()->generation != current)
            return Output::failure(
                makeError(ErrorCode::ConfigConflict, "Core target does not originate from current generation"));
    }
    auto result = detail::reserveReplacementPair(current, candidate, value.operation, &value.closure);
    return result.hasValue() ? Output::success(ReplacementReservation(std::move(result).value()))
                             : Output::failure(result.error());
}
Result<void> ReplacementReservation::authorizeRecovery(const ReplacementScope& scope) const
{
    const auto control = control_;
    if (!control)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "module reservation is empty or released"));
    auto snapshot = scope.snapshot();
    if (!snapshot.hasValue())
        return Result<void>::failure(snapshot.error());
    const auto& value = snapshot.value();
    if (value.state != ReplacementScopeState::Stopped || value.cancelled || value.deliveryBlocked)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "recovery requires a quiescent Core scope"));
    return detail::authorizeRecoveryPair(control, value.operation);
}
ReplacementReservation& ReplacementReservation::operator=(ReplacementReservation&& other) noexcept
{
    if (this != &other) {
        reset();
        control_ = std::move(other.control_);
    }
    return *this;
}
ReplacementReservation::~ReplacementReservation()
{
    reset();
}
void ReplacementReservation::reset() noexcept
{
    const auto control = std::move(control_);
    if (control)
        detail::releaseReplacementPair(control);
}
Result<ModuleReplacementSnapshot> ReplacementReservation::snapshot() const
{
    const auto control = control_;
    if (control)
        return Result<ModuleReplacementSnapshot>::success(control->snapshot());
    auto error = makeError(ErrorCode::InvalidArgument, "module reservation is empty or released");
    error.phase = "module.replacement";
    return Result<ModuleReplacementSnapshot>::failure(std::move(error));
}
} // namespace cordis::module
