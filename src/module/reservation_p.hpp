/** @file reservation_p.hpp
 * @brief 预约只保存宿主值；唯一索引及同步由进程目录管理。
 */
#pragma once
#include <atomic>
#include <cordis/module/replacement.hpp>
#include <cordis/replacement.hpp>
#include <map>
#include <vector>

namespace cordis::module::detail {
struct BorrowedGeneration {
    GenerationId generation;
    bool original;
    bool required;
};
using BorrowedModules = std::map<std::string, std::vector<BorrowedGeneration>>;
struct ReplacementReservationControl {
    std::string moduleId;
    GenerationId current;
    GenerationId candidate;
    OperationId operation;
    GenerationId authorized;
    BorrowedModules borrowed;
    std::atomic_bool released{false};
    ModuleReplacementSnapshot snapshot() const { return {moduleId, current, candidate}; }
};
Result<std::shared_ptr<ReplacementReservationControl>>
reserveReplacementPair(GenerationId current, GenerationId candidate, OperationId operation = {},
                       const ReplacementClosure* closure = nullptr);
Result<void> authorizeRecoveryPair(const std::shared_ptr<ReplacementReservationControl>& control,
                                   OperationId operation);
void releaseReplacementPair(const std::shared_ptr<ReplacementReservationControl>& control) noexcept;
Result<void> drainGeneration(GenerationId generation);
} // namespace cordis::module::detail
