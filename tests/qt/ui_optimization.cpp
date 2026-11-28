/** @file ui_optimization.cpp
 * @brief 真实 Ela 窗口验证偏好隔离/失败恢复、键盘开关和类型化状态呈现。
 */
#include <ElaComboBox.h>
#include <ElaAppBar.h>
#include <ElaLineEdit.h>
#include <ElaMenu.h>
#include <ElaMenuBar.h>
#include <ElaPlainTextEdit.h>
#include <ElaSpinBox.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaWindow.h>
#include <QAction>
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>
#include <QFileDialog>
#include <QImage>
#include <QListView>
#include <QScreen>
#include <QGuiApplication>
#include <ElaApplication.h>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <eve/ui/ela_shell.hpp>
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

using namespace eve;
using namespace eve::ui;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
#ifdef Q_OS_WIN
#include "windows_ime.hpp"
#endif

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
    explicit Scene(const QString& path, bool attach = true, bool background = false)
        : shell(scheduler, path.toUtf8().toStdString())
    {
        if (attach)
            shell.management()->attach(kernel).value();
        for (auto widget : QApplication::topLevelWidgets())
            if (auto root = qobject_cast<ElaWindow*>(widget))
                window = root;
        CHECK(window && kernel->snapshot().value().instances.empty());
        if (background)
            window->setAttribute(Qt::WA_ShowWithoutActivating);
        shell.show().value();
    }
    /** @brief 使用宿主稳定名称查找实际 Ela 控件。 */
    template <class T> T* find(const char* name) const
    {
        auto value = window->findChild<T*>(QString::fromLatin1(name));
        CHECK(value);
        return value;
    }
    /** @brief 设置通过真实 QAction 导航进入。 */
    void settings() { find<QAction>("EveOpenSettings")->trigger(); }
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
        scene.find<ElaComboBox>("EveSettingsTheme")->setCurrentIndex(1);
        scene.find<ElaComboBox>("EveSettingsNavigationMode")->setCurrentIndex(1);
        scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->setValue(310);
        scene.find<ElaSpinBox>("EveSettingsPanelHeight")->setValue(230);
        scene.find<QAction>("EveViewPanel")->trigger();
        CHECK(!scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
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
        CHECK(scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
        scene.settings();
        CHECK(scene.find<ElaSpinBox>("EveSettingsPanelHeight")->value() == 230);
        scene.find<QPushButton>("EveSettingsReset")->click();
        CHECK(scene.window->getNavigationBarWidth() == 240);
        CHECK(eTheme->getThemeMode() == ElaThemeType::Light);
        scene.close();
    }
    CHECK(read(path).value("navigationWidth").toInt() == 240);

    // 用原有 v1 文件验证两个端点，再通过控件尝试越界写入。
    struct DimensionCase {
        QSize loaded, requested, persisted;
    };
    const DimensionCase cases[] = {{{180, 480}, {361, 119}, {360, 120}},
                                   {{360, 120}, {179, 481}, {180, 480}}};
    for (const auto& dimensions : cases) {
        saved["navigationWidth"] = dimensions.loaded.width();
        saved["panelHeight"] = dimensions.loaded.height();
        write(path, QJsonDocument(saved).toJson());
        Scene scene(path);
        scene.settings();
        auto* widthControl = scene.find<ElaSpinBox>("EveSettingsNavigationWidth");
        auto* heightControl = scene.find<ElaSpinBox>("EveSettingsPanelHeight");
        CHECK(widthControl->value() == dimensions.loaded.width() && heightControl->value() == dimensions.loaded.height());
        CHECK(widthControl->minimum() == 180 && widthControl->maximum() == 360);
        CHECK(heightControl->minimum() == 120 && heightControl->maximum() == 480);
        widthControl->setValue(dimensions.requested.width());
        heightControl->setValue(dimensions.requested.height());
        scene.close();
        const auto clamped = read(path);
        CHECK(clamped.value("navigationWidth").toInt() == dimensions.persisted.width());
        CHECK(clamped.value("panelHeight").toInt() == dimensions.persisted.height());
    }
}

/** @brief 损坏/未知版本/越界/错误类型不进入恢复，也不在无操作时覆盖文件。 */
void invalid()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("ui.json");
    for (const auto& bytes :
         {QByteArray("invalid"), QByteArray("{\"version\":2}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":9999,\"panelHeight\":160}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":179,\"panelHeight\":480}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":361,\"panelHeight\":120}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":180,\"panelHeight\":481}"),
          QByteArray("{\"version\":1,\"theme\":0,\"navigationMode\":2,\"navigationWidth\":360,\"panelHeight\":119}"),
          QByteArray(
              "{\"version\":1,\"theme\":true,\"navigationMode\":2,\"navigationWidth\":240,\"panelHeight\":160}")}) {
        write(path, bytes);
        Scene scene(path);
        scene.settings();
        CHECK(scene.find<ElaText>("EvePreferencesStatus")->text().contains(QString::fromUtf8("无效")));
        CHECK(scene.window->getNavigationBarWidth() == 240);
        CHECK(scene.find<ElaSpinBox>("EveSettingsPanelHeight")->value() == 160);
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
    scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->setValue(280);
    until(
        [&] { return scene.find<ElaText>("EvePreferencesStatus")->text().contains(QString::fromUtf8("保存失败")); });
    CHECK(scene.window->getNavigationBarWidth() == 280 && !QFile::exists(path));
    CHECK(QFile::remove(parent));
    scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->setValue(290);
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
    scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->setValue(300);
    until(
        [&] { return scene.find<ElaText>("EvePreferencesStatus")->text().contains(QString::fromUtf8("保存失败")); });
    CHECK(read(path).value("navigationWidth").toInt() == 240);
    CHECK(CloseHandle(handle));
    scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->setValue(320);
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
    auto toggle = scene.find<ElaToggleSwitch>("EveSettingsPanel");
    auto panel = scene.find<QTabWidget>("EveWorkbenchPanel");
    CHECK(toggle->focusPolicy() == Qt::StrongFocus && !toggle->accessibleName().isEmpty());
    auto accessible = QAccessible::queryAccessibleInterface(toggle);
    CHECK(accessible && accessible->role() == QAccessible::CheckBox && accessible->state().checkable);
    CHECK(accessible->text(QAccessible::Name) == toggle->accessibleName());
    auto actions = accessible->actionInterface();
    CHECK(actions && actions->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    toggle->setFocus(Qt::TabFocusReason);
    QTest::keyClick(toggle, Qt::Key_Space);
    until([&] { return !panel->isHidden(); });
    CHECK(accessible->state().checked);
    auto target = scene.find<QComboBox>("EveOutputTarget");
    auto source = QAccessible::queryAccessibleInterface(target);
    CHECK(source && source->role() == QAccessible::ComboBox && !source->text(QAccessible::Name).isEmpty());
    CHECK(source->text(QAccessible::Value) == target->currentText());
    auto followButton = scene.find<QAbstractButton>("EveOutputFollow");
    auto follow = QAccessible::queryAccessibleInterface(followButton);
    CHECK(follow && follow->state().checkable && follow->state().checked == followButton->isChecked());
    CHECK(!follow->text(QAccessible::Name).isEmpty());
    auto bell = QAccessible::queryAccessibleInterface(scene.find<QToolButton>("EveShowNotifications"));
    CHECK(bell && bell->role() == QAccessible::Button && bell->text(QAccessible::Name).contains(QStringLiteral("通知")));
    QTest::keyClick(toggle, Qt::Key_Return);
    until([&] { return panel->isHidden(); });
    CHECK(!accessible->state().checked);
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
    CHECK(accessible->state().disabled);
    actions->doAction(QAccessibleActionInterface::toggleAction());
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
    scene.find<ElaComboBox>("EveSettingsTheme")->setCurrentIndex(1);
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
    CHECK(scene.find<ElaText>("EveHomeEmpty")->isVisible());
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
    auto table = scene.find<ElaTableView>("EveInstanceTable");
    CHECK(table->model()->rowCount() == 3);
    for (int row = 0; row < 3; ++row) {
        CHECK(!table->model()->index(row, 2).data(Qt::DecorationRole).value<QIcon>().isNull());
        CHECK(!table->model()->index(row, 2).data(Qt::AccessibleDescriptionRole).toString().isEmpty());
    }
    until([&] { return scene.window->findChildren<QWidget*>("EvePluginCard").size() == 3; });
    CHECK(!scene.find<ElaText>("EveHomeEmpty")->isVisible());
    const auto originalCards = scene.window->findChildren<QWidget*>("EvePluginCard");
    for (auto card : scene.window->findChildren<QWidget*>("EvePluginCard")) {
        CHECK(card->findChild<ElaText*>("EvePluginStateIcon"));
        CHECK(card->font().family() == QApplication::font().family());
    }
    CHECK(QApplication::font().family() == "Microsoft YaHei");
    eTheme->setThemeMode(ElaThemeType::Dark);
    const auto refreshedCards = scene.window->findChildren<QWidget*>("EvePluginCard");
    CHECK(originalCards.size() == refreshedCards.size());
    for (int index = 0; index < originalCards.size(); ++index)
        CHECK(originalCards[index] == refreshedCards[index]);
    scene.window->resize(1000, 600);
    QCoreApplication::processEvents();
    scene.shell.management()->refresh().value();
    CHECK(scene.find<ElaText>("EveNotification")->height() <= 24);
    CHECK(scene.window->statusBar()->height() <= 40);
    scene.find<ElaComboBox>("EveOutputTarget")->setCurrentIndex(0);
    CHECK(
        scene.find<ElaPlainTextEdit>("EveWorkbenchOutput")->toPlainText().contains(QString::fromStdString(notice)));
    CHECK(scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
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
    until([&] { return scene.window->findChildren<QWidget*>("EvePluginCard").size() == 1; });
    for (auto theme : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeMode(theme);
        for (const auto& action : {"EveOpenHome", "EveOpenPlugins", "EveOpenSettings", "EveOpenAbout"}) {
            scene.find<QAction>(action)->trigger();
            scene.window->resize(1200, 800);
            QTest::qWait(40);
            const auto path = QCoreApplication::applicationDirPath() + "/ui-special-" + QString::fromLatin1(action) +
                              (theme == ElaThemeType::Light ? "-light-" : "-dark-") +
                              QString::number(qRound(scene.window->devicePixelRatioF() * 100)) + ".png";
            CHECK(scene.window->grab().save(path));
        }
        for (const auto size : {QSize(560, 600), QSize(1000, 600), QSize(1880, 900)}) {
            scene.window->resize(size);
            QTest::qWait(20);
            const auto contained = [&](QWidget* widget) {
                return scene.window->rect().contains(QRect(widget->mapTo(scene.window, QPoint()), widget->size()));
            };
            auto menu = scene.find<ElaMenuBar>("EveMenuBar");
            auto overflow = scene.find<QToolButton>("EveMenuOverflow");
            auto appBar = scene.find<ElaAppBar>("ElaAppBar");
            const auto aligned = [appBar](QWidget* widget) {
                return qAbs(widget->mapTo(appBar, widget->rect().center()).y() - appBar->rect().center().y()) <= 1;
            };
            CHECK(aligned(scene.find<QLabel>("ElaAppBarIcon")));
            int windowButtons = 0;
            for (const auto button : appBar->findChildren<QAbstractButton*>(QString(), Qt::FindDirectChildrenOnly)) {
                if (!button->isVisible()) continue;
                CHECK(aligned(button));
                ++windowButtons;
            }
            CHECK(windowButtons == 3);
            CHECK(menu->isVisible() != overflow->isVisible());
            if (menu->isVisible())
                CHECK(contained(menu) && aligned(menu) && menu->rect().contains(menu->actionGeometry(menu->actions().back())));
            else
                CHECK(contained(overflow) && aligned(overflow));
            auto center = scene.find<QToolButton>("EveCommandCenterButton");
            CHECK(center->isVisible() && contained(center) && aligned(center));
            for (const auto name : {"EveNavigateBack", "EveNavigateForward", "EveTitleNavigation", "EveTogglePanel"}) {
                auto button = scene.find<QToolButton>(name);
                if (button->isVisible())
                    CHECK(contained(button) && aligned(button));
            }
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
        auto search = scene.find<ElaLineEdit>("EveSettingsSearch");
        search->setText("layout");
        capture("settings-filter");
        CHECK(scene.find<QWidget>("EveSettingsLayoutSection")->isVisible());
        search->setText("no-such-setting");
        capture("settings-empty");
        CHECK(scene.find<QWidget>("EveSettingsSearchEmpty")->isVisible());
        search->clear();
        scene.find<QAction>("EveOpenOutput")->trigger();
        auto panel = scene.find<QTabWidget>("EveWorkbenchPanel");
        auto output = scene.find<ElaPlainTextEdit>("EveWorkbenchOutput");
        output->setPlainText(QString::fromUtf8("工作台输出\n通知不会自动展开此面板。\n复制与清空仅作用于当前输出。"));
        panel->setCurrentWidget(output);
        capture("output-tools");
        for (auto name : {"EveOutputMore", "EveHidePanel"}) {
            auto button = scene.find<QWidget>(name);
            CHECK(button->isVisible());
            CHECK(panel->rect().contains(QRect(button->mapTo(panel, QPoint()), button->size())));
        }
        scene.find<QToolButton>("EveHidePanel")->click();
        auto tabs = scene.find<QTabBar>("EvePageTabs");
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
    auto tabs = scene.find<QTabBar>("EvePageTabs");
    QCoreApplication::processEvents();
    const auto point = tabs->tabRect(index).center();
    CHECK(QMetaObject::invokeMethod(tabs, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, point)));
    auto menu = qobject_cast<ElaMenu*>(QApplication::activePopupWidget());
    CHECK(menu && menu->objectName() == "EveTabContextMenu");
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
    for (auto name : {"EveOpenSettings", "EveOpenAbout"})
        scene.find<QAction>(name)->trigger();
    auto tabs = scene.find<QTabBar>("EvePageTabs");
    CHECK(tabs->count() == 3);
    auto settings = scene.find<QWidget>("EveSettingsPage");
    auto width = scene.find<ElaSpinBox>("EveSettingsNavigationWidth");
    width->setValue(280);
    tabAction(tabMenu(scene, 1), "EveTabCloseRight");
    CHECK(tabs->count() == 2 && settings->isVisible());
    scene.find<QAction>("EveOpenAbout")->trigger();
    tabAction(tabMenu(scene, 1), "EveTabCloseOthers");
    CHECK(tabs->count() == 1 && settings->isVisible() && width->value() == 280);
    tabAction(tabMenu(scene, 0), "EveTabCloseAll");
    CHECK(tabs->count() == 0 && scene.find<QWidget>("ElaEmptyWorkbench")->isVisible());
    scene.settings();
    CHECK(scene.find<QWidget>("EveSettingsPage") == settings && width->value() == 280);
    CHECK(scene.kernel->snapshot().value().instances.empty());
    scene.close();
}
/** @brief 打开菜单后的新页不被旧快照关闭；锚点撤销及新菜单使旧操作失效。 */
void tabSnapshot()
{
    Scene scene({});
    scene.settings();
    auto tabs = scene.find<QTabBar>("EvePageTabs");
    auto menu = tabMenu(scene, 1);
    scene.find<QAction>("EveOpenAbout")->trigger();
    tabAction(menu, "EveTabCloseAll");
    CHECK(tabs->count() == 1 && tabs->tabText(0) == QString::fromUtf8("关于 Eve"));
    scene.settings();
    menu = tabMenu(scene, 1);
    scene.find<QAction>("EveCloseView")->trigger();
    tabAction(menu, "EveTabCloseOthers");
    CHECK(tabs->count() == 1);
    scene.settings();
    QPointer<QAction> stale(tabMenu(scene, 0)->findChild<QAction*>("EveTabCloseAll"));
    menu = tabMenu(scene, 1);
    if (stale)
        stale->trigger();
    CHECK(tabs->count() == 2);
    menu->close();
    menu = tabMenu(scene, 0);
    scene.find<QAction>("EveCloseView")->trigger();
    scene.settings();
    tabAction(menu, "EveTabCloseAll");
    CHECK(tabs->count() == 1 && tabs->tabText(0) == QString::fromUtf8("设置"));
    menu = tabMenu(scene, 0);
    scene.find<QAction>("EveCloseView")->trigger();
    scene.settings();
    tabAction(menu, "EveTabCloseAll");
    CHECK(tabs->count() == 1);
    scene.close();
}
/** @brief 原生页切换的 Hide 回调重入关闭，菜单操作不能提前删除窗口。 */
void tabReentry()
{
    Scene scene({});
    scene.settings();
    scene.find<QAction>("EveOpenAbout")->trigger();
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
    scene.find<QWidget>("EveAboutPage")->installEventFilter(&probe);
    tabAction(tabMenu(scene, 2), "EveTabCloseAll");
    CHECK(entered && retained && closing);
    until([&] { return closing->isReady(); });
    scene.close();
}
/** @brief 工具只在输出页出现；复制/清空与菜单共用动作，隐藏面板保持内容。 */
void panelTools()
{
    Scene scene({});
    scene.window->resize(1380,800);
    auto panel = scene.find<QTabWidget>("EveWorkbenchPanel");
    auto output = scene.find<ElaPlainTextEdit>("EveWorkbenchOutput");
    auto copy = scene.find<QToolButton>("EveCopyPanelOutput");
    auto clear = scene.find<QToolButton>("EveClearPanelOutput");
    CHECK(panel->isHidden() && !copy->isVisible() && !clear->isVisible());
    scene.find<QAction>("EveOpenCommands")->trigger();
    CHECK(panel->isHidden() && !copy->isVisible());
    CHECK(scene.find<QWidget>("EveCommandPanel")->isVisible());
    QTest::keyClick(scene.find<QLineEdit>("EveCommandFilter"),Qt::Key_Escape);
    scene.find<QAction>("EveOpenOutput")->trigger();
    panel->setCurrentWidget(output);
    CHECK(!copy->isHidden() && !clear->isHidden());
    scene.shell.writeOutput("line one\nline two").value();
    copy->click();
    CHECK(QApplication::clipboard()->text() == output->toPlainText());
    clear->click();
    CHECK(output->toPlainText().isEmpty());
    scene.shell.writeOutput("kept output").value();
    const auto kept = output->toPlainText();
    scene.find<QToolButton>("EveHidePanel")->click();
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
    auto search = scene.find<ElaLineEdit>("EveSettingsSearch");
    auto appearance = scene.find<QWidget>("EveSettingsAppearanceSection");
    auto layout = scene.find<QWidget>("EveSettingsLayoutSection");
    auto shortcuts = scene.find<QWidget>("EveSettingsShortcutsSection");
    const auto width = scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->value();
    search->setText("theme");
    CHECK(!appearance->isHidden() && layout->isHidden() && shortcuts->isHidden());
    search->setText(QString::fromUtf8("宽度"));
    CHECK(appearance->isHidden() && !layout->isHidden());
    search->setText("no-such-setting");
    CHECK(scene.find<ElaText>("EveSettingsSearchEmpty")->isVisible());
    CHECK(QTest::qWaitForWindowExposed(scene.window));
    scene.window->raise();
    scene.window->activateWindow();
    CHECK(QTest::qWaitForWindowActive(scene.window));
    QCoreApplication::processEvents();
    search->setFocus();
    until([&] { return QApplication::activeWindow() == scene.window && QApplication::focusWidget() == search; });
    QTest::keyClick(search, Qt::Key_Escape);
    CHECK(search->text().isEmpty() && !appearance->isHidden() && !layout->isHidden() && !shortcuts->isHidden());
    scene.find<ElaComboBox>("EveSettingsTheme")->setFocus();
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F, Qt::ControlModifier);
    CHECK(QApplication::focusWidget() == search);
    CHECK(scene.find<ElaSpinBox>("EveSettingsNavigationWidth")->value() == width);
    scene.close();
    CHECK(!QFile::exists(path));
}
void captureSettingWindow(ElaWindow* window, const QString& name, bool includeDesktopMaterial = false)
{
    if (!qEnvironmentVariableIsSet("EVE_SETTINGS_EVIDENCE")) return;
    const QDir evidence(qEnvironmentVariable("EVE_SETTINGS_EVIDENCE"));
    CHECK(QApplication::platformName()=="windows" && evidence.mkpath("."));
    window->raise();
    if (includeDesktopMaterial) {
        window->activateWindow();
        CHECK(QTest::qWaitForWindowActive(window));
    }
    QTest::qWait(100);
    auto screen=QGuiApplication::screenAt(window->frameGeometry().center());
    CHECK(screen);
    const auto position = window->mapToGlobal(QPoint{}) - screen->geometry().topLeft();
    const auto screenshot = includeDesktopMaterial
        ? screen->grabWindow(0, position.x(), position.y(), window->width(), window->height())
        : screen->grabWindow(window->winId());
    CHECK(screenshot.save(evidence.filePath(name)));
}
void settingsNavigation()
{
    Scene scene({});
    auto page = scene.find<QWidget>("EveSettingsPage");
    auto nav = scene.find<QListView>("EveSettingsNavigation");
    auto content = page->findChild<QScrollArea*>();
    auto search = scene.find<ElaLineEdit>("EveSettingsSearch");
    CHECK(content && nav->model()->rowCount() == 5);
    CHECK(QTest::qWaitForWindowExposed(scene.window));
    scene.window->raise();
    scene.window->activateWindow();
    CHECK(QTest::qWaitForWindowActive(scene.window));
    CHECK(!page->isVisible());
    QTest::keyClick(scene.window, Qt::Key_Comma, Qt::ControlModifier);
    until([&] { return page->isVisible(); });
    captureSettingWindow(scene.window, "navigation-wide.png");
    CHECK(nav->accessibleName() == QString::fromUtf8("设置导航"));
    nav->setFocus(Qt::TabFocusReason);
    QTest::keyClick(nav, Qt::Key_Down);
    CHECK(nav->currentIndex().row() == 1);
    QTest::keyClick(nav, Qt::Key_Return);
    CHECK(page->findChild<QWidget*>("EveSettingsLayoutSection")->isAncestorOf(QApplication::focusWidget()));
    const auto last = nav->model()->index(4, 0);
    QTest::mouseClick(nav->viewport(), Qt::LeftButton, {}, nav->visualRect(last).center());
    until([&]{ return content->verticalScrollBar()->value() > 0 && nav->currentIndex() == last; });
    content->verticalScrollBar()->setValue(0);
    CHECK(nav->currentIndex().row() == 0);
    search->setText(QString::fromUtf8("面板高度"));
    CHECK(!nav->isRowHidden(1) && nav->isRowHidden(0) && nav->isRowHidden(4));
    search->setFocus();
    until([&] { return QApplication::focusWidget()==search; });
    QTest::keyClick(search, Qt::Key_Return);
    CHECK(scene.find<ElaSpinBox>("EveSettingsPanelHeight")->hasFocus() ||
          scene.find<ElaSpinBox>("EveSettingsPanelHeight")->isAncestorOf(QApplication::focusWidget()));
    search->clear();
    scene.window->resize(700, 640);
    until([&]{ return nav->flow() == QListView::LeftToRight; });
    nav->setCurrentIndex(nav->model()->index(2,0));
    nav->setFocus(Qt::TabFocusReason);
    QTest::keyClick(nav, Qt::Key_Right);
    QTest::keyClick(nav, Qt::Key_Space);
    CHECK(nav->currentIndex().row() == 3 && !nav->isRowHidden(3));
    captureSettingWindow(scene.window, "navigation-narrow.png");
    CHECK(page->findChild<QWidget*>("EveSettingsDeveloperSection")->isAncestorOf(QApplication::focusWidget()));
    scene.close();
}
#ifdef Q_OS_WIN
bool acrylicAllowed(ElaWindow& window);
void verifyAcrylicSurface(ElaWindow& window);
void settingsDirectAcrylic()
{
    CHECK(QApplication::platformName() == "windows");
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const auto path = directory.filePath("appearance.json");
    {
        Scene scene(path);
        if (!acrylicAllowed(*scene.window)) {
            std::cout << "SKIP: desktop currently requires solid surfaces\n";
            scene.close();
            return;
        }
        scene.settings();
        auto material = scene.find<ElaComboBox>("EveSettingsMaterial");
        CHECK(material->currentIndex() == ElaApplicationType::Normal);
        captureSettingWindow(scene.window, "acrylic-standard.png", true);
        material->setCurrentIndex(ElaApplicationType::Acrylic);
        verifyAcrylicSurface(*scene.window);
        captureSettingWindow(scene.window, "acrylic-direct.png", true);
        eTheme->setThemeMode(ElaThemeType::Dark);
        verifyAcrylicSurface(*scene.window);
        material->setCurrentIndex(ElaApplicationType::Normal);
        material->setCurrentIndex(ElaApplicationType::Acrylic);
        verifyAcrylicSurface(*scene.window);
        scene.close();
    }
    CHECK(read(path).value("material").toInt() == ElaApplicationType::Acrylic);
    eApp->setWindowDisplayMode(ElaApplicationType::Normal);
    Scene restored(path);
    restored.settings();
    CHECK(restored.find<ElaComboBox>("EveSettingsMaterial")->currentIndex() == ElaApplicationType::Acrylic);
    verifyAcrylicSurface(*restored.window);
    captureSettingWindow(restored.window, "acrylic-restored.png", true);
    restored.close();
}

void settingsNativeIme()
{
    Scene scene({});
    scene.settings();
    eve::test::verifyNativePinyin(scene.find<QLineEdit>("EveSettingsSearch"));
    captureSettingWindow(scene.window, "settings-ime-commit.png");
    scene.find<QAction>("EveOpenCommands")->trigger();
    auto commandPanel = scene.find<QWidget>("EveCommandPanel");
    CHECK(commandPanel->isVisible());
    eve::test::verifyNativePinyin(scene.find<QLineEdit>("EveCommandFilter"));
    CHECK(commandPanel->isVisible());
    scene.close();
}
void outputSourceSpeech()
{
    CHECK(QApplication::platformName() == "windows");
    Scene scene({});
    scene.shell.writeOutput("native accessibility source", OutputLevel::Info).value();
    scene.find<QAction>("EveOpenOutput")->trigger();
    const auto combo = scene.find<QComboBox>("EveOutputTarget");
    const int platform = combo->findText(QStringLiteral("平台"));
    CHECK(platform >= 0);
    scene.window->raise();
    scene.window->activateWindow();
    CHECK(QTest::qWaitForWindowActive(scene.window));
    combo->setCurrentIndex(platform);
    combo->setFocus();
    QTest::qWait(1200);
    combo->showPopup();
    QTest::qWait(1500);
    combo->hidePopup();
    scene.close();
}
#endif
void auditElaThemeSurfaces(ElaWindow& workbench);
void settingsAppearance()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir dir;
    const auto file = dir.filePath("ui.json"), picture = dir.filePath("壁纸.webp");
    QImage sample(64,32,QImage::Format_RGB32);
    for (int x=0; x<sample.width(); ++x)
        for (int y=0; y<sample.height(); ++y)
            sample.setPixelColor(x,y,QColor(35+3*x, 52+4*y, 225-2*x));
    {
        Scene scene(file);
        auto plugin = scene.kernel->registerPlugin("appearance.fixture", [](Context) { return completedTask(); }).value();
        auto instance = scene.kernel->mount(plugin).value();
        until([&] { return instance.ready().isReady(); });
        scene.shell.management()->refresh().value();
        scene.settings();
        auto tabs = scene.find<QTabBar>("EvePageTabs");
        auto centralPanel = tabs->parentWidget()->parentWidget();
        QImage surface(centralPanel->size(), QImage::Format_ARGB32_Premultiplied);
        surface.fill(Qt::transparent);
        centralPanel->render(&surface, {}, {}, QWidget::DrawWindowBackground);
        const int bottom = surface.height() - 2;
        CHECK(surface.pixelColor(2, bottom).alpha() == 0);
        CHECK(surface.pixelColor(surface.width() - 3, bottom).alpha() == 0);
        CHECK(surface.pixelColor(surface.width() / 2, bottom).alpha() > 0);
        QColor quickOpenFill, cardActionFill;
        const auto buttonColor = [&](QWidget* button, const QPixmap& image) {
            const auto point = button->mapTo(scene.window, QPoint(button->width() - 10, button->height() / 2));
            return image.toImage().pixelColor(point * image.devicePixelRatio());
        };
        for (const auto& path : {dir.filePath("a.png"), dir.filePath("b.jpg"), picture}) {
            const auto extension = QFileInfo(path).suffix();
            auto background = sample;
            if (extension == "png") background.fill(QColor(55, 78, 240));
            if (extension == "jpg") background.fill(QColor(238, 75, 57));
            CHECK(background.save(path));
            scene.find<QToolButton>("EveBackgroundPicker")->click();
            auto dialog = scene.window->findChild<QFileDialog*>();
            CHECK(dialog);
            QPointer<QFileDialog> lifetime(dialog);
            dialog->selectFile(path);
            CHECK(QMetaObject::invokeMethod(dialog, "accept"));
            until([&] { return !lifetime; });
            CHECK(!scene.window->getWindowPixmap(ElaThemeType::Light).isNull());
            if (scene.window->property("EveWallpaperVisible").toBool() && extension != "webp") {
                scene.find<QAction>("EveOpenHome")->trigger();
                auto quick = scene.find<QToolButton>("EveCommandCenterButton");
                auto manage = scene.find<QPushButton>("EvePluginManage");
                until([&] { return quick->isVisible() && manage->isVisible(); });
                const auto image = scene.window->grab();
                const auto quickColor = buttonColor(quick, image);
                const auto manageColor = buttonColor(manage, image);
                if (extension == "png") {
                    quickOpenFill = quickColor;
                    cardActionFill = manageColor;
                } else {
                    CHECK(quickColor.red() > quickOpenFill.red() && quickColor.blue() < quickOpenFill.blue());
                    CHECK(manageColor.red() > cardActionFill.red() && manageColor.blue() < cardActionFill.blue());
                }
                scene.settings();
            }
        }
        until([&]{ return !scene.find<QPushButton>("EveResetBackground")->isHidden(); });
        auto effects = scene.find<ElaComboBox>("EveSettingsTransition");
        const auto settingsKey = scene.window->getCurrentNavigationPageKey();
        for(int mode : {0, 2, 3, 4, 1}) {
            effects->setCurrentIndex(mode);
            scene.find<QPushButton>("EveSettingsTransitionPreview")->click();
            scene.find<QAction>("EveOpenAbout")->trigger();
            CHECK(scene.window->getCurrentNavigationPageKey()!=settingsKey);
            scene.settings();
            CHECK(scene.window->getCurrentNavigationPageKey()==settingsKey);
        }
        scene.find<ElaToggleSwitch>("EveSettingsUserCard")->setIsToggled(false);
        scene.find<ElaToggleSwitch>("EveSettingsDoNotDisturb")->setIsToggled(true);
        for (int theme : {0, 1}) {
            scene.find<ElaComboBox>("EveSettingsTheme")->setCurrentIndex(theme);
            for (int mode : {0, 1, 4, 3, 5, 2}) {
                scene.find<ElaComboBox>("EveSettingsMaterial")->setCurrentIndex(mode);
                CHECK(scene.find<ElaComboBox>("EveSettingsMaterial")->currentIndex()==mode);
                CHECK(!scene.window->getWindowPixmap(theme ? ElaThemeType::Dark : ElaThemeType::Light).isNull());
                const QDir evidence(qEnvironmentVariable("EVE_SETTINGS_EVIDENCE"));
                if (qEnvironmentVariableIsSet("EVE_SETTINGS_EVIDENCE")) {
                    CHECK(QApplication::platformName()=="windows" && evidence.mkpath("."));
                    scene.window->raise();
                    QTest::qWait(350);
                    auto screen = QGuiApplication::screenAt(scene.window->frameGeometry().center());
                    CHECK(screen);
                    const auto image = screen->grabWindow(scene.window->winId());
                    CHECK(!image.isNull() && image.save(evidence.filePath(QString("settings-%1-material-%2.png").arg(theme).arg(mode))));
                    std::cout << "theme=" << theme << " requestedMaterial=" << mode
                              << " appliedMaterial=" << static_cast<int>(eApp->getWindowDisplayMode())
                              << " systemAccepted=" << scene.window->property("EveRequestedMaterialAccepted").toBool()
                              << " nativeSize=" << image.width() << "x" << image.height()
                              << " dpr=" << image.devicePixelRatio() << std::endl;
                }
            }
        }
        CHECK(scene.find<ElaComboBox>("EveSettingsMaterial")->currentIndex() == 2);
        scene.close();
    }
    const auto saved = read(file);
    CHECK(saved.value("background").toString() == picture && saved.value("material").toInt()==2);
    CHECK(saved.value("transition").toInt()==1 && saved.value("userCardVisible").toBool(true)==false);
    {
        Scene scene(file);
        scene.settings();
        CHECK(!scene.find<ElaToggleSwitch>("EveSettingsUserCard")->getIsToggled());
        CHECK(!scene.find<QPushButton>("EveResetBackground")->isHidden());
        CHECK(scene.find<ElaComboBox>("EveSettingsTransition")->currentIndex()==1);
        scene.find<QPushButton>("EveResetBackground")->click();
        CHECK(scene.find<ElaComboBox>("EveSettingsMaterial")->currentIndex()==2);
        scene.find<QPushButton>("EveSettingsReset")->click();
        CHECK(scene.find<ElaComboBox>("EveSettingsMaterial")->currentIndex()==0);
        CHECK(scene.find<ElaToggleSwitch>("EveSettingsUserCard")->getIsToggled());
        CHECK(scene.find<ElaToggleSwitch>("EveSettingsDoNotDisturb")->getIsToggled());
        scene.find<QPushButton>("EveSettingsOutput")->click();
        CHECK(!scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
        scene.close();
    }
    CHECK(!read(file).contains("background") && read(file).value("doNotDisturb").toBool());
}
/** @brief 检索的可见性回调修改输入后收敛；重入关闭等待当前卡片遍历结束。 */
void settingsReentry()
{
    Scene scene({});
    scene.settings();
    auto search = scene.find<ElaLineEdit>("EveSettingsSearch");
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
    scene.find<QWidget>("EveSettingsLayoutSection")->installEventFilter(&normalize);
    search->setText("theme");
    until([&] { return !scene.find<QWidget>("EveSettingsLayoutSection")->isHidden(); });
    CHECK(normalized && scene.find<QWidget>("EveSettingsAppearanceSection")->isHidden());
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
    scene.find<QWidget>("EveSettingsAppearanceSection")->installEventFilter(&close);
    search->setText("layout");
    CHECK(entered && retained && closing);
    until([&] { return closing->isReady(); });
    scene.close();
}
/** @brief 通知只在显式点击时进入历史，无 Kernel 绑定时仍能显示纯值历史。 */
void notifications()
{
    Scene scene({}, false);
    auto bell = scene.find<QToolButton>("EveShowNotifications");
    auto center = scene.find<QWidget>("EveNotificationCenter");
    auto list = scene.find<QListView>("EveNotificationList");
    auto empty = scene.find<QLabel>("EveNotificationEmpty");
    until([&] { return scene.window->isActiveWindow(); });
    bell->click();
    CHECK(empty->isVisible() && !list->isVisible() && list->model()->rowCount() == 0);
    CHECK(!scene.find<QPushButton>("EveNotificationMarkRead")->isEnabled() &&
          !scene.find<QPushButton>("EveNotificationOpenLog")->isEnabled());
    until([&] { return scene.find<QPushButton>("EveNotificationClose")->hasFocus(); });
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    CHECK(!center->isVisible());
    scene.settings();
    auto search = scene.find<QLineEdit>("EveSettingsSearch");
    search->setText(QStringLiteral("主题"));
    search->setFocus();
    search->selectAll();
    const int pageCount = scene.find<QTabBar>("EvePageTabs")->count();
    std::optional<Context> activeOwner;
    auto runtime =
        scene.kernel
            ->registerPlugin(
                "notifier",
                [&](Context context) {
                    activeOwner = context;
                    for (int i = 0; i < 10; ++i)
                        scene.shell.notifications()
                            ->post(context, {NotificationLevel::Warning, "literal <b>warning</b>", {}})
                            .value();
                    scene.shell.notifications()->post(context, {NotificationLevel::Error, "second error", {}}).value();
                    return completedTask();
                })
            .value();
    auto instance = scene.kernel->mount(runtime).value();
    until([&] { return instance.ready().isReady(); });
    CHECK(scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
    CHECK(scene.find<QTabBar>("EvePageTabs")->count() == pageCount);
    CHECK(search->hasFocus() && search->selectedText() == QStringLiteral("主题"));
    scene.find<QToolButton>("EveShowNotifications")->click();
    CHECK(center->isVisible() && !center->isWindow() && list->isVisible() && !empty->isVisible() &&
          list->model()->rowCount() == 2);
    for (auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode);
        const auto view = list->viewport();
        const auto sample = view->mapTo(center, QPoint(view->width() / 2, view->height() - 10));
        const auto image = center->grab();
        const auto color = image.toImage().pixelColor(sample * image.devicePixelRatio());
        CHECK(mode == ElaThemeType::Dark ? color.lightness() < 100 : color.lightness() > 180);
    }
    const auto warning = list->model()->index(1, 0).data().toString();
    CHECK(warning.contains("literal <b>warning</b>") && warning.contains(QStringLiteral("×10")));
    CHECK(list->model()->index(0, 0).data().toString().contains(QStringLiteral("错误")));
    CHECK(scene.window->grab().save(QCoreApplication::applicationDirPath() + "/ui-special-notification-center.png"));
    CHECK(scene.find<QTabWidget>("EveWorkbenchPanel")->isHidden());
    auto markRead = center->findChild<QPushButton*>("EveNotificationMarkRead");
    CHECK(markRead && markRead->isEnabled());
    markRead->click();
    CHECK(!markRead->isEnabled() && !list->model()->index(0, 0).data().toString().contains(QStringLiteral("未读")));
    QTest::keyClick(list, Qt::Key_Escape);
    CHECK(!center->isVisible() && search->hasFocus() && search->selectedText() == QStringLiteral("主题"));
    scene.find<ElaToggleSwitch>("EveSettingsDoNotDisturb")->setIsToggled(true);
    const auto bars = scene.window->findChildren<QWidget*>("ElaMessageBar").size();
    scene.shell.notifications()->post(*activeOwner, {NotificationLevel::Warning, "muted warning", {}}).value();
    const auto status = scene.find<QLabel>("EveNotification");
    CHECK(status->toolTip() == status->text() && status->text().contains("muted warning"));
    CHECK(scene.window->findChildren<QWidget*>("ElaMessageBar").size() == bars && !center->isVisible());
    scene.find<QToolButton>("EveShowNotifications")->click();
    CHECK(list->model()->index(0, 0).data().toString().contains("muted warning"));
    scene.find<QPushButton>("EveNotificationOpenLog")->click();
    CHECK(!center->isVisible() && scene.find<QTabWidget>("EveWorkbenchPanel")->isVisible());
    scene.close();
}

void notificationArchiveWorker(const QString& workspace, bool seed)
{
    Scene scene(QDir(workspace).filePath(seed ? "seed-preferences.json" : "reader-preferences.json"));
    scene.shell.startOutputArchive(workspace.toStdString(), "notification-qa").value();
    if (seed) {
        scene.settings();
        scene.find<ElaToggleSwitch>("EveSettingsDoNotDisturb")->setIsToggled(true);
        const auto runtime =
            scene.kernel
                ->registerPlugin("history-notifier",
                                 [&](Context ctx) {
                                     scene.shell.notifications()
                                         ->post(ctx, {NotificationLevel::Warning, "archived-read", OperationId{17}})
                                         .value();
                                     scene.shell.notifications()
                                         ->post(ctx, {NotificationLevel::Error, "archived-unread", OperationId{18}})
                                         .value();
                                     scene.shell.output()
                                         ->write(ctx, "通知", "channel-is-not-a-notification", OutputLevel::Error)
                                         .value();
                                     return completedTask();
                                 })
                .value();
        auto instance = scene.kernel->mount(runtime).value();
        until([&] { return instance.ready().isReady(); });
    }
    scene.find<QToolButton>("EveShowNotifications")->click();
    auto list = scene.find<QListView>("EveNotificationList");
    until([&] { return list->model()->rowCount() == 2; });
    CHECK(list->model()->index(1, 0).data().toString().contains("archived-read"));
    CHECK(list->model()->index(0, 0).data().toString().contains("archived-unread"));
    if (!seed) {
        CHECK(!list->model()->index(1, 0).data().toString().contains(QStringLiteral("未读")));
        CHECK(list->model()->index(0, 0).data().toString().contains(QStringLiteral("未读")));
        CHECK(scene.window->findChildren<QWidget*>("ElaMessageBar").isEmpty());
        CHECK(scene.shell.notifications()->snapshot().value().empty());
    }
    list->setCurrentIndex(list->model()->index(seed ? 1 : 0, 0));
    const auto run = list->currentIndex().data(Qt::UserRole + 3).toString();
    scene.find<QPushButton>("EveNotificationOpenLog")->click();
    CHECK(scene.find<QComboBox>("EveOutputRun")->currentData().toString() == run);
    CHECK(scene.find<QPlainTextEdit>("EveWorkbenchOutput")->textCursor().selectedText().contains(
        seed ? "archived-read" : "archived-unread"));
    if (!seed)
        CHECK(scene.find<QComboBox>("EveOutputRun")->currentIndex() != 0);
    scene.close();
}
void notificationArchive()
{
    QTemporaryDir workspace;
    CHECK(workspace.isValid());
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("EVE_QA_NOTICE_WORKSPACE", workspace.path());
    for (const auto name : {"notification_archive_seed", "notification_archive_read"}) {
        QProcess worker;
        worker.setProcessEnvironment(env);
        worker.setProcessChannelMode(QProcess::ForwardedChannels);
        worker.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(name)});
        CHECK(worker.waitForStarted() && worker.waitForFinished(30000) && worker.exitStatus() == QProcess::NormalExit &&
              worker.exitCode() == 0);
    }
}
void notificationArchiveFault()
{
    QTemporaryDir workspace;
    CHECK(workspace.isValid());
    QStandardPaths::setTestModeEnabled(true);
    write(workspace.filePath(".eve"), "test fixture blocks the project log directory");
    const auto key =
        QCryptographicHash::hash(QFileInfo(workspace.path()).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256)
            .toHex();
    QDir logs(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                  .filePath("logs/" + QString::fromLatin1(key)));
    CHECK(!logs.exists());
    Scene scene(workspace.filePath("preferences.json"));
    scene.shell.startOutputArchive(workspace.path().toStdString(), "notification-qa").value();
    const auto runs = logs.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    CHECK(runs.size() == 1);
    scene.shell.writeOutput("trigger archive fault", OutputLevel::Info).value();
    auto bell = scene.find<QToolButton>("EveShowNotifications");
    until([&] { return bell->accessibleName().contains(QStringLiteral("1 条未读")); });
    auto bars = scene.window->findChildren<QWidget*>("ElaMessageBar");
    CHECK(bars.size() == (scene.window->property("EveReducedMotion").toBool() ? 0 : 1));
    bell->click();
    auto status = scene.find<QLabel>("EveNotificationArchiveStatus");
    CHECK(status->isVisible() && status->text().contains(QStringLiteral("已改存到")));
    scene.find<QPushButton>("EveNotificationMarkRead")->click();
    CHECK(bell->accessibleName().contains(QStringLiteral("0 条未读")));
    scene.shell.writeOutput("still usable", OutputLevel::Info).value();
    QTest::qWait(550);
    CHECK(status->isVisible() && bell->accessibleName().contains(QStringLiteral("0 条未读")));
    CHECK(scene.window->findChildren<QWidget*>("ElaMessageBar").size() <= 1);
    scene.close();
    QDir run(logs.filePath(runs.first()));
    for (const auto& entry : run.entryList(QDir::Files))
        CHECK(run.remove(entry));
    CHECK(logs.rmdir(runs.first()));
    CHECK(QDir().rmdir(logs.absolutePath()));
}
#ifdef Q_OS_WIN
bool systemAnimation()
{
    BOOL enabled = FALSE;
    CHECK(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0));
    return enabled != FALSE;
}
// Manual Windows run only. Both the guard and the parent harness restore the session value.
struct RestoreClientAnimation {
    bool original = systemAnimation();
    bool changed = false;
    static bool set(bool enabled)
    {
        return SystemParametersInfoW(SPI_SETCLIENTAREAANIMATION, 0,
                                     reinterpret_cast<void*>(static_cast<ULONG_PTR>(enabled)), 0) != FALSE;
    }
    void disable()
    {
        CHECK(original && set(false));
        changed = true;
        CHECK(!systemAnimation());
    }
    void restore()
    {
        CHECK(set(original) && systemAnimation() == original);
        changed = false;
    }
    ~RestoreClientAnimation()
    {
        if (changed) {
            BOOL restored = FALSE;
            if (!set(original) || !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &restored, 0) ||
                (restored != FALSE) != original)
                std::cerr << "Native cleanup could not confirm system animation recovery.\n";
        }
    }
};
void observeSystemSurfaces(ElaWindow& window, const QString& settingsPath, const QString& imagePath,
                           const QString& isolatedRestriction);
void settingsSystemSurfaces(const QString& isolatedRestriction = {})
{
    CHECK(QApplication::platformName() == "windows");
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    QImage pattern(144, 96, QImage::Format_RGB32);
    for (int y = 0; y < pattern.height(); ++y)
        for (int x = 0; x < pattern.width(); ++x)
            pattern.setPixel(x, y, ((x + y) / 32) % 2 ? qRgb(0, 74, 139) : qRgb(22, 174, 143));
    const auto imagePath = temporary.filePath("surface-test.png");
    CHECK(pattern.save(imagePath));
    const auto path = temporary.filePath("preferences.json");
    const QJsonObject values{{"version", 1}, {"theme", 0}, {"navigationMode", 2}, {"navigationWidth", 240},
                             {"panelHeight", 160}, {"material", 2}, {"background", imagePath}};
    write(path, QJsonDocument(values).toJson());
    Scene scene(path, true, true);
    scene.settings();
    until([&] { return scene.find<QComboBox>("EveSettingsMaterial")->isVisible(); });
    observeSystemSurfaces(*scene.window, path, imagePath, isolatedRestriction);
    scene.close();
}
void settingsSystemAnimation()
{
    CHECK(QApplication::platformName() == "windows");
    RestoreClientAnimation system;
    CHECK(system.original);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const auto file = dir.filePath("preferences.json");
    Scene scene(file, true, true);
    scene.settings();
    auto transition = scene.find<QComboBox>("EveSettingsTransition");
    auto preview = scene.find<QPushButton>("EveSettingsTransitionPreview");
    const auto hwnd = reinterpret_cast<HWND>(scene.window->winId());
    const auto key = scene.window->getCurrentNavigationPageKey();
    transition->setCurrentIndex(static_cast<int>(ElaWindowType::Popup));
    until([&] { return read(file).value("transition").toInt() == ElaWindowType::Popup; });

    std::optional<Context> owner;
    const auto definition = scene.kernel
                                ->registerPlugin("motion-test",
                                                 [&](Context ctx) {
                                                     owner = ctx;
                                                     return completedTask();
                                                 })
                                .value();
    auto instance = scene.kernel->mount(definition).value();
    until([&] { return instance.ready().isReady(); });
    const auto post = [&](const char* message) {
        scene.shell.notifications()->post(*owner, {NotificationLevel::Warning, message, {}}).value();
    };
    const auto barCount = [&] {
        int count = 0;
        for (const auto bar : scene.window->findChildren<QWidget*>("ElaMessageBar", Qt::FindDirectChildrenOnly))
            count += bar->isVisible();
        return count;
    };
    const auto playing = [&] {
        for (const auto animation : scene.window->findChildren<QPropertyAnimation*>())
            if (animation->propertyName() == "pPopupAnimationYOffset" &&
                animation->state() == QAbstractAnimation::Running)
                return true;
        return false;
    };
    const auto state = [&] {
        return QJsonObject{{"systemAnimation", systemAnimation()},
                           {"effectiveTransition", static_cast<int>(scene.window->getStackSwitchMode())},
                           {"selectedTransition", transition->currentIndex()},
                           {"savedTransition", read(file).value("transition")},
                           {"previewEnabled", preview->isEnabled()},
                           {"previewPlaying", playing()},
                           {"settingsVisible", transition->isVisible()},
                           {"visibleNoticeBars", barCount()},
                           {"noticeCount", static_cast<int>(scene.shell.notifications()->snapshot().value().size())}};
    };
    const auto notifyWindow = [&] {
        DWORD_PTR result = 0;
        CHECK(SendMessageTimeoutW(hwnd, WM_SETTINGCHANGE, SPI_SETCLIENTAREAANIMATION, 0,
                                  SMTO_ABORTIFHUNG, 2000, &result));
    };
    post("before");
    CHECK(barCount() == 1 && preview->isEnabled());
    preview->click();
    CHECK(playing());
    const auto before = state();
    system.disable();
    notifyWindow();
    until([&] {
        return scene.window->getStackSwitchMode() == ElaWindowType::None && !playing() && transition->isVisible() &&
               !preview->isEnabled();
    });
    post("during");
    CHECK(barCount() == 0 && scene.find<QLabel>("EveNotification")->text().contains("during"));
    CHECK(scene.window->getCurrentNavigationPageKey() == key && transition->currentIndex() == ElaWindowType::Popup);
    const auto during = state();
    system.restore();
    notifyWindow();
    until([&] { return scene.window->getStackSwitchMode() == ElaWindowType::Popup && preview->isEnabled(); });
    post("after");
    CHECK(barCount() == 1 && scene.window->getCurrentNavigationPageKey() == key);
    const auto after = state();
    CHECK(after.value("noticeCount").toInt() == 3 &&
          during.value("savedTransition") == before.value("savedTransition") &&
          after.value("savedTransition") == before.value("savedTransition"));
    scene.close();
    const QJsonObject result{{"hwnd", QString::number(reinterpret_cast<quintptr>(hwnd))},
                             {"before", before},
                             {"during", during},
                             {"after", after}};
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << std::endl;
}
#endif
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
        else if (name == "settings_navigation")
            settingsNavigation();
        else if (name == "settings_appearance")
            settingsAppearance();
        else if (name == "theme_surfaces") {
            Scene scene({}, true, true);
            scene.settings();
            auditElaThemeSurfaces(*scene.window);
            scene.close();
        }
#ifdef Q_OS_WIN
        else if (name == "settings_direct_acrylic")
            settingsDirectAcrylic();
        else if (name == "settings_native_ime")
            settingsNativeIme();
        else if (name == "output_source_speech")
            outputSourceSpeech();
        else if (name == "settings_system_animation")
            settingsSystemAnimation();
        else if (name == "settings_system_surfaces")
            settingsSystemSurfaces();
        else if (name == "settings_policy_contrast")
            settingsSystemSurfaces(QStringLiteral("contrast"));
        else if (name == "settings_policy_transparency")
            settingsSystemSurfaces(QStringLiteral("transparency"));
        else if (name == "settings_private_hive_transparency")
            settingsSystemSurfaces(QStringLiteral("private-hive"));
#endif
        else if (name == "notifications")
            notifications();
        else if (name == "notification_archive")
            notificationArchive();
        else if (name == "notification_archive_fault")
            notificationArchiveFault();
        else if (name == "notification_archive_seed" || name == "notification_archive_read")
            notificationArchiveWorker(qEnvironmentVariable("EVE_QA_NOTICE_WORKSPACE"),name=="notification_archive_seed");
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
