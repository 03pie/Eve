/** @file state_budget_p.hpp
 * @brief 纯值的共享字节预算；不持有实例、工厂或用户回调。
 */
#pragma once
#include <cordis/migration_state.hpp>
#include <memory>
#include <mutex>
#include <utility>

namespace cordis::detail {
inline constexpr std::size_t maxReplacementStateBytes = 16 * maxStateJsonBytes;
using StateValueCheck = bool (*)(std::string_view) noexcept;
class StateBudget;
class StateBudgetLease {
public:
    StateBudgetLease() = default;
    StateBudgetLease(const StateBudgetLease&) = delete;
    StateBudgetLease& operator=(const StateBudgetLease&) = delete;
    StateBudgetLease(StateBudgetLease&& value) noexcept
        : budget_(std::move(value.budget_)), bytes_(std::exchange(value.bytes_, 0))
    {
    }
    StateBudgetLease& operator=(StateBudgetLease&& value) noexcept;
    ~StateBudgetLease();
    void reset() noexcept;
    const std::shared_ptr<StateBudget>& budget() const noexcept { return budget_; }

private:
    friend class StateBudget;
    StateBudgetLease(std::shared_ptr<StateBudget> budget, std::size_t bytes) : budget_(std::move(budget)), bytes_(bytes)
    {
    }
    std::shared_ptr<StateBudget> budget_;
    std::size_t bytes_ = 0;
};
class StateBudget : public std::enable_shared_from_this<StateBudget> {
public:
    static std::shared_ptr<StateBudget> create(std::size_t limit = maxReplacementStateBytes)
    {
        return std::shared_ptr<StateBudget>(new StateBudget(limit));
    }
    Result<StateBudgetLease> hold(std::size_t bytes);
    std::size_t used() const noexcept;

private:
    friend class StateBudgetLease;
    explicit StateBudget(std::size_t limit) : limit_(limit) {}
    const std::size_t limit_;
    mutable std::mutex mutex_;
    std::size_t used_ = 0;
};
/** @brief 共享的不可变快照只计费一次；最后一份拥有值的引用退出时解除预算。 */
struct OwnedStateSnapshot {
    StateBudgetLease bytes;
    StateSnapshot value;
    StateValueCheck format = nullptr;
};
Result<std::shared_ptr<const OwnedStateSnapshot>> ownStateSnapshot(StateSnapshot, std::shared_ptr<StateBudget> = {},
                                                                   StateValueCheck = nullptr);
} // namespace cordis::detail
