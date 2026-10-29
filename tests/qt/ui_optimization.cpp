/** @file ui_optimization.cpp
 * @brief 真实 Ela 窗口验证偏好隔离/失败恢复、键盘开关和类型化状态呈现。
 */
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaSpinBox.h>
#include <ElaSuggestBox.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cordis/ui/ela_shell.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

using namespace cordis;
using namespace cordis::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

/** @brief 超时为故障保护，等待条件来自任务和真实磁盘结果。 */
void until(const std::function<bool()>& condition)
{
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    deadline.start(5000);
    if (!condition())
        loop.exec();
    CHECK(condition());
}
/** @brief 写入测试专用路径，所有失败均为断言。 */
void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
}
/** @brief 获取实际 JSON，不把文件存在当作偏好内容正确。 */
QJsonObject read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
/** @brief 每个场景显式指定偏好路径，不访问本机用户设置。 */
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell;
    QPointer<ElaWindow> window;
    /** @brief 只挂载外壳，偏好恢复不能创建业务实例。 */
    explicit Scene(const QString& path, bool attach = true) : shell(scheduler, path.toUtf8().toStdString())
    {
        if (attach)
            shell.management()->attach(kernel).value();
        shell.show().value();
        for (auto widget : QApplication::topLevelWidgets())
            if (auto root = qobject_cast<ElaWindow*>(widget))
                window = root;
        CHECK(window && kernel->snapshot().value().instances.empty());
    }
    /** @brief 使用宿主稳定名称查找实际 Ela 控件。 */
    template <class T> T* find(const char* name) const
    {
        auto value = window->findChild<T*>(QString::fromLatin1(name));
        CHECK(value);
        return value;
    }
    /** @brief 设置通过真实 QAction 导航进入。 */
    void settings() { find<QAction>("CordisOpenSettings")->trigger(); }
    /** @brief 关闭必须经过 Kernel 和窗口的完整屏障，写盘不能改变实例账目。 */
    void close()
    {
        auto stopped = kernel->close();
        until([&] { return stopped.isReady(); });
        CHECK(stopped.tryResult()->hasValue() && stopped.tryResult()->value().quiescent);
        auto closed = shell.close();
        until([&] { return closed.isReady(); });
        CHECK(closed.tryResult()->hasValue() && !window);
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};

/** @brief 多次打开/关闭真实窗口，偏好恢复不自动打开底部或创建插件。 */
void roundtrip()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const auto path = dir.filePath("settings/ui.json");
    {
        Scene scene(path);
        CHECK(!QFile::exists(path));
        scene.settings();
        scene.find<ElaComboBox>("CordisSettingsTheme")->setCurrentIndex(1);
        scene.find<ElaComboBox>("CordisSettingsNavigationMode")->setCurrentIndex(1);
        scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->setValue(310);
        scene.find<ElaSpinBox>("CordisSettingsPanelHeight")->setValue(230);
        scene.find<QAction>("CordisViewPanel")->trigger();
        CHECK(!scene.find<QTabWidget>("CordisWorkbenchPanel")->isHidden());
        scene.close(); // debounce 尚未完成也必须落盘。
    }
    auto saved = read(path);
    CHECK(saved.value("version").toInt() == 1 && saved.value("theme").toInt() == 1);
    CHECK(saved.value("navigationWidth").toInt() == 310 && saved.value("panelHeight").toInt() == 230);
    CHECK(saved.size() == 5); // 只保存界面值，不保存实例或页签。
    eTheme->setThemeMode(ElaThemeType::Light);
    {
        Scene scene(path);
        CHECK(eTheme->getThemeMode() == ElaThemeType::Dark);
        CHECK(scene.window->getNavigationBarWidth() == 310);
        CHECK(scene.window->getNavigationBarDisplayMode() == ElaNavigationType::Maximal);
        CHECK(scene.find<QTabWidget>("CordisWorkbenchPanel")->isHidden());
        scene.settings();
        CHECK(scene.find<ElaSpinBox>("CordisSettingsPanelHeight")->value() == 230);
        scene.find<QPushButton>("CordisSettingsReset")->click();
        CHECK(scene.window->getNavigationBarWidth() == 240);
        CHECK(eTheme->getThemeMode() == ElaThemeType::Light);
        scene.close();
    }
    CHECK(read(path).value("navigationWidth").toInt() == 240);
}

/** @brief 损坏/未知版本/越界/错误类型不进入恢复，也不在无操作时覆盖文件。 */
void invalid()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("ui.json");
    for (const auto& bytes :
         {QByteArray("invalid"), QByteArray("{\"version\":2}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":9999,\"panelHeight\":160}"),
          QByteArray(
              "{\"version\":1,\"theme\":true,\"navigationMode\":2,\"navigationWidth\":240,\"panelHeight\":160}")}) {
        write(path, bytes);
        Scene scene(path);
        scene.settings();
        CHECK(scene.find<ElaText>("CordisPreferencesStatus")->text().contains(QString::fromUtf8("无效")));
        CHECK(scene.window->getNavigationBarWidth() == 240);
        scene.close();
        QFile file(path);
        CHECK(file.open(QIODevice::ReadOnly) && file.readAll() == bytes);
    }
}

/** @brief 路径父项是真实文件，原子保存失败后仍能继续修改并在解除故障后保存。 */
void failure()
{
    QTemporaryDir dir;
    const auto parent = dir.filePath("blocked");
    write(parent, "keep");
    const auto path = parent + "/ui.json";
    Scene scene(path);
    scene.settings();
    scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->setValue(280);
    until(
        [&] { return scene.find<ElaText>("CordisPreferencesStatus")->text().contains(QString::fromUtf8("保存失败")); });
    CHECK(scene.window->getNavigationBarWidth() == 280 && !QFile::exists(path));
    CHECK(QFile::remove(parent));
    scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->setValue(290);
    until([&] { return read(path).value("navigationWidth").toInt() == 290; });
    CHECK(scene.kernel->snapshot().value().instances.empty());
    scene.close();
}

#ifdef Q_OS_WIN
/** @brief Windows 原生句柄拒绝替换时旧偏好完整保留，解除后能恢复原子保存。 */
void locked()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("ui.json");
    const QByteArray original(
        "{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":240,\"panelHeight\":160}");
    write(path, original);
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(handle != INVALID_HANDLE_VALUE);
    Scene scene(path);
    scene.settings();
    scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->setValue(300);
    until(
        [&] { return scene.find<ElaText>("CordisPreferencesStatus")->text().contains(QString::fromUtf8("保存失败")); });
    CHECK(read(path).value("navigationWidth").toInt() == 240);
    CHECK(CloseHandle(handle));
    scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->setValue(320);
    until([&] { return read(path).value("navigationWidth").toInt() == 320; });
    scene.close();
}
#endif

/** @brief 两个入口用不同文件，关闭会话模式也不创建默认文件。 */
void isolation()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("a.json"), b = dir.filePath("b.json");
    {
        Scene scene(a);
        scene.window->setNavigationBarWidth(280);
        scene.close();
    }
    {
        Scene scene(b);
        CHECK(scene.window->getNavigationBarWidth() == 240);
        scene.window->setNavigationBarWidth(320);
        scene.close();
    }
    CHECK(read(a).value("navigationWidth").toInt() == 280 && read(b).value("navigationWidth").toInt() == 320);
    {
        Scene scene({});
        scene.window->setNavigationBarWidth(300);
        scene.close();
    }
}

/** @brief Space/Enter 直接驱动 Ela 开关；禁用与自动重复不能产生额外操作。 */
void keyboard()
{
    Scene scene({});
    scene.settings();
    auto toggle = scene.find<ElaToggleSwitch>("CordisSettingsPanel");
    auto panel = scene.find<QTabWidget>("CordisWorkbenchPanel");
    CHECK(toggle->focusPolicy() == Qt::StrongFocus && !toggle->accessibleName().isEmpty());
    toggle->setFocus(Qt::TabFocusReason);
    QTest::keyClick(toggle, Qt::Key_Space);
    until([&] { return !panel->isHidden(); });
    QTest::keyClick(toggle, Qt::Key_Return);
    until([&] { return panel->isHidden(); });
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
    QApplication::sendEvent(toggle, &repeat);
    CHECK(panel->isHidden());
    scene.window->activateWindow();
    until([&] { return scene.window->isActiveWindow(); });
    toggle->setFocus(Qt::TabFocusReason);
    CHECK(QApplication::focusWidget() == toggle);
    QTest::keyClick(toggle, Qt::Key_Tab);
    auto nextFocus = QApplication::focusWidget();
    CHECK(nextFocus && nextFocus != toggle);
    QTest::keyClick(nextFocus, Qt::Key_Backtab);
    CHECK(QApplication::focusWidget() == toggle);
    toggle->setEnabled(false);
    QTest::keyClick(toggle, Qt::Key_Space);
    CHECK(panel->isHidden());
    scene.close();
}

/** @brief 设置变更的主题信号重入关闭并泵送 Qt，窗口必须保留至设置调用返回。 */
void reentry()
{
    Scene scene({});
    scene.settings();
    bool entered = false, retained = false;
    std::optional<Task<void>> closing;
    auto callback = QObject::connect(eTheme, &ElaTheme::themeModeChanged, qApp, [&](ElaThemeType::ThemeMode) {
        if (entered)
            return;
        entered = true;
        closing.emplace(scene.shell.close());
        QEventLoop loop;
        QTimer::singleShot(40, &loop, &QEventLoop::quit);
        loop.exec();
        retained = scene.window && !closing->isReady();
    });
    scene.find<ElaComboBox>("CordisSettingsTheme")->setCurrentIndex(1);
    CHECK(entered && retained && closing);
    QObject::disconnect(callback);
    until([&] { return closing->isReady(); });
    CHECK(closing->tryResult()->hasValue());
    scene.close();
}

/** @brief 真实失败/等待/运行状态都有 Ela 图标，主题和布局改变不启动业务。 */
void states()
{
    Scene scene({});
    CHECK(scene.find<ElaText>("CordisHomeEmpty")->isVisible());
    const auto notice = std::string(300, 'x') + "\n" + std::string(300, 'y');
    auto live = scene.kernel
                    ->registerPlugin(
                        "运行插件",
                        [&](Context context) {
                            scene.shell.notifications()->post(context, {NotificationLevel::Info, notice, {}}).value();
                            return completedTask();
                        })
                    .value();
    auto bad =
        scene.kernel
            ->registerPlugin("失败插件",
                             [](Context) { return failedTask(makeError(ErrorCode::ActivationFailed, "expected")); })
            .value();
    auto missing = scene.kernel
                       ->registerPlugin("等待插件", [](Context) { return completedTask(); },
                                        {ServiceRequirement::of<int>({"missing"})})
                       .value();
    auto a = scene.kernel->mount(live).value();
    auto b = scene.kernel->mount(bad).value();
    auto c = scene.kernel->mount(missing).value();
    until([&] {
        return a.ready().isReady() && b.operation().value().target.isReady() && c.operation().value().target.isReady();
    });
    scene.shell.management()->refresh().value();
    auto table = scene.find<ElaTableView>("CordisInstanceTable");
    CHECK(table->model()->rowCount() == 3);
    for (int row = 0; row < 3; ++row) {
        CHECK(!table->model()->index(row, 2).data(Qt::DecorationRole).value<QIcon>().isNull());
        CHECK(!table->model()->index(row, 2).data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    }
    until([&] { return scene.window->findChildren<QWidget*>("CordisPluginCard").size() == 3; });
    CHECK(!scene.find<ElaText>("CordisHomeEmpty")->isVisible());
    const auto originalCards = scene.window->findChildren<QWidget*>("CordisPluginCard");
    for (auto card : scene.window->findChildren<QWidget*>("CordisPluginCard")) {
        CHECK(card->findChild<ElaText*>("CordisPluginStateIcon"));
        CHECK(card->font().family() == QApplication::font().family());
    }
    CHECK(QApplication::font().family() == "Microsoft YaHei");
    eTheme->setThemeMode(ElaThemeType::Dark);
    const auto refreshedCards = scene.window->findChildren<QWidget*>("CordisPluginCard");
    CHECK(originalCards.size() == refreshedCards.size());
    for (int index = 0; index < originalCards.size(); ++index)
        CHECK(originalCards[index] == refreshedCards[index]);
    scene.window->resize(1000, 600);
    QCoreApplication::processEvents();
    scene.shell.management()->refresh().value();
    CHECK(scene.find<ElaText>("CordisNotification")->height() <= 24);
    CHECK(scene.window->statusBar()->height() <= 40);
    scene.find<ElaComboBox>("CordisOutputTarget")->setCurrentIndex(1);
    CHECK(
        scene.find<ElaPlainTextEdit>("CordisWorkbenchOutput")->toPlainText().contains(QString::fromStdString(notice)));
    CHECK(scene.find<QTabWidget>("CordisWorkbenchPanel")->isHidden());
    CHECK(a.snapshot().value().state == InstanceState::Active && b.snapshot().value().state == InstanceState::Failed);
    scene.close();
}

/** @brief 同一真实窗口保存各宿主页的明暗对照，可在 windows 或 offscreen 平台运行。 */
void showcase()
{
    Scene scene({});
    auto live =
        scene.kernel->registerPlugin("长名称插件：中文名称和状态需要完整可读", [](Context) { return completedTask(); })
            .value();
    auto instance = scene.kernel->mount(live).value();
    until([&] { return instance.ready().isReady(); });
    scene.shell.management()->refresh().value();
    until([&] { return scene.window->findChildren<QWidget*>("CordisPluginCard").size() == 1; });
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        for (const auto& action : {"CordisOpenHome", "CordisOpenPlugins", "CordisOpenSettings", "CordisOpenAbout"}) {
            scene.find<QAction>(action)->trigger();
            scene.window->resize(1200, 800);
            QTest::qWait(40);
            const auto path = QCoreApplication::applicationDirPath() + "/ui-special-" + QString::fromLatin1(action) +
                              (theme == ElaThemeType::Light ? "-light-" : "-dark-") +
                              QString::number(qRound(scene.window->devicePixelRatioF() * 100)) + ".png";
            CHECK(scene.window->grab().save(path));
        }
        for (const auto size : {QSize(1000, 600), QSize(1880, 900)}) {
            scene.window->resize(size);
            QTest::qWait(20);
            const auto contained = [&](QWidget* widget) {
                return scene.window->rect().contains(QRect(widget->mapTo(scene.window, QPoint()), widget->size()));
            };
            auto menu = scene.find<ElaMenuBar>("CordisMenuBar");
            CHECK(contained(menu) && menu->rect().contains(menu->actionGeometry(menu->actions().back())));
            CHECK(contained(scene.find<ElaSuggestBox>("CordisPageSearch")));
            CHECK(contained(scene.find<QWidget>("CordisNavigateBack")));
            CHECK(contained(scene.find<QWidget>("CordisNavigateForward")));
        }
        const auto capture = [&](const QString& name) {
            QTest::qWait(40);
            const auto path = QCoreApplication::applicationDirPath() + "/ui-structure-" + name +
                              (theme == ElaThemeType::Light ? "-light-" : "-dark-") +
                              QString::number(qRound(scene.window->devicePixelRatioF() * 100)) + ".png";
            CHECK(scene.window->grab().save(path));
        };
        scene.window->resize(1000, 600);
        scene.settings();
        auto search = scene.find<ElaLineEdit>("CordisSettingsSearch");
        search->setText("layout");
        capture("settings-filter");
        CHECK(scene.find<QWidget>("CordisSettingsLayoutSection")->isVisible());
        search->setText("no-such-setting");
        capture("settings-empty");
        CHECK(scene.find<QWidget>("CordisSettingsSearchEmpty")->isVisible());
        search->clear();
        scene.find<QAction>("CordisOpenCommands")->trigger();
        auto panel = scene.find<QTabWidget>("CordisWorkbenchPanel");
        auto output = scene.find<ElaPlainTextEdit>("CordisWorkbenchOutput");
        output->setPlainText(QString::fromUtf8("工作台输出\n通知不会自动展开此面板。\n复制与清空仅作用于当前输出。"));
        panel->setCurrentWidget(output);
        capture("output-tools");
        for (auto name : {"CordisCopyPanelOutput", "CordisClearPanelOutput", "CordisHidePanel"}) {
            auto button = scene.find<QWidget>(name);
            CHECK(button->isVisible());
            CHECK(panel->rect().contains(QRect(button->mapTo(panel, QPoint()), button->size())));
        }
        scene.find<QToolButton>("CordisHidePanel")->click();
        auto tabs = scene.find<QTabBar>("CordisPageTabs");
        const auto point = tabs->tabRect(0).center();
        CHECK(
            QMetaObject::invokeMethod(tabs, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
        auto popup = qobject_cast<ElaMenu*>(QApplication::activePopupWidget());
        CHECK(popup);
        // ElaMenu 使用 400ms 展开动画，截图等待动画完成，避免将中间帧误判为裁切。
        QTest::qWait(450);
        const auto popupPath = QCoreApplication::applicationDirPath() + "/ui-structure-tab-menu-" +
                               (theme == ElaThemeType::Light ? "light-" : "dark-") +
                               QString::number(qRound(scene.window->devicePixelRatioF() * 100)) + ".png";
        CHECK(popup->grab().save(popupPath));
        popup->close();
    }
    scene.close();
}

/** @brief 对真实页签发出上下文请求，返回有界的原生 Ela 菜单。 */
ElaMenu* tabMenu(Scene& scene, int index)
{
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    QCoreApplication::processEvents();
    const auto point = tabs->tabRect(index).center();
    CHECK(QMetaObject::invokeMethod(tabs, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    auto menu = qobject_cast<ElaMenu*>(QApplication::activePopupWidget());
    CHECK(menu && menu->objectName() == "CordisTabContextMenu");
    return menu;
}
/** @brief 使用菜单的真实动作，关闭弹层不强制处理延迟销毁。 */
void tabAction(ElaMenu* menu, const char* name)
{
    QPointer<ElaMenu> guard(menu);
    auto action = menu->findChild<QAction*>(name);
    CHECK(action && action->isEnabled());
    action->trigger();
    if (guard)
        guard->close();
}
/** @brief 批量关闭仅关闭视图，保留页面内容、稳定身份和空画布语义。 */
void tabContext()
{
    Scene scene({});
    for (auto name : {"CordisOpenSettings", "CordisOpenAbout"})
        scene.find<QAction>(name)->trigger();
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    CHECK(tabs->count() == 3);
    auto settings = scene.find<QWidget>("CordisSettingsPage");
    auto width = scene.find<ElaSpinBox>("CordisSettingsNavigationWidth");
    width->setValue(280);
    tabAction(tabMenu(scene, 1), "CordisTabCloseRight");
    CHECK(tabs->count() == 2 && settings->isVisible());
    scene.find<QAction>("CordisOpenAbout")->trigger();
    tabAction(tabMenu(scene, 1), "CordisTabCloseOthers");
    CHECK(tabs->count() == 1 && settings->isVisible() && width->value() == 280);
    tabAction(tabMenu(scene, 0), "CordisTabCloseAll");
    CHECK(tabs->count() == 0 && scene.find<QWidget>("ElaEmptyWorkbench")->isVisible());
    scene.settings();
    CHECK(scene.find<QWidget>("CordisSettingsPage") == settings && width->value() == 280);
    CHECK(scene.kernel->snapshot().value().instances.empty());
    scene.close();
}
/** @brief 打开菜单后的新页不被旧快照关闭；锚点撤销及新菜单使旧操作失效。 */
void tabSnapshot()
{
    Scene scene({});
    scene.settings();
    auto tabs = scene.find<QTabBar>("CordisPageTabs");
    auto menu = tabMenu(scene, 1);
    scene.find<QAction>("CordisOpenAbout")->trigger();
    tabAction(menu, "CordisTabCloseAll");
    CHECK(tabs->count() == 1 && tabs->tabText(0) == QString::fromUtf8("关于 Cordis"));
    scene.settings();
    menu = tabMenu(scene, 1);
    scene.find<QAction>("CordisCloseView")->trigger();
    tabAction(menu, "CordisTabCloseOthers");
    CHECK(tabs->count() == 1);
    scene.settings();
    QPointer<QAction> stale(tabMenu(scene, 0)->findChild<QAction*>("CordisTabCloseAll"));
    menu = tabMenu(scene, 1);
    if (stale)
        stale->trigger();
    CHECK(tabs->count() == 2);
    menu->close();
    menu = tabMenu(scene, 0);
    scene.find<QAction>("CordisCloseView")->trigger();
    scene.settings();
    tabAction(menu, "CordisTabCloseAll");
    CHECK(tabs->count() == 1 && tabs->tabText(0) == QString::fromUtf8("设置"));
    menu = tabMenu(scene, 0);
    scene.find<QAction>("CordisCloseView")->trigger();
    scene.settings();
    tabAction(menu, "CordisTabCloseAll");
    CHECK(tabs->count() == 1);
    scene.close();
}
/** @brief 原生页切换的 Hide 回调重入关闭，菜单操作不能提前删除窗口。 */
void tabReentry()
{
    Scene scene({});
    scene.settings();
    scene.find<QAction>("CordisOpenAbout")->trigger();
    bool entered = false, retained = false;
    std::optional<Task<void>> closing;
    struct Probe : QObject {
        std::function<void()> callback;
        bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::Hide)
                callback();
            return false;
        }
    } probe;
    probe.callback = [&] {
        if (entered)
            return;
        entered = true;
        closing.emplace(scene.shell.close());
        QEventLoop nested;
        QTimer::singleShot(40, &nested, &QEventLoop::quit);
        nested.exec();
        retained = scene.window && !closing->isReady();
    };
    scene.find<QWidget>("CordisAboutPage")->installEventFilter(&probe);
    tabAction(tabMenu(scene, 2), "CordisTabCloseAll");
    CHECK(entered && retained && closing);
    until([&] { return closing->isReady(); });
    scene.close();
}
/** @brief 工具只在输出页出现；复制/清空与菜单共用动作，隐藏面板保持内容。 */
void panelTools()
{
    Scene scene({});
    auto panel = scene.find<QTabWidget>("CordisWorkbenchPanel");
    auto output = scene.find<ElaPlainTextEdit>("CordisWorkbenchOutput");
    auto copy = scene.find<QToolButton>("CordisCopyPanelOutput");
    auto clear = scene.find<QToolButton>("CordisClearPanelOutput");
    CHECK(panel->isHidden() && copy->isHidden() && clear->isHidden());
    scene.find<QAction>("CordisOpenCommands")->trigger();
    CHECK(!panel->isHidden() && copy->isHidden());
    panel->setCurrentWidget(output);
    CHECK(!copy->isHidden() && !clear->isHidden());
    scene.shell.writeOutput("line one\nline two").value();
    copy->click();
    CHECK(QApplication::clipboard()->text() == output->toPlainText());
    clear->click();
    CHECK(output->toPlainText().isEmpty());
    scene.shell.writeOutput("kept output").value();
    const auto kept = output->toPlainText();
    scene.find<QToolButton>("CordisHidePanel")->click();
    CHECK(panel->isHidden() && output->toPlainText() == kept);
    scene.close();
}
/** @brief 检索只改变显示，保留设置值；Ctrl+F/Escape 可恢复，不产生偏好写入。 */
void settingsSearch()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("preferences.json");
    Scene scene(path);
    scene.settings();
    auto search = scene.find<ElaLineEdit>("CordisSettingsSearch");
    auto appearance = scene.find<QWidget>("CordisSettingsAppearanceSection");
    auto layout = scene.find<QWidget>("CordisSettingsLayoutSection");
    auto shortcuts = scene.find<QWidget>("CordisSettingsShortcutsSection");
    const auto width = scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->value();
    search->setText("theme");
    CHECK(!appearance->isHidden() && layout->isHidden() && shortcuts->isHidden());
    search->setText(QString::fromUtf8("宽度"));
    CHECK(appearance->isHidden() && !layout->isHidden());
    search->setText("no-such-setting");
    CHECK(scene.find<ElaText>("CordisSettingsSearchEmpty")->isVisible());
    CHECK(QTest::qWaitForWindowExposed(scene.window));
    scene.window->raise();
    scene.window->activateWindow();
    CHECK(QTest::qWaitForWindowActive(scene.window));
    QCoreApplication::processEvents();
    search->setFocus();
    until([&] { return QApplication::activeWindow() == scene.window && QApplication::focusWidget() == search; });
    QTest::keyClick(search, Qt::Key_Escape);
    CHECK(search->text().isEmpty() && !appearance->isHidden() && !layout->isHidden() && !shortcuts->isHidden());
    scene.find<ElaComboBox>("CordisSettingsTheme")->setFocus();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F, Qt::ControlModifier);
    CHECK(QApplication::focusWidget() == search);
    CHECK(scene.find<ElaSpinBox>("CordisSettingsNavigationWidth")->value() == width);
    scene.close();
    CHECK(!QFile::exists(path));
}
/** @brief 检索的可见性回调修改输入后收敛；重入关闭等待当前卡片遍历结束。 */
void settingsReentry()
{
    Scene scene({});
    scene.settings();
    auto search = scene.find<ElaLineEdit>("CordisSettingsSearch");
    bool normalized = false, entered = false, retained = false;
    std::optional<Task<void>> closing;
    struct Probe : QObject {
        std::function<void()> callback;
        bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::Hide)
                callback();
            return false;
        }
    } normalize, close;
    normalize.callback = [&] {
        if (!normalized) {
            normalized = true;
            search->setText("layout");
        }
    };
    scene.find<QWidget>("CordisSettingsLayoutSection")->installEventFilter(&normalize);
    search->setText("theme");
    until([&] { return !scene.find<QWidget>("CordisSettingsLayoutSection")->isHidden(); });
    CHECK(normalized && scene.find<QWidget>("CordisSettingsAppearanceSection")->isHidden());
    search->clear();
    close.callback = [&] {
        if (entered)
            return;
        entered = true;
        closing.emplace(scene.shell.close());
        QEventLoop nested;
        QTimer::singleShot(40, &nested, &QEventLoop::quit);
        nested.exec();
        retained = scene.window && !closing->isReady();
    };
    scene.find<QWidget>("CordisSettingsAppearanceSection")->installEventFilter(&close);
    search->setText("layout");
    CHECK(entered && retained && closing);
    until([&] { return closing->isReady(); });
    scene.close();
}
/** @brief 通知只在显式点击时进入历史，无 Kernel 绑定时仍能显示纯值历史。 */
void notifications()
{
    Scene scene({}, false);
    auto runtime =
        scene.kernel
            ->registerPlugin(
                "notifier",
                [&](Context context) {
                    scene.shell.notifications()
                        ->post(context, {NotificationLevel::Warning, "literal <b>warning</b>", {}})
                        .value();
                    scene.shell.notifications()->post(context, {NotificationLevel::Error, "second error", {}}).value();
                    return completedTask();
                })
            .value();
    auto instance = scene.kernel->mount(runtime).value();
    until([&] { return instance.ready().isReady(); });
    CHECK(scene.find<QTabWidget>("CordisWorkbenchPanel")->isHidden());
    CHECK(scene.find<QTabBar>("CordisPageTabs")->count() == 1);
    scene.find<QToolButton>("CordisShowNotifications")->click();
    auto notices = scene.find<ElaPlainTextEdit>("CordisManagementNotices");
    CHECK(scene.find<QTabWidget>("CordisManagementTabs")->currentWidget() == notices);
    CHECK(notices->toPlainText().contains("literal <b>warning</b>") && notices->toPlainText().contains("second error"));
    CHECK(notices->toPlainText().contains(QString::fromUtf8("错误")));
    CHECK(scene.find<QTabWidget>("CordisWorkbenchPanel")->isHidden());
    QTest::qWait(300);
    CHECK(notices->toPlainText().contains("second error"));
    scene.close();
}

/** @brief 每个场景独立应用，字体来自同一 Ela 应用基线。 */
int main(int argc, char** argv)
{
    try {
        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "offscreen")
            CHECK(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc") >= 0);
#endif
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "roundtrip")
            roundtrip();
        else if (name == "invalid")
            invalid();
        else if (name == "failure")
            failure();
        else if (name == "isolation")
            isolation();
        else if (name == "keyboard")
            keyboard();
        else if (name == "states")
            states();
        else if (name == "showcase")
            showcase();
        else if (name == "reentry")
            reentry();
        else if (name == "tab_context")
            tabContext();
        else if (name == "tab_snapshot")
            tabSnapshot();
        else if (name == "tab_reentry")
            tabReentry();
        else if (name == "panel_tools")
            panelTools();
        else if (name == "settings_search")
            settingsSearch();
        else if (name == "settings_reentry")
            settingsReentry();
        else if (name == "notifications")
            notifications();
#ifdef Q_OS_WIN
        else if (name == "locked")
            locked();
#endif
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
