/**
 * @file fake_clock.cpp
 * @brief 实现虚拟时钟的定时注册、时间推进和关闭逻辑。
 */
#include <cordis/testing/fake_clock.hpp>
#include <algorithm>
#include <limits>

namespace cordis::testing {
/**
 * @brief 关闭虚拟时钟，并丢弃所有尚未执行的任务。
 */
FakeClock::~FakeClock() { closeImpl(); }

/**
 * @brief 注册相对当前虚拟时间触发的任务。
 * @param delay 距离任务触发的延迟，不得为负数。
 * @param job 到期时执行或关闭时丢弃的任务。
 * @return 成功时返回可用于取消任务的令牌；失败时返回错误。
 */
Result<TimerToken> FakeClock::after(std::chrono::milliseconds delay, ScheduledJob job)
{
    if (std::this_thread::get_id() != owner_) return Result<TimerToken>::failure(makeError(ErrorCode::WrongThread, "clock requires owner thread"));
    if (!open_) return Result<TimerToken>::failure(makeError(ErrorCode::ClockClosed, "clock closed"));
    if (!job.run || delay.count() < 0 || delay.count() > std::numeric_limits<std::int64_t>::max() - now_)
        return Result<TimerToken>::failure(makeError(ErrorCode::InvalidArgument, "invalid timer delay"));
    if (order_ == std::numeric_limits<std::uint64_t>::max())
        return Result<TimerToken>::failure(makeError(ErrorCode::InvalidArgument, "timer identity exhausted"));
    auto active = std::make_shared<std::atomic_bool>(true);
    entries_.push_back({now_ + delay.count(), ++order_, active, std::move(job)});
    return Result<TimerToken>::success(TimerToken(active));
}

/**
 * @brief 统计仍处于活动状态的定时任务。
 * @return 尚未取消的待处理任务数量。
 */
std::size_t FakeClock::pending() const
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.active->load(); }));
}

/**
 * @brief 推进虚拟时间，并按截止时间和注册顺序执行到期任务。
 * @param amount 要推进的时长，不得为负数。
 * @param stepLimit 本次推进允许执行的最大任务数，用于限制零延迟循环。
 * @return 成功时返回已执行的任务数；失败时返回线程、时钟、步数或回调错误。
 */
Result<std::size_t> FakeClock::advance(std::chrono::milliseconds amount, std::size_t stepLimit)
{
    if (std::this_thread::get_id() != owner_) return Result<std::size_t>::failure(makeError(ErrorCode::WrongThread, "clock requires owner thread"));
    if (!open_) return Result<std::size_t>::failure(makeError(ErrorCode::ClockClosed, "clock closed"));
    if (advancing_ || amount.count() < 0 || amount.count() > std::numeric_limits<std::int64_t>::max() - now_)
        return Result<std::size_t>::failure(makeError(ErrorCode::InvalidArgument, "invalid clock advance"));
    const auto target = now_ + amount.count();
    advancing_ = true;
    std::size_t steps = 0;
    std::vector<ErrorInfo> errors;
    while (true) {
        // 先移出已取消任务再析构载荷，避免析构过程重入时破坏容器迭代。
        std::vector<Entry> cancelled;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (!it->active->load()) { cancelled.push_back(std::move(*it)); it = entries_.erase(it); }
            else ++it;
        }
        cancelled.clear();
        auto next = std::min_element(entries_.begin(), entries_.end(), [](const Entry& left, const Entry& right) {
            return left.deadline < right.deadline || (left.deadline == right.deadline && left.order < right.order);
        });
        if (next == entries_.end() || next->deadline > target) break;
        if (steps == stepLimit) {
            advancing_ = false;
            auto error = makeError(ErrorCode::StepLimitExceeded, "clock step limit exceeded");
            error.causes = std::move(errors);
            return Result<std::size_t>::failure(std::move(error));
        }
        Entry entry = std::move(*next);
        entries_.erase(next);
        now_ = entry.deadline;
        if (entry.active->exchange(false)) {
            ++steps;
            try { entry.job.run(); }
            catch (const std::exception& error) { errors.push_back(makeError(ErrorCode::CallbackFailed, error.what())); }
            catch (...) { errors.push_back(makeError(ErrorCode::CallbackFailed, "timer callback threw")); }
        }
    }
    now_ = target;
    advancing_ = false;
    if (!errors.empty()) {
        auto error = makeError(ErrorCode::CallbackFailed, "one or more timers failed");
        error.causes = std::move(errors);
        return Result<std::size_t>::failure(std::move(error));
    }
    return Result<std::size_t>::success(steps);
}

/**
 * @brief 在所属线程关闭时钟，并通知所有待处理任务已被丢弃。
 * @return 所属线程调用时返回成功；其他线程调用时返回错误。
 */
Result<void> FakeClock::close()
{
    if (std::this_thread::get_id() != owner_) return Result<void>::failure(makeError(ErrorCode::WrongThread, "clock requires owner thread"));
    closeImpl();
    return Result<void>::success();
}

/**
 * @brief 关闭时钟，并在容器外触发待处理任务的丢弃回调。
 *
 * 先将任务移出容器，使回调重入时不会破坏容器操作。
 */
void FakeClock::closeImpl()
{
    open_ = false;
    auto entries = std::move(entries_);
    entries_.clear();
    for (auto& entry : entries) {
        if (entry.active->exchange(false) && entry.job.discarded) {
            try { entry.job.discarded(); } catch (...) { /* 继续丢弃其余计时任务。 */ }
        }
    }
}
} // namespace cordis::testing
