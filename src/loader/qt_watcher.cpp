/** @file qt_watcher.cpp
 * @brief 合并通知、恢复文件/目录监听，以存在性和原文 hash 判定变化，物理自身写入按序号消重。
 */
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <cordis/loader/qt_watcher.hpp>
#include <limits>
#include <stdexcept>
#include <utility>
namespace cordis::loader::detail {
/** @brief 仅内容版本相同才忽略，mtime 或多次原生通知不能制造外部修改。 */
bool sameDisk(const ConfigurationFileVersion& left, const ConfigurationFileVersion& right)
{
    return left.path == right.path && left.exists == right.exists && left.sha256 == right.sha256;
}
/** @brief 纯宿主状态；除关闭请求和投递失败门禁外均由 owner 更新。 */
struct FileWatchControl : std::enable_shared_from_this<FileWatchControl> {
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::shared_ptr<QtConfigurationIo> io;
    QPointer<QObject> lifetime;
    QFileSystemWatcher* native = nullptr;
    QTimer *debounce = nullptr, *maintenance = nullptr;
    QString path, parent;
    ConfigurationFileVersion observed;
    std::uint64_t ownSequence = 0, changeSequence = 0;
    std::size_t observations = 0;
    std::shared_ptr<const ConfigurationFileSnapshot> external;
    std::optional<ErrorInfo> error;
    unsigned debounceMilliseconds = 75;
    bool reading = false, again = false, closing = false, closed = false;
    std::atomic_bool requestedClose{false}, blocked{false};
    Promise<void> stopped;
    /** @brief 所有公开方法都拒绝跨线程读取或修改非原子账目。 */
    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(makeError(ErrorCode::WrongThread, "file watcher requires owner thread"));
        return Result<void>::success();
    }
    /** @brief 注册失败保持可见错误；返回新增登记，调用方须复读覆盖重连窗口。 */
    bool arm()
    {
        if (closing || requestedClose.load())
            return false;
        // 原生删除通知可能延迟；登记表不能证明目录仍存在。
        if (!QFileInfo(parent).isDir()) {
            if (native->directories().contains(parent))
                native->removePath(parent);
            if (native->files().contains(path))
                native->removePath(path);
            error = makeError(ErrorCode::ConfigIo, "configuration directory watch unavailable");
            return false;
        }
        bool added = false;
        if (!QFileInfo::exists(path) && native->files().contains(path))
            native->removePath(path);
        if (!native->directories().contains(parent)) {
            if (native->addPath(parent))
                added = true;
            else
                error = makeError(ErrorCode::ConfigIo, "configuration directory watch unavailable");
        }
        if (QFileInfo::exists(path) && !native->files().contains(path)) {
            if (native->addPath(path))
                added = true;
            else
                error = makeError(ErrorCode::ConfigIo, "configuration file watch unavailable");
        }
        return added;
    }
    /** @brief 文件与目录事件共用有界合并窗口；正在读取时只记录一次后续请求。 */
    void request()
    {
        if (closing || requestedClose.load())
            return;
        if (reading) {
            again = true;
            return;
        }
        if (!debounce->isActive())
            debounce->start(static_cast<int>(debounceMilliseconds));
    }
    /** @brief 后台读取只传文件值，owner 回执弱持监听控制块。 */
    void read()
    {
        if (closing || requestedClose.load()) {
            beginClose();
            return;
        }
        if (reading) {
            again = true;
            return;
        }
        if (!scheduler->isOpen()) {
            blocked.store(true);
            beginClose();
            return;
        }
        reading = true;
        again = false;
        auto self = shared_from_this();
        std::weak_ptr<FileWatchControl> weak = self;
        auto receipt = io->read().thenOn(scheduler, [weak](const Result<ConfigurationDiskObservation>& result) {
            auto state = weak.lock();
            if (!state)
                return;
            state->reading = false;
            if (state->requestedClose.load() || state->closing) {
                state->beginClose();
                return;
            }
            if (state->again) {
                state->again = false;
                state->request();
                return;
            }
            if (!result.hasValue())
                state->error = result.error();
            else if (result.value().disk.version.path != state->observed.path)
                state->error = makeError(ErrorCode::ConfigConflict, "configuration watcher path changed");
            else {
                const auto& disk = result.value().disk;
                const auto& writes = result.value();
                const bool committed = writes.commitSequence > state->ownSequence && writes.lastCommitted;
                const bool own = committed && sameDisk(*writes.lastCommitted, disk.version);
                // 新提交被外部回退到旧观察值时，也不能按旧 hash 忽略。
                const bool overwrittenCommit = committed && !own;
                state->ownSequence = writes.commitSequence;
                state->error.reset();
                if (!sameDisk(state->observed, disk.version) || overwrittenCommit) {
                    state->observed = disk.version;
                    if (own)
                        state->external.reset();
                    else if (state->changeSequence == std::numeric_limits<std::uint64_t>::max())
                        state->error = makeError(ErrorCode::StepLimitExceeded, "file change sequence exhausted");
                    else {
                        ++state->changeSequence;
                        state->external = std::make_shared<const ConfigurationFileSnapshot>(disk);
                    }
                }
            }
            // 读取期间重建目录可能没有原生通知；重新登记后再复读一次覆盖该窗口。
            if (state->arm())
                state->request();
            ++state->observations;
        });
        cordis::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
            if (!result->hasValue())
                if (auto state = weak.lock())
                    state->blocked.store(true);
        });
    }
    /** @brief 私有 Qt 心跳不依赖业务 scheduler 仍开放，关闭回执丢失时报告失败。 */
    void tick()
    {
        if (requestedClose.load() || blocked.load()) {
            beginClose();
            return;
        }
        const bool missing = !QFileInfo(parent).isDir() || !native->directories().contains(parent) ||
                             (QFileInfo::exists(path) != native->files().contains(path));
        if (missing) {
            arm();
            request();
        }
    }
    /** @brief 先断开原生入口，再等待已经接受的回执；投递失败不能伪装成功。 */
    void beginClose()
    {
        if (closed)
            return;
        requestedClose.store(true);
        if (!closing) {
            closing = true;
            debounce->stop();
            QObject::disconnect(native, nullptr, lifetime, nullptr);
            auto paths = native->files() + native->directories();
            if (!paths.empty())
                native->removePaths(paths);
        }
        if (blocked.load()) {
            reading = false;
            error = makeError(ErrorCode::SchedulerClosed, "configuration watch delivery blocked");
        }
        if (!reading && lifetime)
            lifetime->deleteLater();
    }
};
/** @brief 应用保活监听，窗口/插件页释放不能留下悬空 Qt 回调。 */
class WatchLifetime final : public QObject {
public:
    /** @brief 私有 Qt 对象均属于应用 owner，业务状态弱观察时仍可完成退出。 */
    explicit WatchLifetime(std::shared_ptr<FileWatchControl> state)
        : QObject(QCoreApplication::instance()), state_(std::move(state))
    {
    }
    /** @brief 结束时拒绝未静止或投递失败的关闭，不发布假成功。 */
    ~WatchLifetime() override
    {
        state_->closing = true;
        state_->closed = true;
        state_->lifetime = nullptr;
        if (state_->reading || state_->blocked.load())
            state_->stopped.reject(
                makeError(ErrorCode::SchedulerClosed, "configuration watcher did not drain owner delivery"));
        else
            state_->stopped.resolve();
    }

private:
    std::shared_ptr<FileWatchControl> state_;
};
} // namespace cordis::loader::detail
namespace cordis::loader {
/** @brief 原始文件与父目录同时注册，目录通知覆盖 QSaveFile 替换和删除重建。 */
QtConfigurationWatcher::QtConfigurationWatcher(std::shared_ptr<qt::QtScheduler> scheduler,
                                               std::shared_ptr<QtConfigurationIo> io,
                                               const ConfigurationDiskObservation& initial,
                                               unsigned debounceMilliseconds)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() || !io || !io->usesScheduler(scheduler) ||
        initial.disk.version.path.empty() ||
        !QDir::isAbsolutePath(QString::fromUtf8(initial.disk.version.path.c_str())) || debounceMilliseconds > 60000)
        throw std::invalid_argument("invalid configuration watcher setup");
    auto state = std::make_shared<detail::FileWatchControl>();
    state->scheduler = std::move(scheduler);
    state->io = std::move(io);
    state->path = QString::fromUtf8(initial.disk.version.path.c_str());
    state->parent = QFileInfo(state->path).absolutePath();
    state->observed = initial.disk.version;
    state->ownSequence = initial.commitSequence;
    state->debounceMilliseconds = debounceMilliseconds;
    state->lifetime = new detail::WatchLifetime(state);
    state->native = new QFileSystemWatcher(state->lifetime);
    state->debounce = new QTimer(state->lifetime);
    state->debounce->setSingleShot(true);
    state->maintenance = new QTimer(state->lifetime);
    state->maintenance->setInterval(250);
    std::weak_ptr<detail::FileWatchControl> weak = state;
    QObject::connect(state->native, &QFileSystemWatcher::fileChanged, state->lifetime, [weak] {
        if (auto value = weak.lock()) {
            value->arm();
            value->request();
        }
    });
    QObject::connect(state->native, &QFileSystemWatcher::directoryChanged, state->lifetime, [weak] {
        if (auto value = weak.lock()) {
            value->arm();
            value->request();
        }
    });
    QObject::connect(state->debounce, &QTimer::timeout, state->lifetime, [weak] {
        if (auto value = weak.lock())
            value->read();
    });
    QObject::connect(state->maintenance, &QTimer::timeout, state->lifetime, [weak] {
        if (auto value = weak.lock())
            value->tick();
    });
    state->arm();
    state->request();
    state->maintenance->start();
    control_ = std::move(state);
}
/** @brief 任意线程释放只写原子门禁，owner 心跳完成原生断连。 */
QtConfigurationWatcher::~QtConfigurationWatcher()
{
    control_->requestedClose.store(true);
}
/** @brief 关闭后不访问已经销毁的 QFileSystemWatcher。 */
Result<ConfigurationWatchSnapshot> QtConfigurationWatcher::snapshot() const
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue())
        return Result<ConfigurationWatchSnapshot>::failure(checked.error());
    return Result<ConfigurationWatchSnapshot>::success(
        {state->changeSequence, state->observations, state->external, state->error, state->reading,
         !state->closed && state->native->directories().contains(state->parent),
         !state->closed && state->native->files().contains(state->path), state->closing || state->requestedClose.load(),
         state->closed, state->blocked.load()});
}
/** @brief 只清理当前通知，不把所读原文设为保存基线。 */
Result<void> QtConfigurationWatcher::acknowledge(std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue())
        return checked;
    if (state->requestedClose.load() || state->closing)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration watcher closing"));
    if (expected != state->changeSequence || !state->external)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "configuration disk notice changed"));
    state->external.reset();
    return Result<void>::success();
}
/** @brief 手动刷新也遵守合并与单读门禁，不直接读取或覆盖编辑。 */
Result<void> QtConfigurationWatcher::refresh()
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue())
        return checked;
    if (state->requestedClose.load() || state->closing)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "configuration watcher closing"));
    state->request();
    return Result<void>::success();
}
/** @brief 成功只证明 watcher/回执静止，IO worker 的 join 仍由共享后端 close 负责。 */
Task<void> QtConfigurationWatcher::close()
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue())
        return failedTask(checked.error());
    state->beginClose();
    return state->stopped.task();
}
} // namespace cordis::loader
