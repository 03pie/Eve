/** @file upgrade_view_main.cpp
 * @brief 实际模块升级窗口入口与共用查询；不另建宿主或推进器。
 */
#include "upgrade_fault_p.hpp"
#include "upgrade_view_p.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QTimer>
#include <algorithm>
#include <iostream>

namespace upgrade_view_test {
namespace {
template <class T> T* child(QWidget* owner, const char* name)
{
    const auto value = owner->findChild<T*>(name);
    PLATFORM_CHECK(value);
    return value;
}
QWidget* modules()
{
    for (auto* window : QApplication::topLevelWidgets())
        if (auto* action = window->findChild<QAction*>("CordisOpenModules")) {
            PLATFORM_CHECK(action->isEnabled());
            action->trigger();
            for (auto* widget : QApplication::allWidgets())
                if (widget->objectName() == "CordisModuleDiagnostics")
                    return widget;
        }
    throw std::runtime_error("real module menu missing");
}
} // namespace
View::View(Scene& value) : scene(value), body(modules()), window(body->window())
{
    PLATFORM_CHECK(body->isVisible());
    child<QTimer>(body, "CordisModulesRefreshTimer")->stop();
    table = child<QTableView>(body, "CordisModuleTable");
    source = child<QLineEdit>(body, "CordisUpgradeSource");
    browse = child<QPushButton>(body, "CordisUpgradeBrowse");
    review = child<QPushButton>(body, "CordisUpgradePreview");
    start = child<QPushButton>(body, "CordisUpgradeStart");
    cancel = child<QPushButton>(body, "CordisUpgradeCancel");
    save = child<QPushButton>(body, "CordisUpgradeSave");
    refreshButton = child<QPushButton>(body, "CordisModuleDiagnosticsRefresh");
    previewText = child<QPlainTextEdit>(body, "CordisUpgradePreviewText");
    reportText = child<QPlainTextEdit>(body, "CordisUpgradeReportText");
    status = child<QLabel>(body, "CordisUpgradeStatus");
    saveStatus = child<QLabel>(body, "CordisUpgradeSaveStatus");
}
void View::refresh()
{
    PLATFORM_CHECK(body && refreshButton->isEnabled());
    refreshButton->click();
}
void View::select(const std::string& alias)
{
    PLATFORM_CHECK(table->isEnabled());
    for (int row = 0; row != table->model()->rowCount(); ++row) {
        const auto index = table->model()->index(row, 0);
        if (index.data(Qt::UserRole).toString() == QString::fromStdString(alias)) {
            table->selectionModel()->setCurrentIndex(index,
                                                     QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            refresh();
            return;
        }
    }
    throw std::runtime_error("module alias missing: " + alias);
}
void View::preview(const std::string& alias, const QString& path)
{
    select(alias);
    PLATFORM_CHECK(QDir::isAbsolutePath(path) && source->isEnabled());
    source->setText(path);
    PLATFORM_CHECK(review->isEnabled());
    review->click();
    PLATFORM_CHECK(start->isEnabled() && !previewText->toPlainText().isEmpty());
}
OperationId View::begin()
{
    PLATFORM_CHECK(start->isEnabled());
    start->click();
    const auto snapshot = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(snapshot.active && snapshot.active->replacement && snapshot.active->replacement->operation.valid());
    return snapshot.active->replacement->operation;
}
WorkspaceUpgradeReport View::settled(std::size_t previousReports)
{
    until([&] {
        const auto snapshot = take(scene.state->upgradeSnapshot());
        return !snapshot.active && snapshot.history.size() > previousReports;
    });
    refresh();
    const auto snapshot = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(snapshot.history.size() == previousReports + 1);
    return snapshot.history.back();
}
void View::saveNow()
{
    refresh();
    PLATFORM_CHECK(save->isEnabled());
    save->click();
    until([&] {
        const auto snapshot = take(scene.state->session->snapshot());
        return !snapshot.store.writing && snapshot.store.queuedWrites == 0;
    });
    barrier(scene.scheduler);
    refresh();
}
QString candidatePath(const QString& sourceBin, const std::string& alias, const std::string& variant)
{
    return QDir(sourceBin).absoluteFilePath(QString::fromStdString(
        std::string(alias == "notebook" ? "qt-upgrade/" : "native-upgrade/") + alias + '-' + variant + "/module.json"));
}
QString copiedCandidate(Scene& scene, const QString& sourceBin, const std::string& alias)
{
    const auto path = candidatePath(sourceBin, alias);
    const auto artifact = take(preflightModule(utf8(QFileInfo(path).absolutePath()), "module.json"));
    const auto directory = scene.scratch.filePath(QString::fromUtf8("候选 副本"));
    PLATFORM_CHECK(QDir().mkpath(directory));
    PLATFORM_CHECK(QFile::copy(path, QDir(directory).filePath("module.json")));
    PLATFORM_CHECK(QFile::copy(QString::fromStdString(artifact.libraryPath()),
                               QDir(directory).filePath(QString::fromStdString(artifact.manifest().library))));
    return QDir(directory).filePath("module.json");
}
std::string diskText(const Scene& scene)
{
    QFile file(scene.file);
    PLATFORM_CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll().toStdString();
}
void writeDisk(const Scene& scene, const QByteArray& text)
{
    QFile file(scene.file);
    PLATFORM_CHECK(file.open(QIODevice::WriteOnly));
    PLATFORM_CHECK(file.write(text) == text.size());
}
QListView* commandList()
{
    for (auto* window : QApplication::topLevelWidgets())
        if (auto* list = window->findChild<QListView*>("CordisCommandList"))
            return list;
    throw std::runtime_error("actual command list missing");
}
std::optional<Candidate> notebookCandidate(const Scene& scene, GenerationId previous, bool held)
{
    const auto core = take(scene.state->kernel->snapshot());
    const auto graph = take(scene.state->kernel->services().dependencyGraph());
    for (const auto& instance : core.instances) {
        const auto origin = take(scene.state->manager->runtimeOrigin(instance.runtime));
        if (!origin || origin->generation == previous || origin->pluginId != "example.notebook")
            continue;
        const auto node = std::find_if(graph.instances.begin(), graph.instances.end(),
                                       [&](const auto& row) { return row.instance == instance.instance; });
        if (node != graph.instances.end() && node->publicationHeld == held)
            return Candidate{instance, *origin};
    }
    return {};
}
void committed(const Scene& scene, const WorkspaceUpgradeReport& report)
{
    PLATFORM_CHECK(report.replacement && report.replacement->committed &&
                   report.replacement->outcome == ReplacementOutcome::Applied && !report.replacement->recovered &&
                   !report.replacement->resourcesRetained && !report.failure && !report.retained &&
                   !report.deliveryBlocked);
    const auto generation = platform_upgrade_test::module(scene, report.candidate);
    PLATFORM_CHECK(generation.role == GenerationRole::Current &&
                   platform_upgrade_test::module(scene, report.previous).role == GenerationRole::Retired);
    const auto snapshot = take(scene.state->modulesSnapshot());
    const auto entry = std::find_if(snapshot.current.begin(), snapshot.current.end(),
                                    [&](const auto& row) { return row.alias == report.alias; });
    PLATFORM_CHECK(entry != snapshot.current.end() && entry->generation == report.candidate);
    const auto origin = take(scene.state->manager->runtimeOrigin(entry->runtime));
    PLATFORM_CHECK(origin && origin->generation == report.candidate && origin->pluginId == entry->plugin);
    const auto locator = generation.manifest.sha256 + "/module.json";
    const auto session = take(scene.state->session->snapshot());
    PLATFORM_CHECK(session.coordinator.desired &&
                   session.coordinator.desired->source().structure().modules().at(report.alias) == locator &&
                   take(scene.state->driver->configurationDefinitions()->module(entry->plugin)).manifest == locator);
    const auto kernel = take(scene.state->kernel->snapshot());
    PLATFORM_CHECK(std::any_of(kernel.names.begin(), kernel.names.end(),
                               [&](const auto& row) { return row.name == entry->plugin && row.id == entry->runtime; }));
}
void visibleReport(const View& view, const WorkspaceUpgradeReport& report)
{
    PLATFORM_CHECK(report.replacement &&
                   view.reportText->toPlainText().contains(QString::number(report.replacement->operation.value())) &&
                   view.reportText->toPlainText().contains(QString::fromStdString(report.alias)));
}
} // namespace upgrade_view_test
int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        PLATFORM_CHECK(application.arguments().size() == 3);
        const auto source = QDir(application.arguments()[1]).absolutePath();
        const auto name = application.arguments()[2];
        PLATFORM_CHECK(upgrade_view_test::controlsCase(name, source) ||
                       upgrade_view_test::operationsCase(name, source) ||
                       upgrade_view_test::failuresCase(name, source) || upgrade_fault_test::stateCase(name, source) ||
                       upgrade_fault_test::stopCase(name, source) || upgrade_fault_test::nativeCase(name, source) ||
                       upgrade_fault_test::s09Case(name, source) || upgrade_fault_test::retirementCase(name, source));
        std::cout << "Upgrade view case passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
