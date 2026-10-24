/** @file management.cpp
 * @brief 真实 Qt/Ela 验证基础管理页、值诊断、受控操作及关闭重入。
 */
#include <cordis/ui/ela_shell.hpp>
#include <QAbstractItemModel>
#include <QApplication>
#include <QEventLoop>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QTreeView>
#include <QListView>
#include <QTabWidget>
#include <QTabBar>
#include <QMouseEvent>
#include <QSplitter>
#include <QToolButton>
#include <QImage>
#include <ElaTheme.h>
#include <QTest>
#include <QTimer>
#include <QWidget>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 使用真实事件循环和显式 gate，超时只用于失败诊断。 */
void until(const std::function<bool()>& predicate)
{
    if (predicate()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0); deadline.start(5000); loop.exec();
    CHECK(predicate());
}
/** @brief 观察排队回执，避免把源 Task 已完成误认为 owner 已处理。 */
template<class T> void wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    auto seen = std::make_shared<bool>(false);
    auto receipt = task.thenOn(scheduler, [seen](const Result<T>&) { *seen = true; });
    until([&] { return receipt.isReady(); });
    CHECK(*seen && receipt.tryResult()->hasValue() && task.tryResult()->hasValue());
}
/** @brief 每项测试有独立宿主，Kernel 由测试所有，管理层仅持弱引用。 */
struct Scenario {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    std::shared_ptr<ManagementController> management = shell.management();
    std::shared_ptr<DiagnosticsService> diagnostics = shell.diagnostics();
    /** @brief 自动刷新绑定在宿主内核上，不等待插件全部激活。 */
    Scenario() { management->attach(kernel).value(); }
    /** @brief 根据稳定 objectName 找宿主控件，不把 QWidget 暴露给插件。 */
    template<class T> T* widget(const char* name)
    {
        for (auto window : QApplication::topLevelWidgets()) if (auto found = window->findChild<T*>(name)) return found;
        return nullptr;
    }
    /** @brief 管理模型和外壳不得提前关闭 Qt 循环。 */
    void close()
    {
        auto closed = kernel->close(); wait(closed, scheduler); CHECK(closed.tryResult()->value().quiescent);
        wait(shell.close(), scheduler); CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};
/** @brief 界面缺服务时仍可进入，精确依赖原因展示后可随服务发布自动恢复。 */
void pending()
{
    Scenario scene;
    int entered = 0;
    auto runtime = scene.kernel->registerPlugin("等待存储", [&](Context) { ++entered; return completedTask(); },
        {ServiceRequirement::of<int>({"missing.store"}, 2, {"write"})}).value();
    auto instance = scene.kernel->mount(runtime).value();
    until([&] { return instance.snapshot().value().state == InstanceState::Pending && instance.operation().value().target.isReady(); });
    scene.management->refresh().value(); scene.shell.showManagement().value();
    auto data = scene.diagnostics->snapshot().value();
    CHECK(data.instances.size() == 1 && data.instances[0].name == "等待存储" && entered == 0);
    CHECK(data.instances[0].dependencies[0].error->code == ErrorCode::DependencyMissing && data.instances[0].canStop && !data.instances[0].canRetry);
    auto table = scene.widget<QTableView>("CordisInstanceTable");
    auto details = scene.widget<QPlainTextEdit>("CordisManagementDetails");
    CHECK(table && table->model()->rowCount() == 1 && table->model()->index(0, 2).data().toString() == QString::fromUtf8("等待依赖"));
    CHECK(details && details->toPlainText().contains("missing.store/v1 @1") && details->toPlainText().contains("write"));
    auto provider = scene.kernel->registerPlugin("store", [](Context ctx) { ctx.provide({"missing.store"}, std::make_shared<int>(1)).value(); return completedTask(); }, {},
        {ServiceOutput::of<int>({"missing.store"}, true, {2, {"write"}})}).value();
    auto storage = scene.kernel->mount(provider).value(); wait(storage.ready(), scene.scheduler); wait(instance.ready(), scene.scheduler);
    scene.management->refresh().value();
    CHECK(!scene.diagnostics->snapshot().value().instances[0].dependencies[0].error && entered == 1);
    scene.close();
}
/** @brief diagnostics/v1 可由普通消费者读取，快照无插件对象、可独立复制并包含 UI 账目。 */
void diagnostics()
{
    Scenario scene;
    std::optional<DiagnosticsSnapshot> observed;
    auto provider = scene.kernel->registerPlugin("diagnostics", [&](Context ctx) { ctx.provide({"diagnostics"}, scene.diagnostics).value(); return completedTask(); }, {},
        {ServiceOutput::of<DiagnosticsService>({"diagnostics"})}).value();
    auto consumer = scene.kernel->registerPlugin("<b>客户端</b>", [&](Context ctx) {
        ctx.consume<DiagnosticsService>({"diagnostics"}).value().withService([&](DiagnosticsService& service) { observed.emplace(service.snapshot().value()); }).value();
        scene.shell.pages()->add(ctx, {"page", "页面", "", [] { return std::make_unique<QWidget>(); }}).value();
        scene.shell.commands()->add(ctx, {"command", "命令", [] { return completedTask(); }, {}}).value();
        scene.shell.notifications()->post(ctx, {NotificationLevel::Info, "diagnostic <b>text</b>", OperationId(7)}).value();
        return completedTask();
    }, {ServiceRequirement::of<DiagnosticsService>({"diagnostics"})}).value();
    auto service = scene.kernel->mount(provider).value(); wait(service.ready(), scene.scheduler);
    auto client = scene.kernel->mount(consumer).value(); wait(client.ready(), scene.scheduler);
    auto data = scene.diagnostics->snapshot().value();
    CHECK(observed && data.pages.size() == 1 && data.commands.size() == 1 && data.notifications.size() == 1 && data.bindings.size() == 1);
    data.instances[0].name = "changed";
    CHECK(scene.diagnostics->snapshot().value().instances[0].name == "diagnostics");
    scene.management->refresh().value();
    CHECK(scene.widget<QTableView>("CordisInstanceTable")->model()->index(1, 1).data().toString() == QString::fromUtf8("<b>客户端</b>"));
    CHECK(scene.widget<QPlainTextEdit>("CordisManagementNotices")->toPlainText().contains("<b>text</b>"));
    scene.close();
}
/** @brief 通过真实按钮发起重试，重复请求被拒绝，记录新 OperationId 和真实失败结果。 */
void retry()
{
    Scenario scene;
    int attempts = 0;
    auto runtime = scene.kernel->registerPlugin("flaky", [&](Context) {
        return ++attempts == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "first failure")) : completedTask();
    }).value();
    auto instance = scene.kernel->mount(runtime).value();
    until([&] { return instance.snapshot().value().state == InstanceState::Failed && instance.snapshot().value().resources.effects.empty(); });
    auto id = instance.snapshot().value().instance;
    auto old = instance.operation().value();
    scene.management->refresh().value();
    auto button = scene.widget<QPushButton>("CordisManagementRetry"); CHECK(button && button->isEnabled());
    QTest::mouseClick(button, Qt::LeftButton); QTest::mouseClick(button, Qt::LeftButton);
    CHECK(attempts == 1 && !scene.management->retry(id).hasValue());
    until([&] { auto data = scene.diagnostics->snapshot().value(); return data.operations.size() == 1 && !data.operations[0].pending; });
    auto operation = scene.diagnostics->snapshot().value().operations[0];
    CHECK(operation.stable->outcome == StableOutcome::Active && operation.operation == instance.operation().value().id && operation.operation != old.id);
    CHECK(attempts == 2 && instance.snapshot().value().revision == 1 && instance.snapshot().value().epoch == 2);
    auto badRuntime = scene.kernel->registerPlugin("always-bad", [](Context) { return failedTask(makeError(ErrorCode::ActivationFailed, "still failed")); }).value();
    auto bad = scene.kernel->mount(badRuntime).value();
    until([&] { return bad.snapshot().value().state == InstanceState::Failed && bad.snapshot().value().resources.effects.empty(); });
    scene.management->retry(bad.snapshot().value().instance).value();
    until([&] { auto data = scene.diagnostics->snapshot().value(); return data.operations.size() == 2 && !data.operations.back().pending; });
    auto failed = scene.diagnostics->snapshot().value().operations.back();
    CHECK(failed.stable->outcome == StableOutcome::Failed && failed.error && failed.error->message == "still failed");
    scene.close();
}
/** @brief 停止请求不内联，等待 worker 完成后 owner 确认清理；双击不重复停止。 */
void stop()
{
    Scenario scene;
    Promise<void> gate;
    auto runtime = scene.kernel->registerPlugin("drain", [&](Context ctx) {
        ctx.onCleanup([&](CleanupContext) { return gate.task(); }, "blocked-worker").value(); return completedTask();
    }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    auto survivor = scene.kernel->mount(runtime).value(); wait(survivor.ready(), scene.scheduler);
    const auto id = instance.snapshot().value().instance;
    scene.management->stop(id).value();
    CHECK(instance.snapshot().value().state == InstanceState::Active && !scene.management->stop(id).hasValue());
    until([&] { return instance.snapshot().value().state == InstanceState::Unloading; });
    CHECK(scene.diagnostics->snapshot().value().operations[0].pending && !scene.diagnostics->snapshot().value().instances[0].canRetry);
    std::thread worker([&] { gate.resolve(); }); worker.join();
    CHECK(scene.diagnostics->snapshot().value().operations[0].pending);
    until([&] { return !scene.diagnostics->snapshot().value().operations[0].pending; });
    auto data = scene.diagnostics->snapshot().value();
    CHECK(data.instances.size() == 1 && data.instances[0].instance.instance == survivor.snapshot().value().instance && data.operations[0].cleanup->quiescent && !scene.management->stop(id).hasValue());
    CHECK(scene.widget<QPlainTextEdit>("CordisManagementOperations")->toPlainText().contains(QString::fromUtf8("实例=%1").arg(id.value())));
    CHECK(scene.widget<QTableView>("CordisInstanceTable")->currentIndex().row() == -1);
    auto stopButton = scene.widget<QPushButton>("CordisManagementStop"); CHECK(!stopButton->isEnabled());
    QTest::mouseClick(stopButton, Qt::LeftButton); wait(completedTask(), scene.scheduler);
    scene.management->refresh().value();
    CHECK(scene.widget<QTableView>("CordisInstanceTable")->currentIndex().row() == -1 && !stopButton->isEnabled());
    CHECK(survivor.snapshot().value().state == InstanceState::Active && scene.diagnostics->snapshot().value().operations.size() == 1);
    scene.close();
}
/** @brief 清除选择后即使仍有当前单元格，也不能继续操作或在刷新时恢复旧选择。 */
void deselect()
{
    Scenario scene;
    auto runtime = scene.kernel->registerPlugin("selection", [](Context) { return completedTask(); }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    scene.management->refresh().value();
    auto table = scene.widget<QTableView>("CordisInstanceTable");
    auto button = scene.widget<QPushButton>("CordisManagementStop");
    CHECK(table && button && button->isEnabled());
    table->clearSelection();
    CHECK(table->currentIndex().row() == 0 && table->selectionModel()->selectedRows().empty());
    CHECK(!button->isEnabled());
    scene.management->refresh().value();
    CHECK(table->selectionModel()->selectedRows().empty() && !button->isEnabled());
    QTest::mouseClick(button, Qt::LeftButton); wait(completedTask(), scene.scheduler);
    CHECK(instance.snapshot().value().state == InstanceState::Active && scene.diagnostics->snapshot().value().operations.empty());
    table->selectRow(0);
    CHECK(button->isEnabled());
    scene.close();
}
/** @brief 错误线程、跨 scheduler/Kernel、未知编号及内核关闭都不能绕过管理门禁。 */
void guards()
{
    Scenario scene;
    auto runtime = scene.kernel->registerPlugin("guard", [](Context) { return completedTask(); }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    const auto id = instance.snapshot().value().instance;
    bool rejected = false;
    std::thread worker([&] {
        rejected = !scene.management->attach(scene.kernel).hasValue() && !scene.management->refresh().hasValue() &&
            !scene.management->stop(id).hasValue() && !scene.management->retry(id).hasValue() && !scene.diagnostics->snapshot().hasValue() && !scene.shell.showManagement().hasValue();
    }); worker.join(); CHECK(rejected);
    auto otherScheduler = std::make_shared<qt::QtScheduler>();
    auto other = std::make_shared<Kernel>(otherScheduler);
    CHECK(scene.management->attach(other).error().code == ErrorCode::InvalidArgument);
    CHECK(!scene.management->attach(std::make_shared<Kernel>(scene.scheduler)).hasValue());
    CHECK(scene.management->attach(scene.kernel).hasValue() && !scene.management->stop({}).hasValue() && !scene.management->retry(id).hasValue());
    scene.management->stop(id).value();
    auto closed = scene.kernel->close();
    CHECK(!scene.management->stop(id).hasValue());
    wait(closed, scene.scheduler);
    until([&] { return !scene.diagnostics->snapshot().value().operations[0].pending; });
    CHECK(scene.diagnostics->snapshot().value().operations[0].error->code == ErrorCode::ContextClosing);
    wait(other->close(), otherScheduler); otherScheduler->close().value();
    scene.close();
}
/** @brief 已排队重试在外壳关闭后取消；晚到宿主回调只更新值，不访问已删除控件。 */
void queuedClose()
{
    Scenario scene;
    int attempts = 0;
    auto runtime = scene.kernel->registerPlugin("close", [&](Context) { ++attempts; return failedTask(makeError(ErrorCode::ActivationFailed, "failure")); }).value();
    auto instance = scene.kernel->mount(runtime).value();
    until([&] { return instance.snapshot().value().state == InstanceState::Failed && instance.snapshot().value().resources.effects.empty(); });
    QPointer<QWidget> page = scene.widget<QWidget>("CordisManagement"); CHECK(page);
    scene.management->retry(instance.snapshot().value().instance).value();
    auto closed = scene.shell.close();
    wait(closed, scene.scheduler);
    until([&] { return !scene.diagnostics->snapshot().value().operations[0].pending; });
    CHECK(attempts == 1 && page.isNull() && !scene.management->refresh().hasValue() && !scene.shell.showManagement().hasValue());
    CHECK(!scene.diagnostics->snapshot().value().managementOpen && scene.diagnostics->snapshot().value().operations[0].error.has_value());
    scene.close();
}
/** @brief Kernel 由宿主释放后刷新返回错误，模型不延长 Kernel 生命周期或重绑定旧请求。 */
void expiredKernel()
{
    Scenario scene;
    std::weak_ptr<Kernel> weak = scene.kernel;
    wait(scene.kernel->close(), scene.scheduler);
    scene.kernel.reset();
    CHECK(weak.expired() && !scene.management->refresh().hasValue() && !scene.diagnostics->snapshot().hasValue());
    CHECK(scene.widget<QTableView>("CordisInstanceTable")->model()->rowCount() == 0);
    CHECK(!scene.management->attach(std::make_shared<Kernel>(scene.scheduler)).hasValue());
    wait(scene.shell.close(), scene.scheduler); scene.scheduler->close().value();
}
/** @brief 刷新模型信号里重入关闭并推进 Qt，当前刷新返回前原生页面必须仍存活。 */
void viewReentry()
{
    Scenario scene;
    auto runtime = scene.kernel->registerPlugin("view", [](Context) { return completedTask(); }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    auto table = scene.widget<QTableView>("CordisInstanceTable");
    QPointer<QWidget> page = scene.widget<QWidget>("CordisManagement");
    CHECK(table && page);
    // 定时刷新可能已填充列表；显式清空以保证本次手动刷新一定发出插入信号。
    qobject_cast<QStandardItemModel*>(table->model())->setRowCount(0);
    std::optional<Task<void>> closed;
    bool heldInside = false;
    auto connection = QObject::connect(table->model(), &QAbstractItemModel::rowsInserted, table, [&] {
        closed = scene.shell.close(); QCoreApplication::processEvents(); heldInside = page && !closed->isReady();
    });
    auto refreshed = scene.management->refresh();
    QObject::disconnect(connection);
    CHECK(!refreshed.hasValue() && closed && heldInside);
    wait(*closed, scene.scheduler); CHECK(page.isNull());
    scene.close();
}
/** @brief 页面切换信号中重入关闭，导航调用返回前不得删除正在执行切页的 Qt 对象。 */
void navigationReentry()
{
    Scenario scene;
    QPointer<QWidget> page = scene.widget<QWidget>("CordisManagement");
    auto stack = qobject_cast<QStackedWidget*>(page->parentWidget());
    CHECK(stack);
    std::optional<Task<void>> closed;
    bool heldInside = false;
    auto connection = QObject::connect(stack, &QStackedWidget::currentChanged, stack, [&](int) {
        closed = scene.shell.close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        heldInside = page && !closed->isReady();
    });
    auto shown = scene.shell.showManagement();
    QObject::disconnect(connection);
    CHECK(!shown.hasValue() && shown.error().code == ErrorCode::ContextClosing && closed && heldInside);
    wait(*closed, scene.scheduler); CHECK(page.isNull());
    scene.close();
}
/** @brief 真实清理失败保留资源并显示原因，不能把 retry 按钮当作强制释放。 */
void cleanupFailure()
{
    Scenario scene;
    auto runtime = scene.kernel->registerPlugin("cleanup-failure", [](Context ctx) {
        ctx.onCleanup([](CleanupContext) { auto error = makeError(ErrorCode::CleanupFailed, "worker did not join"); error.phase = "worker.stop"; return failedTask(std::move(error)); }).value();
        return completedTask();
    }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    const auto id = instance.snapshot().value().instance;
    scene.management->stop(id).value();
    until([&] { return !scene.diagnostics->snapshot().value().operations[0].pending; });
    auto snapshot = scene.diagnostics->snapshot().value();
    CHECK(snapshot.operations[0].cleanup && !snapshot.operations[0].cleanup->quiescent && !snapshot.operations[0].error->causes.empty());
    CHECK(!scene.management->retry(id).hasValue() && !snapshot.instances[0].canRetry);
    CHECK(scene.widget<QPlainTextEdit>("CordisManagementDetails")->toPlainText().contains("worker did not join"));
    auto closed = scene.kernel->close(); wait(closed, scene.scheduler); CHECK(!closed.tryResult()->value().quiescent);
    wait(scene.shell.close(), scene.scheduler); scene.scheduler->close().value();
}
/** @brief 队列遭丢弃只发布原子诊断，不能伪报管理操作完成。 */
void blocked()
{
    Scenario scene;
    auto runtime = scene.kernel->registerPlugin("blocked", [](Context) { return completedTask(); }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    scene.management->stop(instance.snapshot().value().instance).value();
    scene.scheduler->close().value();
    auto data = scene.diagnostics->snapshot().value();
    CHECK(data.operations[0].pending && data.operations[0].deliveryBlocked && data.instances[0].busy);
    CHECK(instance.snapshot().value().state == InstanceState::Active);
    // 故意违反正常退出协议，不伪造 Kernel 静止或强制清空插件资源。
}
/** @brief 明暗主题切换不重启实例、不改变选择，诊断标签拖动也不能产生独立窗口。 */
void theme()
{
    Scenario scene;
    int entered = 0;
    auto runtime = scene.kernel->registerPlugin("theme", [&](Context) { ++entered; return completedTask(); }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    scene.management->refresh().value(); scene.shell.showManagement().value();
    auto table = scene.widget<QTableView>("CordisInstanceTable");
    auto label = scene.widget<QLabel>("CordisManagementSummary");
    auto tabs = scene.widget<QTabWidget>("CordisManagementTabs");
    auto details = scene.widget<QPlainTextEdit>("CordisManagementDetails");
    CHECK(table->inherits("ElaTableView") && label->inherits("ElaText") && tabs->inherits("ElaTabWidget"));
    CHECK(details->inherits("ElaPlainTextEdit") && scene.widget<QPushButton>("CordisManagementStop")->inherits("ElaPushButton"));
    CHECK(scene.widget<QTreeView>("ElaNavigationView")->inherits("ElaNavigationView"));
    CHECK(scene.widget<QListView>("CordisCommandList")->inherits("ElaListView"));
    const auto selected = table->selectionModel()->selectedRows().front().data(Qt::UserRole);
    const auto epoch = instance.snapshot().value().epoch;
    const auto operation = instance.operation().value().id;
    scene.shell.show().value();
    for (auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode);
        scene.management->refresh().value();
        CHECK(label->palette().color(QPalette::WindowText) == eTheme->getThemeColor(mode, ElaThemeType::BasicText));
        // CSS 透明背景会覆盖 Base 调色板；检查最终合成图，防止透明编辑器仍叠在白色容器上。
        const auto rendered = details->grab().toImage();
        const auto background = rendered.pixelColor(rendered.width() - 20, rendered.height() - 20);
        CHECK(mode == ElaThemeType::Dark ? background.lightness() < 100 : background.lightness() > 180);
        CHECK(table->selectionModel()->selectedRows().front().data(Qt::UserRole) == selected);
        CHECK(instance.snapshot().value().epoch == epoch && instance.operation().value().id == operation && entered == 1);
    }
    const int windows = QApplication::topLevelWidgets().size();
    auto bar = tabs->tabBar();
    const QPoint start = bar->tabRect(0).center();
    QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, QPointF(start.x(), -100), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &move);
    QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, start);
    CHECK(tabs->count() == 4 && tabs->widget(0) == details && QApplication::topLevelWidgets().size() == windows);
    tabs->setCurrentIndex(1); CHECK(tabs->currentIndex() == 1);
    scene.close();
}
/** @brief 工作台页签和侧栏共享导航，面板可隐藏恢复，撤页后模型和页签同步删除。 */
void workbench()
{
    Scenario scene;
    std::optional<PageHandle> page;
    auto runtime = scene.kernel->registerPlugin("workbench", [&](Context ctx) {
        page = scene.shell.pages()->add(ctx, {"document", "文档页", "工作区", [] { return std::make_unique<QWidget>(); }}).value();
        scene.shell.notifications()->post(ctx, {NotificationLevel::Info, "workbench <b>output</b>", {}}).value();
        return page->ready();
    }).value();
    auto instance = scene.kernel->mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    scene.shell.show().value(); scene.shell.showManagement().value();
    auto tabs = scene.widget<QTabBar>("CordisPageTabs");
    auto explorer = scene.widget<QTreeView>("CordisResourceExplorer");
    auto side = scene.widget<QWidget>("CordisWorkbenchSidebar");
    auto panel = scene.widget<QTabWidget>("CordisWorkbenchPanel");
    CHECK(scene.widget<QSplitter>("ElaWorkbenchContainer") && scene.widget<QSplitter>("ElaWorkbenchPanelSplitter"));
    CHECK(tabs && tabs->count() == 3 && tabs->currentIndex() == 1 && explorer->model()->rowCount() == 3);
    tabs->setCurrentIndex(2); CHECK(page->snapshot().value().current);
    scene.shell.showManagement().value(); CHECK(tabs->currentIndex() == 1 && !page->snapshot().value().current);
    CHECK(explorer->currentIndex().data(Qt::UserRole).toString() == tabs->tabData(1).toString());
    auto toggleSide = scene.widget<QToolButton>("CordisToggleSidebar");
    auto togglePanel = scene.widget<QToolButton>("CordisTogglePanel");
    toggleSide->click(); CHECK(side->isHidden()); toggleSide->click(); CHECK(!side->isHidden());
    togglePanel->click(); CHECK(panel->isHidden()); togglePanel->click(); CHECK(!panel->isHidden());
    CHECK(scene.widget<QPlainTextEdit>("CordisWorkbenchOutput")->toPlainText().contains("<b>output</b>"));
    auto closed = instance.stop(); wait(closed, scene.scheduler);
    CHECK(tabs->count() == 2 && explorer->model()->rowCount() == 2 && !page->snapshot().value().widgetAlive);
    scene.close();
}
/** @brief 每个场景运行独立的真实 QApplication；offscreen 由 CTest 环境指定。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
        const std::string name = argv[1];
        if (name == "pending") pending();
        else if (name == "diagnostics") diagnostics();
        else if (name == "retry") retry();
        else if (name == "stop") stop();
        else if (name == "deselect") deselect();
        else if (name == "guards") guards();
        else if (name == "queued_close") queuedClose();
        else if (name == "expired_kernel") expiredKernel();
        else if (name == "view_reentry") viewReentry();
        else if (name == "navigation_reentry") navigationReentry();
        else if (name == "cleanup_failure") cleanupFailure();
        else if (name == "blocked") blocked();
        else if (name == "theme") theme();
        else if (name == "workbench") workbench();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
