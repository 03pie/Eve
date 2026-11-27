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
#include <algorithm>
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
    await(scene.host.open(std::nullopt, workspace));
    const auto target = scene.host.applyInitial();
    const auto applied = await(target.settled);
    REQUIRE(applied->value().outcome == eve::loader::PlanOutcome::Applied);
    REQUIRE(applied->value().entries.empty() && applied->value().errors.empty());
    const auto session = scene.host.session->snapshot().value();
    REQUIRE(session.coordinator.desiredRevision == target.revision && session.coordinator.running.empty());
    REQUIRE(session.coordinator.desired && session.coordinator.desired->source().structure().modules().empty());
    REQUIRE(scene.host.session->registeredPlugins().value().empty());
    scene.host.desktop()->refresh().value();
    const auto diagnostics = scene.host.desktop()->diagnostics()->snapshot().value();
    REQUIRE(diagnostics.managementOpen && diagnostics.configuration && diagnostics.configuration->open);
    REQUIRE(diagnostics.configuration->entries.empty() && diagnostics.configuration->availablePlugins.empty());
    REQUIRE(diagnostics.kernel.names.size() == 2);
    for (const char* runtime : {"eve.desktop", "host.configuration"})
        REQUIRE(std::any_of(diagnostics.kernel.names.begin(), diagnostics.kernel.names.end(),
                            [runtime](const auto& name) { return name.name == runtime; }));
    const auto& names = diagnostics.kernel.names;
    const auto desktop = std::find_if(names.begin(), names.end(), [](const auto& name) {
        return name.name == "eve.desktop";
    });
    const auto& instances = diagnostics.kernel.instances;
    const auto owner = std::find_if(instances.begin(), instances.end(),
                                    [&](const auto& instance) { return instance.runtime == desktop->id; });
    REQUIRE(owner != instances.end() && owner->state == eve::InstanceState::Active);
    // Protected system runtimes are visible in the diagnostic kernel snapshot only:
    // the management table (and its stop, retry and load controls) excludes them.
    REQUIRE(diagnostics.instances.empty());
    const eve::module::ModuleManager modules;
    const auto origin = modules.runtimeOrigin(desktop->id).value();
    REQUIRE(origin && origin->pluginId == "eve.desktop" && origin->moduleId == "org.eve.desktop");
    const auto generations = modules.snapshot().value();
    const auto loaded = std::find_if(generations.begin(), generations.end(), [&](const auto& generation) {
        return generation.generation == origin->generation;
    });
    REQUIRE(loaded != generations.end() && loaded->nativeMapped && loaded->businessAccepted);
    const auto officialFile = QDir(QCoreApplication::applicationDirPath()).filePath("system/desktop/eve_desktop.dll");
    REQUIRE(QFileInfo(QString::fromStdString(loaded->libraryPath)).canonicalFilePath() ==
            QFileInfo(officialFile).canonicalFilePath());
    const auto graph = scene.host.kernel->services().dependencyGraph().value();
    for (const char* service : {"ui.pages", "ui.commands", "ui.output", "ui.notifications"}) {
        REQUIRE(std::count_if(graph.bindings.begin(), graph.bindings.end(), [&](const auto& binding) {
            return binding.binding.key.name == service && binding.provider == owner->instance &&
                   binding.managedProvider && binding.binding.state == eve::BindingState::Published;
        }) == 1);
    }

    const auto sequence = scene.host.writeOutput(marker.toStdString()).value();
    if (exerciseWindow) {
        scene.host.desktop()->show().value();
        until([] { return visibleWidget<QWidget>("EveWelcomePage") != nullptr; });
        auto* welcome = visibleWidget<QWidget>("EveWelcomePage");
        until([&] {
            const auto cards = welcome->findChildren<QWidget*>("EvePluginCard");
            return cards.size() == 1 && cards.first()->isVisible() &&
                   cards.first()->property("EveExtensionId").toString() == "org.eve.desktop";
        });
        auto* window = welcome->window();
        auto* add = window->findChild<QAction*>("EveAddPlugin");
        auto* configuration = window->findChild<QAction*>("EveConfigurationFile");
        REQUIRE(add && configuration);
        until([&] { return !add->isEnabled() && configuration->isEnabled(); });
        configuration->trigger();
        until([] { return visibleWidget<QPlainTextEdit>("EveConfigEditor") != nullptr; });
        REQUIRE(visibleWidget<QPlainTextEdit>("EveConfigEditor")->toPlainText().contains("plugins"));
        scene.host.desktop()->showOutputEvent(sequence, nullptr).value();
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

void openModuleWorkspace(const QString& workspace, const QByteArray& original, const char* expectedFailure = nullptr)
{
    REQUIRE(QDir().mkpath(workspace));
    const auto file = QDir(workspace).filePath("session.yaml");
    {
        QFile config(file);
        REQUIRE(config.open(QIODevice::WriteOnly));
        REQUIRE(config.write(original) == original.size() && config.flush());
    }

    Scene scene;
    const auto opened = scene.host.open(std::nullopt, workspace);
    until([&] { return opened.isReady(); });
    const auto result = opened.tryResult();
    REQUIRE(result);
    if (expectedFailure) {
        REQUIRE(!result->hasValue() && result->error().phase == expectedFailure);
        REQUIRE(!scene.host.session && readFile(file) == original);
    } else {
        REQUIRE(result->hasValue());
        REQUIRE(await(scene.host.applyInitial().settled)->value().outcome == eve::loader::PlanOutcome::Applied);
        const auto items = scene.host.extensionControls()->list().value();
        REQUIRE(std::any_of(items.begin(), items.end(), [](const auto& item) {
            return item.legacyReference && item.missing && item.pluginId == "fixture.external" && item.instances == 1;
        }));
        REQUIRE(scene.host.session->snapshot().value().coordinator.running.empty());
    }
    scene.close();
    REQUIRE(eve::loader::parseConfiguration(readFile(file).toStdString()).value().normalizedYaml() ==
            eve::loader::parseConfiguration(original.toStdString()).value().normalizedYaml());
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
        openModuleWorkspace(temp.filePath("external"),
            "formatVersion: 1\nmodules: {extension: {manifest: module.json}}\n"
            "plugins: [{id: existing, plugin: fixture.external, module: extension, config: {}}]\n");
        openModuleWorkspace(temp.filePath("reserved"),
            "formatVersion: 1\nplugins: [{id: parent, plugin: external, disabled: true, "
            "children: [{id: system, plugin: eve.desktop, disabled: true}]}]\n",
            "WORKBENCH_RESERVED_PLUGIN");
        verifyWorkbenchDataPaths(QDir(temp.path()));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Platform startup: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
