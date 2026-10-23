/**
 * @file fake_clock.hpp
 * @brief 声明用于测试计时行为的虚拟时钟及其待处理任务。
 */
#pragma once

#include <cordis/clock.hpp>
#include <cstdint>
#include <thread>
#include <vector>

namespace cordis::testing {
/**
 * @brief 由所属线程手动推进的虚拟时钟，无需真实等待即可测试计时行为。
 */
class FakeClock final : public IClock {
public:
    /**
     * @brief 创建虚拟时钟，并将当前线程设为所属线程。
     */
    FakeClock() : owner_(std::this_thread::get_id()) {}
    /**
     * @brief 禁止复制，避免多个对象误共享虚拟时钟状态。
     */
    FakeClock(const FakeClock&) = delete;
    /**
     * @brief 禁止赋值，确保虚拟时钟状态不会被替换。
     */
    FakeClock& operator=(const FakeClock&) = delete;
    /**
     * @brief 关闭时钟并丢弃尚未执行的任务。
     */
    ~FakeClock() override;
    /**
     * @brief 注册相对当前虚拟时间触发的任务。
     * @param delay 距离任务触发的延迟，不得为负数。
     * @param job 到期时执行或关闭时丢弃的任务。
     * @return 成功时返回可用于取消任务的令牌；失败时返回错误。
     */
    Result<TimerToken> after(std::chrono::milliseconds delay, ScheduledJob job) override;
    /**
     * @brief 推进虚拟时间，并按截止时间和注册顺序执行到期任务。
     * @param amount 要推进的时长，不得为负数。
     * @param stepLimit 本次推进允许执行的最大任务数，用于限制零延迟循环。
     * @return 成功时返回已执行的任务数；失败时返回线程、时钟、步数或回调错误。
     */
    Result<std::size_t> advance(std::chrono::milliseconds amount, std::size_t stepLimit = 10000);
    /**
     * @brief 返回当前虚拟时间，以毫秒为单位。
     */
    [[nodiscard]] std::int64_t nowMilliseconds() const noexcept { return now_; }
    /**
     * @brief 返回尚未取消的待处理任务数量。
     * @return 当前仍活动的定时任务数。
     */
    [[nodiscard]] std::size_t pending() const;
    /**
     * @brief 在所属线程关闭时钟，并通知所有待处理任务已被丢弃。
     * @return 所属线程调用时返回成功；其他线程调用时返回错误。
     */
    Result<void> close();

private:
    /**
     * @brief 关闭时钟并丢弃待处理任务；调用方负责满足线程约束。
     */
    void closeImpl();

    /**
     * @brief 保存定时任务的截止时间、稳定顺序、取消状态及回调。
     */
    struct Entry {
        std::int64_t deadline;
        std::uint64_t order;
        std::shared_ptr<std::atomic_bool> active;
        ScheduledJob job;
    };
    const std::thread::id owner_;
    bool open_ = true;
    bool advancing_ = false;
    std::int64_t now_ = 0;
    std::uint64_t order_ = 0;
    std::vector<Entry> entries_;
};
} // namespace cordis::testing
