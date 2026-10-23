/** @file clock.hpp
 * @brief 声明基于 Qt 单次计时器的 IClock 和异步关闭屏障。
 */
#pragma once
#include <cordis/clock.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/task.hpp>
#include <cstddef>
#include <memory>

namespace cordis::qt {
namespace detail { struct ClockControl; }
/** @brief owner 读取的计时账目；pending 包括已到期但尚未由 scheduler 执行的任务。 */
struct ClockSnapshot {
    bool open;
    bool nativeDestroyed;
    std::size_t pending;
    std::size_t running;
    std::size_t accepted;
    std::size_t executed;
    std::size_t discarded;
    std::size_t cancelled;
    std::size_t callbackErrors;
};
/** @brief 应用线程的生产时钟；句柄析构不取消任务，退出前须等待 close。 */
class QtClock final : public IClock {
public:
    /** @brief 使用开放的应用线程 QtScheduler 创建原生计时资源。 */
    explicit QtClock(std::shared_ptr<QtScheduler> scheduler);
    /** @brief 只释放句柄，Qt 宿主仍持有已接受任务。 */
    ~QtClock() override;
    /** @brief 禁止复制，使用 shared_ptr 共享同一时钟身份。 */
    QtClock(const QtClock&) = delete;
    /** @brief 禁止覆盖仍有任务的时钟。 */
    QtClock& operator=(const QtClock&) = delete;
    /** @brief owner 注册 0..INT_MAX 毫秒单次任务；Token 可跨线程取消，但载荷到期或关闭时才释放。 */
    Result<TimerToken> after(std::chrono::milliseconds delay, ScheduledJob job) override;
    /** @brief owner 停表、丢弃未执行任务；等待原生对象和执行中回调载荷全部释放。 */
    Task<void> close() const;
    /** @brief owner 查询，close 成功后 nativeDestroyed 为 true 且 pending/running 均为零。 */
    Result<ClockSnapshot> snapshot() const;
private:
    std::shared_ptr<detail::ClockControl> control_;
};
} // namespace cordis::qt
