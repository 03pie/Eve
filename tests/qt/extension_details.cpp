/** Exercise the real Ela home cards, details window, package review, and desktop picker. */
#include <eve/ui/ela_shell.hpp>
#include <eve/ui/extensions.hpp>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <stdexcept>

using namespace eve;
using namespace eve::ui;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void expect(const char* phase, const std::function<bool()>& done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 4000) QTest::qWait(5);
    require(done(), phase);
}

template <class T> T* child(QWidget* root, const char* id)
{
    auto widget = root->findChild<T*>(QString::fromLatin1(id));
    require(widget != nullptr, id);
    return widget;
}

QWidget* card(QWidget* home, const QString& moduleId)
{
    for (auto candidate : home->findChildren<QWidget*>("EvePluginCard"))
        if (candidate->property("EveExtensionId").toString() == moduleId && candidate->isVisible())
            return candidate;
    return nullptr;
}

QWidget* visibleDetails()
{
    for (auto top : QApplication::topLevelWidgets())
        if (top->objectName() == "EveExtensionDialog" && top->isVisible()) return top;
    return nullptr;
}

double luminance(const QColor& color)
{
    const auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

void checkTabText(QWidget* dialog, QLabel* label, bool dark)
{
    require(label->isVisible(), "extension tab text visible");
    const QRect bounds(label->mapTo(dialog, QPoint{}), label->size());
    const auto frame = dialog->grab();
    const auto scale = frame.devicePixelRatioF();
    const auto image = frame.toImage().copy(QRect(bounds.topLeft() * scale, bounds.size() * scale));
    require(!image.isNull(), "render extension tab text");
    // Both fixture captions are short and left-aligned. The far right shows the actual
    // scroll area behind the glyphs, including any fill introduced by the native widget.
    const auto background = luminance(image.pixelColor(image.width() - 2, image.height() / 2));
    int readablePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const auto ink = luminance(image.pixelColor(x, y));
            const auto contrast = dark ? (ink + 0.05) / (background + 0.05) : (background + 0.05) / (ink + 0.05);
            if (contrast >= 4.5 && ++readablePixels >= 8) return;
        }
    }
    throw std::runtime_error(QStringLiteral("%1 has insufficient rendered contrast in %2 theme, background %3")
        .arg(label->objectName(), dark ? QStringLiteral("dark") : QStringLiteral("light"),
             image.pixelColor(image.width() - 2, image.height() / 2).name(QColor::HexArgb)).toStdString());
}

void chooseFile(const QString& path)
{
    QTimer::singleShot(0, [path] {
        if (auto dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            dialog->selectFile(path);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        }
    });
}

void askToUninstall(QWidget* dialog)
{
    const auto menu = child<QPushButton>(dialog, "EveExtensionMore")->menu();
    require(menu != nullptr, "management actions menu");
    for (auto action : menu->actions()) {
        if (action->text().contains(QStringLiteral("卸载"))) { action->trigger(); return; }
    }
    throw std::runtime_error("Uninstall action is missing.");
}
}

void extensionDetails()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
    if (QApplication::platformName() == "offscreen")
        require(QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc") >= 0,
                "system font for offscreen glyph rendering");
#endif
    auto scheduler = std::make_shared<qt::QtScheduler>();
    ElaShell shell(scheduler);
    auto controls = std::make_shared<ExtensionControls>();
    ExtensionItem desktop;
    desktop.id = "org.eve.desktop";
    desktop.name = "Ela";
    desktop.kind = ExtensionKind::Desktop;
    desktop.builtin = desktop.current = true;
    desktop.version = "1.0";
    desktop.publisher = "Eve";
    ExtensionItem business;
    business.id = "org.test.editor";
    business.name = "Editor";
    business.pluginId = "sample.editor";
    business.description = "A local editor";
    business.packageHash = "installed-hash";
    business.version = "1.0";
    business.publisher = "Test publisher";
    business.actions = {ExtensionAction::ShowLogs, ExtensionAction::Uninstall};
    std::vector<ExtensionItem> inventory{desktop};
    controls->list = [&] { return Result<std::vector<ExtensionItem>>::success(inventory); };
    QTemporaryDir temporary;
    const auto path = temporary.filePath("editor.evepkg");
    QFile file(path);
    require(file.open(QFile::WriteOnly), "test file");
    file.close();
    Promise<ExtensionPreview> abandoned, reviewed;
    int previews = 0, installations = 0, removals = 0;
    controls->preview = [&](QString selected) {
        require(selected == path, "chosen path");
        return ++previews == 1 ? abandoned.task() : reviewed.task();
    };
    controls->apply = [&](ExtensionAction action, QString id, QString selected, QString hash) {
        require(id == business.id, "selected identity");
        if (action == ExtensionAction::Install) {
            require(selected == path && hash == "reviewed-hash", "reviewed package");
            ++installations;
            inventory.push_back(business);
        } else {
            require(action == ExtensionAction::Uninstall && hash == "installed-hash", "reviewed uninstall identity");
            ++removals;
            inventory.pop_back();
        }
        return completedTask();
    };
    shell.bindExtensions(controls).value();
    shell.show().value();
    QWidget* window = nullptr;
    for (auto top : QApplication::topLevelWidgets())
        if (qobject_cast<ElaWindow*>(top) && top->findChild<QAction*>("EveOpenExtensions")) window = top;
    require(window != nullptr, "extension shortcut");
    auto home = child<QWidget>(window, "EveWelcomePage");
    auto tabs = child<QTabBar>(window, "EvePageTabs");
    window->activateWindow();
    expect("activate workbench", [&] { return window->isActiveWindow(); });
    child<QAction>(window, "EveOpenSettings")->trigger();
    expect("left home before shortcut", [&] { return !home->isVisible(); });
    QTest::keyClick(window, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    expect("shortcut returns to home cards", [&] { return card(home, desktop.id) != nullptr; });
    require(!window->findChild<QWidget*>("EveExtensionsPage") && !visibleDetails(), "no extensions navigation node");
    const auto homeTabs = tabs->count();
    auto commands = child<QListView>(window, "EveCommandList");
    require(!commands->model()->match(commands->model()->index(0, 0), Qt::DisplayRole, QStringLiteral("从扩展安装包安装"), 1,
                                     Qt::MatchContains).isEmpty(), "install action in palette");

    chooseFile(path);
    child<QAction>(window, "EveInstallExtension")->trigger();
    expect("pending package review", [&] { return previews == 1 && visibleDetails(); });
    QTest::keyClick(visibleDetails(), Qt::Key_Escape);
    expect("dismiss pending review", [] { return !visibleDetails(); });
    QTest::mouseClick(card(home, desktop.id), Qt::LeftButton);
    expect("card opens desktop details", [] { return visibleDetails() != nullptr; });
    auto dialog = visibleDetails();
    auto title = child<QLabel>(dialog, "EveExtensionTitle");
    require(title->text() == desktop.name, "desktop identity");
    abandoned.resolve({business, path, "stale-hash"});
    // Process the actual scheduled callback after returning to a different extension.
    auto barrier = completedTask().thenOn(scheduler, [](const Result<void>&) {});
    expect("abandoned review callback delivered", [&] { return barrier.isReady(); });
    require(title->text() == desktop.name && installations == 0, "late preview did not displace selected desktop");
    dialog->close();
    expect("desktop details dismissed", [] { return !visibleDetails(); });

    chooseFile(path);
    child<QPushButton>(home, "EveHomeInstallExtension")->click();
    expect("home shares the preview action", [&] { return previews == 2 && visibleDetails(); });
    dialog = visibleDetails();
    reviewed.resolve({business, path, "reviewed-hash"});
    auto primary = child<QPushButton>(dialog, "EveExtensionPrimary");
    expect("preview ready", [&] { return primary->isVisible() && primary->text() == QStringLiteral("确认安装") && primary->isEnabled(); });
    auto trust = child<QLabel>(dialog, "EveExtensionPackageReview");
    require(trust->isVisible() && trust->text().contains("reviewed-hash") && installations == 0, "digest before install");
    primary->click();
    expect("installed extension joins original cards", [&] { return installations == 1 && card(home, business.id); });
    if (visibleDetails()) visibleDetails()->close();
    expect("return to home after installation", [] { return !visibleDetails(); });

    auto editorCard = card(home, business.id);
    window->activateWindow();
    expect("return focus to workbench", [&] { return window->isActiveWindow(); });
    editorCard->setFocus(Qt::TabFocusReason);
    expect("keyboard focus on extension card", [&] { return editorCard->hasFocus(); });
    QTest::keyClick(editorCard, Qt::Key_Return);
    expect("card keyboard activation", [] { return visibleDetails() != nullptr; });
    dialog = visibleDetails();
    require(child<QLabel>(dialog, "EveExtensionTitle")->text() == business.name, "business title");
    require(child<QLabel>(dialog, "EveExtensionDescription")->text().contains(business.description), "plugin introduction");
    require(child<QLabel>(dialog, "EveExtensionMetadata")->text().contains(business.publisher), "plugin publisher");
    auto sections = child<QTabWidget>(dialog, "EveExtensionTabs");
    require(sections->currentIndex() == 0, "introduction shown first");
    for (const auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode);
        auto bar = sections->tabBar();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
        checkTabText(dialog, child<QLabel>(dialog, "EveExtensionDescription"), mode == ElaThemeType::Dark);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(1).center());
        checkTabText(dialog, child<QLabel>(dialog, "EveExtensionWorkspace"), mode == ElaThemeType::Dark);
    }
    auto logs = child<QPushButton>(dialog, "EveExtensionShowLogs");
    require(logs->isVisible() && logs->isEnabled(), "plugin log action");
    logs->click();
    expect("log action exposes selected source", [&] {
        return !visibleDetails() && child<QComboBox>(window, "EveOutputTarget")->currentData().toString() == "plugin/sample.editor";
    });

    QTest::mouseClick(card(home, business.id), Qt::LeftButton);
    expect("reopen card", [] { return visibleDetails() != nullptr; });
    dialog = visibleDetails();
    askToUninstall(dialog);
    primary = child<QPushButton>(dialog, "EveExtensionPrimary");
    require(primary->text() == QStringLiteral("确认卸载") && removals == 0, "uninstall requires confirmation");
    child<QPushButton>(dialog, "EveExtensionCancel")->click();
    require(removals == 0 && card(home, business.id), "cancel keeps installed card");
    askToUninstall(dialog);
    primary->click();
    expect("uninstall refreshes home", [&] { return removals == 1 && !card(home, business.id); });
    if (visibleDetails()) visibleDetails()->close();
    require(tabs->count() == homeTabs && home->isVisible(), "details do not leave navigation tabs");

    child<QAction>(window, "EveMenuBack")->trigger();
    auto choose = child<QPushButton>(window, "EveSettingsChooseDesktop");
    expect("back returns straight to settings", [&] { return choose->isVisible(); });
    bool picked = false;
    QTimer::singleShot(0, [&] {
        auto menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || menu->objectName() != "EveSettingsDesktopMenu") return;
        for (auto action : menu->actions()) {
            if (action->data().toString() != desktop.id) continue;
            picked = true;
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
            break;
        }
    });
    choose->click();
    expect("desktop picked from native menu", [&] { return picked && visibleDetails(); });
    require(child<QLabel>(visibleDetails(), "EveExtensionTitle")->text() == desktop.name && choose->isVisible(),
            "settings opens selected desktop in place");
    auto closed = shell.close();
    expect("shell closes its details", [&] { return closed.isReady() && !visibleDetails(); });
    closed.tryResult()->value();
    scheduler->close().value();
}
