/** @file config_store.cpp
 * @brief 用真实 Qt 文件事务验证 Store 写入、冲突决策、锁失败及真实事件循环退出刷新。
 */
#include <cordis/loader/qt_store.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QLockFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace cordis;
using namespace cordis::loader;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 生成真实验证过的规范目标。 */
std::shared_ptr<const ValidatedCandidate> target(const std::string& text)
{
    auto source = parseConfiguration("formatVersion: 1\nplugins: [{id: notes, plugin: demo, config: '" + text + "'}]").value();
    return std::make_shared<const ValidatedCandidate>(validateCandidate(std::move(source),
        {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}).value());
}
/** @brief 模拟非协作外部编辑，不使用 Store 基线。 */
void externalWrite(const QString& path, const std::string& text)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
    file.close(); CHECK(file.error() == QFileDevice::NoError);
}
/** @brief 每项测试只操作专属临时目录，创建 Store 前真实读取初始磁盘基线。 */
struct Scenario {
    QTemporaryDir directory;
    QString path = directory.filePath(QString::fromUtf8("配置.yaml"));
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::shared_ptr<QtConfigurationWriter> writer = std::make_shared<QtConfigurationWriter>(path);
    ConfigurationStore store{scheduler, writer, readConfigurationFile(path).value()};
    /** @brief 每个实例均要求实际可用的目录。 */
    Scenario() { CHECK(directory.isValid()); }
    /** @brief FIFO 只控制 Store 调度，真正 I/O 使用生产 Qt 后端。 */
    void pump() { scheduler->runUntilIdle().value(); }
    /** @brief 显式排空后才关闭 scheduler。 */
    void close() { auto task = store.close(); pump(); CHECK(task.tryResult()->hasValue()); scheduler->close(); }
};
/** @brief 保存任务先返回，泵送后文件与历史保存状态一起推进；close 自动保存未排队最新目标。 */
void persist()
{
    Scenario scene;
    auto first = target("one"); scene.store.track(first, 1).value(); auto saved = scene.store.save(1).value();
    CHECK(!saved.isReady() && !QFile::exists(scene.path)); scene.pump();
    CHECK(saved.tryResult()->value().outcome == SaveOutcome::Written);
    auto state = scene.store.snapshot().value();
    CHECK(!state.dirty && state.persistedRevision == 1 && state.persisted == first);
    CHECK(readConfigurationFile(scene.path).value().version.sha256 == state.baseline->version.sha256);
    auto same = scene.store.save(1).value(); scene.pump(); CHECK(same.tryResult()->value().outcome == SaveOutcome::Unchanged);
    auto second = target("two"); scene.store.track(second, 2).value(); scene.close();
    CHECK(readConfigurationFile(scene.path).value().text == second->source().normalizedYaml());
    CHECK(scene.store.snapshot().value().persistedRevision == 2 && saved.tryResult()->value().revision == 1);
    CHECK(!QFile::exists(scene.path + QStringLiteral(".cordis.lock")));
}
/** @brief 外部非法 YAML 不被保存覆盖，显式 rebase 后新保存才可使用外部版本。 */
void conflict()
{
    Scenario scene;
    scene.store.track(target("one"), 1).value(); scene.store.save(1).value(); scene.pump();
    auto baseline = scene.store.snapshot().value().baseline;
    const std::string external = "# external\nplugins: [incomplete";
    externalWrite(scene.path, external);
    auto next = target("two"); scene.store.track(next, 2).value(); auto saved = scene.store.save(2).value(); scene.pump();
    CHECK(saved.tryResult()->value().outcome == SaveOutcome::Conflict && saved.tryResult()->value().external->text == external);
    CHECK(scene.store.snapshot().value().baseline == baseline && scene.store.snapshot().value().persistedRevision == 1);
    CHECK(scene.store.snapshot().value().desired == next && scene.store.snapshot().value().dirty);
    CHECK(readConfigurationFile(scene.path).value().text == external);
    scene.store.rebase(readConfigurationFile(scene.path).value(), 2).value();
    auto retry = scene.store.save(2).value(); scene.pump();
    CHECK(retry.tryResult()->value().outcome == SaveOutcome::Written && !scene.store.snapshot().value().dirty);
    CHECK(saved.tryResult()->value().external->text == external); scene.close();
}
/** @brief 真实锁占用使保存失败，保留 dirty 和基线；释放后可重试同一目标修订。 */
void failure()
{
    Scenario scene;
    scene.store.track(target("locked"), 1).value();
    std::optional<Task<SaveReport>> first;
    {
        QLockFile lock(scene.path + QStringLiteral(".cordis.lock"));
        lock.setStaleLockTime(0); CHECK(lock.tryLock(0));
        first = scene.store.save(1).value(); scene.pump();
        CHECK(first->tryResult()->value().outcome == SaveOutcome::Failed && first->tryResult()->value().error->code == ErrorCode::ConfigConflict);
        CHECK(scene.store.snapshot().value().dirty && !scene.store.snapshot().value().persistedRevision && !QFile::exists(scene.path));
    }
    auto retry = scene.store.save(1).value(); scene.pump();
    CHECK(retry.tryResult()->value().outcome == SaveOutcome::Written && first->tryResult()->value().outcome == SaveOutcome::Failed);
    scene.close();
}
/** @brief 使用生产 QtScheduler 保持事件循环，最后一个目标落盘并收到回执后才停止 scheduler。 */
void qtShutdown()
{
    QTemporaryDir directory; CHECK(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("exit.yaml"));
    auto scheduler = std::make_shared<cordis::qt::QtScheduler>();
    auto writer = std::make_shared<QtConfigurationWriter>(path);
    ConfigurationStore store(scheduler, writer, readConfigurationFile(path).value());
    auto desired = target("exit"); store.track(desired, 5).value();
    auto close = store.close(); CHECK(!close.isReady() && !QFile::exists(path));
    QEventLoop loop;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto receipt = close.thenOn(scheduler, [&](const Result<void>&) { loop.quit(); });
    deadline.start(5000); loop.exec();
    CHECK(receipt.isReady() && receipt.tryResult()->hasValue() && close.tryResult()->hasValue());
    CHECK(store.snapshot().value().closed && store.snapshot().value().persistedRevision == 5 && !store.snapshot().value().dirty);
    CHECK(readConfigurationFile(path).value().text == desired->source().normalizedYaml());
    scheduler->close().value();
}
/** @brief 控制台 Qt 应用即可执行，无桌面窗口和人工验收替代。 */
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "persist") persist();
        else if (name == "conflict") conflict();
        else if (name == "failure") failure();
        else if (name == "shutdown") qtShutdown();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
