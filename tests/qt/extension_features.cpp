/** Exercise real workspace contributions and static package declarations in the feature tab. */
#include <eve/desktop/layout.hpp>
#include <eve/kernel.hpp>
#include <eve/ui/ela_shell.hpp>
#include <QAction>
#include <eve/ui/extensions.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <stdexcept>

using namespace eve;
using namespace eve::ui;
namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void expect(const char* message, const std::function<bool()>& condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 5000) QTest::qWait(5);
    require(condition(), message);
}
template<class T> void wait(const Task<T>& task)
{
    expect("feature test task", [&] { return task.isReady(); });
    task.tryResult()->value();
}
template<class T> T* widget(QWidget* parent, const char* name)
{
    auto result = parent->findChild<T*>(QString::fromLatin1(name));
    require(result, name);
    return result;
}
QWidget* details()
{
    for (auto window : QApplication::topLevelWidgets())
        if (window->objectName() == "EveExtensionDialog" && window->isVisible()) return window;
    return nullptr;
}
QWidget* openDetails(QWidget* home, const QString& id)
{
    for (auto card : home->findChildren<QWidget*>("EvePluginCard")) {
        if (card->property("EveExtensionId").toString() != id || !card->isVisible()) continue;
        QTest::mouseClick(widget<QPushButton>(card, "EvePluginManage"), Qt::LeftButton);
        expect("open extension details", [] { return details(); });
        return details();
    }
    throw std::runtime_error("extension card missing");
}
QTabWidget* featureTabs(QWidget* window)
{
    return widget<QTabWidget>(window, "EveExtensionTabs");
}
int featureTab(QWidget* window)
{
    return featureTabs(window)->indexOf(widget<QWidget>(window, "EveExtensionFeatures"));
}
void openFeatures(QWidget* window)
{
    auto tabs = featureTabs(window);
    const auto index = featureTab(window);
    require(tabs->isTabVisible(index), "feature tab available");
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier, tabs->tabBar()->tabRect(index).center());
    require(tabs->currentIndex() == index, "feature tab selected");
}
ExtensionItem item(const char* id, const char* plugin)
{
    ExtensionItem result;
    result.id = result.name = QString::fromLatin1(id);
    result.pluginId = QString::fromLatin1(plugin);
    return result;
}
}

void extensionFeatures()
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto scheduler = std::make_shared<qt::QtScheduler>();
    auto kernel = std::make_shared<Kernel>(scheduler);
    ElaShell shell(scheduler);
    shell.management()->attach(kernel).value();
    auto plugin = item("org.test.features", "test.features");
    plugin.defaultConfig = R"({"mode":"safe","retries":2})";
    plugin.pending = true;
    plugin.state = QStringLiteral("Enabled in this workspace");
    plugin.actions = {ExtensionAction::Disable};
    auto missing = item("org.test.missing", "test.missing");
    missing.missing = true;
    auto desktop = item("org.test.desktop", "test.desktop");
    desktop.kind = ExtensionKind::Desktop;
    auto builtin = item("org.eve.desktop", "eve.desktop");
    builtin.kind = ExtensionKind::Desktop;
    builtin.builtin = true;
    builtin.desktopServices << "ui.pages" << "ui.commands" << eve::desktop::layoutService;

    unsigned activations = 0, predicates = 0, commandsRun = 0;
    auto contribute = [&](Context context, const std::string& label) {
        require(shell.pages()->add(context, {label + ".page", label + " page", "",
            [] { return std::make_unique<QWidget>(); }}).hasValue(), "register page contribution");
        require(shell.pages()->add(context, {label + ".window", label + " window", "",
            [] { return std::make_unique<QWidget>(); }, PagePresentation::Dialog}).hasValue(), "register window contribution");
        auto command = shell.commands()->add(context, {label + ".command", label + " command",
            [&] { ++commandsRun; return completedTask(); },
            [&] { ++predicates; return true; }}).value();
        return command.ready();
    };
    auto runtime = kernel->registerPlugin("test.features", [&](Context context) {
        return contribute(context, ++activations == 1 ? "current" : "candidate");
    }).value();
    auto another = kernel->registerPlugin("test.other", [&](Context context) {
        return contribute(context, "unrelated");
    }).value();
    auto running = kernel->mount(runtime).value();
    auto unrelated = kernel->mount(another).value();
    auto group = kernel->holdPublication().value();
    auto unpublished = kernel->mountHeld(group, runtime).value();
    wait(running.ready());
    wait(unrelated.ready());
    wait(unpublished.ready());
    require(shell.commands()->snapshot().value().size() == 3, "held contribution exists in diagnostics");

    QTemporaryDir temporary;
    const auto filePath = temporary.filePath("preview.evepkg");
    QFile file(filePath);
    require(file.open(QIODevice::WriteOnly), "create preview file");
    file.close();
    auto controls = std::make_shared<ExtensionControls>();
    controls->list = [&] { return Result<std::vector<ExtensionItem>>::success({plugin, missing, desktop, builtin}); };
    controls->preview = [&](const QString& path) {
        require(path == filePath, "selected preview fixture");
        Promise<ExtensionPreview> promise;
        ExtensionPreview preview;
        preview.item = plugin;
        preview.path = path;
        preview.sha256 = "preview-digest";
        promise.resolve(std::move(preview));
        return promise.task();
    };
    controls->apply = [](ExtensionAction, const QString&, const QString&, const QString&) -> Task<void> {
        throw std::runtime_error("inspecting features must not apply changes");
    };
    shell.bindExtensions(controls).value();
    shell.show().value();
    QWidget* home = nullptr;
    for (auto top : QApplication::topLevelWidgets())
        if (auto found = top->findChild<QWidget*>("EveWelcomePage")) home = found;
    require(home, "workbench home");

    auto dialog = openDetails(home, plugin.id);
    openFeatures(dialog);
    auto defaults = widget<QPlainTextEdit>(dialog, "EveExtensionDefaultConfig");
    auto pages = widget<QLabel>(dialog, "EveExtensionFeaturePages");
    auto commands = widget<QLabel>(dialog, "EveExtensionFeatureCommands");
    require(defaults->isReadOnly() && defaults->toPlainText().contains(QStringLiteral("\"mode\": \"safe\"")),
            "readable package defaults");
    require(pages->text().contains("current.page") && pages->text().contains("current.window") &&
            !pages->text().contains("candidate.") && !pages->text().contains("unrelated."), "only owned published pages and windows");
    require(commands->text().contains("current.command") && !commands->text().contains("candidate.") &&
            !commands->text().contains("unrelated.") && predicates == 0 && commandsRun == 0,
            "only owned published commands without querying or executing plugin code");
    require(widget<QLabel>(dialog, "EveExtensionContributionVersion")->isVisible(),
            "pending version explains the origin of runtime contributions");
    wait(running.stop());
    plugin.state = QStringLiteral("Disabled in this workspace");
    plugin.actions = {ExtensionAction::Enable};
    widget<QAction>(home->window(), "EveRefreshPlugins")->trigger();
    require(widget<QLabel>(dialog, "EveExtensionState")->text() == plugin.state &&
            widget<QPushButton>(dialog, "EveExtensionPrimary")->text() == QStringLiteral("Enable") &&
            featureTabs(dialog)->currentIndex() == featureTab(dialog),
            "catalog refresh updates an open detail without changing tabs");
    expect("stopped generation removed from visible features", [&] {
        return !pages->isVisible() && !commands->isVisible() && defaults->isVisible();
    });
    group.commit().value();
    expect("newly published generation appears without reopening details", [&] {
        return pages->isVisible() && commands->text().contains("candidate.command");
    });
    dialog->close();

    dialog = openDetails(home, missing.id);
    require(!featureTabs(dialog)->isTabVisible(featureTab(dialog)), "missing extension has no fabricated features");
    dialog->close();
    dialog = openDetails(home, desktop.id);
    openFeatures(dialog);
    auto interface = widget<QLabel>(dialog, "EveExtensionDesktopInterface");
    require(interface->text().contains(eve::desktop::layoutService) && !interface->text().contains("ui.pages"),
            "external desktop shows the layout contract");
    dialog->close();
    dialog = openDetails(home, builtin.id);
    openFeatures(dialog);
    require(interface->text().contains("ui.pages") && interface->text().contains(eve::desktop::layoutService),
            "built-in desktop shows provided host services");
    dialog->close();

    QTimer::singleShot(0, [filePath] {
        if (auto picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
            picker->selectFile(filePath);
            QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
        }
    });
    widget<QPushButton>(home, "EveHomeInstallExtension")->click();
    expect("preview exposes static declarations", [] { return details() && featureTabs(details())->isTabVisible(featureTab(details())); });
    dialog = details();
    openFeatures(dialog);
    require(defaults->isVisible() && !pages->isVisible() && !commands->isVisible(),
            "same-plugin package preview never renders live contributions");
    dialog->close();

    const auto stopped = kernel->close();
    wait(stopped);
    require(stopped.tryResult()->value().quiescent, "feature fixture resources are quiescent");
    wait(shell.close());
    scheduler->close().value();
}
