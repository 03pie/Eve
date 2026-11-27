#include <eve/ui/ela_shell.hpp>
#include <eve/ui/extensions.hpp>
#include <ElaWindow.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <stdexcept>

using namespace eve;
using namespace eve::ui;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void expect(const char* phase, const std::function<bool()>& done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 3000) QTest::qWait(5);
    require(done(), phase);
}
template <class T> T* child(QWidget* root, const char* id)
{
    auto widget = root->findChild<T*>(QString::fromLatin1(id));
    require(widget != nullptr, id);
    return widget;
}
}

void extensionsCenter()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto scheduler = std::make_shared<qt::QtScheduler>();
    ElaShell shell(scheduler);
    auto controls = std::make_shared<ExtensionControls>();
    ExtensionItem desktop;
    desktop.id = "org.eve.desktop"; desktop.name = "Ela"; desktop.kind = ExtensionKind::Desktop;
    desktop.builtin = desktop.current = true; desktop.version = "1.0"; desktop.publisher = "Eve";
    ExtensionItem business;
    business.id = "sample.editor"; business.name = "Editor"; business.pluginId = "sample.editor";
    business.packageHash = "installed-hash";
    business.version = "1.0"; business.publisher = "Test publisher";
    business.actions = {ExtensionAction::ShowLogs, ExtensionAction::Uninstall};
    std::vector<ExtensionItem> inventory{desktop};
    controls->list = [&] { return Result<std::vector<ExtensionItem>>::success(inventory); };
    QTemporaryDir temporary;
    const auto path = temporary.filePath("editor.evepkg");
    QFile file(path);
    require(file.open(QFile::WriteOnly), "test file"); file.close();
    Promise<ExtensionPreview> scanned;
    int previews = 0, installations = 0, removals = 0;
    controls->preview = [&](QString selected) { require(selected == path, "chosen path"); ++previews; return scanned.task(); };
    controls->apply = [&](ExtensionAction action, QString id, QString selected, QString hash) {
        require(id == business.id, "selected identity");
        if (action == ExtensionAction::Install) {
            require(selected == path && hash == "reviewed-hash", "reviewed package");
            ++installations;
            inventory.push_back(business);
        } else {
            require(action == ExtensionAction::Uninstall && hash == "installed-hash", "reviewed uninstall identity");
            ++removals;
        }
        return completedTask();
    };
    shell.bindExtensions(controls).value();
    shell.show().value();
    QWidget* window = nullptr;
    for (auto candidate : QApplication::topLevelWidgets())
        if (qobject_cast<ElaWindow*>(candidate) && candidate->findChild<QAction*>("EveOpenExtensions")) window = candidate;
    require(window != nullptr, "extension menu");
    window->activateWindow();
    expect("activate workbench", [&] { return window->isActiveWindow(); });
    QTest::keyClick(window, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
    auto page = child<QWidget>(window, "EveExtensionsPage");
    expect("shortcut", [&] { return page->isVisible(); });
    auto list = child<QListView>(page, "EveExtensionsList");
    require(list->model()->rowCount() == 1, "built-in desktop in an empty workspace");
    auto commands = child<QListView>(window, "EveCommandList");
    require(!commands->model()->match(commands->model()->index(0, 0), Qt::DisplayRole, QStringLiteral("从扩展安装包安装"), 1,
                                     Qt::MatchContains).isEmpty(), "install action in palette");
    QTimer::singleShot(0, [path] {
        auto dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (dialog) { dialog->selectFile(path); QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection); }
    });
    child<QAction>(window, "EveInstallExtension")->trigger();
    require(previews == 1 && installations == 0, "preview before mutation");
    auto install = child<QPushButton>(page, "EveExtensionsChooseFile");
    require(!install->isEnabled(), "preview pending");
    scanned.resolve({business, path, "reviewed-hash"});
    auto primary = child<QPushButton>(page, "EveExtensionPrimary");
    expect("preview ready", [&] { return primary->text() == QStringLiteral("确认安装") && primary->isEnabled(); });
    require(child<QLabel>(page, "EveExtensionDescription")->text().contains("reviewed-hash"), "digest visible");
    primary->click();
    expect("installation ready", [&] { return installations == 1 && list->model()->rowCount() == 2; });
    require(list->currentIndex().data(Qt::UserRole).toString() == business.id, "selection after install");
    primary->click();
    require(child<QComboBox>(window, "EveOutputTarget")->currentData().toString() == "plugin/sample.editor", "plugin logs");
    auto menu = child<QPushButton>(page, "EveExtensionMore")->menu();
    menu->actions().first()->trigger();
    require(primary->text() == QStringLiteral("确认卸载") && removals == 0, "uninstall confirmation");
    child<QPushButton>(page, "EveExtensionCancel")->click();
    require(removals == 0, "cancel keeps install");
    menu->actions().first()->trigger();
    primary->click();
    expect("confirmed uninstall delivered", [&] { return removals == 1 && install->isEnabled(); });
    child<QAction>(window, "EveOpenSettings")->trigger();
    child<QPushButton>(window, "EveSettingsChooseDesktop")->click();
    expect("desktop settings entry", [&] { return page->isVisible() && list->model()->rowCount() == 1; });
    require(list->currentIndex().data(Qt::UserRole).toString() == desktop.id, "desktop-only settings");
    page->setFixedWidth(440);
    auto back = child<QPushButton>(page, "EveExtensionsBack");
    expect("narrow list", [&] { return list->isVisible() && !back->isVisible(); });
    list->setFocus(); QTest::keyClick(list, Qt::Key_Return);
    expect("narrow detail", [&] { return back->isVisible() && !list->isVisible(); });
    back->click();
    expect("narrow back restores focus", [&] { return list->isVisible() && list->hasFocus(); });
    auto closed = shell.close();
    expect("shell closed", [&] { return closed.isReady(); });
    closed.tryResult()->value();
    scheduler->close().value();
}
