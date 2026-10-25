/** @file qt_io.cpp
 * @brief 专用串行线程只处理文件值，Qt owner 以 QThread.wait(0) 确认线程静止。
 */
#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <condition_variable>
#include <cordis/loader/qt_io.hpp>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace cordis::loader::detail {
/** @brief 有界后台任务和关闭上下文由应用保活，不依赖业务 scheduler 完成 join。 */
struct FileIoControl : std::enable_shared_from_this<FileIoControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    QString path;
    ConfigurationFileLimits limits;
    std::size_t maximumPending;
    mutable std::mutex mutex;
    std::unique_ptr<QThread> worker;
    std::condition_variable ready;
    std::deque<std::function<void()>> jobs;
    bool started = false;
    QObject* lifetime = nullptr;
    std::size_t pending = 0, completed = 0;
    std::uint64_t commitSequence = 0;
    std::optional<ConfigurationFileVersion> lastCommitted;
    bool closing = false, closed = false;
    Promise<void> stopped;
    /** @brief 一次关闭唤醒仅包含宿主控制块，持锁期间不运行完成回调。 */
    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (closing)
            return;
        closing = true;
        ready.notify_one();
        if (lifetime)
            QMetaObject::invokeMethod(lifetime, [self = shared_from_this()] { self->poll(); }, Qt::QueuedConnection);
    }
    /** @brief 只有所有 worker 已退出并 join 后才允许删除 owner 上下文。 */
    void poll()
    {
        if (worker->wait(0)) {
            if (lifetime)
                lifetime->deleteLater();
            return;
        }
        QTimer::singleShot(5, lifetime, [self = shared_from_this()] { self->poll(); });
    }
    /** @brief 只接受 owner 请求，锁内固定容量及关闭门禁，worker 不访问 Qt owner 控件。 */
    Result<void> enqueue(std::function<void()> job)
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "file I/O requires owner thread"));
        std::lock_guard<std::mutex> lock(mutex);
        if (closing || !scheduler->isOpen())
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "file I/O is closing"));
        if (pending >= maximumPending)
            return Result<void>::failure(makeError(ErrorCode::StepLimitExceeded, "file I/O queue is full"));
        jobs.push_back(std::move(job));
        ++pending;
        if (!started) {
            worker->start();
            if (!worker->isRunning()) {
                jobs.pop_back();
                --pending;
                return Result<void>::failure(makeError(ErrorCode::ConfigIo, "file worker could not start"));
            }
            started = true;
        }
        ready.notify_one();
        return Result<void>::success();
    }
    /** @brief worker 在锁外执行及销毁任务；关门后继续排空，队列为空才退出。 */
    void run()
    {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock, [&] { return closing || !jobs.empty(); });
                if (jobs.empty())
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
            job = {};
            std::lock_guard<std::mutex> lock(mutex);
            --pending;
            ++completed;
        }
    }
};
/** @brief QThread 不运行插件或 owner 事件循环，仅借用保活至 join 的宿主状态。 */
class FileWorker final : public QThread {
public:
    /** @brief 生命周期由 FileIoControl 持有，IO 上下文在 join 前保活控制块。 */
    explicit FileWorker(FileIoControl& state) : state_(state) {}

protected:
    /** @brief 整个执行器生命周期只进入一次排空循环，不在每次任务后反复销毁线程。 */
    void run() override { state_.run(); }

private:
    FileIoControl& state_;
};
/** @brief 应用拥有的最后保障；正常路径先 join 后销毁，遗漏显式关闭时同步排空。 */
class IoLifetime final : public QObject {
public:
    /** @brief 保活后台状态直到显式关闭或应用退出。 */
    IoLifetime(QObject* parent, std::shared_ptr<FileIoControl> state) : QObject(parent), state_(std::move(state)) {}
    /** @brief 不销毁仍在运行的线程；不使用 terminate、detach 或超时伪造静止。 */
    ~IoLifetime() override
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->closing = true;
            state_->lifetime = nullptr;
            state_->ready.notify_one();
        }
        state_->worker->wait();
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->closed = true;
        }
        state_->stopped.resolve();
    }

private:
    std::shared_ptr<FileIoControl> state_;
};
/** @brief 文件异常有固定阶段，不穿越 worker 边界。 */
ErrorInfo ioError(const char* message)
{
    auto error = makeError(ErrorCode::ConfigIo, message);
    error.phase = "loader.io.worker";
    return error;
}
} // namespace cordis::loader::detail
namespace cordis::loader {
/** @brief 专用线程惰性启动并保持到 close，owner 不等待文件 I/O。 */
QtConfigurationIo::QtConfigurationIo(std::shared_ptr<qt::QtScheduler> scheduler, QString path,
                                     ConfigurationFileLimits limits, std::size_t maximumPending)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() || !QCoreApplication::instance() ||
        path.isEmpty() || path.contains(QChar(0)) || !QDir::isAbsolutePath(path) || !limits.maximumBytes ||
        limits.maximumBytes >= static_cast<std::size_t>(std::numeric_limits<int>::max()) || !maximumPending ||
        maximumPending > 1024)
        throw std::invalid_argument("invalid owner file I/O setup");
    auto state = std::make_shared<detail::FileIoControl>();
    state->scheduler = std::move(scheduler);
    state->path = std::move(path);
    state->limits = limits;
    state->maximumPending = maximumPending;
    state->worker = std::make_unique<detail::FileWorker>(*state);
    state->lifetime = new detail::IoLifetime(QCoreApplication::instance(), state);
    control_ = std::move(state);
}
/** @brief 线程安全关门不接管业务会话的保存责任。 */
QtConfigurationIo::~QtConfigurationIo()
{
    control_->stop();
}
/** @brief Promise 可在 worker 完成；用户延续仍必须通过 thenOn 排回 owner。 */
Task<ConfigurationDiskObservation> QtConfigurationIo::read()
{
    auto state = control_;
    Promise<ConfigurationDiskObservation> done;
    auto accepted = state->enqueue([state, done] {
        try {
            auto result = readConfigurationFile(state->path, state->limits);
            if (!result.hasValue()) {
                done.reject(result.error());
                return;
            }
            ConfigurationDiskObservation observed;
            observed.disk = std::move(result).value();
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                observed.commitSequence = state->commitSequence;
                observed.lastCommitted = state->lastCommitted;
            }
            done.resolve(std::move(observed));
        } catch (const std::exception& error) {
            done.reject(detail::ioError(error.what()));
        } catch (...) {
            done.reject(detail::ioError("file read threw"));
        }
    });
    if (!accepted.hasValue())
        done.reject(accepted.error());
    return done.task();
}
/** @brief 提交 hash 在回执前保存，watcher 可识别确实由本后端写入的版本。 */
Task<ConfigurationFileCommit> QtConfigurationIo::commit(std::shared_ptr<const ValidatedCandidate> target,
                                                        ConfigurationFileVersion expected)
{
    auto state = control_;
    Promise<ConfigurationFileCommit> done;
    if (!target) {
        done.reject(makeError(ErrorCode::ConfigInvalid, "file writer requires a target"));
        return done.task();
    }
    auto accepted = state->enqueue([state, done, target = std::move(target), expected = std::move(expected)] {
        try {
            auto result = commitConfigurationFile(state->path, *target, expected, state->limits);
            if (!result.hasValue()) {
                done.reject(result.error());
                return;
            }
            if (result.value().outcome == FileCommitOutcome::Committed) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->lastCommitted = result.value().disk.version;
                ++state->commitSequence;
            }
            done.resolve(std::move(result).value());
        } catch (const std::exception& error) {
            done.reject(detail::ioError(error.what()));
        } catch (...) {
            done.reject(detail::ioError("file commit threw"));
        }
    });
    if (!accepted.hasValue())
        done.reject(accepted.error());
    return done.task();
}
/** @brief 所有跨线程账目在同一 mutex 下复制，不发布未同步版本。 */
Result<ConfigurationIoSnapshot> QtConfigurationIo::snapshot() const
{
    auto state = control_;
    if (!state->scheduler->isCurrentThread())
        return Result<ConfigurationIoSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "file I/O snapshot requires owner"));
    std::lock_guard<std::mutex> lock(state->mutex);
    return Result<ConfigurationIoSnapshot>::success(
        {state->pending, state->completed, state->commitSequence, state->lastCommitted, state->closing, state->closed});
}
/** @brief 调度器身份在构造后不可改变。 */
bool QtConfigurationIo::usesScheduler(const std::shared_ptr<qt::QtScheduler>& scheduler) const noexcept
{
    return control_->scheduler == scheduler;
}
/** @brief 关闭不依赖业务队列可投递，底层 Qt 循环仍须运行到线程 join 和上下文销毁。 */
Task<void> QtConfigurationIo::close()
{
    auto state = control_;
    if (!state->scheduler->isCurrentThread())
        return failedTask(makeError(ErrorCode::WrongThread, "file I/O close requires owner"));
    state->stop();
    return state->stopped.task();
}
} // namespace cordis::loader
