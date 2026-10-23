/** @file scheduler.cpp
 * @brief 以宿主 FIFO 账目和 Qt 唤醒事件实现执行/丢弃仲裁及非重入调度。
 */
#include <cordis/qt/scheduler.hpp>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QThread>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cordis::qt::detail {
/** @brief 每个进程注册一次独立的宿主队列唤醒类型。 */
QEvent::Type wakeType()
{
    static const int type = QEvent::registerEventType();
    if (type == -1) throw std::runtime_error("Qt event types exhausted");
    return static_cast<QEvent::Type>(type);
}
/** @brief 队列仅在 mutex 下改账目；所有插件函数及其析构均在锁外。 */
struct SchedulerControl {
    const std::thread::id owner = std::this_thread::get_id();
    mutable std::mutex mutex;
    QObject* receiver = nullptr;
    std::deque<std::unique_ptr<ScheduledJob>> jobs;
    bool open = true;
    bool running = false;
    bool wakePending = false;
    std::size_t accepted = 0;
    std::size_t executed = 0;
    std::size_t discarded = 0;
    std::size_t errors = 0;
    /** @brief 调用方持锁；Qt 唤醒不携带用户闭包，不在当前栈调用用户代码。 */
    void wake()
    {
        if (!open || running || wakePending || jobs.empty()) return;
        QCoreApplication::postEvent(receiver, new QEvent(wakeType()));
        wakePending = true;
    }
    /** @brief 记录被隔离的回调异常，允许继续处理其他任务。 */
    void recordError() { std::lock_guard<std::mutex> lock(mutex); ++errors; }
    /** @brief owner 关门并转移队列，通知和闭包析构不得在锁内发生。 */
    void stop()
    {
        std::deque<std::unique_ptr<ScheduledJob>> removed;
        {
            std::lock_guard<std::mutex> lock(mutex);
            open = false;
            receiver = nullptr;
            discarded += jobs.size();
            removed.swap(jobs);
        }
        for (auto& job : removed) if (job->discarded) {
            try { job->discarded(); } catch (...) { recordError(); }
        }
    }
    /** @brief 每次唤醒只执行一项，嵌套 Qt 循环也不能进入第二项业务。 */
    void advance()
    {
        std::unique_ptr<ScheduledJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            wakePending = false;
            if (!open || running || jobs.empty()) return;
            job = std::move(jobs.front());
            jobs.pop_front();
            running = true;
            ++executed;
        }
        try { job->run(); } catch (...) { recordError(); }
        // 析构捕获也可能重入 Qt，因此仍算在当前任务窗口内。
        job.reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
            wake();
        }
    }
};
/** @brief lifetime 子对象强持控制块；应用退出或对象析构均执行丢弃协议。 */
class Receiver final : public QObject {
public:
    /** @brief 由指定 owner 对象持有，不把对象所有权交给跨线程句柄。 */
    Receiver(QObject* parent, std::shared_ptr<SchedulerControl> control)
        : QObject(parent), control_(std::move(control)) {}
    /** @brief Qt owner 析构时关闭队列，先于 QObject 基类销毁解除投递入口。 */
    ~Receiver() override { control_->stop(); }
protected:
    /** @brief 仅处理本适配器的唤醒，其他事件交给 QObject。 */
    bool event(QEvent* event) override
    {
        if (event->type() != wakeType()) return QObject::event(event);
        auto control = control_;
        control->advance();
        return true;
    }
private:
    std::shared_ptr<SchedulerControl> control_;
};
} // namespace cordis::qt::detail

namespace cordis::qt {
/** @brief 验证线程和应用，再创建应用线程的宿主接收对象。 */
QtScheduler::QtScheduler(QObject* lifetime)
{
    auto app = QCoreApplication::instance();
    if (!app || QCoreApplication::closingDown() || QThread::currentThread() != app->thread())
        throw std::invalid_argument("QtScheduler requires application owner thread");
    if (!lifetime) lifetime = app;
    if (lifetime->thread() != app->thread()) throw std::invalid_argument("scheduler lifetime requires application thread");
    detail::wakeType();
    control_ = std::make_shared<detail::SchedulerControl>();
    auto receiver = new detail::Receiver(lifetime, control_);
    control_->receiver = receiver;
    QObject::connect(app, &QCoreApplication::aboutToQuit, receiver, [control = control_] { control->stop(); }, Qt::DirectConnection);
}
/** @brief 不在可能的工作线程析构 Qt 接收对象或已接受的插件任务。 */
QtScheduler::~QtScheduler() = default;
/** @brief 与 stop 在相同锁中仲裁；唤醒入队后才向调用方确认接受。 */
bool QtScheduler::post(ScheduledJob job)
{
    if (!job.run) return false;
    auto control = control_;
    // 不假设 std::function 只移动指针：小对象的移动/析构也能重入调度器。
    auto pending = std::make_unique<ScheduledJob>(std::move(job));
    std::lock_guard<std::mutex> lock(control->mutex);
    if (!control->open) return false;
    control->jobs.push_back(std::move(pending));
    try { control->wake(); }
    catch (...) {
        pending = std::move(control->jobs.back());
        control->jobs.pop_back();
        throw;
    }
    ++control->accepted;
    return true;
}
/** @brief 使用不可变宿主线程编号，查询不访问 QObject。 */
bool QtScheduler::isCurrentThread() const noexcept { return std::this_thread::get_id() == control_->owner; }
/** @brief 与 post/close 共享锁，查询结果不承诺下一次投递一定接受。 */
bool QtScheduler::isOpen() const { std::lock_guard<std::mutex> lock(control_->mutex); return control_->open; }
/** @brief 只允许 owner 关闭，防止插件闭包在非 owner 线程销毁。 */
Result<void> QtScheduler::close() const
{
    if (!isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "scheduler close requires owner thread"));
    // discarded 可同时释放 facade 和 lifetime，保留控制块直到整批通知返回。
    auto control = control_;
    control->stop();
    return Result<void>::success();
}
/** @brief 关闭后账目仍可见，执行计数包含尚未退出的当前任务。 */
Result<SchedulerSnapshot> QtScheduler::snapshot() const
{
    if (!isCurrentThread()) return Result<SchedulerSnapshot>::failure(makeError(ErrorCode::WrongThread, "scheduler snapshot requires owner thread"));
    std::lock_guard<std::mutex> lock(control_->mutex);
    return Result<SchedulerSnapshot>::success({control_->open, control_->running, control_->jobs.size(), control_->accepted, control_->executed, control_->discarded, control_->errors});
}
} // namespace cordis::qt
