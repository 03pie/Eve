/** @file scheduler.hpp
 * @brief 声明 Qt 主线程排队调度器及显式关闭诊断。
 */
#pragma once
#include <cordis/scheduler.hpp>
#include <cstddef>
#include <memory>

class QObject;
namespace cordis::qt {
namespace detail { struct SchedulerControl; }
/** @brief owner 读取的任务账目；executed 包含当前正在执行的任务。 */
struct SchedulerSnapshot {
    bool open;
    bool running;
    std::size_t queued;
    std::size_t accepted;
    std::size_t executed;
    std::size_t discarded;
    std::size_t callbackErrors;
};
/** @brief 由 Qt 对象生命周期拥有队列，句柄销毁不取消已经接受的任务。 */
class QtScheduler final : public IScheduler {
public:
    /** @brief 在应用主线程构造；lifetime 默认为应用，须同线程且不可迁移；事件处理中使用 deleteLater 销毁。 */
    explicit QtScheduler(QObject* lifetime = nullptr);
    /** @brief 释放句柄；队列继续由 lifetime 子对象持有，应用退出前须显式关闭。 */
    ~QtScheduler() override;
    /** @brief 禁止复制/替换调度器身份，使用 shared_ptr 共享同一实例。 */
    QtScheduler(const QtScheduler&) = delete;
    /** @brief 禁止替换尚有已接受任务的控制块。 */
    QtScheduler& operator=(const QtScheduler&) = delete;
    /** @brief 任意线程排队，拒绝返回 false 且不调用 discarded；同线程也不内联。 */
    bool post(ScheduledJob job) override;
    /** @brief 判断调用线程是否是构造时的应用主线程。 */
    bool isCurrentThread() const noexcept override;
    /** @brief 任意线程查询是否接受新任务。 */
    bool isOpen() const override;
    /** @brief owner 幂等关闭；锁外丢弃队列，已进入任务继续执行；不代表 Kernel 静止。 */
    Result<void> close() const;
    /** @brief owner 读取诊断，lifetime 销毁后仍可查询关闭账目。 */
    Result<SchedulerSnapshot> snapshot() const;
private:
    std::shared_ptr<detail::SchedulerControl> control_;
};
} // namespace cordis::qt
