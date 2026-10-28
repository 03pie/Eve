/** @file configuration_management.cpp
 * @brief 真实 Session/Kernel 验证配置管理路由、Home 身份、CAS 和关闭屏障。
 */
#include <ElaDialog.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaWindow.h>
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_store.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
using namespace cordis::loader;
using namespace cordis::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 完成来自可观察的真实状态，超时只报告失败。 */
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
/** @brief 任务和 owner 回执均成功才继续。 */
template <class T> void wait(const Task<T>& task)
{
    until([&] { return task.isReady(); });
    CHECK(task.tryResult()->hasValue());
}
/** @brief 使用真实适配器，可注入宿主协议故障而不伪造 Core 状态。 */
struct Proxy final : IConfigurationManagement {
    std::shared_ptr<IConfigurationManagement> delegate;
    bool throws = false, malformed = false;
    /** @brief 只读来源异常由管理边界转为显式错误。 */
    Result<ConfigurationManagementSnapshot> snapshot() const override
    {
        if (throws)
            throw std::runtime_error("snapshot fault");
        return delegate->snapshot();
    }
    /** @brief 故意不推进修订的回执必须被拒绝。 */
    Result<Task<ConfigurationActionReport>> setEnabled(const std::string& path, bool value,
                                                       std::uint64_t revision) override
    {
        if (malformed) {
            Promise<ConfigurationActionReport> done;
            done.resolve({revision, ConfigurationOutcome::Applied, {}});
            return Result<Task<ConfigurationActionReport>>::success(done.task());
        }
        return delegate->setEnabled(path, value, revision);
    }
    /** @brief 正常重试仍通过完整目标收敛。 */
    Result<Task<ConfigurationActionReport>> reconcile(const std::string& path, std::uint64_t revision) override
    {
        return delegate->reconcile(path, revision);
    }
};
/** @brief 一个窗口、一个配置会话与独占配置 Runtime；无磁盘共享。 */
struct Scene {
    QTemporaryDir directory;
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    std::shared_ptr<ConfigurationSession> session;
    std::shared_ptr<IConfigurationManagement> route;
    RuntimeId runtime;
    std::optional<Runtime> pluginDefinition;
    QPointer<ElaWindow> window;
    bool fail = false;
    int activations = 0;
    /** @brief 贡献页用于检测停用撤销和 Home 去重。 */
    explicit Scene(bool bind = true)
    {
        CHECK(directory.isValid());
        auto definition =
            kernel
                ->registerPlugin(
                    "demo",
                    [&](Context context) {
                        ++activations;
                        if (fail)
                            return failedTask(makeError(ErrorCode::ActivationFailed, "test failure"));
                        return shell.pages()
                            ->add(context, {"page", "Demo page", "Example", [] { return std::make_unique<QWidget>(); }})
                            .value()
                            .ready();
                    })
                .value();
        runtime = definition.id();
        pluginDefinition.emplace(definition);
        auto driver =
            std::make_shared<KernelReconcileDriver>(scheduler, kernel, std::vector<KernelPlugin>{{"demo", definition}});
        auto path = directory.filePath("cordis.yaml");
        session = std::make_shared<ConfigurationSession>(
            scheduler, driver,
            std::vector<PluginDefinition>{{"demo", {}, [](const std::string&) { return Result<void>::success(); }}},
            std::make_shared<QtConfigurationWriter>(path), readConfigurationFile(path).value());
        route = createConfigurationManagement(session, scheduler);
        shell.management()->attach(kernel).value();
        if (bind)
            shell.management()->attachConfiguration(route, {runtime}).value();
        shell.show().value();
        for (auto widget : QApplication::topLevelWidgets())
            if (auto candidate = qobject_cast<ElaWindow*>(widget))
                window = candidate;
        CHECK(window);
    }
    /** @brief 根据稳定宿主 objectName 查找真实控件。 */
    template <class T> T* find(const char* name) const
    {
        auto result = window->findChild<T*>(name);
        CHECK(result);
        return result;
    }
    /** @brief 接受新目标并等到协调结果，失败亦是稳定可检查的结果。 */
    void apply(const std::string& yaml = "formatVersion: 1\nplugins: [{id: r, plugin: demo}]")
    {
        auto draft = session->preview(yaml).value();
        auto accepted = session->apply(draft).value();
        wait(accepted.settled);
        shell.management()->refresh().value();
    }
    /** @brief 只查找路径身份，不借用临时实例号。 */
    QWidget* card(const QString& path = "r") const
    {
        auto home = find<QWidget>("CordisWelcomePage");
        for (auto item : home->findChildren<QWidget*>("CordisPluginCard"))
            if (item->property("CordisPluginId").toString() == "configuration:" + path)
                return item;
        return nullptr;
    }
    /** @brief Home 周期刷新后才允许进行真实开关操作。 */
    ElaToggleSwitch* toggle(const QString& path = "r")
    {
        until([&] { return card(path) != nullptr; });
        auto result = card(path)->findChild<ElaToggleSwitch*>("CordisPluginPower");
        CHECK(result);
        return result;
    }
    /** @brief 控制器历史必须收齐回执，不能只等待 Session 修订。 */
    ManagementOperation operation(std::uint64_t request)
    {
        until([&] {
            for (const auto& row : shell.diagnostics()->snapshot().value().operations)
                if (row.request == request)
                    return !row.pending;
            return false;
        });
        for (const auto& row : shell.diagnostics()->snapshot().value().operations)
            if (row.request == request)
                return row;
        throw std::runtime_error("operation missing");
    }
    /** @brief 宿主按 Session、Kernel、UI、scheduler 顺序等待静止。 */
    void close()
    {
        if (session)
            wait(session->close());
        auto core = kernel->close();
        wait(core);
        CHECK(core.tryResult()->value().quiescent);
        wait(shell.close());
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};
/** @brief 停用/启用改变期望并生成新实例，卡片按路径保留且贡献不会重复。 */
void home()
{
    Scene scene;
    scene.apply();
    auto power = scene.toggle();
    QPointer<QWidget> card = scene.card();
    const auto original = scene.route->snapshot().value().entries.front().instance;
    CHECK(original.valid());
    until([&] { return power->isEnabled() && power->getIsToggled(); });
    QTest::mouseClick(power, Qt::LeftButton);
    until([&] {
        auto state = scene.session->snapshot().value();
        return state.coordinator.desiredRevision == 2 && state.coordinator.running.empty();
    });
    until([&] { return power->isEnabled() && !power->getIsToggled(); });
    CHECK(scene.card() == card && scene.shell.pages()->snapshot().value().empty());
    CHECK(scene.shell.diagnostics()->snapshot().value().instances.empty());
    QTest::mouseClick(power, Qt::LeftButton);
    until([&] {
        auto state = scene.route->snapshot().value();
        return state.revision == 3 && !state.entries.empty() && state.entries.front().state == InstanceState::Active;
    });
    until([&] { return power->isEnabled() && power->getIsToggled(); });
    CHECK(scene.card() == card);
    CHECK(scene.route->snapshot().value().entries.front().instance != original);
    CHECK(scene.find<QWidget>("CordisWelcomePage")->findChildren<QWidget*>("CordisPluginCard").size() == 1);
    CHECK(scene.session->snapshot().value().store.dirty);
    CHECK(!scene.shell.management()->stop(scene.route->snapshot().value().entries.front().instance).hasValue());
    scene.close();
}
/** @brief 父禁用时子配置仍在 Home 中，单独启停不制造无效实例。 */
void disabled()
{
    Scene scene;
    scene.apply(
        "formatVersion: 1\nplugins: [{id: r, plugin: demo, disabled: true, children: [{id: c, plugin: demo}]}]");
    auto child = scene.toggle("r/c");
    CHECK(child->getIsToggled());
    CHECK(scene.activations == 0);
    CHECK(scene.card("r/c")->findChild<QLabel*>("CordisPluginStatus")->text() == QString::fromUtf8("父项停用"));
    auto op = scene.shell.management()->setConfigurationEnabled("r/c", false, 1).value();
    CHECK(!scene.operation(op).error);
    CHECK(scene.activations == 0 && scene.route->snapshot().value().entries.size() == 2);
    scene.close();
}
/** @brief 页面按钮控制配置；失败重试协调完整目标；普通实例保留手动流程。 */
void management()
{
    Scene scene;
    scene.fail = true;
    scene.apply();
    auto table = scene.find<QTableView>("CordisManagementConfigurationTable");
    table->setCurrentIndex(table->model()->index(0, 0));
    table->selectRow(0);
    scene.shell.management()->refresh().value();
    auto retry = scene.find<QPushButton>("CordisManagementConfigurationReconcile");
    CHECK(retry->isEnabled());
    scene.fail = false;
    retry->click();
    until([&] { return scene.activations == 2; });
    until([&] {
        auto rows = scene.shell.diagnostics()->snapshot().value().operations;
        return !rows.empty() && !rows.back().pending;
    });
    auto record = scene.shell.diagnostics()->snapshot().value().operations.back();
    CHECK(record.configurationResult && record.configurationResult->outcome == ConfigurationOutcome::Applied &&
          !record.error);
    scene.find<QPushButton>("CordisManagementConfigurationDisable")->click();
    until([&] { return scene.session->snapshot().value().coordinator.running.empty(); });
    auto manual = scene.kernel->registerPlugin("manual", [](Context) { return completedTask(); }).value();
    auto instance = scene.kernel->mount(manual).value();
    wait(instance.ready());
    auto stopped = scene.shell.management()->stop(instance.snapshot().value().instance).value();
    CHECK(!scene.operation(stopped).error);
    auto loaded = scene.shell.management()->load(instance.snapshot().value().instance).value();
    CHECK(!scene.operation(loaded).error);
    scene.close();
}
/** @brief 接受后、派发前被外部更新，旧管理请求只能收到冲突。 */
void stale()
{
    Scene scene;
    scene.apply();
    auto request = scene.shell.management()->setConfigurationEnabled("r", false, 1).value();
    auto newer = scene.session->setEnabled("r", true, 1).value();
    wait(newer.settled);
    auto record = scene.operation(request);
    CHECK(record.error && record.error->code == ErrorCode::ConfigConflict);
    CHECK(scene.route->snapshot().value().revision == 2 && scene.route->snapshot().value().entries.front().enabled);
    CHECK(!scene.shell.management()->setConfigurationEnabled("r", false, 1).hasValue());
    scene.close();
}
/** @brief 开关发出时固定修订，刷新控件属性不能把旧点击升级成新目标操作。 */
void homeStale()
{
    Scene scene;
    scene.apply();
    auto power = scene.toggle();
    until([&] { return power->isEnabled(); });
    power->toggled(false);
    auto newer = scene.session->setEnabled("r", true, 1).value();
    scene.shell.management()->refresh().value();
    wait(newer.settled);
    until([&] { return scene.find<QLabel>("CordisHomeError")->isVisible(); });
    CHECK(scene.route->snapshot().value().revision == 2 && scene.route->snapshot().value().entries.front().enabled);
    scene.close();
}
/** @brief 来源抛错或失效时保留缓存条目并灰显，禁止自动降级为手动停止。 */
void unavailable()
{
    Scene scene(false);
    auto proxy = std::make_shared<Proxy>();
    proxy->delegate = scene.route;
    scene.shell.management()->attachConfiguration(proxy, {scene.runtime}).value();
    scene.apply();
    auto power = scene.toggle();
    auto id = scene.route->snapshot().value().entries.front().instance;
    proxy->throws = true;
    scene.shell.management()->refresh().value();
    auto snapshot = scene.shell.diagnostics()->snapshot().value();
    CHECK(snapshot.configurationError && snapshot.configuration->entries.size() == 1);
    CHECK(!snapshot.configuration->entries.front().canSetEnabled && snapshot.instances.empty());
    CHECK(!scene.shell.management()->stop(id).hasValue());
    proxy.reset();
    until([&] { return !power->isEnabled(); });
    CHECK(scene.card());
    CHECK(!scene.shell.management()->setConfigurationEnabled("r", false, 1).hasValue());
    CHECK(!scene.shell.management()->attachConfiguration(scene.route, {scene.runtime}).hasValue());
    scene.close();
}
/** @brief 绑定只允许一次且必须先排空受保护 Runtime 的手动请求。 */
void binding()
{
    Scene scene(false);
    CHECK(!scene.shell.management()->attachConfiguration(scene.route, {RuntimeId(999999)}).hasValue());
    CHECK(!scene.shell.management()->attachConfiguration(scene.route, {scene.runtime, scene.runtime}).hasValue());
    bool wrongThread = false;
    std::thread worker([&] {
        auto result = scene.shell.management()->attachConfiguration(scene.route, {scene.runtime});
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrongThread);
    bool wrongScheduler = false;
    try {
        createConfigurationManagement(scene.session, std::make_shared<qt::QtScheduler>());
    } catch (const std::invalid_argument&) {
        wrongScheduler = true;
    }
    CHECK(wrongScheduler);
    auto instance = scene.kernel->mount(*scene.pluginDefinition).value();
    wait(instance.ready());
    auto pending = scene.shell.management()->stop(instance.snapshot().value().instance).value();
    auto rejected = scene.shell.management()->attachConfiguration(scene.route, {scene.runtime});
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict);
    CHECK(!scene.operation(pending).error);
    scene.shell.management()->attachConfiguration(scene.route, {scene.runtime}).value();
    scene.shell.management()->attachConfiguration(scene.route, {scene.runtime}).value();
    CHECK(!scene.shell.management()->attachConfiguration(std::make_shared<Proxy>(), {scene.runtime}).hasValue());
    scene.close();
}
/** @brief 适配器不延长会话生命周期，关闭及释放后缓存仍不能获得操作权限。 */
void sessionExpired()
{
    Scene scene;
    scene.apply();
    auto power = scene.toggle();
    wait(scene.session->close());
    scene.session.reset();
    scene.shell.management()->refresh().value();
    auto snapshot = scene.shell.diagnostics()->snapshot().value();
    CHECK(snapshot.configurationError && snapshot.configuration->entries.size() == 1 && snapshot.instances.empty());
    CHECK(!scene.route->snapshot().hasValue());
    until([&] { return !power->isEnabled(); });
    scene.close();
}
/** @brief 外壳关门后取消未派发请求，不推进配置修订。 */
void queuedClose()
{
    Scene scene;
    scene.apply();
    auto request = scene.shell.management()->setConfigurationEnabled("r", false, 1).value();
    auto closed = scene.shell.close();
    wait(closed);
    auto record = scene.operation(request);
    CHECK(record.error && record.error->code == ErrorCode::ContextClosing);
    CHECK(scene.session->snapshot().value().coordinator.desiredRevision == 1);
    scene.close();
}
/** @brief 配置模型信号同步关闭并泵送删除，不可销毁当前渲染中的控件。 */
void reentry()
{
    Scene scene;
    bool entered = false, survived = false;
    std::optional<Task<void>> closed;
    auto model = scene.find<QTableView>("CordisManagementConfigurationTable")->model();
    QObject::connect(model, &QAbstractItemModel::rowsInserted, model, [&] {
        if (entered)
            return;
        entered = true;
        closed.emplace(scene.shell.close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        survived = static_cast<bool>(scene.window);
    });
    auto draft = scene.session->preview("formatVersion: 1\nplugins: [{id: r, plugin: demo}]").value();
    auto accepted = scene.session->apply(draft).value();
    wait(accepted.settled);
    // 定时刷新可能已在等待期间触发关闭；显式刷新也允许返回 ContextClosing。
    auto refreshed = scene.shell.management()->refresh();
    CHECK(refreshed.hasValue() || refreshed.error().code == ErrorCode::ContextClosing);
    CHECK(closed.has_value());
    wait(*closed);
    CHECK(entered && survived && !scene.window);
    scene.close();
}
/** @brief 不合规宿主适配器不能把未推进修订的回执显示为成功。 */
void malformed()
{
    Scene scene(false);
    auto proxy = std::make_shared<Proxy>();
    proxy->delegate = scene.route;
    proxy->malformed = true;
    scene.shell.management()->attachConfiguration(proxy, {scene.runtime}).value();
    scene.apply();
    auto empty = scene.shell.management()->setConfigurationEnabled("", false, 1);
    CHECK(!empty.hasValue() && empty.error().code == ErrorCode::InvalidArgument);
    auto request = scene.shell.management()->setConfigurationEnabled("r", false, 1).value();
    auto record = scene.operation(request);
    CHECK(record.error && record.error->code == ErrorCode::CallbackFailed);
    CHECK(scene.route->snapshot().value().revision == 1);
    scene.close();
}
/** @brief 明暗主题截图覆盖配置表与路径卡片，供检查真实合成布局。 */
void layout()
{
    Scene scene;
    scene.apply();
    scene.toggle();
    until(
        [&] { return scene.card()->findChild<QLabel*>("CordisPluginStatus")->text() == QString::fromUtf8("运行中"); });
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        scene.window->resize(1200, 800);
        QCoreApplication::processEvents();
        const auto suffix = theme == ElaThemeType::Light ? "light" : "dark";
        CHECK(scene.window->grab().save(QCoreApplication::applicationDirPath() + "/configuration-home-" + suffix +
                                        ".png"));
        scene.shell.showManagement().value();
        auto table = scene.find<QTableView>("CordisManagementConfigurationTable");
        auto tabs = scene.find<QWidget>("CordisManagementConfiguration")->parentWidget();
        while (tabs && !qobject_cast<QTabWidget*>(tabs))
            tabs = tabs->parentWidget();
        CHECK(tabs);
        qobject_cast<QTabWidget*>(tabs)->setCurrentWidget(scene.find<QWidget>("CordisManagementConfiguration"));
        QCoreApplication::processEvents();
        CHECK(table->isVisible() && table->height() > 100 && table->columnWidth(0) >= 180);
        CHECK(scene.window->grab().save(QCoreApplication::applicationDirPath() + "/configuration-management-" + suffix +
                                        ".png"));
        scene.find<QAction>("CordisOpenHome")->trigger();
    }
    scene.close();
}
/** @brief 管理与配置通过卡片进入同一宿主窗口，不增加中央页签，关闭不停止实例。 */
void cardWindows()
{
    Scene scene;
    scene.apply();
    scene.toggle();
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    const auto navigation = scene.window->getCurrentNavigationPageKey();
    auto before = scene.route->snapshot().value().entries.front();
    scene.card()->findChild<QPushButton*>("CordisPluginManage")->click();
    QPointer<ElaDialog> dialog = scene.find<ElaDialog>("CordisManagement");
    CHECK(dialog->isVisible() && dialog->parentWidget() == scene.window);
    CHECK(tabs->count() == 1 && scene.window->getCurrentNavigationPageKey() == navigation);
    CHECK(dialog->windowTitle().contains("r") && !scene.find<QTableView>("CordisInstanceTable")->isVisible());
    dialog->close();
    CHECK(dialog->isHidden() && scene.route->snapshot().value().entries.front().instance == before.instance);
    scene.card()->findChild<QPushButton*>("CordisPluginConfigure")->click();
    CHECK(dialog->isVisible());
    CHECK(scene.find<QTabWidget>("CordisManagementTabs")->currentWidget() ==
          scene.find<QWidget>("CordisManagementConfiguration"));
    CHECK(scene.find<QPlainTextEdit>("CordisPluginConfigurationEditor")->toPlainText() == "{}");
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        QTest::qWait(30);
        const auto suffix = theme == ElaThemeType::Light ? "light" : "dark";
        auto editor = scene.find<QPlainTextEdit>("CordisPluginConfigurationEditor");
        const auto point = editor->viewport()->mapTo(
            dialog, QPoint(editor->viewport()->width() - 20, editor->viewport()->height() - 20));
        const auto background = dialog->grab().toImage().pixelColor(point);
        CHECK(theme == ElaThemeType::Dark ? background.lightness() < 100 : background.lightness() > 180);
        CHECK(dialog->grab().save(QCoreApplication::applicationDirPath() + "/card-management-" + suffix + ".png"));
    }
    scene.apply("formatVersion: 1\nplugins: []\n");
    CHECK(!scene.find<QPushButton>("CordisPluginConfigurationApply")->isEnabled());
    scene.find<QAction>("CordisOpenNotifications")->trigger();
    CHECK(scene.find<QTabWidget>("CordisManagementTabs")->currentWidget() ==
          scene.find<QPlainTextEdit>("CordisManagementNotices"));
    scene.close();
    CHECK(!dialog);
}
/** @brief 平台菜单从已注册目录创建配置实例，无中央导航、重复添加或隐式即时保存。 */
void addInstance()
{
    Scene scene;
    CHECK(scene.session->registeredPlugins().value() == std::vector<std::string>{"demo"});
    until([&] { return scene.find<QAction>("CordisAddPlugin")->isEnabled(); });
    scene.find<QAction>("CordisAddPlugin")->trigger();
    auto dialog = scene.find<ElaDialog>("CordisAddPluginDialog");
    CHECK(dialog->isVisible());
    auto types = scene.find<QComboBox>("CordisAddPluginType");
    CHECK(types->count() == 1 && types->currentText() == "demo");
    scene.find<QLineEdit>("CordisAddPluginId")->setText("added");
    scene.find<QPlainTextEdit>("CordisAddPluginConfiguration")->setPlainText("{\"title\":\"added\"}");
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        QTest::qWait(30);
        const auto suffix = theme == ElaThemeType::Light ? "light" : "dark";
        CHECK(dialog->grab().save(QCoreApplication::applicationDirPath() + "/add-plugin-" + suffix + ".png"));
    }
    scene.find<QPushButton>("CordisAddPluginSubmit")->click();
    until([&] { return dialog->isHidden(); });
    auto state = scene.route->snapshot().value();
    CHECK(state.revision == 1 && state.dirty && state.entries.size() == 1 && state.entries[0].path == "added");
    CHECK(!QFile::exists(scene.directory.filePath("cordis.yaml")));
    until([&] { return scene.card("added") != nullptr; });
    CHECK(scene.find<QTabBar>("CordisPageTabs")->count() == 1);
    auto duplicate = scene.route->addInstance("added", "demo", "{}", state.revision);
    CHECK(!duplicate.hasValue() && scene.route->snapshot().value().revision == state.revision);
    scene.close();
    Scene baseline(false);
    wait(baseline.session->close());
    const auto path = baseline.directory.filePath("cordis.yaml");
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("formatVersion: 1\nplugins: [{id: existing, plugin: demo}]\n") > 0);
    file.close();
    auto driver = std::make_shared<KernelReconcileDriver>(
        baseline.scheduler, baseline.kernel, std::vector<KernelPlugin>{{"demo", *baseline.pluginDefinition}});
    baseline.session = std::make_shared<ConfigurationSession>(
        baseline.scheduler, driver,
        std::vector<PluginDefinition>{{"demo", {}, [](const std::string&) { return Result<void>::success(); }}},
        std::make_shared<QtConfigurationWriter>(path), readConfigurationFile(path).value());
    baseline.route = createConfigurationManagement(baseline.session, baseline.scheduler);
    CHECK(!baseline.route->addInstance("new", "demo", "{}", 0).hasValue());
    CHECK(baseline.route->snapshot().value().revision == 0 && baseline.activations == 0);
    CHECK(readConfigurationFile(path).value().text.find("existing") != std::string::npos);
    baseline.close();
}
/** @brief 子树编辑只修改指定 config，CAS、未知插件、重复身份与非法 JSON 都不接受新目标。 */
void editInstance()
{
    Scene scene;
    scene.apply(
        "formatVersion: 1\nplugins: [{id: r, plugin: demo, config: {keep: yes}, children: [{id: c, plugin: "
        "demo}]}, {id: sibling, plugin: demo, config: {big: 9007199254740993, decimal: 1.2345678901234567890}}]\n");
    auto edited = scene.route->updateConfiguration("r/c", "{\"title\":\"child\"}", 1).value();
    wait(edited);
    auto state = scene.session->snapshot().value();
    const auto& entries = state.coordinator.desired->source().structure().entries();
    CHECK(entries.size() == 3 && entries.at("r/c").config == "{\"title\":\"child\"}" &&
          entries.at("sibling").config == "{\"big\":9007199254740993,\"decimal\":1.2345678901234567890}");
    CHECK(entries.at("r").config.find("keep") != std::string::npos && state.coordinator.desiredRevision == 2);
    CHECK(!scene.route->updateConfiguration("r/c", "{}", 1).hasValue());
    CHECK(!scene.route->updateConfiguration("missing", "{}", 2).hasValue());
    CHECK(!scene.route->addInstance("unknown", "other", "{}", 2).hasValue());
    CHECK(!scene.route->addInstance("broken", "demo", "{broken:", 2).hasValue());
    CHECK(scene.route->snapshot().value().revision == 2);
    scene.toggle();
    scene.card()->findChild<QPushButton*>("CordisPluginConfigure")->click();
    auto editor = scene.find<QPlainTextEdit>("CordisPluginConfigurationEditor");
    editor->setPlainText("{\"draft\":true}");
    auto change = scene.route->setEnabled("sibling", false, 2).value();
    wait(change);
    scene.find<QPushButton>("CordisPluginConfigurationApply")->click();
    CHECK(editor->toPlainText() == "{\"draft\":true}" && scene.route->snapshot().value().revision == 3);
    scene.find<QPushButton>("CordisPluginConfigurationReload")->click();
    CHECK(editor->toPlainText().contains("keep") && !editor->toPlainText().contains("draft"));
    editor->setPlainText("{\"new\":true}");
    scene.find<QPushButton>("CordisPluginConfigurationApply")->click();
    until([&] { return scene.find<QPushButton>("CordisPluginConfigurationReload")->isEnabled(); });
    CHECK(scene.route->snapshot().value().revision == 4 && scene.route->snapshot().value().dirty);
    scene.find<QPushButton>("CordisPluginConfigurationSave")->click();
    until([&] { return !scene.route->snapshot().value().dirty; });
    scene.close();
}
/** @brief 保存冲突不回报成功，只有显式采用磁盘基线后才允许重新保存。 */
void saveConflict()
{
    Scene scene;
    wait(scene.route->addInstance("a", "demo", "{}", 0).value());
    QFile file(scene.directory.filePath("cordis.yaml"));
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("formatVersion: 1\nplugins: []\n") > 0);
    file.close();
    auto saving = scene.route->saveConfiguration(1).value();
    until([&] { return saving.isReady(); });
    CHECK(!saving.tryResult()->hasValue() && scene.session->snapshot().value().store.dirty);
    scene.session->rebaseExternal(1).value();
    wait(scene.route->saveConfiguration(1).value());
    CHECK(!scene.session->snapshot().value().store.dirty);
    scene.close();
}
/** @brief 弹窗贡献仍属于 epoch/effect，关闭只隐藏，停用完整删除窗口与内容。 */
void dialogLifetime()
{
    Scene scene;
    std::optional<PageHandle> page;
    QPointer<QWidget> body;
    auto runtime = scene.kernel
                       ->registerPlugin("dialog-owner",
                                        [&](Context context) {
                                            page = scene.shell.pages()
                                                       ->add(context, {"dialog", "Configuration", "",
                                                                       [&] {
                                                                           auto widget = std::make_unique<QWidget>();
                                                                           body = widget.get();
                                                                           return widget;
                                                                       },
                                                                       PagePresentation::Dialog})
                                                       .value();
                                            return page->ready();
                                        })
                       .value();
    auto instance = scene.kernel->mount(runtime).value();
    wait(instance.ready());
    CHECK(!page->snapshot().value().navigationPresent && scene.find<QTabBar>("CordisPageTabs")->count() == 1);
    page->navigate().value();
    QPointer<QWidget> dialog = body->window();
    CHECK(dialog->isVisible() && page->snapshot().value().current);
    dialog->close();
    CHECK(!page->snapshot().value().current && page->snapshot().value().widgetAlive);
    page->navigate().value();
    const auto id = QString::number(instance.snapshot().value().instance.value());
    QPointer<QWidget> card;
    until([&] {
        for (auto item : scene.window->findChildren<QWidget*>("CordisPluginCard"))
            if (item->property("CordisPluginId").toString() == id)
                card = item;
        return !card.isNull();
    });
    CHECK(!card->findChild<QPushButton*>("CordisPluginConfigure")->isEnabled());
    card->findChild<QPushButton*>("CordisPluginManage")->click();
    auto stop = scene.find<QPushButton>("CordisManagementStop");
    CHECK(stop->isVisible() && stop->isEnabled());
    stop->click();
    until([&] { return instance.snapshot().value().state == InstanceState::Disposed && !body && !dialog; });
    CHECK(!body && !dialog && !page->navigate().hasValue());
    scene.close();
}
/** @brief 独立进程避免 Ela 全局主题或路由残留污染其他场景。 */
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
        if (name == "home")
            home();
        else if (name == "disabled")
            disabled();
        else if (name == "management")
            management();
        else if (name == "stale")
            stale();
        else if (name == "home_stale")
            homeStale();
        else if (name == "unavailable")
            unavailable();
        else if (name == "session_expired")
            sessionExpired();
        else if (name == "binding")
            binding();
        else if (name == "queued_close")
            queuedClose();
        else if (name == "reentry")
            reentry();
        else if (name == "malformed")
            malformed();
        else if (name == "layout")
            layout();
        else if (name == "card_windows")
            cardWindows();
        else if (name == "add_instance")
            addInstance();
        else if (name == "edit_instance")
            editInstance();
        else if (name == "save_conflict")
            saveConflict();
        else if (name == "dialog_lifetime")
            dialogLifetime();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
