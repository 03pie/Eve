/** @file state_budget.cpp
 * @brief 值拥有期与预算租约一致；纯值可在任意线程释放，不执行业务代码。
 */
#include "state_budget_p.hpp"
#include "state_value_p.hpp"

namespace cordis::detail {
StateBudgetLease& StateBudgetLease::operator=(StateBudgetLease&& value) noexcept
{
    if (this != &value) {
        reset();
        budget_ = std::move(value.budget_);
        bytes_ = std::exchange(value.bytes_, 0);
    }
    return *this;
}
StateBudgetLease::~StateBudgetLease()
{
    reset();
}
void StateBudgetLease::reset() noexcept
{
    auto budget = std::move(budget_);
    if (!budget)
        return;
    std::lock_guard<std::mutex> lock(budget->mutex_);
    budget->used_ -= std::exchange(bytes_, 0);
}
Result<StateBudgetLease> StateBudget::hold(std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes > limit_ - used_) {
        auto error = makeError(ErrorCode::InvalidArgument, "replacement state JSON budget exceeded");
        error.phase = "instance.state.budget";
        return Result<StateBudgetLease>::failure(std::move(error));
    }
    used_ += bytes;
    return Result<StateBudgetLease>::success(StateBudgetLease(shared_from_this(), bytes));
}
std::size_t StateBudget::used() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return used_;
}
Result<std::shared_ptr<const OwnedStateSnapshot>>
ownStateSnapshot(StateSnapshot value, std::shared_ptr<StateBudget> budget, StateValueCheck format)
{
    using Output = Result<std::shared_ptr<const OwnedStateSnapshot>>;
    auto checked = checkStateSnapshot(value);
    if (!checked.hasValue())
        return Output::failure(checked.error());
    if (format && !format(value.json)) {
        auto error = makeError(ErrorCode::InvalidArgument, "state payload does not match host value format");
        error.phase = "instance.state";
        return Output::failure(std::move(error));
    }
    StateBudgetLease bytes;
    if (budget) {
        auto held = budget->hold(value.json.size());
        if (!held.hasValue())
            return Output::failure(held.error());
        bytes = std::move(held.value());
    }
    return Output::success(
        std::make_shared<const OwnedStateSnapshot>(OwnedStateSnapshot{std::move(bytes), std::move(value), format}));
}
} // namespace cordis::detail
