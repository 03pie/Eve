/** @file config_io.cpp
 * @brief 真实文件、私有 worker、原生 watcher 与 owner 事件循环验证后台配置资源。
 */
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <chrono>
#include <condition_variable>
#include <cordis/loader/qt_watcher.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 超时只负责诊断，完成取决于真实文件及资源状态。 */
void until(const std::function<bool()>& done)
{
    if (done())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0);
    deadline.start(5000);
    loop.exec();
    CHECK(done());
}
/** @brief 值回执可先于线程退出，但任务失败不可当作成功。 */
template <class T> void wait(const Task<T>& task)
{
    until([&] { return task.isReady(); });
    CHECK(task.tryResult()->hasValue());
}
/** @brief 不经过保存器模拟外部修改。 */
void write(const QString& path, const std::string& text)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
    file.close();
}
/** @brief 后台只接收通过验证的不可变目标，不执行 schema。 */
std::shared_ptr<const ValidatedCandidate> target(const char* value)
{
    auto parsed =
        parseConfiguration(std::string("formatVersion: 1\nplugins: [{id: r, plugin: demo, config: '") + value + "'}]")
            .value();
    return std::make_shared<const ValidatedCandidate>(
        validateCandidate(std::move(parsed), {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}})
            .value());
}
/** @brief 每项使用独立目录，关闭先 watcher 后 I/O 再 scheduler。 */
struct Scene {
    QTemporaryDir directory;
    QString path = directory.filePath(QString::fromUtf8("配置.yaml"));
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<QtConfigurationIo> io = std::make_shared<QtConfigurationIo>(scheduler, path);
    std::shared_ptr<QtConfigurationWatcher> watcher;
    /** @brief 由真实后台读取获取初始基线。 */
    ConfigurationFileSnapshot read()
    {
        auto task = io->read();
        wait(task);
        return task.tryResult()->value().disk;
    }
    /** @brief 零合并窗口缩短测试，仍使用真实原生通知及排队读取。 */
    void watch()
    {
        auto initial = io->read();
        wait(initial);
        watcher = std::make_shared<QtConfigurationWatcher>(scheduler, io, initial.tryResult()->value(), 0);
        scan();
    }
    /** @brief 显式复读及物理任务计数使消重断言不依赖固定睡眠。 */
    void scan()
    {
        auto observed = watcher->snapshot().value().observations;
        watcher->refresh().value();
        until([&] { return watcher->snapshot().value().observations > observed; });
    }
    /** @brief 显式等待物理 join，close 之后所有新业务均被拒绝。 */
    void close()
    {
        if (watcher)
            wait(watcher->close());
        wait(io->close());
        CHECK(io->snapshot().value().closed && io->snapshot().value().pending == 0);
        scheduler->close().value();
    }
};
/** @brief 原文/hash/缺失与空文件保持同步低层接口的一致语义。 */
void read()
{
    Scene scene;
    CHECK(!scene.read().version.exists);
    write(scene.path, "# raw\n");
    auto actual = scene.read();
    CHECK(actual.text == "# raw\n" && !actual.version.sha256.empty());
    scene.close();
}
/** @brief 同一后端读写严格串行，紧随提交的读取配对该次写入元数据。 */
void serial()
{
    Scene scene;
    auto before = scene.read();
    auto earlier = scene.io->read();
    auto first = scene.io->commit(target("first"), before.version);
    auto after = scene.io->read();
    wait(after);
    wait(first);
    wait(earlier);
    CHECK(earlier.tryResult()->value().commitSequence == 0 && !earlier.tryResult()->value().lastCommitted);
    CHECK(after.tryResult()->value().disk.text == target("first")->source().normalizedYaml());
    CHECK(after.tryResult()->value().commitSequence == 1 && after.tryResult()->value().lastCommitted);
    auto second = scene.io->commit(target("second"), after.tryResult()->value().disk.version);
    wait(second);
    CHECK(second.tryResult()->value().outcome == FileCommitOutcome::Committed);
    scene.close();
}
/** @brief 后台写仍保留冲突原文，锁失败仍明确失败。 */
void conflict()
{
    Scene scene;
    auto before = scene.read();
    write(scene.path, "external");
    auto saved = scene.io->commit(target("one"), before.version);
    wait(saved);
    CHECK(saved.tryResult()->value().outcome == FileCommitOutcome::Conflict &&
          saved.tryResult()->value().disk.text == "external");
    QLockFile lock(scene.path + ".cordis.lock");
    CHECK(lock.tryLock(0));
    auto locked = scene.io->commit(target("two"), scene.read().version);
    until([&] { return locked.isReady(); });
    CHECK(!locked.tryResult()->hasValue());
    lock.unlock();
    scene.close();
}
/** @brief close 拒绝新任务，但排空关门前全部请求；完成时线程已 join。 */
void shutdown()
{
    Scene scene;
    auto before = scene.read();
    auto saved = scene.io->commit(target("last"), before.version);
    std::vector<Task<ConfigurationDiskObservation>> reads;
    for (int i = 0; i < 8; ++i)
        reads.push_back(scene.io->read());
    auto closed = scene.io->close();
    CHECK(!scene.io->read().tryResult()->hasValue());
    wait(closed);
    wait(saved);
    for (const auto& task : reads)
        wait(task);
    CHECK(scene.io->snapshot().value().pending == 0);
    CHECK(readConfigurationFile(scene.path).value().text == target("last")->source().normalizedYaml());
    scene.close();
}
/** @brief 释放 facade 不取消已经接受的文件事务；控制块保活到物理完成。 */
void dropped()
{
    Scene scene;
    auto before = scene.read();
    auto done = scene.io->commit(target("kept"), before.version);
    std::weak_ptr<QtConfigurationIo> weak = scene.io;
    scene.io.reset();
    wait(done);
    CHECK(weak.expired());
    CHECK(readConfigurationFile(scene.path).value().text == target("kept")->source().normalizedYaml());
    // 应用拥有的上下文会自行 join；业务 scheduler 不负责物理 I/O。
    scene.scheduler->close().value();
}
/** @brief 线程、空目标、无效容量与文件预算在正确边界失败。 */
void guards()
{
    Scene scene;
    bool wrong = false;
    std::thread worker([&] {
        auto task = scene.io->read();
        wrong =
            task.isReady() && !task.tryResult()->hasValue() && task.tryResult()->error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    CHECK(!scene.io->commit({}, {}).tryResult()->hasValue());
    bool invalid = false;
    try {
        QtConfigurationIo bad(scene.scheduler, scene.path, {}, 0);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    CHECK(invalid);
    auto limited = std::make_shared<QtConfigurationIo>(scene.scheduler, scene.path, ConfigurationFileLimits{2});
    write(scene.path, "oversize");
    auto task = limited->read();
    until([&] { return task.isReady(); });
    CHECK(!task.tryResult()->hasValue() && task.tryResult()->error().code == ErrorCode::ConfigInvalid);
    wait(limited->close());
    scene.close();
}
/** @brief 真实原生文件通知发布原文，确认通知不改磁盘且同 hash 不重复。 */
void changes()
{
    Scene scene;
    scene.watch();
    write(scene.path, "changed");
    until([&] { return scene.watcher->snapshot().value().external != nullptr; });
    auto state = scene.watcher->snapshot().value();
    CHECK(state.external->text == "changed");
    CHECK(!scene.watcher->acknowledge(state.changeSequence + 1).hasValue());
    scene.watcher->acknowledge(state.changeSequence).value();
    scene.scan();
    CHECK(!scene.watcher->snapshot().value().external &&
          scene.watcher->snapshot().value().changeSequence == state.changeSequence);
    write(scene.path, "changed");
    scene.scan();
    CHECK(!scene.watcher->snapshot().value().external);
    scene.close();
}
/** @brief 原子替换后文件监听重新注册，后续原地编辑仍被发现；删除与重建也可观察。 */
void replace()
{
    Scene scene;
    write(scene.path, "old");
    scene.watch();
    QSaveFile file(scene.path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("replace") == 7 && file.commit());
    until([&] {
        auto state = scene.watcher->snapshot().value();
        return state.external && state.external->text == "replace" && state.watchingFile;
    });
    write(scene.path, "next");
    until([&] {
        auto state = scene.watcher->snapshot().value();
        return state.external && state.external->text == "next";
    });
    CHECK(QFile::remove(scene.path));
    until([&] {
        auto state = scene.watcher->snapshot().value();
        return state.external && !state.external->version.exists;
    });
    write(scene.path, "reborn");
    until([&] {
        auto state = scene.watcher->snapshot().value();
        return state.external && state.external->text == "reborn" && state.watchingFile;
    });
    scene.close();
}
/** @brief 自身新提交消重；外部再回退到相同旧内容必须作为新变化，不能永久 hash 白名单。 */
void own()
{
    Scene scene;
    auto initial = scene.read();
    scene.watch();
    auto saved = scene.io->commit(target("own"), initial.version);
    wait(saved);
    scene.scan();
    CHECK(!scene.watcher->snapshot().value().external && scene.watcher->snapshot().value().changeSequence == 0);
    write(scene.path, "external");
    until([&] { return scene.watcher->snapshot().value().external != nullptr; });
    auto seq = scene.watcher->snapshot().value().changeSequence;
    scene.watcher->acknowledge(seq).value();
    write(scene.path, target("own")->source().normalizedYaml());
    until([&] {
        auto state = scene.watcher->snapshot().value();
        return state.external && state.changeSequence > seq;
    });
    scene.close();
}
/** @brief 目录删除后恢复监听；固定路径继续受原读层的普通文件及预算检查。 */
void directory()
{
    Scene scene;
    auto nested = scene.directory.filePath("nested");
    CHECK(QDir().mkpath(nested));
    auto path = nested + "/config.yaml";
    auto io = std::make_shared<QtConfigurationIo>(scene.scheduler, path);
    auto initial = io->read();
    wait(initial);
    auto watcher = std::make_shared<QtConfigurationWatcher>(scene.scheduler, io, initial.tryResult()->value(), 0);
    CHECK(QDir().rmdir(nested));
    until([&] { return !watcher->snapshot().value().watchingDirectory; });
    CHECK(QDir().mkpath(nested));
    write(path, "new directory");
    until([&] {
        auto state = watcher->snapshot().value();
        return state.watchingDirectory && state.external && state.external->text == "new directory";
    });
    wait(watcher->close());
    wait(io->close());
    scene.close();
}
/** @brief 业务 scheduler 关闭不妨碍 IO join，watcher 未交付回执则报告失败。 */
void blocked()
{
    Scene scene;
    scene.watch();
    scene.scheduler->close().value();
    scene.watcher->refresh().value();
    until([&] { return scene.watcher->snapshot().value().deliveryBlocked; });
    auto stopped = scene.watcher->close();
    until([&] { return stopped.isReady(); });
    CHECK(!stopped.tryResult()->hasValue());
    wait(scene.io->close());
    CHECK(scene.io->snapshot().value().closed && scene.watcher->snapshot().value().deliveryBlocked);
}
/** @brief 关门前只排了合并计时器时不会再发起读取。 */
void queuedClose()
{
    Scene scene;
    scene.watch();
    auto completed = scene.io->snapshot().value().completed;
    scene.watcher->refresh().value();
    wait(scene.watcher->close());
    CHECK(scene.io->snapshot().value().completed == completed);
    CHECK(!scene.watcher->refresh().hasValue());
    scene.close();
}
/** @brief 暂停 owner 事件处理，模拟界面繁忙期间完成后台保存，不用 sleep 猜测时序。 */
template <class T> void waitWorker(const Task<T>& task)
{
    struct Gate {
        std::mutex mutex;
        std::condition_variable ready;
        bool completed = false;
    };
    auto gate = std::make_shared<Gate>();
    cordis::detail::TaskAccess::observe(task, [gate](typename Task<T>::Snapshot) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->completed = true;
        gate->ready.notify_one();
    });
    std::unique_lock<std::mutex> lock(gate->mutex);
    CHECK(gate->ready.wait_for(lock, std::chrono::seconds(5), [&] { return gate->completed; }));
    CHECK(task.tryResult()->hasValue());
}
/** @brief 自身保存之后、监听注册之前的窗口不能将自身提交错报为外部改动。 */
void lateWatch()
{
    Scene scene;
    auto initial = scene.io->read();
    wait(initial);
    auto saved = scene.io->commit(target("own"), initial.tryResult()->value().disk.version);
    waitWorker(saved);
    scene.watcher =
        std::make_shared<QtConfigurationWatcher>(scene.scheduler, scene.io, initial.tryResult()->value(), 0);
    scene.scan();
    CHECK(!scene.watcher->snapshot().value().external);
    wait(scene.watcher->close());
    write(scene.path, "external baseline");
    auto external = scene.io->read();
    wait(external);
    write(scene.path, target("own")->source().normalizedYaml());
    scene.watcher =
        std::make_shared<QtConfigurationWatcher>(scene.scheduler, scene.io, external.tryResult()->value(), 0);
    scene.scan();
    CHECK(scene.watcher->snapshot().value().external);
    scene.close();
}
/** @brief 新提交被外部改回旧已观察内容仍须通知，不能仅比较最后观察的 hash。 */
void ownRevert()
{
    Scene scene;
    write(scene.path, "original");
    auto initial = scene.read();
    scene.watch();
    auto saved = scene.io->commit(target("own"), initial.version);
    waitWorker(saved);
    write(scene.path, "original");
    scene.scan();
    auto state = scene.watcher->snapshot().value();
    CHECK(state.external && state.external->text == "original" && state.changeSequence == 1);
    scene.close();
}
/** @brief 每个场景独立应用，生产路径只使用真实临时文件。 */
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "late_watch")
            lateWatch();
        else if (name == "own_revert")
            ownRevert();
        else if (name == "read")
            read();
        else if (name == "serial")
            serial();
        else if (name == "conflict")
            conflict();
        else if (name == "shutdown")
            shutdown();
        else if (name == "dropped")
            dropped();
        else if (name == "guards")
            guards();
        else if (name == "changes")
            changes();
        else if (name == "replace")
            replace();
        else if (name == "own")
            own();
        else if (name == "directory")
            directory();
        else if (name == "blocked")
            blocked();
        else if (name == "queued_close")
            queuedClose();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
