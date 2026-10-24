/** @file pages.cpp
 * @brief 在真实固定版本 Ela 中验证页面 effect、分类隔离、服务恢复及异步销毁。
 */
#include <cordis/ui/ela_shell.hpp>
#include <cordis/kernel.hpp>
#include <ElaWindow.h>
#include <ElaMenu.h>
#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QPointer>
#include <QStyle>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QShortcutEvent>
#include <QTreeView>
#include <QVBoxLayout>
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
#include <vector>

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 真实 Qt 循环等待，保护超时不伪造完成。 */
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
/** @brief 同时检查源任务与排队回执；通知不借用等待函数的栈。 */
template<class T> void wait(const Task<T>& task, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    auto seen = std::make_shared<bool>(false);
    auto receipt = task.thenOn(scheduler, [seen](const Result<T>&) { *seen = true; });
    until([&] { return receipt.isReady(); });
    if (auto result = task.tryResult(); result && !result->hasValue()) std::cerr << "task failed: " << result->error().message << '\n';
    CHECK(*seen && receipt.tryResult()->hasValue() && task.tryResult()->hasValue());
}
/** @brief 页面及其子对象都必须在工厂载荷释放前完成析构。 */
class Probe final : public QWidget {
public:
    /** @brief 保存不抛错的析构探针。 */
    explicit Probe(std::function<void()> callback) : callback_(std::move(callback)) {}
    /** @brief 观察真正的 QWidget 析构，后续任务负责最终断言。 */
    ~Probe() override { callback_(); }
private:
    std::function<void()> callback_;
};
/** @brief 提供无需使用 Ela 类型的有效页面工厂。 */
PageSpec spec(std::string id = "notes", std::string category = "插件")
{
    return {std::move(id), "笔记", std::move(category), [] { return std::make_unique<QLabel>(QString::fromUtf8("插件页面")); }};
}
/** @brief 为每个场景提供独立 Kernel 和外壳，正常退出的屏障顺序统一。 */
struct Scenario {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    Kernel kernel{scheduler};
    ElaShell shell{scheduler};
    std::shared_ptr<UiPagesService> pages = shell.pages();
    /** @brief 内核先撤销消费者，再完整销毁外壳，最后停调度器。 */
    void close()
    {
        auto closed = kernel.close();
        wait(closed, scheduler);
        CHECK(closed.tryResult()->value().quiescent);
        CHECK(pages->snapshot().value().empty());
        wait(shell.close(), scheduler);
        CHECK(!shell.show().hasValue());
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        CHECK(scheduler->close().hasValue());
    }
};
/** @brief 当前/非当前页撤销、工厂保留、分类子页和重复关闭。 */
void lifetime()
{
    Scenario scene;
    std::optional<Context> context;
    std::optional<PageHandle> first, second;
    bool destroyed = false, factoryHeld = false;
    auto payload = std::make_shared<int>(0);
    std::weak_ptr<int> weak = payload;
    auto plugin = scene.kernel.registerPlugin("pages", [&](Context ctx) {
        context = ctx;
        first = scene.pages->add(ctx, {"first", "第一页", "分组", [&, payload] {
            return std::make_unique<Probe>([&] { destroyed = true; factoryHeld = !weak.expired(); });
        }}).value();
        second = scene.pages->add(ctx, spec("second", "分组")).value();
        return second->ready();
    }).value();
    auto instance = scene.kernel.mount(plugin).value();
    wait(instance.ready(), scene.scheduler);
    wait(first->ready(), scene.scheduler);
    payload.reset();
    CHECK(!weak.expired() && scene.pages->snapshot().value().size() == 2);
    int duplicateFactory = 0;
    auto duplicate = scene.pages->add(*context, {"first", "重复", "", [&] { ++duplicateFactory; return std::make_unique<QWidget>(); }});
    CHECK(!duplicate.hasValue() && duplicate.error().code == ErrorCode::ServiceConflict && duplicateFactory == 0);
    CHECK(first->navigate().hasValue() && first->snapshot().value().current);
    CHECK(second->navigate().hasValue() && second->snapshot().value().current);
    auto closed = first->close();
    CHECK(!closed.isReady());
    CHECK(!first->navigate().hasValue() && first->snapshot().value().retiring);
    wait(closed, scene.scheduler);
    CHECK(closed.tryResult()->value().quiescent && destroyed && factoryHeld && weak.expired());
    CHECK(second->snapshot().value().current && !first->navigate().hasValue());
    wait(second->close(), scene.scheduler);
    CHECK(!second->snapshot().value().widgetAlive && !second->snapshot().value().navigationPresent);
    CHECK(second->close().tryResult()->value().quiescent);
    scene.close();
}
/** @brief 同 id 和同分类在不同实例内互不影响，旧 Context 不得登记新贡献。 */
void namespaces()
{
    Scenario scene;
    std::vector<PageHandle> handles;
    std::vector<Context> contexts;
    auto runtime = scene.kernel.registerPlugin("isolated", [&](Context ctx) {
        contexts.push_back(ctx);
        handles.push_back(scene.pages->add(ctx, spec()).value());
        return handles.back().ready();
    }).value();
    auto first = scene.kernel.mount(runtime).value();
    auto second = scene.kernel.mount(runtime).value();
    wait(first.ready(), scene.scheduler); wait(second.ready(), scene.scheduler);
    CHECK(handles.size() == 2 && handles[0].snapshot().value().owner.instance != handles[1].snapshot().value().owner.instance);
    CHECK(handles[1].navigate().hasValue());
    wait(first.stop(), scene.scheduler);
    CHECK(scene.pages->snapshot().value().size() == 1 && handles[1].snapshot().value().current);
    CHECK(!scene.pages->add(contexts.front(), spec("stale")).hasValue() && !contexts.front().identity().hasValue());
    scene.close();
}
/** @brief 提供者退役自动清页，重新发布 ui.pages/v1 后消费者以新 epoch 恢复。 */
void recovery()
{
    Scenario scene;
    std::vector<PageHandle> handles;
    auto provider = scene.kernel.registerPlugin("ui", [&](Context ctx) {
        CHECK(ctx.provide({"ui.pages"}, scene.pages).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<UiPagesService>({"ui.pages"})}).value();
    auto consumer = scene.kernel.registerPlugin("consumer", [&](Context ctx) {
        auto service = ctx.consume<UiPagesService>({"ui.pages"}).value();
        auto registered = service.withService([&](UiPagesService& pages) { handles.push_back(pages.add(ctx, spec()).value()); });
        if (!registered.hasValue()) return failedTask(registered.error());
        return handles.back().ready();
    }, {ServiceRequirement::of<UiPagesService>({"ui.pages"})}).value();
    auto notes = scene.kernel.mount(consumer).value();
    until([&] { return notes.snapshot().value().state == InstanceState::Pending; });
    CHECK(handles.empty());
    auto initial = scene.kernel.mount(provider).value();
    wait(initial.ready(), scene.scheduler); wait(notes.ready(), scene.scheduler);
    const auto old = handles.front();
    wait(initial.stop(), scene.scheduler);
    until([&] { return notes.snapshot().value().state == InstanceState::Pending; });
    CHECK(scene.pages->snapshot().value().empty() && !old.navigate().hasValue());
    auto replacement = scene.kernel.mount(provider).value();
    wait(replacement.ready(), scene.scheduler);
    until([&] { return notes.snapshot().value().epoch == 2; });
    wait(notes.awaitActive(2), scene.scheduler);
    CHECK(handles.size() == 2 && handles.back().snapshot().value().owner.epoch == 2);
    CHECK(handles.back().navigate().hasValue() && !old.navigate().hasValue());
    scene.close();
}
/** @brief 工厂抛错/空值触发 setup 失败，导航和闭包仍完整回滚。 */
void factories()
{
    Scenario scene;
    std::vector<PageHandle> handles;
    auto runtime = scene.kernel.registerPlugin("bad-page", [&](Context ctx) {
        PageSpec page = spec();
        page.factory = [config = ctx.config()]() -> std::unique_ptr<QWidget> {
            if (config == "throw") throw std::runtime_error("factory failed");
            return {};
        };
        handles.push_back(scene.pages->add(ctx, std::move(page)).value());
        return handles.back().ready();
    }).value();
    auto first = scene.kernel.mount(runtime, "throw").value();
    auto second = scene.kernel.mount(runtime, "null").value();
    until([&] { return first.ready().isReady() && second.ready().isReady() && scene.pages->snapshot().value().empty(); });
    CHECK(!first.ready().tryResult()->hasValue() && !second.ready().tryResult()->hasValue());
    CHECK(handles.size() == 2 && !handles.front().snapshot().value().widgetAlive);
    scene.close();
}
/** @brief 工厂中重入 shell.close 时，尚未返回的 effect 也必须加入关闭屏障。 */
void reentrant()
{
    Scenario scene;
    std::optional<Task<void>> closed;
    bool destroyed = false;
    auto runtime = scene.kernel.registerPlugin("reentrant", [&](Context ctx) {
        auto page = scene.pages->add(ctx, {"close", "关闭", "", [&] {
            closed = scene.shell.close();
            return std::make_unique<Probe>([&] { destroyed = true; });
        }}).value();
        return page.ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value();
    until([&] { return closed && closed->isReady(); });
    CHECK(closed->tryResult()->hasValue() && destroyed && scene.pages->snapshot().value().empty());
    CHECK(!instance.ready().tryResult()->hasValue());
    scene.close();
}
/** @brief 所有业务/诊断入口拒绝 worker，跨 scheduler Context 也不能借 UI 服务登记。 */
void guards()
{
    Scenario scene;
    std::optional<Context> context;
    std::optional<PageHandle> handle;
    auto runtime = scene.kernel.registerPlugin("guard", [&](Context ctx) {
        context = ctx; handle = scene.pages->add(ctx, spec()).value(); return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    bool rejected = false;
    std::thread worker([&] {
        rejected = !context->identity().hasValue() && !scene.pages->add(*context, spec("worker")).hasValue() &&
            !handle->navigate().hasValue() && !handle->snapshot().hasValue() && !scene.pages->snapshot().hasValue() &&
            handle->close().tryResult()->error().code == ErrorCode::WrongThread &&
            !scene.shell.show().hasValue() && scene.shell.close().tryResult()->error().code == ErrorCode::WrongThread;
    });
    worker.join(); CHECK(rejected);
    CHECK(handle->navigate().hasValue() && !handle->snapshot().value().retiring);
    auto otherScheduler = std::make_shared<qt::QtScheduler>();
    Kernel other(otherScheduler);
    std::optional<Context> foreign;
    auto foreignRuntime = other.registerPlugin("foreign", [&](Context ctx) { foreign = ctx; return completedTask(); }).value();
    auto foreignInstance = other.mount(foreignRuntime).value(); wait(foreignInstance.ready(), otherScheduler);
    auto added = scene.pages->add(*foreign, spec("foreign"));
    CHECK(!added.hasValue() && added.error().code == ErrorCode::InvalidArgument);
    wait(other.close(), otherScheduler); CHECK(otherScheduler->close().hasValue());
    scene.close();
}
/** @brief 手动关闭外壳也必须等页面 effect，业务 Context 不能重新打开外壳。 */
void shutdown()
{
    Scenario scene;
    std::optional<Context> context;
    std::optional<PageHandle> handle;
    auto runtime = scene.kernel.registerPlugin("shutdown", [&](Context ctx) {
        context = ctx; handle = scene.pages->add(ctx, spec()).value(); return handle->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    CHECK(handle->navigate().hasValue());
    auto closed = scene.shell.close(); CHECK(!closed.isReady()); wait(closed, scene.scheduler);
    CHECK(scene.pages->snapshot().value().empty() && !handle->snapshot().value().widgetAlive);
    CHECK(!scene.pages->add(*context, spec("late")).hasValue() && !handle->navigate().hasValue());
    scene.close();
}
/** @brief owner 刚进入停用、页面尚未删除时，真实鼠标输入不能再执行插件按钮。 */
void inputGate()
{
    Scenario scene;
    QPointer<QPushButton> button;
    QPointer<QLineEdit> edit;
    std::optional<PageHandle> handle;
    int clicked = 0;
    auto runtime = scene.kernel.registerPlugin("input", [&](Context ctx) {
        auto page = scene.pages->add(ctx, {"input", "输入", "", [&] {
            auto widget = std::make_unique<QWidget>();
            auto layout = new QVBoxLayout(widget.get());
            button = new QPushButton(QString::fromUtf8("操作"), widget.get());
            edit = new QLineEdit(widget.get());
            layout->addWidget(button);
            layout->addWidget(edit);
            QObject::connect(button, &QPushButton::clicked, widget.get(), [&] { ++clicked; });
            return widget;
        }}).value();
        handle = page;
        return page.ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    QTest::mouseClick(button, Qt::LeftButton);
    QInputMethodEvent activeInput;
    activeInput.setCommitString(QString::fromUtf8("输入"));
    QApplication::sendEvent(edit, &activeInput);
    CHECK(clicked == 1 && edit->text() == QString::fromUtf8("输入"));
    auto closed = handle->close();
    CHECK(!closed.isReady() && button);
    QTest::mouseClick(button, Qt::LeftButton);
    QInputMethodEvent retiredInput;
    retiredInput.setCommitString(QString::fromUtf8("已关闭"));
    QApplication::sendEvent(edit, &retiredInput);
    CHECK(clicked == 1 && edit->text() == QString::fromUtf8("输入"));
    wait(closed, scene.scheduler);
    CHECK(button.isNull());
    wait(instance.update("new", 1).value().target, scene.scheduler);
    wait(instance.awaitActive(2), scene.scheduler);
    CHECK(button);
    auto stopped = instance.stop();
    CHECK(!stopped.isReady() && button);
    QTest::mouseClick(button, Qt::LeftButton);
    CHECK(clicked == 1);
    wait(stopped, scene.scheduler);
    CHECK(button.isNull());
    scene.close();
}
/** @brief 快捷键事件的接收者是 QObject；必须沿 parent 找到页面后应用停用门禁。 */
void shortcutGate()
{
    Scenario scene;
    QPointer<QShortcut> shortcut;
    int activated = 0;
    auto runtime = scene.kernel.registerPlugin("shortcut", [&](Context ctx) {
        auto page = scene.pages->add(ctx, {"shortcut", "快捷键", "", [&] {
            auto widget = std::make_unique<QWidget>();
            shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), widget.get());
            QObject::connect(shortcut, &QShortcut::activated, widget.get(), [&] { ++activated; });
            return widget;
        }}).value();
        return page.ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value();
    wait(instance.ready(), scene.scheduler);
    QShortcutEvent active(shortcut->key(), shortcut->id());
    QApplication::sendEvent(shortcut, &active);
    CHECK(activated == 1);
    auto stopped = instance.stop();
    CHECK(!stopped.isReady() && shortcut);
    QShortcutEvent retired(shortcut->key(), shortcut->id());
    QApplication::sendEvent(shortcut, &retired);
    CHECK(activated == 1);
    wait(stopped, scene.scheduler);
    CHECK(shortcut.isNull());
    scene.close();
}
/** @brief 方向键选择与鼠标使用同一导航门禁，关闭当前页后回到安全页。 */
void keyboardNavigation()
{
    Scenario scene;
    std::optional<PageHandle> first, second;
    auto runtime = scene.kernel.registerPlugin("keyboard", [&](Context ctx) {
        first = scene.pages->add(ctx, spec("first", "")).value();
        second = scene.pages->add(ctx, spec("second", "")).value();
        return second->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value();
    wait(instance.ready(), scene.scheduler);
    CHECK(first->navigate().hasValue());
    QTreeView* navigation = nullptr;
    for (auto window : QApplication::topLevelWidgets())
        if (auto tree = window->findChild<QTreeView*>("ElaNavigationView")) navigation = tree;
    CHECK(navigation);
    QTest::keyClick(navigation, Qt::Key_Down);
    CHECK(second->snapshot().value().current && !first->snapshot().value().current);
    wait(second->close(), scene.scheduler);
    CHECK(!first->snapshot().value().current);
    scene.close();
}
/** @brief 反复导航后立即撤页，并让原生延迟滚动实际触发；ASan 检查旧模型节点不被访问。 */
void deferredRemoval()
{
    Scenario scene;
    std::optional<PageHandle> page;
    auto runtime = scene.kernel.registerPlugin("native-removal", [&](Context ctx) {
        page = scene.pages->add(ctx, spec()).value(); return page->ready();
    }).value();
    for (int i = 0; i < 12; ++i) {
        auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
        page->navigate().value();
        wait(instance.stop(), scene.scheduler);
        CHECK(!page->snapshot().value().widgetAlive && scene.pages->snapshot().value().empty());
    }
    // 原生导航的滚动延迟为 200 ms；此 gate 让旧延迟路径在窗口仍存活时经过一次真实触发。
    QEventLoop loop;
    QTimer::singleShot(650, &loop, &QEventLoop::quit);
    loop.exec();
    scene.close();
}
/** @brief 原生展开/紧凑导航保持页面归属，撤销后直接调用 Ela 导航也不能绕过门禁。 */
void nativeModes()
{
    Scenario scene;
    ElaWindow* window = nullptr;
    for (auto widget : QApplication::topLevelWidgets()) if (auto host = qobject_cast<ElaWindow*>(widget)) window = host;
    CHECK(window && window->getIsNavigationBarEnable() && window->getCurrentStackIndex() == 0);
    std::optional<PageHandle> page;
    QPointer<QWidget> nativePage;
    auto runtime = scene.kernel.registerPlugin("native-modes", [&](Context ctx) {
        auto value = spec();
        value.factory = [&] { auto widget = std::make_unique<QWidget>(); nativePage = widget.get(); return widget; };
        page = scene.pages->add(ctx, std::move(value)).value(); return page->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    const auto key = nativePage->property("ElaPageKey").toString(); CHECK(!key.isEmpty());
    for (auto mode : {ElaNavigationType::Compact, ElaNavigationType::Maximal, ElaNavigationType::Minimal, ElaNavigationType::Maximal}) {
        window->setNavigationBarDisplayMode(mode);
        page->navigate().value();
        CHECK(window->getCurrentNavigationPageKey() == key && page->snapshot().value().current);
    }
    scene.shell.showManagement().value();
    const auto management = window->getCurrentNavigationPageKey();
    auto closed = page->close();
    window->navigation(key);
    CHECK(window->getCurrentNavigationPageKey() == management && !page->snapshot().value().current);
    wait(closed, scene.scheduler); CHECK(nativePage.isNull());
    scene.close();
}
/** @brief 紧凑菜单随页面撤销删除动作，分组仍存活时不能留下指向旧节点的入口。 */
void compactRemoval()
{
    Scenario scene;
    std::optional<PageHandle> first, second;
    auto runtime = scene.kernel.registerPlugin("compact-removal", [&](Context ctx) {
        first = scene.pages->add(ctx, {"first", "待撤销页面", "共享分组", [] { return std::make_unique<QWidget>(); }}).value();
        second = scene.pages->add(ctx, {"second", "保留页面", "共享分组", [] { return std::make_unique<QWidget>(); }}).value();
        return second->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    ElaWindow* window = nullptr;
    for (auto widget : QApplication::topLevelWidgets()) if (auto host = qobject_cast<ElaWindow*>(widget)) window = host;
    CHECK(window);
    QPointer<ElaMenu> menu;
    QPointer<QAction> removedAction;
    for (auto candidate : window->findChildren<ElaMenu*>()) for (auto action : candidate->actions())
        if (action->text() == QString::fromUtf8("待撤销页面")) { menu = candidate; removedAction = action; }
    CHECK(menu && removedAction && menu->actions().size() == 2);
    wait(first->close(), scene.scheduler);
    CHECK(removedAction.isNull() && menu && menu->actions().size() == 1);
    menu->actions().front()->trigger();
    CHECK(second->snapshot().value().current);
    wait(second->close(), scene.scheduler);
    CHECK(menu.isNull());
    scene.close();
}
/** @brief 展开分组后切到紧凑模式，在模式恢复前删除分组不能访问缓存的旧节点。 */
void removedExpandedGroup()
{
    Scenario scene;
    ElaWindow* window = nullptr;
    for (auto widget : QApplication::topLevelWidgets()) if (auto host = qobject_cast<ElaWindow*>(widget)) window = host;
    CHECK(window);
    window->setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    std::optional<PageHandle> page;
    auto runtime = scene.kernel.registerPlugin("expanded-removal", [&](Context ctx) {
        page = scene.pages->add(ctx, spec("page", "将被删除的分组")).value(); return page->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    page->navigate().value();
    window->setNavigationBarDisplayMode(ElaNavigationType::Compact);
    wait(instance.stop(), scene.scheduler);
    window->setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    CHECK(!page->snapshot().value().navigationPresent && scene.pages->snapshot().value().empty());
    scene.shell.showManagement().value();
    scene.close();
}
/** @brief 原生菜单与宿主程序入口不同；切页信号重入删除窗口后，原生调用不得继续访问旧节点。 */
void nativeCloseReentry()
{
    Scenario scene;
    QPointer<ElaWindow> window;
    for (auto widget : QApplication::topLevelWidgets()) if (auto host = qobject_cast<ElaWindow*>(widget)) window = host;
    CHECK(window);
    auto management = window->findChild<QWidget*>("CordisManagement");
    CHECK(management);
    const auto key = management->property("ElaPageKey").toString();
    auto stack = qobject_cast<QStackedWidget*>(management->parentWidget());
    CHECK(stack);
    std::optional<Task<void>> closed;
    QObject::connect(stack, &QStackedWidget::currentChanged, stack, [&](int) {
        closed = scene.shell.close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    window->navigation(key);
    CHECK(closed);
    wait(*closed, scene.scheduler);
    CHECK(window.isNull());
    scene.close();
}
/** @brief 关闭窗口必须删除导航样式和仍在运行的动画，不得留下访问旧 viewport 的回调。 */
void animationCleanup()
{
    Scenario scene;
    std::optional<PageHandle> page;
    auto runtime = scene.kernel.registerPlugin("native-animation", [&](Context ctx) {
        page = scene.pages->add(ctx, spec()).value(); return page->ready();
    }).value();
    auto instance = scene.kernel.mount(runtime).value(); wait(instance.ready(), scene.scheduler);
    page->navigate().value(); scene.shell.showManagement().value();
    QPointer<QStyle> style;
    for (auto window : QApplication::topLevelWidgets()) if (auto view = window->findChild<QTreeView*>("ElaNavigationView"))
        style = view->findChild<QStyle*>("ElaNavigationOwnedStyle");
    CHECK(style);
    std::vector<QPointer<QPropertyAnimation>> animations;
    for (auto animation : style->findChildren<QPropertyAnimation*>()) animations.push_back(animation);
    CHECK(!animations.empty());
    wait(scene.shell.close(), scene.scheduler);
    CHECK(style.isNull());
    for (const auto& animation : animations) CHECK(animation.isNull());
    scene.close();
}
/** @brief 每个场景运行真实 Ela DLL 与 Qt QApplication，offscreen 由 CTest 指定。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        const std::string name = argv[1];
        if (name == "lifetime") lifetime();
        else if (name == "namespaces") namespaces();
        else if (name == "recovery") recovery();
        else if (name == "factories") factories();
        else if (name == "reentrant") reentrant();
        else if (name == "guards") guards();
        else if (name == "shutdown") shutdown();
        else if (name == "input_gate") inputGate();
        else if (name == "keyboard_navigation") keyboardNavigation();
        else if (name == "deferred_removal") deferredRemoval();
        else if (name == "native_modes") nativeModes();
        else if (name == "compact_removal") compactRemoval();
        else if (name == "removed_expanded_group") removedExpandedGroup();
        else if (name == "native_close_reentry") nativeCloseReentry();
        else if (name == "animation_cleanup") animationCleanup();
        else if (name == "shortcut_gate") shortcutGate();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
