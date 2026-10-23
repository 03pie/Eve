/**
 * @file scheduler.hpp
 * @brief 声明任务及其调度接口。
 */
#pragma once

#include <cordis/result.hpp>
#include <functional>

namespace cordis {
/**
 * @brief 调度器接收的任务及其丢弃通知。
 *
 * 每个已接受任务必须恰好运行一次或被丢弃一次。
 */
struct ScheduledJob {
    std::function<void()> run;
    /** @brief 任务被丢弃时调用，且必须在调度器锁外执行。 */
    std::function<void()> discarded;
};

/**
 * @brief 定义线程安全的任务调度接口。
 */
class IScheduler {
public:
    /**
     * @brief 销毁调度器接口对象。
     */
    virtual ~IScheduler() = default;
    /**
     * @brief 尝试向调度器投递任务。
     *
     * 可从任意线程调用。返回 false 表示任务未被接受，由调用方处理拒绝。
     * @param job 待投递任务。
     * @return 任务被接受时返回 true。
     */
    virtual bool post(ScheduledJob job) = 0;
    /**
     * @brief 判断调用方是否运行在调度器所属线程。
     * @return 当前线程是调度器所属线程时返回 true。
     */
    [[nodiscard]] virtual bool isCurrentThread() const noexcept = 0;
    /**
     * @brief 查询调度器是否仍接受任务。
     * @return 调度器处于开启状态时返回 true。
     */
    [[nodiscard]] virtual bool isOpen() const = 0;
};
} // namespace cordis
