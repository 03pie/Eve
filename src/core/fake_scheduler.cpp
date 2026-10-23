/**
 * @file fake_scheduler.cpp
 * @brief 实现用于测试的线程安全手动 FIFO 调度器。
 */
#include <cordis/testing/fake_scheduler.hpp>
#include <exception>
#include <utility>

namespace cordis::testing {
/**
 * @brief 将构造线程记录为调度器所属线程。
 */
FakeScheduler::FakeScheduler() : owner_(std::this_thread::get_id()) {}
/**
 * @brief 关闭调度器并丢弃所有尚未执行的任务。
 */
FakeScheduler::~FakeScheduler() { close(); }

/**
 * @brief 在线程安全的前提下将任务追加到 FIFO 队列。
 * @param job 要投递的任务。
 * @return 任务有效且调度器仍开放时返回 true。
 */
bool FakeScheduler::post(ScheduledJob job)
{
    if (!job.run) return false;
    // std::function 的移动也可能调用用户代码，先在锁外建立稳定的任务存储。
    auto pending = std::make_unique<ScheduledJob>(std::move(job));
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return false;
    queue_.push_back(std::move(pending));
    return true;
}

/**
 * @brief 判断调用线程是否为调度器所属线程。
 * @return 调用线程为所属线程时返回 true。
 */
bool FakeScheduler::isCurrentThread() const noexcept
{
    return std::this_thread::get_id() == owner_;
}

/**
 * @brief 在线程安全的前提下查询调度器是否开放。
 * @return 调度器仍接受任务时返回 true。
 */
bool FakeScheduler::isOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

/**
 * @brief 查询 FIFO 队列中的待处理任务数。
 * @return 当前排队任务数。
 */
std::size_t FakeScheduler::pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

/**
 * @brief 在所属线程执行队列中的一个任务。
 * @return 执行任务时返回 true；队列为空时返回 false；发生错误时返回失败结果。
 */
Result<bool> FakeScheduler::runOne()
{
    if (!isCurrentThread()) return Result<bool>::failure(makeError(ErrorCode::WrongThread, "pump requires owner thread"));
    if (pumping_) return Result<bool>::failure(makeError(ErrorCode::InvalidArgument, "nested pump is forbidden"));
    /** @brief 晚于任务捕获析构退出 pump，异常路径也维持相同的重入门禁。 */
    struct PumpGuard {
        bool& pumping;
        /** @brief 恢复门禁；该对象先于任务声明，因而晚于任务析构。 */
        ~PumpGuard() { pumping = false; }
    } guard{pumping_};
    std::unique_ptr<ScheduledJob> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return Result<bool>::success(false);
        job = std::move(queue_.front());
        queue_.pop_front();
    }
    pumping_ = true;
    try {
        job->run();
    } catch (const std::exception& error) {
        return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, error.what()));
    } catch (...) {
        return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, "unknown callback exception"));
    }
    return Result<bool>::success(true);
}

/**
 * @brief 在所属线程执行任务，直到队列为空或达到步数上限。
 * @param stepLimit 本次调用允许执行的最大任务数。
 * @return 成功时返回已执行任务数；线程、回调或步数限制错误时返回失败结果。
 */
Result<std::size_t> FakeScheduler::runUntilIdle(std::size_t stepLimit)
{
    if (!isCurrentThread()) return Result<std::size_t>::failure(makeError(ErrorCode::WrongThread, "pump requires owner thread"));
    std::size_t steps = 0;
    while (steps < stepLimit) {
        auto result = runOne();
        if (!result.hasValue()) return Result<std::size_t>::failure(result.error());
        if (!result.value()) return Result<std::size_t>::success(steps);
        ++steps;
    }
    if (pending() != 0) return Result<std::size_t>::failure(makeError(ErrorCode::StepLimitExceeded, "scheduler step limit exceeded"));
    return Result<std::size_t>::success(steps);
}

/**
 * @brief 关闭调度器并在互斥锁外通知已排队任务被丢弃。
 */
void FakeScheduler::close()
{
    std::deque<std::unique_ptr<ScheduledJob>> discarded;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
        discarded.swap(queue_);
    }
    // 任务析构和完成回调可能重入 post/close，因此必须在释放锁后通知。
    for (auto& job : discarded) {
        if (job->discarded) {
            try { job->discarded(); } catch (...) { /* 丢弃通知抛错也必须继续处理其余任务。 */ }
        }
    }
}
} // namespace cordis::testing
