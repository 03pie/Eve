/** @file clock.cpp
 * @brief 以 Qt 原生计时触发宿主调度，关闭等待 QObject 删除及全部业务载荷释放。
 */
#include <cordis/qt/clock.hpp>
#include <cordis/qt/object.hpp>
#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cordis::qt::detail {
/** @brief Qt 排队载荷只持宿主记录；业务闭包可在关闭时独立释放。 */
struct TimerEntry {
    std::uint64_t id = 0;
    std::shared_ptr<std::atomic_bool> active = std::make_shared<std::atomic_bool>(true);
    std::unique_ptr<ScheduledJob> job;
    QPointer<QTimer> timer;
};
/** @brief 除令牌原子标志外均只由应用线程修改。 */
struct ClockControl : std::enable_shared_from_this<ClockControl> {
    std::shared_ptr<QtScheduler> scheduler;
    std::optional<QObjectResource> resource;
    QObject* host = nullptr;
    std::map<std::uint64_t, std::shared_ptr<TimerEntry>> entries;
    std::uint64_t nextId = 0;
    bool open = true;
    bool closing = false;
    bool draining = false;
    bool nativeDestroyed = false;
    std::size_t running = 0;
    std::size_t accepted = 0;
    std::size_t executed = 0;
    std::size_t discarded = 0;
    std::size_t cancelled = 0;
    std::size_t errors = 0;
    Promise<void> closed;
    /** @brief 不以 Token 非活动或 deleteLater 已请求代替资源释放确认。 */
    void checkClosed()
    {
        if (closing && !draining && nativeDestroyed && running == 0 && entries.empty()) closed.resolve();
    }
    /** @brief 先摘账目再执行/丢弃；载荷析构期间仍保留运行屏障，允许重入 close。 */
    void retire(const std::shared_ptr<TimerEntry>& entry, bool execute)
    {
        if (!entry->job) return;
        auto job = std::move(entry->job);
        entries.erase(entry->id);
        if (entry->timer) { entry->timer->stop(); entry->timer->deleteLater(); }
        const bool active = entry->active->exchange(false);
        ++running;
        if (!active) ++cancelled;
        else if (execute && open) {
            ++executed;
            try { job->run(); } catch (...) { ++errors; }
        } else {
            ++discarded;
            if (job->discarded) try { job->discarded(); } catch (...) { ++errors; }
        }
        job.reset();
        --running;
        checkClosed();
    }
    /** @brief 原生 timeout 不执行用户代码，只把稳定记录投递到非重入的 QtScheduler。 */
    void due(std::uint64_t id)
    {
        auto found = entries.find(id);
        if (found == entries.end()) return;
        auto entry = found->second;
        if (entry->timer) { entry->timer->stop(); entry->timer->deleteLater(); }
        if (!open || !entry->active->load()) { retire(entry, false); return; }
        auto self = shared_from_this();
        try {
            if (scheduler->post({[self, entry] { self->retire(entry, true); },
                                 [self, entry] { self->retire(entry, false); }})) return;
        } catch (...) { /* 拒绝或分配异常均需完成已接受计时任务的丢弃协议。 */ }
        retire(entry, false);
    }
    /** @brief 先关门和移出整批记录，丢弃回调重入不会破坏遍历。 */
    void stop()
    {
        if (!open) return;
        open = false;
        draining = true;
        auto removed = std::move(entries);
        entries.clear();
        for (const auto& item : removed) retire(item.second, false);
        draining = false;
        checkClosed();
    }
    /** @brief 资源屏障完成后直接更新宿主账目，业务 continuation 仍由 Task 排队。 */
    Task<void> startClose()
    {
        if (closing) return closed.task();
        closing = true;
        stop();
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(resource->close(), [self](Task<void>::Snapshot result) {
            if (!result->hasValue()) self->closed.reject(result->error());
            else { self->nativeDestroyed = true; self->checkClosed(); }
        });
        return closed.task();
    }
};
/** @brief 原生 QObject 持有控制块；即使最后一个 facade 在 worker 释放，队列仍存活。 */
class ClockHost final : public QObject {
public:
    /** @brief 初始不设置 parent，由 QObjectResource 接管完整删除屏障。 */
    explicit ClockHost(std::shared_ptr<ClockControl> control) : control_(std::move(control)) {}
    /** @brief 原生资源在意外退出时也先处理计时任务的丢弃，而后才销毁 Qt 子对象。 */
    ~ClockHost() override { control_->host = nullptr; control_->stop(); }
private:
    std::shared_ptr<ClockControl> control_;
};
} // namespace cordis::qt::detail

namespace cordis::qt {
/** @brief 构造不依赖时间流逝；所有原生资源从建立起就有应用线程删除守卫。 */
QtClock::QtClock(std::shared_ptr<QtScheduler> scheduler)
{
    auto app = QCoreApplication::instance();
    if (!app || !scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() ||
        QThread::currentThread() != app->thread()) throw std::invalid_argument("QtClock requires open application-thread scheduler");
    control_ = std::make_shared<detail::ClockControl>();
    control_->scheduler = std::move(scheduler);
    std::unique_ptr<QObject> host = std::make_unique<detail::ClockHost>(control_);
    control_->host = host.get();
    control_->resource.emplace(host, control_->scheduler);
}
/** @brief Qt 宿主负责生命周期，避免在不确定线程销毁原生对象和插件载荷。 */
QtClock::~QtClock() = default;
/** @brief 每个计时器仅投递一次；业务任务的仲裁延迟到 scheduler 真正进入执行时。 */
Result<TimerToken> QtClock::after(std::chrono::milliseconds delay, ScheduledJob job)
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread()) return Result<TimerToken>::failure(makeError(ErrorCode::WrongThread, "QtClock after requires owner thread"));
    if (!control->open || !control->scheduler->isOpen()) return Result<TimerToken>::failure(makeError(ErrorCode::ClockClosed, "QtClock closed"));
    if (!job.run || delay.count() < 0 || delay.count() > std::numeric_limits<int>::max())
        return Result<TimerToken>::failure(makeError(ErrorCode::InvalidArgument, "QtClock delay must fit nonnegative int milliseconds"));
    auto entry = std::make_shared<detail::TimerEntry>();
    entry->job = std::make_unique<ScheduledJob>(std::move(job));
    // 用户移动构造可能重入 close，原生对象建立前重新验证入口。
    if (!control->open || !control->host || !control->scheduler->isOpen())
        return Result<TimerToken>::failure(makeError(ErrorCode::ClockClosed, "QtClock closed during registration"));
    if (control->nextId == std::numeric_limits<std::uint64_t>::max())
        return Result<TimerToken>::failure(makeError(ErrorCode::InvalidArgument, "QtClock timer identity exhausted"));
    entry->id = ++control->nextId;
    auto timer = std::make_unique<QTimer>(control->host);
    entry->timer = timer.get();
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    std::weak_ptr<detail::ClockControl> weak = control;
    QObject::connect(timer.get(), &QTimer::timeout, control->host, [weak, id = entry->id] {
        if (auto state = weak.lock()) state->due(id);
    }, Qt::DirectConnection);
    control->entries.emplace(entry->id, entry);
    ++control->accepted;
    timer->start(static_cast<int>(delay.count()));
    timer.release();
    return Result<TimerToken>::success(TimerToken(entry->active));
}
/** @brief Qt 对象清理可以继续，但必须先于 scheduler 关闭完成其业务清理链。 */
Task<void> QtClock::close() const
{
    auto control = control_;
    if (!control->scheduler->isCurrentThread()) return failedTask(makeError(ErrorCode::WrongThread, "QtClock close requires owner thread"));
    return control->startClose();
}
/** @brief pending 包括取消后尚待回收的记录，不能用 Token 状态冒充载荷已释放。 */
Result<ClockSnapshot> QtClock::snapshot() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<ClockSnapshot>::failure(makeError(ErrorCode::WrongThread, "QtClock snapshot requires owner thread"));
    return Result<ClockSnapshot>::success({control_->open, control_->nativeDestroyed, control_->entries.size(), control_->running,
        control_->accepted, control_->executed, control_->discarded, control_->cancelled, control_->errors});
}
} // namespace cordis::qt
