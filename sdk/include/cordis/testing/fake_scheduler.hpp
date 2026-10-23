/**
 * @file fake_scheduler.hpp
 * @brief 声明用于测试的手动 FIFO 调度器。
 */
#pragma once

#include <cordis/scheduler.hpp>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

namespace cordis::testing {
/**
 * @brief 手动驱动的 FIFO 执行器。
 *
 * 投递和关闭线程安全；只有所属线程可以泵送任务。
 */
class FakeScheduler final : public IScheduler {
public:
    /** @brief 将构造线程设为调度器所属线程。 */
    FakeScheduler();
    /** @brief 关闭调度器并丢弃尚未执行的任务。 */
    ~FakeScheduler() override;
    /**
     * @brief 线程安全地将任务追加到队列尾部。
     * @param job 要加入队列的任务。
     * @return 调度器开启且任务有效并被接受时返回 true。
     */
    bool post(ScheduledJob job) override;
    /** @brief 判断调用线程是否为调度器所属线程。 */
    [[nodiscard]] bool isCurrentThread() const noexcept override;
    /** @brief 在线程安全的前提下查询调度器是否开启。 */
    [[nodiscard]] bool isOpen() const override;
    /** @brief 返回当前队列中的待处理任务数量。 */
    [[nodiscard]] std::size_t pending() const;
    /**
     * @brief 在所属线程运行一个任务。
     * @return 运行任务时返回 true；队列为空时返回 false；失败时返回错误。
     */
    Result<bool> runOne();
    /**
     * @brief 持续运行任务，直到队列为空或达到步数上限。
     * @param stepLimit 本次调用允许运行的最大任务数。
     * @return 成功时返回运行的任务数；失败时返回线程、回调或步数错误。
     */
    Result<std::size_t> runUntilIdle(std::size_t stepLimit = 10000);
    /** @brief 关闭调度器并在锁外通知所有已接受任务被丢弃。 */
    void close();

private:
    const std::thread::id owner_;
    mutable std::mutex mutex_;
    bool open_ = true;
    bool pumping_ = false;
    std::deque<ScheduledJob> queue_;
};
} // namespace cordis::testing
