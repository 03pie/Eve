/** @file reservation_p.hpp
 * @brief 预约只保存宿主值；唯一索引及同步由进程目录管理。
 */
#pragma once
#include <atomic>
#include <cordis/module/replacement.hpp>

namespace cordis::module::detail {
struct ReplacementReservationControl {
    std::string moduleId;
    GenerationId current;
    GenerationId candidate;
    std::atomic_bool released{false};
    ModuleReplacementSnapshot snapshot() const { return {moduleId, current, candidate}; }
};
Result<std::shared_ptr<ReplacementReservationControl>> reserveReplacementPair(GenerationId current,
                                                                              GenerationId candidate);
void releaseReplacementPair(const std::shared_ptr<ReplacementReservationControl>& control) noexcept;
Result<void> drainGeneration(GenerationId generation);
} // namespace cordis::module::detail
