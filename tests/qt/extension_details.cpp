/** Exercise the real Ela home cards, details window, package review, and desktop picker. */
#include <eve/ui/ela_shell.hpp>
#include <eve/ui/extensions.hpp>
#include <ElaTheme.h>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
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

QRect boundsIn(QWidget* dialog, QWidget* widget)
{
    return {widget->mapTo(dialog, QPoint{}), widget->size()};
}

QColor surfacePixel(QWidget* dialog, QWidget* field)
{
    const auto frame = dialog->grab();
    const auto point = field->mapTo(dialog, QPoint(field->width() - 4, field->height() - 5));
    return frame.toImage().pixelColor(point * frame.devicePixelRatioF());
}

void checkTabText(QWidget* dialog, QWidget* widget, QRect textArea, bool dark)
{
    require(widget->isVisible(), "extension tab text visible");
    textArea.translate(boundsIn(dialog, widget).topLeft());
    const auto frame = dialog->grab();
    const auto scale = frame.devicePixelRatioF();
    const auto image = frame.toImage().copy(QRect(textArea.topLeft() * scale, textArea.size() * scale));
    require(!image.isNull(), "render extension tab text");
    // The far right shows the actual surface behind the left-aligned fixture text.
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
        .arg(widget->objectName(), dark ? QStringLiteral("dark") : QStringLiteral("light"),
             image.pixelColor(image.width() - 2, image.height() / 2).name(QColor::HexArgb)).toStdString());
}

void checkReadme(QWidget* dialog, QTextBrowser* reader)
{
    const auto document = reader->document();
    bool heading = false, list = false, anchor = false;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        heading |= block.blockFormat().headingLevel() == 1 && block.text() == QStringLiteral("开始使用");
        list |= block.textList() && block.text() == QStringLiteral("打开工作区。");
        for (auto part = block.begin(); !part.atEnd(); ++part)
            anchor |= part.fragment().charFormat().anchorHref() == QStringLiteral("https://example.invalid/editor");
    }
    require(heading && list && anchor, "README has actual heading, list and link formatting");

    auto sidebar = child<QScrollArea>(dialog, "EveExtensionInstallInfo");
    expect("wide README and narrower installation sidebar share the detail area", [&] {
        const auto readerBounds = boundsIn(dialog, reader);
        const auto sidebarBounds = boundsIn(dialog, sidebar);
        return reader->isVisible() && sidebar->isVisible() && readerBounds.right() < sidebarBounds.left() &&
               readerBounds.width() > sidebarBounds.width() && readerBounds.bottom() > sidebarBounds.top() &&
               sidebarBounds.bottom() > readerBounds.top();
    });
    expect("long README scrolls independently", [&] { return reader->verticalScrollBar()->maximum() > 0; });
    const auto sidebarPosition = sidebar->verticalScrollBar()->value();
    reader->verticalScrollBar()->setValue(reader->verticalScrollBar()->maximum());
    require(sidebar->verticalScrollBar()->value() == sidebarPosition, "reading does not scroll the sidebar");
    reader->verticalScrollBar()->setValue(0);
}

QAction* menuAction(const QMenu* menu, const QString& caption)
{
    if (!menu) return nullptr;
    for (auto action : menu->actions())
        if (action->text().contains(caption)) return action;
    return nullptr;
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
    auto uninstall = child<QPushButton>(dialog, "EveExtensionUninstall");
    require(uninstall->isVisible() && uninstall->isEnabled(), "direct uninstall action");
    QTest::mouseClick(uninstall, Qt::LeftButton);
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
    business.name = QStringLiteral("工作区编辑器");
    business.pluginId = "sample.editor";
    business.description = QStringLiteral("在当前工作区打开和编辑本地文件，按实例管理编辑器配置。");
    business.readme = QStringLiteral(
        "# 开始使用\n\n"
        "编辑器在本地工作区运行。每个实例可以使用不同的配置。\n\n"
        "- 打开工作区。\n"
        "- 启用编辑器实例，选择要编辑的文件。\n\n"
        "## 配置实例\n\n"
        "在 **工作区实例** 中选中目标，点击 **配置实例** 查看配置。"
        "详细选项见[编辑器使用说明](https://example.invalid/editor)。\n\n"
        "## 更新扩展\n\n"
        "展开卸载按钮旁的菜单，选择本地 `.evepkg` 文件。核对版本与文件摘要后确认安装。\n\n"
        "## 运行日志\n\n"
        "从齿轮菜单打开 **查看日志**，可以查看当前插件的输出。\n\n"
        "## 卸载和恢复\n\n"
        "卸载会保留工作区的实例配置。重新安装对应扩展后，可继续使用已有实例。\n\n");
    business.packageHash = "installed-hash";
    business.version = "1.10.0";
    business.changelog = QStringLiteral("# 1.10.0\n\n改进编辑功能。");
    business.defaultConfig = QStringLiteral("{\"autosave\":true}");
    business.categories << QStringLiteral("编辑工具");
    business.repository = "https://example.invalid/source";
    business.license = "MIT";
    business.packageBytes = 3072;
    business.installedAt = QDateTime::currentDateTimeUtc();
    business.publisher = "Test publisher";
    business.state = QStringLiteral("部分实例已启用");
    business.instances = 2;
    business.actions = {ExtensionAction::Enable, ExtensionAction::Disable, ExtensionAction::ShowLogs,
                        ExtensionAction::Install, ExtensionAction::Uninstall};
    std::vector<ExtensionItem> inventory{desktop};
    controls->list = [&] { return Result<std::vector<ExtensionItem>>::success(inventory); };
    QTemporaryDir temporary;
    const auto path = temporary.filePath("editor.evepkg");
    QFile file(path);
    require(file.open(QFile::WriteOnly), "test file");
    file.close();
    Promise<ExtensionPreview> abandoned, reviewed;
    Promise<void> disabled;
    int previews = 0, installations = 0, disables = 0, removals = 0;
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
        } else if (action == ExtensionAction::Disable) {
            require(selected.isEmpty() && hash.isEmpty(), "disable targets current workspace");
            ++disables;
            auto& installed = inventory.back();
            installed.state = QStringLiteral("当前工作区已禁用");
            installed.actions.erase(installed.actions.begin() + 1);
            return disabled.task();
        } else {
            require(action == ExtensionAction::Uninstall, "reviewed uninstall action");
            if (hash != inventory.back().packageHash)
                return failedTask(makeError(ErrorCode::ConfigConflict, "Installed version has changed."));
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
    // Deliver window shortcuts through Qt's platform input path, including on Windows.
    QTest::keyClick(window->windowHandle(), Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    expect("shortcut returns to home cards", [&] { return card(home, desktop.id) != nullptr; });
    require(!window->findChild<QWidget*>("EveExtensionsPage") && !visibleDetails(), "no extensions navigation node");
    const auto homeTabs = tabs->count();
    auto commands = child<QListView>(window, "EveCommandList");
    require(!commands->model()->match(commands->model()->index(0, 0), Qt::DisplayRole, QStringLiteral("从扩展安装包安装"), 1,
                                     Qt::MatchContains).isEmpty(), "install action in palette");

    const auto available = window->screen()->availableGeometry();
    QRect workbenchBounds(QPoint{}, QSize(780, 560).boundedTo(available.size()));
    workbenchBounds.moveCenter(available.center());
    window->setGeometry(workbenchBounds);
    chooseFile(path);
    child<QAction>(window, "EveInstallExtension")->trigger();
    expect("pending package review", [&] { return previews == 1 && visibleDetails(); });
    auto previewWindow = visibleDetails();
    const auto ownerSize = window->frameGeometry().size().boundedTo(available.size());
    const auto expectedSize = QSize(qRound(ownerSize.width() * 0.80), qRound(ownerSize.height() * 0.77));
    require(!previewWindow->isMaximized() &&
            window->frameGeometry().contains(previewWindow->frameGeometry()) &&
            (window->frameGeometry().center() - previewWindow->frameGeometry().center()).manhattanLength() <= 2 &&
            previewWindow->size() == expectedSize,
            qPrintable(QStringLiteral("detail geometry %1,%2 %3x%4, workbench %5,%6 %7x%8, expected %9x%10")
                .arg(previewWindow->frameGeometry().left()).arg(previewWindow->frameGeometry().top())
                .arg(previewWindow->width()).arg(previewWindow->height())
                .arg(window->frameGeometry().left()).arg(window->frameGeometry().top())
                .arg(window->frameGeometry().width()).arg(window->frameGeometry().height())
                .arg(expectedSize.width()).arg(expectedSize.height())));
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
    auto candidate = business;
    candidate.installedAt = {};
    reviewed.resolve({candidate, path, "reviewed-hash"});
    auto primary = child<QPushButton>(dialog, "EveExtensionPrimary");
    expect("preview ready", [&] { return primary->isVisible() && primary->text() == QStringLiteral("确认安装") && primary->isEnabled(); });
    auto trust = child<QLabel>(dialog, "EveExtensionPackageReview");
    require(trust->isVisible() && trust->text().contains("reviewed-hash") && installations == 0, "digest before install");
    require(!child<QPushButton>(dialog, "EveExtensionUninstall")->isVisible() &&
            !child<QPushButton>(dialog, "EveExtensionWorkspaceOptions")->isVisible(),
            "package review shows only confirmation controls");
    primary->click();
    expect("installed extension joins original cards", [&] { return installations == 1 && card(home, business.id); });
    if (visibleDetails()) visibleDetails()->close();
    expect("return to home after installation", [] { return !visibleDetails(); });
    window->resize(window->width() - 50, window->height() - 24);

    auto search = child<QLineEdit>(home, "EveHomeExtensionSearch");
    search->setText("test publisher SAMPLE.EDITOR");
    expect("home search matches publisher and plugin identity", [&] {
        return card(home, business.id) && !card(home, desktop.id);
    });
    search->setText("no matching extension");
    expect("home search shows an empty result", [&] {
        return !card(home, business.id) && child<QLabel>(home, "EveHomeNoMatchingExtensions")->isVisible();
    });
    search->clear();
    expect("clearing search restores all installed cards", [&] {
        return card(home, business.id) && card(home, desktop.id);
    });

    auto editorCard = card(home, business.id);
    window->activateWindow();
    expect("return focus to workbench", [&] { return window->isActiveWindow(); });
    editorCard->setFocus(Qt::TabFocusReason);
    expect("keyboard focus on extension card", [&] { return editorCard->hasFocus(); });
    QTest::keyClick(editorCard, Qt::Key_Return);
    expect("card keyboard activation", [] { return visibleDetails() != nullptr; });
    dialog = visibleDetails();
    const auto detailsWindow = qobject_cast<QDialog*>(dialog);
    require(detailsWindow && detailsWindow->isWindow() && !detailsWindow->isModal() && tabs->count() == homeTabs,
            "plugin information opens in a standalone modeless window");
    const auto resizedOwner = window->frameGeometry().size().boundedTo(available.size());
    require(!dialog->isMaximized() && dialog->width() == qRound(resizedOwner.width() * 0.80) &&
            dialog->width() < expectedSize.width() && dialog->height() <= resizedOwner.height(),
            "reopening uses the resized workbench and leaves room for the stacked layout");
    // Use a wide window for the dual-column and Markdown checks even on Qt's small offscreen screen.
    dialog->resize(QSize(740, dialog->height()).boundedTo(available.size()));
    require(child<QLabel>(dialog, "EveExtensionTitle")->text() == business.name, "business title");
    require(child<QLabel>(dialog, "EveExtensionMetadata")->text().contains(business.publisher), "plugin publisher");
    require(child<QLabel>(dialog, "EveExtensionIdentity")->text().contains(business.id), "installed module identity");
    auto summary = child<QLabel>(dialog, "EveExtensionSummary");
    auto reader = child<QTextBrowser>(dialog, "EveExtensionDescription");
    require(summary->isVisible() && summary->text() == business.description, "plugin summary");
    checkReadme(dialog, reader);
    require(!child<QLabel>(dialog, "EveExtensionPackageBytes")->text().isEmpty() &&
            !child<QLabel>(dialog, "EveExtensionInstalledAt")->text().isEmpty() &&
            child<QLabel>(dialog, "EveExtensionLicense")->text().contains("MIT") &&
            !child<QPushButton>(dialog, "EveExtensionRepository")->isHidden(), "local package facts and declared resources");

    primary = child<QPushButton>(dialog, "EveExtensionPrimary");
    auto uninstall = child<QPushButton>(dialog, "EveExtensionUninstall");
    auto workspaceOptions = child<QPushButton>(dialog, "EveExtensionWorkspaceOptions");
    auto packageOptions = child<QPushButton>(dialog, "EveExtensionPackageOptions");
    auto more = child<QToolButton>(dialog, "EveExtensionMore");
    const auto iconBounds = boundsIn(dialog, child<QLabel>(dialog, "EveExtensionIcon"));
    const auto summaryBounds = boundsIn(dialog, summary);
    const auto primaryBounds = boundsIn(dialog, primary);
    require(iconBounds.right() < summaryBounds.left() && summaryBounds.left() == primaryBounds.left() &&
            summaryBounds.bottom() < primaryBounds.top(), "summary and action row align to the right of the icon");
    require(primary->isVisible() && primary->text() == QStringLiteral("启用") &&
            uninstall->isVisible() && workspaceOptions->isVisible() && packageOptions->isVisible() &&
            more->isVisible() && !more->accessibleName().isEmpty() &&
            menuAction(more->menu(), QStringLiteral("复制模块标识")),
            "workspace actions, uninstall, package options and gear are exposed in the header");
    const auto wideSize = dialog->size();
    auto sidebar = child<QScrollArea>(dialog, "EveExtensionInstallInfo");
    dialog->resize(560, wideSize.height());
    expect("narrow windows stack the installation sidebar after the README", [&] {
        return boundsIn(dialog, reader).bottom() < boundsIn(dialog, sidebar).top();
    });
    dialog->resize(wideSize);
    expect("restoring width places installation information beside the README", [&] {
        return boundsIn(dialog, reader).right() < boundsIn(dialog, sidebar).left();
    });
    dialog->resize(wideSize - QSize(12, 8));
    const auto manualSize = dialog->size();
    auto disable = menuAction(workspaceOptions->menu(), QStringLiteral("停用"));
    require(disable && disable->isEnabled(), "workspace dropdown preserves the alternate action");
    require(menuAction(packageOptions->menu(), QStringLiteral("其他版本")), "package dropdown exposes local version selection");
    disable->trigger();
    require(disables == 1 && !primary->isEnabled() && !uninstall->isEnabled() &&
            !workspaceOptions->isEnabled() && !packageOptions->isEnabled() && !more->isEnabled(),
            "pending action blocks other submissions");
    disabled.resolve();
    expect("workspace action refreshes the available action and state", [&] {
        return primary->isEnabled() && !menuAction(workspaceOptions->menu(), QStringLiteral("停用")) &&
            primary->text() == QStringLiteral("启用") &&
            child<QLabel>(dialog, "EveExtensionState")->text() == QStringLiteral("当前工作区已禁用");
    });
    auto sections = child<QTabWidget>(dialog, "EveExtensionTabs");
    require(sections->currentIndex() == 0, "introduction shown first");
    auto changelog = child<QTextBrowser>(dialog, "EveExtensionChangelog");
    sections->setCurrentWidget(changelog);
    require(changelog->isVisible() && changelog->document()->firstBlock().blockFormat().headingLevel() == 1,
            "changelog tab renders supplied Markdown");
    int workspaceTab = -1;
    for (int i = 0; i < sections->count(); ++i)
        if (sections->tabText(i) == QStringLiteral("工作区实例")) workspaceTab = i;
    require(workspaceTab > 0, "workspace instance tab");
    for (const auto mode : {ElaThemeType::Dark, ElaThemeType::Light}) {
        eTheme->setThemeMode(mode);
        auto bar = sections->tabBar();
        window->setProperty("EveWallpaperVisible", false);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
        const auto plainSurface = surfacePixel(dialog, reader->viewport());
        sections->setCurrentWidget(changelog);
        const auto plainChangelog = surfacePixel(dialog, changelog->viewport());
        auto features = child<QWidget>(dialog, "EveExtensionFeatures");
        sections->setCurrentWidget(features);
        auto defaults = child<QPlainTextEdit>(features, "EveExtensionDefaultConfig");
        require(defaults->isVisible() && defaults->toPlainText().contains(QStringLiteral("\"autosave\": true")),
                "default configuration visible before changing the backdrop");
        const auto plainDefaults = surfacePixel(dialog, defaults->viewport());

        QPixmap wallpaper(24, 24);
        wallpaper.fill(mode == ElaThemeType::Dark ? QColor("#294257") : QColor("#e5d7ec"));
        qobject_cast<ElaWindow*>(window)->setWindowPixmap(mode, wallpaper);
        window->setProperty("EveWallpaperVisible", true);
        sections->setCurrentWidget(features);
        require(surfacePixel(dialog, defaults->viewport()) != plainDefaults, "feature editor reveals the wallpaper");
        const auto code = defaults->cursorRect(QTextCursor(defaults->document()->findBlockByNumber(1)));
        checkTabText(dialog, defaults->viewport(), QRect(0, code.top(), defaults->viewport()->width(), code.height()),
                     mode == ElaThemeType::Dark);
        sections->setCurrentWidget(changelog);
        require(surfacePixel(dialog, changelog->viewport()) != plainChangelog, "changelog reveals the wallpaper");
        const auto changelogHeading = changelog->cursorRect(QTextCursor(changelog->document()->firstBlock()));
        checkTabText(dialog, changelog->viewport(),
                     QRect(0, changelogHeading.top(), changelog->viewport()->width(), changelogHeading.height()),
                     mode == ElaThemeType::Dark);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
        const auto canvas = surfacePixel(dialog, dialog);
        require(surfacePixel(dialog, reader->viewport()) == canvas && canvas != plainSurface,
                "README shares the window wallpaper");
        require(surfacePixel(dialog, sidebar->viewport()) == canvas, "installation sidebar shares the window wallpaper");
        const auto heading = reader->cursorRect(QTextCursor(reader->document()->firstBlock()));
        checkTabText(dialog, reader->viewport(), QRect(0, heading.top(), reader->viewport()->width(), heading.height()),
                     mode == ElaThemeType::Dark);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(workspaceTab).center());
        auto workspace = child<QLabel>(dialog, "EveExtensionWorkspace");
        checkTabText(dialog, workspace, workspace->rect(), mode == ElaThemeType::Dark);
    }
    window->setProperty("EveWallpaperVisible", false);
    auto logs = child<QPushButton>(dialog, "EveExtensionShowLogs");
    require(logs->isVisible() && logs->isEnabled(), "plugin log action");
    auto menuLogs = menuAction(more->menu(), QStringLiteral("查看日志"));
    require(menuLogs && menuLogs->isEnabled(), "gear exposes the plugin log action");
    menuLogs->trigger();
    expect("log action exposes selected source", [&] {
        return !visibleDetails() && child<QComboBox>(window, "EveOutputTarget")->currentData().toString() == "plugin/sample.editor";
    });

    QTest::mouseClick(card(home, business.id), Qt::LeftButton);
    expect("reopen card", [] { return visibleDetails() != nullptr; });
    dialog = visibleDetails();
    require(!dialog->isMaximized() && dialog->size() == manualSize, "reopening preserves the manually resized window");
    controls->installReviewed = [&](const ExtensionPreview& selection) {
        require(selection.installedHash == business.packageHash && selection.sha256 == "candidate-hash",
                "confirmed review carries both package identities");
        return failedTask(makeError(ErrorCode::ConfigConflict, "Version changed in another window."));
    };
    for (const auto& version : {QStringLiteral("1.11.0"), QStringLiteral("1.9.0"), business.version}) {
        controls->preview = [&, version](QString selected) {
            auto next = candidate;
            next.version = version;
            Promise<ExtensionPreview> result;
            result.resolve({next, selected, "candidate-hash", business.version, business.packageHash});
            return result.task();
        };
        chooseFile(path);
        menuAction(packageOptions->menu(), QStringLiteral("其他版本"))->trigger();
        const auto caption = version == "1.9.0" ? QStringLiteral("确认安装旧版") :
                             version == business.version ? QStringLiteral("确认重装") : QStringLiteral("确认更新");
        expect("version change reviewed before mutation", [&] {
            return primary->text() == caption && trust->text().contains(business.version) && trust->text().contains(version);
        });
        sections->setCurrentWidget(changelog);
        require(changelog->isVisible(), "candidate changelog available during confirmation");
        if (version == "1.11.0") {
            primary->click();
            auto retry = child<QPushButton>(dialog, "EveExtensionRetry");
            expect("invalidated review only permits re-preview", [&] {
                return retry->isVisible() && retry->text() == QStringLiteral("重新预览") && !primary->isEnabled();
            });
            retry->click();
            expect("re-preview restores a fresh confirmation", [&] {
                return !retry->isVisible() && primary->isEnabled() && primary->text() == caption;
            });
        }
        child<QPushButton>(dialog, "EveExtensionCancel")->click();
        require(installations == 1, "cancelling a version selection keeps the original package");
    }
    sections->setCurrentIndex(0);
    auto category = child<QPushButton>(dialog, "EveExtensionCategory");
    QTest::keyClick(category, Qt::Key_Space);
    expect("category activates an exact filter on home", [&] {
        return !visibleDetails() && search->text() == QStringLiteral("category:编辑工具") &&
               card(home, business.id) && !card(home, desktop.id);
    });
    search->clear();
    QTest::mouseClick(card(home, business.id), Qt::LeftButton);
    expect("return from category filter to the package", [] { return visibleDetails() != nullptr; });
    askToUninstall(dialog);
    primary = child<QPushButton>(dialog, "EveExtensionPrimary");
    require(primary->text() == QStringLiteral("确认卸载") && removals == 0, "uninstall requires confirmation");
    child<QPushButton>(dialog, "EveExtensionCancel")->click();
    require(removals == 0 && card(home, business.id), "cancel keeps installed card");
    askToUninstall(dialog);
    inventory.back().version = "1.12.0";
    inventory.back().packageHash = "replacement-hash";
    primary->click();
    expect("stale uninstall exits confirmation and shows the current version", [&] {
        return removals == 0 && uninstall->isVisible() &&
            child<QLabel>(dialog, "EveExtensionVersion")->text() == "1.12.0" &&
            !child<QPushButton>(dialog, "EveExtensionRetry")->isVisible();
    });
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
