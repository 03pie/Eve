/** @file workbench.cpp
 * @brief 用真实 Qt/Ela 验证窗口历史、搜索、页签及设置与贡献生命周期的协作。
 */
#include <cordis/kernel.hpp>
#include <cordis/ui/ela_shell.hpp>

#include <ElaMenu.h>
#include <ElaPopularCard.h>
#include <ElaScrollPageArea.h>
#include <ElaSuggestBox.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEventLoop>
#include <QFontDatabase>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

/** @brief 以真实 Qt 循环等待状态变化，超时只作为故障保护。 */
void until(const std::function<bool()>& done)
{
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    deadline.start(5000);
    if (!done())
        loop.exec();
    CHECK(done());
}

/** @brief 等待真实事件循环中的任务，超时必须失败。 */
template <class T> void wait(const Task<T>& task)
{
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (task.isReady())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(1);
    deadline.start(5000);
    if (!task.isReady())
        loop.exec();
    CHECK(task.isReady() && task.tryResult()->hasValue());
}
/** @brief 每个窗口使用独立外壳，测试不依赖 Ela 全局路由历史。 */
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell{scheduler};
    QPointer<ElaWindow> window;
    std::optional<PageHandle> first, second;
    std::optional<Instance> instance;
    /** @brief 以独立窗口内的 key 注册两个页面。 */
    Scene()
    {
        auto runtime =
            kernel
                ->registerPlugin("workbench",
                                 [&](Context context) {
                                     first = shell.pages()
                                                 ->add(context, {"first", "Alpha", "Example",
                                                                 [] {
                                                                     auto editor = std::make_unique<QLineEdit>();
                                                                     editor->setObjectName("WorkbenchPluginEditor");
                                                                     return editor;
                                                                 }})
                                                 .value();
                                     second = shell.pages()
                                                  ->add(context, {"second", "Beta", "Example",
                                                                  [] { return std::make_unique<QWidget>(); }})
                                                  .value();
                                     return second->ready();
                                 })
                .value();
        instance = kernel->mount(runtime).value();
        wait(instance->ready());
        shell.show().value();
        for (auto widget : QApplication::topLevelWidgets()) {
            auto candidate = qobject_cast<ElaWindow*>(widget);
            if (candidate && !candidate->property("WorkbenchTestClaimed").toBool()) {
                window = candidate;
                candidate->setProperty("WorkbenchTestClaimed", true);
                break;
            }
        }
        CHECK(window);
    }
    /** @brief 精确查找宿主入口，避免误用原生私有控件。 */
    template <class T> T* find(const char* name) const
    {
        auto found = window->findChild<T*>(QString::fromLatin1(name));
        CHECK(found);
        return found;
    }
    /** @brief 菜单触发走真实的工作台控制块。 */
    void action(const char* name) { find<QAction>(name)->trigger(); }
    /** @brief 检查生命周期屏障成功，而不仅检查窗口消失。 */
    void close()
    {
        auto task = kernel->close();
        wait(task);
        CHECK(task.tryResult()->value().quiescent);
        wait(shell.close());
        CHECK(!window);
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};

/** @brief 分支截断、相同页面去重、菜单与工具按钮状态一致。 */
void history()
{
    Scene scene;
    auto back = scene.find<QToolButton>("CordisNavigateBack");
    auto forward = scene.find<QToolButton>("CordisNavigateForward");
    CHECK(!back->isEnabled() && !forward->isEnabled());
    scene.first->navigate().value();
    scene.second->navigate().value();
    back->click();
    CHECK(scene.first->snapshot().value().current && forward->isEnabled());
    scene.first->navigate().value();
    scene.action("CordisMenuForward");
    CHECK(scene.second->snapshot().value().current);
    scene.action("CordisMenuBack");
    CHECK(scene.first->snapshot().value().current);
    scene.shell.showManagement().value();
    CHECK(!forward->isEnabled() && !scene.find<QAction>("CordisMenuForward")->isEnabled());
    back->click();
    CHECK(scene.first->snapshot().value().current);
    scene.close();
}

/** @brief 关闭视图保留 QWidget，左侧/服务重新导航恢复同一页签。 */
void tabs()
{
    Scene scene;
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    CHECK(tabs->count() == 1);
    scene.first->navigate().value();
    const auto count = tabs->count();
    QPointer<QLineEdit> editor = scene.find<QLineEdit>("WorkbenchPluginEditor");
    editor->setText("unsaved plugin state");
    scene.action("CordisCloseView");
    CHECK(tabs->count() == count - 1 && scene.first->snapshot().value().widgetAlive);
    scene.first->navigate().value();
    CHECK(tabs->count() == count && scene.first->snapshot().value().current);
    CHECK(editor && scene.find<QLineEdit>("WorkbenchPluginEditor") == editor &&
          editor->text() == "unsaved plugin state");
    scene.action("CordisOpenHome");
    scene.action("CordisCloseView");
    CHECK(tabs->count() == count - 1);
    scene.close();
}

/** @brief 键盘选择建议后进入中央页面，撤销时清理已经显示的结果及历史。 */
void search()
{
    Scene scene;
    auto search = scene.find<ElaSuggestBox>("CordisPageSearch");
    auto edit = search->findChild<QLineEdit*>();
    CHECK(edit);
    scene.action("CordisQuickOpen");
    QTest::keyClicks(edit, "Alpha");
    // 建议弹层应归整窗拥有，否则会被标题栏命令中心的 30 像素高度裁剪。
    bool ownsPopup = false;
    for (auto child : scene.window->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (QString::fromLatin1(child->metaObject()->className()) == "ElaSuggestBoxSearchViewContainer")
            ownsPopup = true;
    CHECK(ownsPopup);
    QTest::keyClick(edit, Qt::Key_Down);
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(scene.first->snapshot().value().current);
    search->setSearchText("Beta");
    QTest::qWait(350);
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(scene.second->snapshot().value().current);
    search->setSearchText("Alpha");
    wait(scene.first->close());
    CHECK(!scene.first->navigate().hasValue());
    QTest::keyClick(edit, Qt::Key_Return);
    CHECK(!scene.first->snapshot().value().current);
    QTest::keyClick(edit, Qt::Key_Escape);
    CHECK(search->getSearchText().isEmpty());
    scene.find<QToolButton>("CordisNavigateBack")->click();
    CHECK(!scene.first->snapshot().value().current);
    scene.close();
}

/** @brief 设置与菜单使用真实主题、显隐和输出，关闭后动作不再执行。 */
void menus()
{
    Scene scene;
    CHECK(!scene.window->findChild<QWidget*>("CordisResourceExplorer"));
    CHECK(!scene.window->findChild<QWidget*>("CordisWorkbenchSidebar"));
    scene.action("CordisOpenSettings");
    scene.action("CordisViewNavigation");
    CHECK(scene.window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal);
    scene.action("CordisViewNavigation");
    CHECK(scene.window->getNavigationBarDisplayMode() == ElaNavigationType::Compact);
    auto theme = scene.find<QComboBox>("CordisSettingsTheme");
    theme->setCurrentIndex(1);
    CHECK(eTheme->getThemeMode() == ElaThemeType::Dark);
    scene.action("CordisToggleTheme");
    CHECK(theme->currentIndex() == 0);
    auto panel = scene.find<QTabWidget>("CordisWorkbenchPanel");
    CHECK(panel->isHidden());
    scene.action("CordisViewPanel");
    CHECK(!panel->isHidden());
    scene.action("CordisViewPanel");
    CHECK(panel->isHidden());
    scene.action("CordisOpenCommands");
    CHECK(!panel->isHidden() && panel->currentIndex() == 0);
    auto output = scene.find<QPlainTextEdit>("CordisWorkbenchOutput");
    output->setPlainText("<literal output>");
    scene.action("CordisCopyOutput");
    CHECK(QApplication::clipboard()->text() == "<literal output>");
    scene.action("CordisClearOutput");
    CHECK(output->toPlainText().isEmpty());
    scene.action("CordisOpenAbout");
    CHECK(scene.find<QWidget>("CordisAboutPage")->isVisible());
    auto closing = scene.shell.close();
    CHECK(!scene.find<QAction>("CordisOpenSettings")->isEnabled());
    wait(closing);
    scene.close();
}

/** @brief 一个窗口的回退不能执行另一个窗口的页面或旧全局命令。 */
void isolation()
{
    Scene first, second;
    first.first->navigate().value();
    second.second->navigate().value();
    first.second->navigate().value();
    first.find<QToolButton>("CordisNavigateBack")->click();
    CHECK(first.first->snapshot().value().current && second.second->snapshot().value().current);
    first.close();
    second.find<QToolButton>("CordisNavigateBack")->click();
    CHECK(!second.second->snapshot().value().current);
    second.close();
}

/** @brief 切页信号重入其他页面后，历史与页签应反映最终堆栈而非最初请求。 */
void navigationReentry()
{
    Scene scene;
    auto editor = scene.find<QLineEdit>("WorkbenchPluginEditor");
    auto stack = qobject_cast<QStackedWidget*>(editor->parentWidget());
    CHECK(stack);
    bool redirected = false;
    QObject::connect(stack, &QStackedWidget::currentChanged, scene.window, [&](int) {
        if (!redirected && stack->currentWidget() == editor) {
            redirected = true;
            scene.second->navigate().value();
        }
    });
    scene.first->navigate().value();
    CHECK(redirected && scene.second->snapshot().value().current);
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    CHECK(tabs->tabText(tabs->currentIndex()) == "Beta");
    scene.find<QToolButton>("CordisNavigateBack")->click();
    CHECK(!scene.first->snapshot().value().current && !scene.second->snapshot().value().current);
    scene.close();
}

/** @brief 建议点击可同步删除搜索框；正在运行的弹层动画也随所有者销毁。 */
void suggestionReentry()
{
    auto parent = std::make_unique<QWidget>();
    QPointer<ElaSuggestBox> search = new ElaSuggestBox(parent.get());
    search->addSuggestion("Alpha");
    parent->show();
    auto edit = search->findChild<QLineEdit*>();
    CHECK(edit);
    search->setSearchText("Alpha");
    QObject::connect(search, &ElaSuggestBox::suggestionClicked, parent.get(),
                     [&](const ElaSuggestBox::SuggestData& data) {
                         CHECK(data.getSuggestText() == "Alpha");
                         delete search.data();
                         CHECK(data.getSuggestText() == "Alpha");
                     });
    QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection);
    CHECK(!search);
    QTest::qWait(350);
}

/** @brief 概览及所有宿主页可关闭，最后一个页签关闭后空白，撤销插件不自动打开首页。 */
void emptyWorkspace()
{
    Scene scene;
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    scene.action("CordisCloseView");
    CHECK(tabs->count() == 0);
    CHECK(scene.find<QWidget>("ElaEmptyWorkbench")->isVisible());
    CHECK(scene.window->grab().save(QCoreApplication::applicationDirPath() + "/ela-empty-workbench.png"));
    CHECK(!scene.first->snapshot().value().current && scene.first->snapshot().value().widgetAlive);
    scene.find<QToolButton>("CordisNavigateBack")->click();
    CHECK(tabs->count() == 1);
    for (auto action : {"CordisOpenPlugins", "CordisOpenSettings", "CordisOpenAbout"})
        scene.action(action);
    CHECK(tabs->count() == 4);
    while (tabs->count() > 0) {
        const auto before = tabs->count();
        scene.action("CordisCloseView");
        CHECK(tabs->count() == before - 1);
    }
    CHECK(scene.find<QWidget>("ElaEmptyWorkbench")->isVisible());
    wait(scene.instance->stop());
    CHECK(tabs->count() == 0);
    scene.action("CordisOpenHome");
    CHECK(tabs->count() == 1);
    scene.close();
}

/** @brief 分组单击直接打开最近子页，右键选择其他子页，默认页撤销后安全回落。 */
void directActivation()
{
    Scene scene;
    auto view = scene.find<QTreeView>("ElaNavigationView");
    const auto group = view->model()->index(2, 0);
    CHECK(group.isValid());
    QTest::qWait(50);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(group).center());
    CHECK(scene.first->snapshot().value().current && !QApplication::activePopupWidget());
    QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                              Q_ARG(QPoint, view->visualRect(group).center()));
    auto menu = qobject_cast<ElaMenu*>(QApplication::activePopupWidget());
    CHECK(menu && menu->actions().size() == 2);
    menu->actions().back()->trigger();
    menu->hide();
    CHECK(scene.second->snapshot().value().current);
    scene.action("CordisCloseView");
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(group).center());
    CHECK(scene.second->snapshot().value().current);
    wait(scene.second->close());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(group).center());
    CHECK(scene.first->snapshot().value().current && !QApplication::activePopupWidget());
    scene.close();
}

/** @brief 状态卡片不跳转或弹出打开按钮，插件撤销仍及时清理；保留原生卡片生命周期回归。 */
void homeCards()
{
    Scene scene;
    auto home = scene.find<QWidget>("CordisWelcomePage");
    auto cards = home->findChildren<ElaScrollPageArea*>("CordisPluginCard");
    CHECK(cards.size() == 1);
    QPointer<ElaScrollPageArea> card = cards.front();
    CHECK(card->width() == 280 && card->height() >= 144);
    QTest::mouseClick(card, Qt::LeftButton);
    CHECK(home->isVisible() && !scene.first->snapshot().value().current);
    scene.second->navigate().value();
    scene.action("CordisOpenHome");
    QTest::mouseClick(card, Qt::LeftButton);
    CHECK(home->isVisible() && !scene.second->snapshot().value().current);
    scene.action("CordisOpenHome");
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(card, &enter);
    QTest::qWait(520);
    QPointer<QWidget> floater = home->findChild<QWidget*>("ElaPopularCardFloater");
    CHECK(!floater);
    wait(scene.instance->stop());
    QTest::qWait(350);
    CHECK(!card && !floater && home->findChildren<ElaScrollPageArea*>("CordisPluginCard").isEmpty());
    scene.close();
    auto parent = std::make_unique<QWidget>();
    QPointer<ElaPopularCard> reentrant = new ElaPopularCard(parent.get());
    QObject::connect(reentrant, &ElaPopularCard::popularCardClicked, parent.get(), [&] { delete reentrant.data(); });
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(reentrant, &release);
    CHECK(!reentrant);
    reentrant = new ElaPopularCard(parent.get());
    QPointer<QWidget> reentrantFloater = parent->findChild<QWidget*>("ElaPopularCardFloater");
    CHECK(reentrantFloater);
    QObject::connect(reentrant, &ElaPopularCard::popularCardClicked, parent.get(), [&] { delete reentrant.data(); });
    QCoreApplication::sendEvent(reentrantFloater, &release);
    CHECK(!reentrant && !reentrantFloater);
}

/** @brief 卡片迁移到新容器后，旧悬浮区域销毁不能留下悬空引用，且可重新设置区域。 */
void cardAreaLifetime()
{
    auto root = std::make_unique<QWidget>();
    auto oldArea = new QWidget(root.get());
    auto card = new ElaPopularCard(oldArea);
    card->setParent(root.get());
    delete oldArea;
    CHECK(!card->getCardFloatArea());
    card->setCardButtonText(QString::fromUtf8("打开"));
    CHECK(!card->grab().isNull());
    card->setCardFloatArea(root.get());
    CHECK(card->getCardFloatArea() == root.get());
    root->resize(600, 400);
    root->show();
    card->show();
    QEvent enter(QEvent::Enter);
    QCoreApplication::sendEvent(card, &enter);
    QTest::qWait(520);
    auto floater = root->findChild<QWidget*>("ElaPopularCardFloater");
    CHECK(floater && floater->isVisible());
}

/** @brief 左右开关刷新不发请求，停止/加载及失败重试不切换页签，连续点击只接受一次。 */
void homePower()
{
    Scene scene;
    scene.shell.management()->attach(scene.kernel).value();
    const auto original = scene.instance->snapshot().value().instance;
    auto home = scene.find<QWidget>("CordisWelcomePage");
    QPointer<ElaToggleSwitch> button = scene.find<ElaToggleSwitch>("CordisPluginPower");
    until([&] { return button && button->isEnabled() && button->getIsToggled(); });
    // 同一输入窗口连续派发真实按下/释放，避免 QTest 内部 qWait 在点击间推进整个 remount。
    auto click = [](const QPointer<ElaToggleSwitch>& target) {
        CHECK(target && target->isEnabled());
        const QPointF point(target->rect().center());
        QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        CHECK(target);
        QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &release);
    };
    CHECK(scene.shell.diagnostics()->snapshot().value().operations.empty());
    click(button);
    click(button);
    CHECK(home->isVisible());
    until([&] { return button && button->isEnabled() && !button->getIsToggled(); });
    CHECK(scene.shell.diagnostics()->snapshot().value().operations.size() == 1);
    CHECK(scene.instance->snapshot().value().state == InstanceState::Disposed &&
          button->accessibleName() == QString::fromUtf8("加载插件"));
    click(button);
    until([&] {
        const auto data = scene.shell.diagnostics()->snapshot().value();
        return data.instances.size() == 1 && data.instances.front().instance.instance != original &&
               data.instances.front().instance.state == InstanceState::Active;
    });
    // 新身份重建卡片后旧按钮合法销毁，不能继续等待旧按钮的短暂禁用状态。
    until([&] { return !button; });
    until([&] {
        auto current = scene.find<ElaToggleSwitch>("CordisPluginPower");
        return current->isEnabled() && current->getIsToggled();
    });
    CHECK(scene.shell.diagnostics()->snapshot().value().operations.size() == 2);
    CHECK(home->isVisible() && scene.find<QTabBar>("CordisPageTabs")->count() == 1);
    int calls = 0;
    auto runtime =
        scene.kernel
            ->registerPlugin("recoverable",
                             [&](Context) {
                                 return ++calls == 1
                                            ? failedTask(makeError(ErrorCode::ActivationFailed, "first attempt"))
                                            : completedTask();
                             })
            .value();
    auto failed = scene.kernel->mount(runtime).value();
    wait(failed.operation().value().target);
    const auto id = QString::number(failed.snapshot().value().instance.value());
    QPointer<ElaToggleSwitch> retry;
    until([&] {
        for (auto card : home->findChildren<QWidget*>("CordisPluginCard"))
            if (card->property("CordisPluginId").toString() == id)
                retry = card->findChild<ElaToggleSwitch*>("CordisPluginPower");
        return retry && retry->isEnabled();
    });
    CHECK(retry->accessibleName() == QString::fromUtf8("重试加载"));
    click(retry);
    until([&] { return failed.snapshot().value().state == InstanceState::Active; });
    CHECK(calls == 2 && home->isVisible());
    scene.close();
}

/** @brief 挂载在途时开关显示实际已加载状态，不能把 busy 的不可停止误作关闭。 */
void homePowerBusy()
{
    Scene scene;
    scene.shell.management()->attach(scene.kernel).value();
    Promise<void> gate;
    int calls = 0;
    auto runtime =
        scene.kernel
            ->registerPlugin("slow", [&, gate](Context) { return ++calls == 2 ? gate.task() : completedTask(); })
            .value();
    auto instance = scene.kernel->mount(runtime).value();
    wait(instance.ready());
    const auto original = instance.snapshot().value().instance;
    scene.shell.management()->stop(original).value();
    until([&] { return !scene.shell.diagnostics()->snapshot().value().operations.back().pending; });
    scene.shell.management()->load(original).value();
    until([&] { return scene.shell.diagnostics()->snapshot().value().operations.back().previousInstance == original; });
    const auto current = scene.shell.diagnostics()->snapshot().value().operations.back().instance;
    auto home = scene.find<QWidget>("CordisWelcomePage");
    QPointer<ElaToggleSwitch> toggle;
    until([&] {
        for (auto card : home->findChildren<QWidget*>("CordisPluginCard"))
            if (card->property("CordisPluginId").toString() == QString::number(current.value()))
                toggle = card->findChild<ElaToggleSwitch*>("CordisPluginPower");
        return toggle && !toggle->isEnabled();
    });
    CHECK(scene.kernel->findInstance(current).value().snapshot().value().state == InstanceState::Loading);
    CHECK(toggle->getIsToggled());
    gate.resolve();
    until([&] { return toggle && toggle->isEnabled(); });
    CHECK(toggle->getIsToggled() && scene.shell.diagnostics()->snapshot().value().operations.size() == 2);
    scene.close();
}

/** @brief 定时刷新卡片期间重入关闭，窗口必须保留到本轮控件写入返回。 */
void homeRefreshReentry()
{
    Scene scene;
    auto card = scene.find<ElaToggleSwitch>("CordisPluginPower");
    scene.shell.management()->attach(scene.kernel).value();
    bool entered = false, retained = false;
    std::optional<Task<void>> closing;
    /** @brief 观察刷新触发的启用变化，关闭可重入但不能提前销毁当前控件写入栈。 */
    struct EnabledProbe final : QObject {
        std::function<void()> callback;
        /** @brief 开关快照同步屏蔽 toggled，仅通过真实 Qt 状态事件触发重入探针。 */
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::EnabledChange)
                callback();
            return QObject::eventFilter(watched, event);
        }
    } probe;
    probe.callback = [&] {
        if (entered)
            return;
        entered = true;
        closing = scene.shell.close();
        QEventLoop nested;
        QTimer::singleShot(60, &nested, &QEventLoop::quit);
        nested.exec();
        retained = scene.window && !closing->isReady();
    };
    card->installEventFilter(&probe);
    QTest::qWait(750);
    CHECK(entered && retained && closing);
    wait(*closing);
    scene.close();
}

/** @brief 相邻页已开始停止但仍有页签时，关闭当前页应选择有效页或空画布。 */
void tabFallback()
{
    Scene scene;
    scene.first->navigate().value();
    scene.second->navigate().value();
    scene.action("CordisOpenHome");
    auto stopping = scene.instance->stop();
    scene.action("CordisCloseView");
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    CHECK(tabs->count() == 0 || tabs->tabText(tabs->currentIndex()) != QString::fromUtf8("概览"));
    CHECK(scene.find<QWidget>("ElaEmptyWorkbench")->isVisible());
    wait(stopping);
    scene.close();
}

/** @brief 文本规范化及模型复位回调重入查询时，回车必须使用当前输入的结果。 */
void searchNormalization()
{
    auto parent = std::make_unique<QWidget>();
    auto search = new ElaSuggestBox(parent.get());
    auto edit = search->findChild<QLineEdit*>();
    CHECK(edit);
    search->addSuggestion("Alpha");
    search->addSuggestion("Beta");
    parent->show();
    QString selected;
    QObject::connect(search, &ElaSuggestBox::suggestionClicked, parent.get(),
                     [&](const ElaSuggestBox::SuggestData& data) { selected = data.getSuggestText(); });
    auto normalizer = QObject::connect(edit, &QLineEdit::textChanged, parent.get(), [&](const QString& text) {
        if (text == "Alpha")
            search->setSearchText("Beta");
    });
    search->setSearchText("Alpha");
    QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection);
    CHECK(search->getSearchText() == "Beta" && selected == "Beta");
    QObject::disconnect(normalizer);
    auto list = parent->findChild<QListView*>();
    CHECK(list);
    CHECK(list->model()->rowCount(list->model()->index(0, 0)) == 0);
    bool redirected = false;
    QObject::connect(list->model(), &QAbstractItemModel::modelAboutToBeReset, parent.get(), [&] {
        if (!redirected) {
            redirected = true;
            search->setSearchText("Beta");
        }
    });
    search->setSearchText("Alpha");
    QMetaObject::invokeMethod(edit, "returnPressed", Qt::DirectConnection);
    CHECK(redirected && search->getSearchText() == "Beta" && selected == "Beta");
}

/** @brief 检查卡片内的每个控件都能完整显示，不能用压缩文字高度来迁就固定卡片高度。 */
void checkCardLayout(QLayout* layout, QWidget* card)
{
    for (int i = 0; i < layout->count(); ++i) {
        auto item = layout->itemAt(i);
        if (auto child = item->layout())
            checkCardLayout(child, card);
        if (auto widget = item->widget()) {
            const auto required = std::max(widget->minimumSizeHint().height(), widget->heightForWidth(widget->width()));
            CHECK(widget->height() >= required);
            CHECK(card->rect().contains(QRect(widget->mapTo(card, QPoint()), widget->size())));
        }
    }
}

/** @brief 覆盖欢迎/设置/关于页的明暗主题、宽窄窗口和滚动溢出，输出可复核截图。 */
void pageLayout()
{
    Scene scene;
    bool scrolled = false;
    for (bool dark : {false, true}) {
        eTheme->setThemeMode(dark ? ElaThemeType::Dark : ElaThemeType::Light);
        for (const auto& entry : {std::make_pair("CordisOpenHome", "CordisWelcomePage"),
                                  std::make_pair("CordisOpenSettings", "CordisSettingsPage"),
                                  std::make_pair("CordisOpenAbout", "CordisAboutPage")}) {
            scene.action(entry.first);
            auto page = scene.find<QWidget>(entry.second);
            for (auto size : {QSize(1200, 800), QSize(1000, 520), QSize(1880, 900)}) {
                scene.window->resize(size);
                QTest::qWait(30);
                if (size == QSize(1200, 800)) {
                    const auto path = QCoreApplication::applicationDirPath() + "/" + QString::fromLatin1(entry.second) +
                                      (dark ? "-dark-" : "-light-") +
                                      QString::number(qRound(scene.window->devicePixelRatioF() * 100)) + ".png";
                    CHECK(scene.window->grab().save(path));
                }
                const auto cards = page->findChildren<ElaScrollPageArea*>();
                CHECK(!cards.isEmpty() || page->objectName() == "CordisWelcomePage");
                for (auto card : cards) {
                    CHECK(card->layout());
                    checkCardLayout(card->layout(), card);
                }
                auto scroll = page->findChild<QScrollArea*>();
                CHECK(scroll && scroll->widget());
                scrolled = scrolled || scroll->verticalScrollBar()->maximum() > 0;
                if (scroll->widget()->height() > scroll->viewport()->height())
                    CHECK(scroll->verticalScrollBar()->maximum() > 0);
            }
        }
    }
    CHECK(scrolled);
    scene.close();
}

/** @brief 按场景隔离进程，测试真实 Qt/Ela 而非模拟布局。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        CHECK(argc == 2);
        const std::string name = argv[1];
#ifdef Q_OS_WIN
        // offscreen 不枚举系统字体，布局检查必须加载与演示相同的真实中文字体。
        if ((name == "layout" || name == "empty") && QApplication::platformName() == "offscreen")
            CHECK(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc") >= 0);
#endif
        if (name == "history")
            history();
        else if (name == "tabs")
            tabs();
        else if (name == "search")
            search();
        else if (name == "menus")
            menus();
        else if (name == "isolation")
            isolation();
        else if (name == "navigation_reentry")
            navigationReentry();
        else if (name == "suggestion_reentry")
            suggestionReentry();
        else if (name == "layout")
            pageLayout();
        else if (name == "empty")
            emptyWorkspace();
        else if (name == "activation")
            directActivation();
        else if (name == "cards")
            homeCards();
        else if (name == "power")
            homePower();
        else if (name == "power_busy")
            homePowerBusy();
        else if (name == "home_refresh_reentry")
            homeRefreshReentry();
        else if (name == "tab_fallback")
            tabFallback();
        else if (name == "search_normalization")
            searchNormalization();
        else if (name == "card_area_lifetime")
            cardAreaLifetime();
        else
            return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
