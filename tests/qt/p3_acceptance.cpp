/** @file p3_acceptance.cpp
 * @brief 以真实 Kernel、后台文件、配置页和 Ela 窗口验证 P3 跨组件退出条件。
 */
#include <ElaWindow.h>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_watcher.hpp>
#include <cordis/qt/connection.hpp>
#include <cordis/qt/worker.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace cordis;
using namespace cordis::loader;
using namespace cordis::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

/** @brief 超时仅用于失败保护，进度由真实状态而非等待时长判定。 */
void until(const std::function<bool()>& done)
{
    if (done())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (done()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0);
    deadline.start(8000);
    loop.exec();
    CHECK(done());
}
/** @brief 同时检查任务终结及其成功值。 */
template <class T> void wait(const Task<T>& task)
{
    until([&] { return task.isReady(); });
    CHECK(task.tryResult()->hasValue());
}
/** @brief 示例 schema 给出独立字段路径，供会话和真实配置页同时展示。 */
Result<void> titleSchema(const std::string& json)
{
    const auto value = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object().value("title");
    if (value.isString() && !value.toString().isEmpty())
        return Result<void>::success();
    auto error = makeError(ErrorCode::ConfigInvalid, "title must be a nonempty string");
    error.entryPath = "config.title";
    return Result<void>::failure(std::move(error));
}
/** @brief 每项拥有真实文件/窗口；业务页面由 Context effect 管理，不能由测试直接删除。 */
struct Scene {
    QTemporaryDir directory;
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    std::shared_ptr<QtConfigurationIo> io;
    std::shared_ptr<QtConfigurationWatcher> watcher;
    std::shared_ptr<ConfigurationSession> session;
    std::shared_ptr<IConfigurationManagement> management;
    QPointer<ElaWindow> window;
    QPointer<QWidget> editorPage;
    std::map<InstanceId, PageHandle> pages;
    std::vector<QPointer<QWidget>> widgets;
    std::function<Task<void>(Context)> resources;
    int activated = 0, blockedActivations = 0;

    /** @brief 不自动接受配置，所有读写经同一个后台执行器。 */
    Scene()
    {
        CHECK(directory.isValid());
        io = std::make_shared<QtConfigurationIo>(scheduler, path());
        auto read = io->read();
        wait(read);
        const auto initial = read.tryResult()->value();
        watcher = std::make_shared<QtConfigurationWatcher>(scheduler, io, initial, 0);
        auto note = kernel->registerPlugin("note", [&](Context context) {
            ++activated;
            const auto title = QJsonDocument::fromJson(QByteArray::fromStdString(context.config()))
                                   .object().value("title").toString();
            if (title == "fail")
                return failedTask(makeError(ErrorCode::ActivationFailed, "requested activation failure"));
            auto page = shell.pages()->add(context, {"note", title.toStdString(), "notes", [&, title] {
                auto widget = std::make_unique<QLabel>(title);
                widgets.emplace_back(widget.get());
                return widget;
            }}).value();
            pages.insert_or_assign(context.identity().value().instance, page);
            if (resources)
                return resources(context);
            return page.ready();
        }).value();
        auto blocked = kernel->registerPlugin("blocked", [&](Context) {
            ++blockedActivations;
            return completedTask();
        }, {ServiceRequirement::of<int>({"missing.store"})}).value();
        auto driver = std::make_shared<KernelReconcileDriver>(scheduler, kernel,
            std::vector<KernelPlugin>{{"note", note}, {"blocked", blocked}});
        session = std::make_shared<ConfigurationSession>(scheduler, driver,
            std::vector<PluginDefinition>{{"note", {}, titleSchema},
                {"blocked", {{"missing.store", 1, 0, {}}}, [](const std::string&) { return Result<void>::success(); }}},
            io, initial.disk);
        management = createConfigurationManagement(session, scheduler);
        shell.management()->attach(kernel).value();
        shell.management()->attachConfiguration(management, {note.id(), blocked.id()}).value();
        auto host = kernel->registerPlugin("host.editor", [&](Context context) {
            return shell.pages()->add(context, {"editor", "Configuration", "host", [&] {
                auto widget = createConfigurationPage(session, {}, watcher);
                editorPage = widget.get();
                return widget;
            }}).value().ready();
        }).value();
        auto mounted = kernel->mount(host).value();
        wait(mounted.ready());
        CHECK(editorPage);
        shell.show().value();
        for (auto widget : QApplication::topLevelWidgets())
            if (auto candidate = qobject_cast<ElaWindow*>(widget))
                window = candidate;
        CHECK(window);
    }
    /** @brief 路径始终限定在本项临时目录。 */
    QString path() const { return directory.filePath("p3.yaml"); }
    /** @brief 通过真实配置页按钮接受候选，返回当前目标修订。 */
    std::uint64_t apply(const std::string& entries)
    {
        auto editor = editorPage->findChild<QPlainTextEdit*>("CordisConfigEditor");
        editor->setPlainText(QString::fromStdString("formatVersion: 1\nplugins: " + entries));
        editorPage->findChild<QPushButton*>("CordisConfigPreview")->click();
        auto button = editorPage->findChild<QPushButton*>("CordisConfigApply");
        CHECK(button->isEnabled());
        const auto previous = session->snapshot().value().coordinator.desiredRevision;
        button->click();
        const auto revision = session->snapshot().value().coordinator.desiredRevision;
        CHECK(revision == previous + 1);
        until([&] {
            const auto value = session->snapshot().value();
            return value.latestPlan && value.latestPlan->revision == revision;
        });
        return revision;
    }
    /** @brief 读取真实文件并确认后台队列已经完成提交。 */
    SaveReport save()
    {
        auto value = session->save(session->snapshot().value().coordinator.desiredRevision).value();
        wait(value);
        return value.tryResult()->value();
    }
    /** @brief 正常退出按 watcher、会话、文件线程、其余内核及窗口顺序排空。 */
    void close()
    {
        wait(watcher->close());
        wait(session->close());
        wait(io->close());
        auto stopped = kernel->close();
        wait(stopped);
        CHECK(stopped.tryResult()->value().quiescent);
        wait(shell.close());
        CHECK(!window && !editorPage && io->snapshot().value().closed && watcher->snapshot().value().closed);
        for (const auto& widget : widgets)
            CHECK(!widget);
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};

/** @brief L01：无效原文的 UI/会话拒绝不改变活动父子树、页面、保存状态或真实文件。 */
void invalidCandidate()
{
    Scene scene;
    scene.apply("[{id: a, plugin: note, config: {title: parent}, children: "
                "[{id: c, plugin: note, config: {title: child}}]}]");
    CHECK(scene.save().outcome == SaveOutcome::Written);
    const auto before = scene.session->snapshot().value();
    const auto disk = readConfigurationFile(scene.path()).value();
    CHECK(before.coordinator.running.size() == 2 && scene.widgets.size() == 2);
    for (const auto& page : scene.pages)
        page.second.navigate().value();
    const std::vector<std::string> invalid{
        "formatVersion: 1\nplugins: [",
        "formatVersion: 1\nplugins: [{id: a, plugin: note, config: {title: 42}}]"};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        const auto checked = scene.session->preview(invalid[i]);
        CHECK(!checked.hasValue());
        if (i == 1) {
            CHECK(checked.error().phase == "loader.schema" && checked.error().entryPath == "a");
            CHECK(checked.error().causes.size() == 1 && checked.error().causes.front().entryPath == "config.title");
        }
        scene.editorPage->findChild<QPlainTextEdit*>("CordisConfigEditor")->setPlainText(QString::fromStdString(invalid[i]));
        scene.editorPage->findChild<QPushButton*>("CordisConfigPreview")->click();
        CHECK(!scene.editorPage->findChild<QPushButton*>("CordisConfigApply")->isEnabled());
        if (i == 1)
            CHECK(scene.editorPage->findChild<QPlainTextEdit*>("CordisConfigDetails")->toPlainText().contains("config.title"));
        const auto after = scene.session->snapshot().value();
        CHECK(after.coordinator.desired == before.coordinator.desired &&
              after.coordinator.desiredRevision == before.coordinator.desiredRevision &&
              after.store.persistedRevision == before.store.persistedRevision && !after.store.dirty &&
              scene.activated == 2 && after.coordinator.running.size() == 2);
        for (std::size_t j = 0; j < 2; ++j) {
            const auto& old = before.coordinator.running[j];
            const auto& current = after.coordinator.running[j];
            CHECK(current.path == old.path && current.instance == old.instance && current.epoch == old.epoch &&
                  current.state == InstanceState::Active && scene.widgets[j]);
        }
        CHECK(readConfigurationFile(scene.path()).value().version.sha256 == disk.version.sha256);
    }
    scene.close();
}
/** @brief L02/U01：显示名称来自配置，改名保持 id，重排不重建，旧页完整撤销。 */
void renameAndReorder()
{
    Scene scene;
    scene.apply("[{id: a, plugin: note, config: {title: first}}, {id: b, plugin: note, config: {title: other}}]");
    const auto before = scene.session->snapshot().value().coordinator.running;
    const auto oldPage = scene.pages.at(before.front().instance);
    oldPage.navigate().value();
    const auto oldWidget = scene.widgets.front();
    scene.apply("[{id: b, plugin: note, config: {title: other}}, {id: a, plugin: note, config: {title: renamed}}]");
    const auto after = scene.session->snapshot().value().coordinator.running;
    CHECK(before.size() == 2 && after.size() == 2 && scene.activated == 3 && !oldWidget);
    CHECK(after[0].path == "a" && after[0].instance == before[0].instance && after[0].epoch > before[0].epoch);
    CHECK(after[1].path == "b" && after[1].instance == before[1].instance && after[1].epoch == before[1].epoch);
    CHECK(!oldPage.navigate().hasValue() && !oldPage.snapshot().value().widgetAlive);
    scene.pages.at(after[0].instance).navigate().value();
    CHECK(qobject_cast<QLabel*>(scene.widgets.back().data())->text() == "renamed");
    CHECK(scene.session->snapshot().value().coordinator.desired->source().structure().entries().at("a").config ==
          "{\"title\":\"renamed\"}");
    scene.close();
}

#ifdef Q_OS_WIN
/** @brief 真实 Windows 句柄禁止目标重命名，仅影响本项临时文件。 */
struct CommitBlock {
    HANDLE handle;
    /** @brief 明确禁止共享删除，触发实际原子提交失败。 */
    explicit CommitBlock(const QString& path)
        : handle(CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
        CHECK(handle != INVALID_HANDLE_VALUE);
    }
    /** @brief 故障注入结束后总会解除句柄。 */
    ~CommitBlock() { CloseHandle(handle); }
};
/** @brief L03：真实部分激活和真实写盘失败并存，期望不回滚，保存状态不伪报成功。 */
void partialFailure()
{
    Scene scene;
    scene.apply("[{id: a, plugin: note, config: {title: first}}]");
    CHECK(scene.save().outcome == SaveOutcome::Written);
    const auto original = readConfigurationFile(scene.path()).value();
    scene.apply("[{id: a, plugin: note, config: {title: second}}, {id: b, plugin: note, config: {title: fail}}]");
    auto state = scene.session->snapshot().value();
    CHECK(state.latestPlan->outcome == PlanOutcome::Failed && state.coordinator.desiredRevision == 2);
    CHECK(state.coordinator.running.size() == 2 && state.coordinator.running[0].state == InstanceState::Active &&
          state.coordinator.running[1].state == InstanceState::Failed);
    CHECK(state.coordinator.running[0].configuration.config == "{\"title\":\"second\"}" &&
          state.coordinator.desired->source().structure().entries().at("a").config == "{\"title\":\"second\"}");
    CHECK(std::any_of(state.latestPlan->entries.begin(), state.latestPlan->entries.end(), [](const EntryExecution& entry) {
        return entry.operation.path == "a" && entry.status == EntryStatus::Applied;
    }));
    {
        CommitBlock block(scene.path());
        const auto failed = scene.save();
        CHECK(failed.outcome == SaveOutcome::Failed && failed.error && failed.error->code == ErrorCode::ConfigIo);
        state = scene.session->snapshot().value();
        CHECK(state.store.dirty && state.store.persistedRevision == 1 && state.coordinator.desiredRevision == 2 &&
              state.latestPlan->outcome == PlanOutcome::Failed);
        CHECK(readConfigurationFile(scene.path()).value().version.sha256 == original.version.sha256);
    }
    CHECK(scene.save().outcome == SaveOutcome::Written);
    state = scene.session->snapshot().value();
    CHECK(state.store.persistedRevision == 2 && !state.store.dirty && state.latestPlan->outcome == PlanOutcome::Failed);
    CHECK(readConfigurationFile(scene.path()).value().text == state.coordinator.desired->source().normalizedYaml());
    scene.close();
}
/** @brief S10/Q01/U01：真实关闭请求等待工作线程，同时文件、管理、页面和 Qt 循环持续工作。 */
void pendingShutdown(QApplication& app)
{
    Scene scene;
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<bool> entered{false};
    std::optional<qt::QtWorker> worker;
    std::optional<qt::QtConnection> connection;
    QObject sender;
    int delivered = 0;
    scene.resources = [&](Context context) {
        worker.emplace(qt::QtWorker::start(context, scene.scheduler, [&, gate](qt::WorkerCancellation) {
            entered.store(true);
            gate.wait();
            return Result<void>::success();
        }).value());
        connection.emplace(qt::QtConnection::observePrefixValues<QString>(context, scene.scheduler, &sender,
            &QObject::objectNameChanged, [&](const QString&) { ++delivered; return completedTask(); }).value());
        return worker->ready();
    };
    scene.apply("[{id: a, plugin: note, config: {title: active}}, {id: z, plugin: blocked}]");
    until([&] { return entered.load(); });
    CHECK(scene.session->snapshot().value().latestPlan->outcome == PlanOutcome::Pending && scene.blockedActivations == 0);
    scene.shell.management()->refresh().value();
    scene.shell.showManagement().value();
    const auto diagnostic = scene.shell.diagnostics()->snapshot().value();
    CHECK(diagnostic.managementOpen && diagnostic.configuration && diagnostic.configuration->entries.size() == 2);
    const auto& configured = diagnostic.configuration->entries;
    const auto pending = std::find_if(configured.begin(), configured.end(), [](const ManagedConfigurationEntry& entry) {
        return entry.path == "z" && entry.hasActual && entry.state == InstanceState::Pending;
    });
    CHECK(pending != configured.end());
    const auto dependencies = scene.kernel->findInstance(pending->instance).value().dependencies().value();
    CHECK(!dependencies.empty() && dependencies.front().error &&
          dependencies.front().error->code == ErrorCode::DependencyMissing);
    sender.setObjectName("before close");
    until([&] { return delivered == 1; });
    std::optional<Task<void>> stopped;
    bool requested = false, drained = false, released = false;
    scene.shell.onCloseRequested([&] {
        requested = true;
        scene.watcher->close().thenOn(scene.scheduler, [&](const Result<void>& watched) {
            CHECK(watched.hasValue());
            stopped.emplace(scene.session->close());
            stopped->thenOn(scene.scheduler, [&](const Result<void>& saved) {
                CHECK(saved.hasValue());
                scene.io->close().thenOn(scene.scheduler, [&](const Result<void>& joined) {
                    CHECK(joined.hasValue());
                    scene.kernel->close().thenOn(scene.scheduler, [&](const Result<CleanupReport>& core) {
                        CHECK(core.hasValue() && core.value().quiescent);
                        scene.shell.close().thenOn(scene.scheduler, [&](const Result<void>& ui) {
                            CHECK(ui.hasValue() && !scene.window && !scene.editorPage);
                            CHECK(worker->snapshot().value().joined && worker->snapshot().value().capturesReleased &&
                                  worker->snapshot().value().hostDestroyed && connection->snapshot().value().closed);
                            CHECK(scene.io->snapshot().value().closed && !scene.session->snapshot().value().store.dirty);
                            CHECK(scene.scheduler->snapshot().value().callbackErrors == 0);
                            scene.scheduler->close().value();
                            drained = true;
                            QCoreApplication::exit(EXIT_SUCCESS);
                        });
                    });
                });
            });
        });
    }).value();
    QTimer observer, deadline;
    observer.setInterval(0);
    QObject::connect(&observer, &QTimer::timeout, [&] {
        if (!released && stopped && worker->snapshot().value().cancellationRequested) {
            CHECK(requested && !stopped->isReady() && scene.window && scene.scheduler->isOpen());
            CHECK(!scene.io->snapshot().value().closed && !worker->snapshot().value().joined);
            sender.setObjectName("after close");
            released = true;
            release.set_value();
        }
    });
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [&] {
        if (!released) { released = true; release.set_value(); }
        QCoreApplication::exit(EXIT_FAILURE);
    });
    QTimer::singleShot(0, scene.window, [&] { scene.window->close(); });
    observer.start();
    deadline.start(8000);
    CHECK(app.exec() == EXIT_SUCCESS && drained && requested && released && delivered == 1);
    CHECK(scene.kernel->snapshot().value().instances.empty());
    for (const auto& widget : scene.widgets)
        CHECK(!widget);
}
#endif
/** @brief 每场景独立进程，避免 QApplication 与全局 Ela 状态相互影响。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        CHECK(argc == 2);
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "offscreen")
            QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
#endif
        const std::string name = argv[1];
        if (name == "invalid") invalidCandidate();
        else if (name == "identity") renameAndReorder();
#ifdef Q_OS_WIN
        else if (name == "partial") partialFailure();
        else if (name == "shutdown") pendingShutdown(app);
#endif
        else return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
