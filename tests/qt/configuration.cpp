/** @file configuration.cpp
 * @brief 真实 Qt、Kernel 和文件事务验证 Ela 配置页，不用控件状态冒充目标/保存成功。
 */
#include <ElaTheme.h>
#include <QAbstractItemModel>
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_store.hpp>
#include <cordis/loader/qt_watcher.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 超时只诊断失败，完成条件来自真实任务/值状态。 */
void until(const std::function<bool()>& condition)
{
    if (condition())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0);
    deadline.start(5000);
    loop.exec();
    CHECK(condition());
}
/** @brief 任务终结后还需让其 owner 回执处理完成。 */
template <class T> void wait(const Task<T>& task)
{
    until([&] { return task.isReady(); });
    CHECK(task.tryResult()->hasValue());
}
/** @brief 只在真实驱动的同步预检窗口注入 owner 编辑重入，实际执行仍由 Kernel 完成。 */
struct ReentrantDriver final : IReconcileDriver {
    std::shared_ptr<IReconcileDriver> delegate;
    std::function<void()> onPreflight;
    /** @brief 先完成一次性重入，再执行真实能力检查。 */
    Result<void> preflight(const ValidatedCandidate& target) override
    {
        auto hook = std::exchange(onPreflight, {});
        if (hook)
            hook();
        return delegate->preflight(target);
    }
    /** @brief 实际规划不替换为测试结果。 */
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate& target) override
    {
        return delegate->plan(target);
    }
    /** @brief 实际动作不替换为测试结果。 */
    Task<StepReport> apply(EntryOperation operation, std::shared_ptr<const ValidatedCandidate> target,
                           std::uint64_t revision) override
    {
        return delegate->apply(std::move(operation), std::move(target), revision);
    }
    /** @brief 快照仍来自真实资源。 */
    Result<std::vector<RunningEntry>> snapshot() const override { return delegate->snapshot(); }
    /** @brief 停止仍等待真实驱动。 */
    Task<CleanupReport> close() override { return delegate->close(); }
};
/** @brief 每个场景绑定临时真实文件，窗口生命周期独立于会话和运行资源。 */
struct Scene {
    QTemporaryDir directory;
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    std::shared_ptr<ConfigurationSession> session;
    std::shared_ptr<ReentrantDriver> driver = std::make_shared<ReentrantDriver>();
    std::shared_ptr<QtConfigurationIo> io;
    std::shared_ptr<QtConfigurationWatcher> watcher;
    QPointer<QWidget> page;
    int activations = 0;
    /** @brief 创建页面不自动接受原文。 */
    explicit Scene(bool watchDisk = false)
    {
        CHECK(directory.isValid());
        auto runtime = kernel
                           ->registerPlugin("demo",
                                            [&](Context) {
                                                ++activations;
                                                return completedTask();
                                            })
                           .value();
        driver->delegate =
            std::make_shared<KernelReconcileDriver>(scheduler, kernel, std::vector<KernelPlugin>{{"demo", runtime}});
        std::shared_ptr<IConfigurationWriter> writer = std::make_shared<QtConfigurationWriter>(path());
        if (watchDisk) {
            io = std::make_shared<QtConfigurationIo>(scheduler, path());
            auto initialDisk = io->read();
            wait(initialDisk);
            watcher = std::make_shared<QtConfigurationWatcher>(scheduler, io, initialDisk.tryResult()->value(), 0);
            writer = io;
        }
        session = std::make_shared<ConfigurationSession>(
            scheduler, driver,
            std::vector<PluginDefinition>{{"demo", {}, [](const std::string&) { return Result<void>::success(); }}},
            writer, readConfigurationFile(path()).value());
        auto widget = createConfigurationPage(
            session, "formatVersion: 1\nplugins: [{id: r, plugin: demo, children: [{id: c, plugin: demo}]}]", watcher);
        page = widget.release();
        page->resize(1200, 800);
        page->show();
    }
    /** @brief 写入目的地固定，冲突不能切换为另一个文件。 */
    QString path() const { return directory.filePath("cordis.yaml"); }
    /** @brief 稳定 objectName 是宿主测试接口，不进入插件 SDK。 */
    template <class T> T* child(const char* name)
    {
        auto value = page->findChild<T*>(name);
        CHECK(value);
        return value;
    }
    /** @brief 使用实际点击入口触发预览/接受，等待真实运行结果。 */
    void apply()
    {
        child<QPushButton>("CordisConfigPreview")->click();
        CHECK(child<QPushButton>("CordisConfigApply")->isEnabled());
        child<QPushButton>("CordisConfigApply")->click();
        until([&] {
            auto value = session->snapshot().value();
            return value.latestPlan.has_value();
        });
        CHECK(session->snapshot().value().latestPlan->outcome == PlanOutcome::Applied);
    }
    /** @brief 先关闭会话，再停止其他根和页面，最后关闭 scheduler。 */
    void close()
    {
        if (watcher)
            wait(watcher->close());
        wait(session->close());
        if (io)
            wait(io->close());
        auto core = kernel->close();
        wait(core);
        CHECK(core.tryResult()->value().quiescent);
        if (page) {
            page->deleteLater();
            until([&] { return !page; });
        }
        wait(shell.close());
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};
/** @brief 预览不挂载，应用才启动；配置开关改变目标并自动级联，不直接操作实例。 */
void workflow()
{
    Scene scene;
    scene.child<QPushButton>("CordisConfigPreview")->click();
    CHECK(scene.activations == 0 && !QFile::exists(scene.path()));
    scene.apply();
    CHECK(scene.activations == 2 && scene.session->snapshot().value().store.dirty);
    auto table = scene.child<QTableView>("CordisConfigEntries");
    until([&] { return table->model()->rowCount() == 2; });
    table->setCurrentIndex(table->model()->index(0, 0));
    table->selectRow(0);
    auto toggle = scene.child<QWidget>("CordisConfigEnabled");
    CHECK(toggle->isEnabled());
    QTest::mouseClick(toggle, Qt::LeftButton);
    until([&] { return scene.session->snapshot().value().coordinator.desiredRevision == 2; });
    until([&] { return scene.session->snapshot().value().coordinator.running.empty(); });
    CHECK(scene.session->snapshot().value().coordinator.desired->source().structure().entries().at("r").disabled);
    CHECK(!QFile::exists(scene.path()));
    scene.child<QPushButton>("CordisConfigSave")->click();
    until([&] { return !scene.session->snapshot().value().store.dirty; });
    CHECK(QFile::exists(scene.path()) && scene.session->snapshot().value().store.persistedRevision == 2);
    scene.close();
}
/** @brief 外部原文保留供审阅，只有显式采用基线并保存才覆盖。 */
void conflict()
{
    Scene scene;
    scene.apply();
    QFile file(scene.path());
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("external: true\n");
    file.close();
    scene.child<QPushButton>("CordisConfigSave")->click();
    until([&] { return static_cast<bool>(scene.session->snapshot().value().store.external); });
    CHECK(scene.session->snapshot().value().store.dirty);
    until([&] { return scene.child<QPushButton>("CordisConfigRebase")->isEnabled(); });
    CHECK(scene.child<QPlainTextEdit>("CordisConfigDetails")->toPlainText().contains("external: true"));
    scene.child<QPushButton>("CordisConfigRebase")->click();
    CHECK(!scene.session->snapshot().value().store.external && scene.activations == 2);
    until([&] { return scene.child<QPushButton>("CordisConfigSave")->isEnabled(); });
    scene.child<QPushButton>("CordisConfigSave")->click();
    until([&] { return !scene.session->snapshot().value().store.dirty; });
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll().toStdString() ==
          scene.session->snapshot().value().coordinator.desired->source().normalizedYaml());
    file.close();
    scene.close();
}
/** @brief 编辑失效候选，外部目标变化使旧候选 CAS 失效，刷新不覆盖编辑内容。 */
void stale()
{
    Scene scene;
    scene.apply();
    auto editor = scene.child<QPlainTextEdit>("CordisConfigEditor");
    editor->setPlainText("invalid");
    scene.child<QPushButton>("CordisConfigPreview")->click();
    CHECK(!scene.child<QPushButton>("CordisConfigApply")->isEnabled() &&
          scene.session->snapshot().value().coordinator.desiredRevision == 1);
    scene.child<QPushButton>("CordisConfigReset")->click();
    scene.child<QPushButton>("CordisConfigPreview")->click();
    scene.session->setEnabled("r", false, 1).value();
    until([&] { return !scene.child<QPushButton>("CordisConfigApply")->isEnabled(); });
    CHECK(editor->toPlainText().toStdString() !=
          scene.session->snapshot().value().coordinator.desired->source().normalizedYaml());
    scene.child<QPushButton>("CordisConfigApply")->click();
    CHECK(scene.session->snapshot().value().coordinator.desiredRevision == 2);
    scene.close();
}
/** @brief 模型插入信号重入关闭及泵送 DeferredDelete，当前 UI 栈不得提前销毁。 */
void reentry()
{
    Scene scene;
    bool entered = false, survived = false;
    std::optional<Task<void>> closed;
    auto model = scene.child<QTableView>("CordisConfigEntries")->model();
    QObject::connect(model, &QAbstractItemModel::rowsInserted, model, [&] {
        if (entered)
            return;
        entered = true;
        closed.emplace(scene.session->close());
        scene.page->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        survived = static_cast<bool>(scene.page);
    });
    scene.child<QPushButton>("CordisConfigPreview")->click();
    scene.child<QPushButton>("CordisConfigApply")->click();
    until([&] { return !scene.page; });
    CHECK(entered && survived);
    wait(*closed);
    CHECK(QFile::exists(scene.path()));
    scene.close();
}
/** @brief 明暗与缩放下保留可编辑区及真实表格，不依赖固定高度卡片。 */
void layout()
{
    Scene scene;
    scene.apply();
    until([&] {
        return scene.child<QTableView>("CordisConfigEntries")->model()->index(0, 2).data().toString() ==
               QString::fromUtf8("运行中");
    });
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        for (auto size : {QSize(1200, 800), QSize(900, 600)}) {
            scene.page->resize(size);
            QCoreApplication::processEvents();
            auto editor = scene.child<QPlainTextEdit>("CordisConfigEditor"),
                 details = scene.child<QPlainTextEdit>("CordisConfigDetails");
            CHECK(editor->height() >= 100 && details->height() >= 80 && editor->width() >= 150);
            const auto pixels = scene.page->grab().toImage();
            const auto ratio = scene.page->devicePixelRatioF();
            for (auto view : {editor, details}) {
                const auto point = view->viewport()->mapTo(scene.page, QPoint(20, view->viewport()->height() - 20));
                const auto background = pixels.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
                CHECK(theme == ElaThemeType::Dark ? background.value() < 120 : background.value() > 160);
            }
            for (auto button : scene.page->findChildren<QPushButton*>())
                CHECK(scene.page->rect().contains(QRect(button->mapTo(scene.page, QPoint(0, 0)), button->size())));
        }
        scene.page->resize(1200, 800);
        QCoreApplication::processEvents();
        const auto suffix = theme == ElaThemeType::Light ? "light" : "dark";
        CHECK(scene.page->grab().save(QCoreApplication::applicationDirPath() + "/configuration-panel-" + suffix +
                                      ".png"));
    }
    scene.close();
}
/** @brief 保存冲突保留窗口后，宿主能恢复关闭按钮并再次收到关闭请求。 */
void closeRetry()
{
    auto scheduler = std::make_shared<qt::QtScheduler>();
    ElaShell shell(scheduler);
    int calls = 0;
    shell.onCloseRequested([&] { ++calls; }).value();
    shell.show().value();
    QWidget* window = nullptr;
    for (auto value : QApplication::topLevelWidgets())
        if (value->objectName() == "CordisElaShell")
            window = value;
    if (!window)
        for (auto value : QApplication::topLevelWidgets())
            if (value->isVisible())
                window = value;
    CHECK(window);
    window->close();
    CHECK(calls == 1);
    window->close();
    CHECK(calls == 1);
    shell.cancelCloseRequest().value();
    window->close();
    CHECK(calls == 2);
    auto closed = shell.close();
    wait(closed);
    CHECK(!shell.cancelCloseRequest().hasValue());
    scheduler->close().value();
}
/** @brief 外部原文只提示，显式载入仍不应用/更改保存基线，之后保存照常冲突。 */
void diskChanges()
{
    Scene scene(true);
    scene.apply();
    auto editor = scene.child<QPlainTextEdit>("CordisConfigEditor");
    editor->setPlainText("unapplied editor");
    QFile file(scene.path());
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("external invalid yaml");
    file.close();
    auto load = scene.child<QPushButton>("CordisConfigDiskLoad");
    until([&] { return load->isEnabled(); });
    CHECK(editor->toPlainText() == "unapplied editor" &&
          scene.session->snapshot().value().coordinator.desiredRevision == 1);
    load->click();
    CHECK(editor->toPlainText() == "external invalid yaml");
    CHECK(scene.session->snapshot().value().coordinator.desiredRevision == 1 &&
          !scene.child<QPushButton>("CordisConfigApply")->isEnabled());
    scene.child<QPushButton>("CordisConfigSave")->click();
    until([&] { return scene.session->snapshot().value().store.external != nullptr; });
    CHECK(scene.session->snapshot().value().store.external->text == "external invalid yaml");
    scene.session->rebaseExternal(1).value();
    auto saved = scene.session->save(1).value();
    wait(saved);
    CHECK(!scene.session->snapshot().value().store.dirty && editor->toPlainText() == "external invalid yaml");
    CHECK(scene.page->grab().save(QCoreApplication::applicationDirPath() + "/configuration-disk-change.png"));
    scene.close();
}
/** @brief 预览/接受/启停重入编辑均保留新文字，不错误启用旧候选或覆盖未应用内容。 */
void editorReentry()
{
    Scene scene;
    auto editor = scene.child<QPlainTextEdit>("CordisConfigEditor");
    const auto original = editor->toPlainText();
    scene.driver->onPreflight = [&] { editor->setPlainText("edited during preview"); };
    scene.child<QPushButton>("CordisConfigPreview")->click();
    CHECK(!scene.child<QPushButton>("CordisConfigApply")->isEnabled());
    CHECK(editor->toPlainText() == "edited during preview");
    editor->setPlainText(original);
    scene.child<QPushButton>("CordisConfigPreview")->click();
    scene.driver->onPreflight = [&] { editor->setPlainText("edited during apply"); };
    scene.child<QPushButton>("CordisConfigApply")->click();
    until([&] { return scene.session->snapshot().value().latestPlan.has_value(); });
    auto table = scene.child<QTableView>("CordisConfigEntries");
    until([&] { return table->model()->rowCount() == 2; });
    table->setCurrentIndex(table->model()->index(0, 0));
    table->selectRow(0);
    auto enabled = scene.child<QWidget>("CordisConfigEnabled");
    CHECK(!enabled->isEnabled());
    CHECK(editor->toPlainText() == "edited during apply");
    scene.child<QPushButton>("CordisConfigReset")->click();
    CHECK(enabled->isEnabled());
    scene.driver->onPreflight = [&] { editor->setPlainText("edited during toggle"); };
    QTest::mouseClick(enabled, Qt::LeftButton);
    until([&] { return scene.session->snapshot().value().coordinator.desiredRevision == 2; });
    CHECK(editor->toPlainText() == "edited during toggle" && !enabled->isEnabled());
    scene.close();
}
/** @brief 每项使用独立 QApplication，offscreen 下显式加载中文字体。 */
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
        if (name == "editor_reentry")
            editorReentry();
        else if (name == "disk_changes")
            diskChanges();
        else if (name == "workflow")
            workflow();
        else if (name == "conflict")
            conflict();
        else if (name == "stale")
            stale();
        else if (name == "reentry")
            reentry();
        else if (name == "layout")
            layout();
        else if (name == "close_retry")
            closeRetry();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
