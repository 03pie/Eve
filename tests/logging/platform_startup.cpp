#include "runtime_log_archive_p.hpp"
#include "workbench.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <stdexcept>

#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace {
using eve::workbench::Workbench;
using eve::ui::detail::readSavedLogRun;
using eve::ui::detail::scanSavedDescending;

template<class Predicate>
void until(Predicate condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        REQUIRE(timer.elapsed() < 8000);
        QTest::qWait(5);
    }
}

template<class T>
typename eve::Task<T>::Snapshot await(const eve::Task<T>& task)
{
    until([&] { return task.isReady(); });
    auto result = task.tryResult();
    REQUIRE(result);
    if (!result->hasValue())
        throw std::runtime_error(result->error().message);
    return result;
}

class Scene {
public:
    Workbench host;

    void close()
    {
        await(host.close());
        host.scheduler->close().value();
        REQUIRE(!host.session && !host.kernel);
        closed_ = true;
    }

    ~Scene()
    {
        if (closed_)
            return;
        try {
            close();
        } catch (const std::exception& error) {
            std::cerr << "cleanup: " << error.what() << '\n';
        }
    }

private:
    bool closed_ = false;
};

template<class T>
T* visibleWidget(const char* name)
{
    for (auto* window : QApplication::topLevelWidgets()) {
        auto* child = window->findChild<T*>(name);
        if (child && child->isVisible())
            return child;
    }
    return nullptr;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    REQUIRE(file.error() == QFileDevice::NoError);
    return bytes;
}

void openEmpty(const QString& workspace, const QString& marker, bool exerciseWindow)
{
    Scene scene;
    scene.host.open(std::nullopt, workspace);
    scene.host.publishUi();
    const auto target = scene.host.applyInitial();
    const auto applied = await(target.settled);
    REQUIRE(applied->value().outcome == eve::loader::PlanOutcome::Applied);
    REQUIRE(applied->value().entries.empty() && applied->value().errors.empty());
    const auto session = scene.host.session->snapshot().value();
    REQUIRE(session.coordinator.desiredRevision == target.revision && session.coordinator.running.empty());
    REQUIRE(session.coordinator.desired && session.coordinator.desired->source().structure().modules().empty());
    REQUIRE(scene.host.session->registeredPlugins().value().empty());
    scene.host.shell.management()->refresh().value();
    const auto diagnostics = scene.host.shell.diagnostics()->snapshot().value();
    REQUIRE(diagnostics.managementOpen && diagnostics.configuration && diagnostics.configuration->open);
    REQUIRE(diagnostics.configuration->entries.empty() && diagnostics.configuration->availablePlugins.empty());
    REQUIRE(diagnostics.kernel.names.size() == 1 && diagnostics.kernel.names.front().name == "host.configuration");

    const auto sequence = scene.host.shell.writeOutput(marker.toStdString()).value();
    if (exerciseWindow) {
        scene.host.shell.show().value();
        until([] { return visibleWidget<QWidget>("EveWelcomePage") != nullptr; });
        auto* welcome = visibleWidget<QWidget>("EveWelcomePage");
        auto* window = welcome->window();
        auto* add = window->findChild<QAction*>("EveAddPlugin");
        auto* configuration = window->findChild<QAction*>("EveConfigurationFile");
        REQUIRE(add && configuration);
        until([&] { return !add->isEnabled() && configuration->isEnabled(); });
        configuration->trigger();
        until([] { return visibleWidget<QPlainTextEdit>("EveConfigEditor") != nullptr; });
        REQUIRE(visibleWidget<QPlainTextEdit>("EveConfigEditor")->toPlainText().contains("plugins"));
        scene.host.shell.showOutputEvent(sequence, nullptr).value();
        until([&] {
            auto* editor = visibleWidget<QPlainTextEdit>("EveWorkbenchOutput");
            return editor && editor->toPlainText().contains(marker);
        });
    }
    scene.close();
}

void verifyClosedRuns(const QString& workspace)
{
    const QDir archive(QDir(workspace).filePath(".eve/logs"));
    const auto runs = archive.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(runs.size() == 2);
    bool startup = false, reopen = false;
    for (const auto& run : runs) {
        REQUIRE(!QFile::exists(archive.filePath(run + "/active.lock")));
        const auto report = readSavedLogRun(workspace, run);
        REQUIRE(report && report->ended);
        bool cleaned = false;
        const auto warning = scanSavedDescending(workspace, run, 0, [&](const auto& record) {
            if (record.sourceId == "platform") {
                startup = startup || record.entry.text == "PLATFORM_EMPTY_START";
                reopen = reopen || record.entry.text == "PLATFORM_EMPTY_REOPEN";
                cleaned = cleaned || record.diagnosticCode == "WORKBENCH_CLEANUP_COMPLETE";
            }
            return true;
        });
        REQUIRE(warning.isEmpty() && cleaned);
    }
    REQUIRE(startup && reopen);
}

/** @brief 旧版用户文件原址打开，Eve 已有的同名文件优先；新工作区与嵌入宿主独立。 */
void verifyWorkbenchDataPaths(const QDir& root)
{
    const QDir previous(root.filePath("CordisPluginWorkbench"));
    const QDir current(root.filePath("EvePluginWorkbench"));
    for (const QString& name : {QStringLiteral("session.yaml"), QStringLiteral("platform-workspace/session.yaml"),
                                QStringLiteral("preferences/workbench.json")}) {
        const auto resolve = [&] { return eve::workbench::workbenchDataFile(current.path(), name); };
        REQUIRE(resolve() == current.filePath(name));
        for (const QDir& data : {previous, current}) {
            const auto path = data.filePath(name);
            REQUIRE(QFileInfo(path).dir().mkpath("."));
            QFile file(path);
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.close();
            REQUIRE(resolve() == path);
        }
        const QDir embedded(root.filePath("IndependentHost"));
        REQUIRE(eve::workbench::workbenchDataFile(embedded.path(), name) == embedded.filePath(name));
    }
}

void rejectModuleWorkspace(const QString& workspace)
{
    REQUIRE(QDir().mkpath(workspace));
    const auto file = QDir(workspace).filePath("session.yaml");
    const QByteArray original = "formatVersion: 1\nmodules: {extension: {manifest: module.json}}\n"
                                "plugins: [{id: existing, plugin: fixture.external, module: extension, config: {}}]\n";
    {
        QFile config(file);
        REQUIRE(config.open(QIODevice::WriteOnly));
        REQUIRE(config.write(original) == original.size() && config.flush());
    }

    Scene scene;
    bool rejected = false;
    try {
        scene.host.open(std::nullopt, workspace);
    } catch (const eve::workbench::ModulePreflightError& error) {
        REQUIRE(std::string(error.what()).find("--module") != std::string::npos);
        rejected = true;
    }
    REQUIRE(rejected && !scene.host.session && readFile(file) == original);
    scene.close();
    REQUIRE(readFile(file) == original);
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QStandardPaths::setTestModeEnabled(true);
    try {
        QTemporaryDir temp;
        REQUIRE(temp.isValid());
        application.setApplicationName("EveStartup-" + QDir(temp.path()).dirName());
        const auto workspace = temp.filePath("empty");
        openEmpty(workspace, "PLATFORM_EMPTY_START", true);
        const auto file = QDir(workspace).filePath("session.yaml");
        const auto saved = readFile(file);
        REQUIRE(!saved.isEmpty());
        openEmpty(workspace, "PLATFORM_EMPTY_REOPEN", false);
        REQUIRE(readFile(file) == saved);
        verifyClosedRuns(workspace);
        rejectModuleWorkspace(temp.filePath("external"));
        verifyWorkbenchDataPaths(QDir(temp.path()));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Platform startup: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
