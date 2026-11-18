/** @file modules_view.cpp
 * @brief 真实菜单、托管模块窗口与纯值目录；关闭重入在独立翻译单元检查。
 */
#include "platform_workspace_p.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTableView>
#include <algorithm>
#include <exception>
#include <iostream>
#include <set>
#include <thread>

namespace modules_view_test {
using namespace platform_workspace_test;
bool closeCase(const QString& name, const QString& sourceBin);

template <class T> T* find(const char* name)
{
    for (auto* window : QApplication::topLevelWidgets()) {
        if (window->objectName() == name)
            if (auto* value = qobject_cast<T*>(window))
                return value;
        if (auto* value = window->findChild<T*>(name))
            return value;
    }
    throw std::runtime_error(std::string("missing real UI object: ") + name);
}
QModelIndex modulesCommand(QListView* list)
{
    for (int row = 0; row != list->model()->rowCount(); ++row) {
        const auto index = list->model()->index(row, 0);
        if (index.data(Qt::UserRole + 1).toString().contains("CordisOpenModules"))
            return index;
    }
    throw std::runtime_error("platform modules command missing");
}
void activateModulesCommand()
{
    auto* list = find<QListView>("CordisCommandList");
    const auto index = modulesCommand(list);
    PLATFORM_CHECK(QMetaObject::invokeMethod(list, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, index)));
}
std::size_t panelCount()
{
    const auto widgets = QApplication::allWidgets();
    return static_cast<std::size_t>(std::count_if(widgets.begin(), widgets.end(), [](const auto* widget) {
        return widget->objectName() == "CordisModuleDiagnostics";
    }));
}
QWidget* openModules(Scene& scene)
{
    auto* action = find<QAction>("CordisOpenModules");
    PLATFORM_CHECK(action->isVisible() && action->isEnabled() && scene.state->modulesPage);
    action->trigger();
    auto* panel = find<QWidget>("CordisModuleDiagnostics");
    until([&] { return panel->isVisible(); });
    PLATFORM_CHECK(panelCount() == 1);
    return panel;
}
void finishModules(Scene& scene)
{
    const bool native = static_cast<bool>(take(scene.state->counter()));
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    if (native)
        scene.verifyClosed();
    else {
        const auto result = take(scene.state->closeSnapshot());
        PLATFORM_CHECK(result.closed && !result.retained && !result.deliveryBlocked && result.session &&
                       result.session->hasValue() && result.kernel && result.kernel->hasValue() &&
                       result.kernel->value().quiescent && result.shell && result.shell->hasValue() &&
                       result.providers && result.providers->hasValue());
        PLATFORM_CHECK(!result.counter && !result.reporter && !result.counterDrain && !result.reporterDrain &&
                       !scene.state->session && !scene.state->driver && !scene.state->kernel);
        const auto generations = take(scene.state->manager->snapshot());
        PLATFORM_CHECK(generations.size() == 1 && generations.front().nativePinned &&
                       generations.front().resources.records.size() == 1 &&
                       generations.front().resources.records.front().label == "qt.loader.root");
    }
    const auto snapshot = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(!snapshot.open && !snapshot.generations.empty());
    for (const auto& entry : snapshot.current)
        PLATFORM_CHECK(entry.generation.valid() && !entry.runtime.valid() && entry.paths.empty());
    scene.release();
}
namespace {
void bareMenu()
{
    const auto scheduler = std::make_shared<qt::QtScheduler>();
    ElaShell shell(scheduler);
    auto* action = find<QAction>("CordisOpenModules");
    QPointer<QWidget> window = find<QListView>("CordisCommandList")->window();
    PLATFORM_CHECK(action->isVisible() && !action->isEnabled());
    activateModulesCommand();
    PLATFORM_CHECK(!shell.onModulesRequested({}).hasValue());
    unsigned calls = 0;
    take(shell.onModulesRequested([&] {
        ++calls;
        return Result<void>::success();
    }));
    PLATFORM_CHECK(action->isEnabled());
    action->trigger();
    activateModulesCommand();
    PLATFORM_CHECK(calls == 2);
    take(shell.onModulesRequested([&] {
        calls += 10;
        return Result<void>::success();
    }));
    action->trigger();
    PLATFORM_CHECK(calls == 12);
    std::optional<Task<void>> closing;
    std::exception_ptr failure;
    take(shell.onModulesRequested([&] {
        try {
            closing.emplace(shell.close());
            barrier(scheduler);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            PLATFORM_CHECK(window && !closing->isReady());
        } catch (...) {
            failure = std::current_exception();
        }
        return Result<void>::success();
    }));
    action->trigger();
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(closing && wait(*closing, scheduler)->hasValue() && !window);
    PLATFORM_CHECK(shell.onModulesRequested([] { return Result<void>::success(); }).error().code ==
                   ErrorCode::ContextClosing);
    take(scheduler->close());
}
void menu(const QString& sourceBin)
{
    bareMenu();
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    QPointer<QWidget> panel = openModules(scene);
    auto* dialog = panel->window();
    find<QAction>("CordisOpenModules")->trigger();
    PLATFORM_CHECK(panelCount() == 1 && find<QWidget>("CordisModuleDiagnostics") == panel);
    dialog->close();
    PLATFORM_CHECK(panel && !panel->isVisible() && !take(scene.state->kernel->snapshot()).closing);
    activateModulesCommand();
    until([&] { return panel->isVisible(); });
    PLATFORM_CHECK(panelCount() == 1 && panel->window() == dialog);
    scene.business();
    finishModules(scene);
    PLATFORM_CHECK(!panel);
}
void catalogue(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    const auto prepared = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(prepared.open && prepared.current.size() == 3 && prepared.generations.size() == 3);
    for (const auto& entry : prepared.current)
        PLATFORM_CHECK(entry.paths.empty());
    const auto initial = scene.apply(scene.state->initial);
    const auto extra =
        take(scene.state->session->addInstance("extra", "example.notebook", "{title: Extra}", initial.revision));
    PLATFORM_CHECK(wait(extra.settled, scene.scheduler)->value().outcome == PlanOutcome::Applied);
    const auto disabled = take(scene.state->session->setEnabled("extra", false, extra.revision));
    PLATFORM_CHECK(wait(disabled.settled, scene.scheduler)->value().outcome == PlanOutcome::Applied);
    const auto other =
        take(scene.state->session->addInstance("other", "example.notebook", "{title: Other}", disabled.revision));
    PLATFORM_CHECK(wait(other.settled, scene.scheduler)->value().outcome == PlanOutcome::Applied);
    const auto snapshot = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(snapshot.open && snapshot.current.size() == 3 && snapshot.configurationErrors.empty());
    for (const auto& entry : snapshot.current) {
        const auto origin = take(scene.state->manager->runtimeOrigin(entry.runtime));
        PLATFORM_CHECK(origin && origin->generation == entry.generation && origin->pluginId == entry.plugin);
        const auto expected = entry.alias == "notebook" ? std::set<std::string>{"extra", "notes", "other"}
                                                        : std::set<std::string>{entry.alias};
        PLATFORM_CHECK(std::set<std::string>(entry.paths.begin(), entry.paths.end()) == expected);
    }
    PLATFORM_CHECK(scene.pages().size() == 2);
    const auto candidate =
        take(scene.state->previewUpgrade("notebook", sourceBin, "qt-upgrade/notebook-b/module.json"));
    const auto upgraded = take(*wait(take(scene.state->upgrade(candidate)), scene.scheduler));
    PLATFORM_CHECK(upgraded.alias == "notebook" && upgraded.previous == candidate.previous && upgraded.replacement &&
                   upgraded.replacement->committed && upgraded.replacement->outcome == ReplacementOutcome::Applied &&
                   !upgraded.failure && !upgraded.retained && !upgraded.deliveryBlocked);
    const auto committed = take(scene.state->modulesSnapshot());
    const auto current = std::find_if(committed.current.begin(), committed.current.end(),
                                      [](const auto& entry) { return entry.alias == "notebook"; });
    PLATFORM_CHECK(committed.open && committed.current.size() == 3 && committed.generations.size() == 4 &&
                   current != committed.current.end() && current->generation == upgraded.candidate &&
                   current->generation == take(scene.state->generation()) &&
                   current->runtime == take(scene.state->notebookRuntime()).id());
    const auto origin = take(scene.state->manager->runtimeOrigin(current->runtime));
    PLATFORM_CHECK(origin && origin->generation == current->generation && origin->pluginId == current->plugin &&
                   current->runtime != candidate.previousRuntime &&
                   std::set<std::string>(current->paths.begin(), current->paths.end()) ==
                       (std::set<std::string>{"extra", "notes", "other"}));
    const auto generation = std::find_if(committed.generations.begin(), committed.generations.end(),
                                         [&](const auto& row) { return row.generation == current->generation; });
    PLATFORM_CHECK(generation != committed.generations.end() && generation->role == GenerationRole::Current &&
                   generation->manifest.moduleVersion == "1.2.0");
    const auto session = take(scene.state->session->snapshot());
    const auto locator = generation->manifest.sha256 + "/module.json";
    PLATFORM_CHECK(session.coordinator.desired &&
                   session.coordinator.desired->source().structure().modules().at("notebook") == locator &&
                   take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest ==
                       locator);
    auto* panel = openModules(scene);
    auto* table = panel->findChild<QTableView*>("CordisModuleTable");
    PLATFORM_CHECK(table && table->inherits("ElaTableView") && table->model()->rowCount() == 3 &&
                   table->model()->columnCount() == 4);
    QString cells;
    for (int row = 0; row != 3; ++row)
        for (int column = 0; column != 4; ++column) {
            const auto value = table->model()->index(row, column).data().toString();
            PLATFORM_CHECK(!value.isEmpty());
            cells += value + '\n';
        }
    PLATFORM_CHECK(cells.contains("example.notebook") && cells.contains("1.2.0") && cells.contains("extra") &&
                   cells.contains("other") && panel->findChild<QPlainTextEdit*>("CordisModuleDiagnosticsText"));
    bool notebookRow = false;
    for (int row = 0; row != table->model()->rowCount(); ++row) {
        const auto name = table->model()->index(row, 0);
        if (name.data(Qt::UserRole).toString() == "notebook") {
            notebookRow = true;
            PLATFORM_CHECK(name.data(Qt::UserRole + 1).toULongLong() == current->generation.value() &&
                           table->model()->index(row, 1).data().toString() == "1.2.0" &&
                           table->model()->index(row, 3).data().toString().contains("extra"));
        }
    }
    PLATFORM_CHECK(notebookRow);
    // 两份真实快照跨完整关闭仍存活；严格 drain 不能被这些值阻止。
    finishModules(scene);
    PLATFORM_CHECK(prepared.current.size() == 3 && snapshot.current.size() == 3);
}
void legacy(const QString& sourceBin)
{
    Scene scene;
    scene.state = take(notebook::Workspace::open(sourceBin + "/qt-workspace", scene.file, scene.scheduler));
    PLATFORM_CHECK(wait(scene.state->ready(), scene.scheduler)->hasValue());
    scene.apply(scene.state->initial);
    const auto snapshot = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(snapshot.open && snapshot.current.size() == 1 && snapshot.current.front().alias == "notebook" &&
                   snapshot.generations.size() == 1 && snapshot.generations.front().manifest.moduleVersion == "1.0.0" &&
                   snapshot.current.front().paths == std::vector<std::string>{"notes"});
    auto* table = openModules(scene)->findChild<QTableView*>("CordisModuleTable");
    PLATFORM_CHECK(table && table->model()->rowCount() == 1);
    finishModules(scene);
}
void guards(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    PLATFORM_CHECK(scene.state->withModuleView({}).error().code == ErrorCode::InvalidArgument);
    const auto thrown = scene.state->withModuleView([] { throw std::runtime_error("view guard exception"); });
    PLATFORM_CHECK(!thrown.hasValue() && thrown.error().code == ErrorCode::CallbackFailed &&
                   thrown.error().message == "view guard exception");
    unsigned calls = 0;
    take(scene.state->withModuleView([&] {
        ++calls;
        take(scene.state->withModuleView([&] { ++calls; }));
    }));
    PLATFORM_CHECK(calls == 2);
    bool rejected = false;
    std::thread worker([&] {
        const auto view = scene.state->withModuleView([] {});
        const auto snapshot = scene.state->modulesSnapshot();
        const auto binding = scene.state->shell.onModulesRequested([] { return Result<void>::success(); });
        rejected = !view.hasValue() && view.error().code == ErrorCode::WrongThread && !snapshot.hasValue() &&
                   snapshot.error().code == ErrorCode::WrongThread && !binding.hasValue() &&
                   binding.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(rejected && wait(scene.state->close(), scene.scheduler)->hasValue());
    PLATFORM_CHECK(scene.state->withModuleView([] {}).error().code == ErrorCode::ContextClosing);
    const auto closed = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(!closed.open && closed.current.size() == 3 && closed.generations.size() == 3);
    finishModules(scene);
}
} // namespace
} // namespace modules_view_test
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        PLATFORM_CHECK(application.arguments().size() == 3);
        const auto source = QDir(application.arguments()[1]).absolutePath();
        const auto name = application.arguments()[2];
        if (name == "menu")
            modules_view_test::menu(source);
        else if (name == "catalogue")
            modules_view_test::catalogue(source);
        else if (name == "legacy")
            modules_view_test::legacy(source);
        else if (name == "guards")
            modules_view_test::guards(source);
        else
            PLATFORM_CHECK(modules_view_test::closeCase(name, source));
        std::cout << "Modules view case passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
