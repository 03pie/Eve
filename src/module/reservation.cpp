/** @file reservation.cpp
 * @brief 模块预约门面不借用管理器寿命，也不调用模块或调度器。
 */
#include "reservation_p.hpp"
#include <cordis/module/manager.hpp>
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
