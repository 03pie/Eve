/**
 * @file clock.hpp
 * @brief 声明计时器令牌和异步时钟接口。
 */
#pragma once

#include <cordis/scheduler.hpp>
#include <atomic>
#include <chrono>
#include <memory>

namespace cordis {
/**
 * @brief 控制一个已注册计时器的取消状态。
 *
 * 取消令牌不会执行回调；回调载荷的释放由时钟负责。
 */
class TimerToken {
public:
    /**
     * @brief 创建不关联计时器的令牌。
     */
    TimerToken() = default;
    /**
     * @brief 创建共享指定活动状态的令牌。
     * @param active 计时器的共享活动标志。
     */
    explicit TimerToken(std::shared_ptr<std::atomic_bool> active) : active_(std::move(active)) {}
    /**
     * @brief 请求取消计时器。
     * @return 此调用首次将计时器设为非活动状态时返回 true。
     */
    bool cancel() const noexcept { return active_ && active_->exchange(false); }
    /**
     * @brief 查询计时器当前是否活动。
     * @return 计时器关联的活动标志为 true 时返回 true。
     */
    [[nodiscard]] bool active() const noexcept { return active_ && active_->load(); }

private:
    std::shared_ptr<std::atomic_bool> active_;
};

/**
 * @brief 提供异步定时任务注册接口的时钟抽象。
 */
class IClock {
public:
    /**
     * @brief 销毁时钟接口对象。
     */
    virtual ~IClock() = default;
    /**
     * @brief 注册在指定延迟后触发的任务。
     *
     * 不会内联执行回调。已接受的任务最终执行或被丢弃，除非计时器先被取消。
     * @param delay 距离任务触发的延迟。
     * @param job 到期时执行或时钟关闭时丢弃的任务。
     * @return 成功时返回计时器令牌；失败时返回错误。
     */
    virtual Result<TimerToken> after(std::chrono::milliseconds delay, ScheduledJob job) = 0;
};
} // namespace cordis
