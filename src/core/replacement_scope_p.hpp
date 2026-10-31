/** @file replacement_scope_p.hpp
 * @brief 一个真实闭包的占用、冻结及停止账目，不保存模块策略或用户回调。
 */
#pragma once
#include "kernel_p.hpp"
#include <atomic>

namespace cordis::detail {
OperationId nextOperationId();
struct ReplacementScopeControl : std::enable_shared_from_this<ReplacementScopeControl> {
    OperationId operation;
    std::shared_ptr<KernelControl> kernel;
    std::shared_ptr<IScheduler> scheduler;
    ReplacementSelection selection;
    ReplacementScopeState state = ReplacementScopeState::Reserved;
    std::atomic_bool abandoned{false}, blocked{false};
    bool cancelled = false;
    bool releasing = false;
    std::size_t next = 0;
    CleanupReport report{true, {}};
    Promise<CleanupReport> stopped;
    Promise<void> released;
    Result<void> check() const;
    bool deliveryBlocked() const;
    Result<ReplacementScopeSnapshot> snapshot() const;
    Result<void> freeze();
    Task<CleanupReport> stop();
    void advanceStop();
    Result<void> release();
    void removeClaims(bool resume);
    void shutdown(bool finished, const CleanupReport& outcome);
};
bool replacementAllowsActivation(const std::weak_ptr<ReplacementScopeControl>& claim);
bool replacementAllowsBusiness(const std::weak_ptr<ReplacementScopeControl>& claim);
Result<void> replacementMutationAccess(const std::weak_ptr<ReplacementScopeControl>& claim);
Result<void> replacementOutputAccess(const std::shared_ptr<ServiceRegistryControl>& registry, const ServiceKey& key);
void shutdownReplacementScopes(const std::shared_ptr<ServiceRegistryControl>& registry, bool finished,
                               const CleanupReport& outcome = {false, {}});
} // namespace cordis::detail
