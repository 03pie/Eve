/** @file platform_workspace_scene.cpp
 * @brief 两个真实业务测试入口共用同一 Workspace 装配和资源断言。
 */
#include "platform_workspace_p.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVariant>
#include <QWidget>
#include <algorithm>
#include <cordis/module/install.hpp>
#include <set>

namespace platform_workspace_test {
Scene::Scene()
{
    PLATFORM_CHECK(scratch.isValid());
    store = scratch.filePath("store");
    file = scratch.filePath("session.yaml");
    PLATFORM_CHECK(QDir().mkpath(store));
}
void Scene::open(const QString& sourceBin)
{
    state = take(notebook::Workspace::openQualification(sourceBin, store, file, scheduler));
    PLATFORM_CHECK(state->scheduler == scheduler && state->kernel && state->driver && state->session);
    PLATFORM_CHECK(state->kernel->usesScheduler(scheduler) && state->session->usesScheduler(scheduler));
    PLATFORM_CHECK(wait(state->ready(), scheduler)->hasValue());
}
AcceptedTarget Scene::apply(const std::string& text, PlanOutcome expected)
{
    auto accepted = take(state->apply(text));
    const auto settled = wait(accepted.settled, scheduler);
    PLATFORM_CHECK(settled->hasValue() && settled->value().outcome == expected);
    return accepted;
}
std::vector<PageSnapshot> Scene::pages() const
{
    auto rows = take(state->shell.pages()->snapshot());
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto& row) { return row.id != "notebook"; }),
               rows.end());
    return rows;
}
QWidget* Scene::page(InstanceId instance) const
{
    for (auto* widget : QApplication::allWidgets())
        if (widget->objectName() == "CordisNotebookPage" &&
            widget->property("cordis.instance").toULongLong() == instance.value())
            return widget;
    return nullptr;
}
void Scene::edit(InstanceId instance, const QString& text) const
{
    auto* widget = page(instance);
    PLATFORM_CHECK(widget);
    auto* editor = widget->findChild<QPlainTextEdit*>("CordisNotebookEditor");
    auto* publish = widget->findChild<QPushButton*>("CordisNotebookPublish");
    PLATFORM_CHECK(editor && publish);
    editor->setPlainText(text);
    publish->click();
    until([&] {
        const auto rows = take(state->shell.output()->snapshot());
        return std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
            return row.owner && row.owner->instance == instance && row.text == utf8(text);
        });
    });
    PLATFORM_CHECK(editor->toPlainText() == text);
}
void Scene::business(std::size_t count, bool separateRealms) const
{
    PLATFORM_CHECK(take(state->counter()) && take(state->counter())->session && take(state->reporter()) &&
                   take(state->reporter())->session);
    const auto counters = take(take(state->counter())->session->snapshot());
    const auto reporters = take(take(state->reporter())->session->snapshot());
    PLATFORM_CHECK(!counters.faulted && !reporters.faulted && counters.instances.size() == count &&
                   reporters.instances.size() == count);
    std::vector<std::string> logs;
    for (const auto& row : reporters.instances) {
        PLATFORM_CHECK(row.state == NativeInstanceState::Started && row.committedServices == 1 &&
                       row.pendingRequests == 0 && !row.deliveryBlocked && !row.uncertain && !row.failure);
        for (const auto& log : row.logs)
            logs.push_back(log.text);
    }
    std::sort(logs.begin(), logs.end());
    PLATFORM_CHECK(logs == std::vector<std::string>(count, "start result {\"value\":1}"));
    const auto graph = take(state->kernel->services().dependencyGraph());
    std::set<RealmId> counterRealms;
    for (const auto& reporter : reporters.instances) {
        const auto edge = std::find_if(graph.bindings.begin(), graph.bindings.end(), [&](const auto& binding) {
            return binding.binding.key.name == "example.counter" &&
                   std::any_of(binding.consumers.begin(), binding.consumers.end(), [&](const auto& consumer) {
                       return consumer.managed && consumer.instance == reporter.instance;
                   });
        });
        PLATFORM_CHECK(edge != graph.bindings.end() && edge->managedProvider);
        PLATFORM_CHECK(std::any_of(counters.instances.begin(), counters.instances.end(),
                                   [&](const auto& counter) { return counter.instance == edge->provider; }));
        counterRealms.insert(edge->binding.key.realm);
    }
    PLATFORM_CHECK(counterRealms.size() == (separateRealms ? count : 1u));
    const auto running = take(state->driver->snapshot());
    const auto session = take(state->session->snapshot());
    const auto kernel = take(state->kernel->snapshot());
    PLATFORM_CHECK(running.size() == count * 3 && session.coordinator.running.size() == running.size());
    for (const auto& row : running) {
        PLATFORM_CHECK(row.state == InstanceState::Active && !row.waitingForParent);
        PLATFORM_CHECK(
            std::any_of(session.coordinator.running.begin(), session.coordinator.running.end(), [&](const auto& other) {
                return row.path == other.path && row.instance == other.instance && row.epoch == other.epoch;
            }));
        PLATFORM_CHECK(std::any_of(kernel.instances.begin(), kernel.instances.end(), [&](const auto& instance) {
            return instance.instance == row.instance && instance.epoch == row.epoch;
        }));
    }
    const auto notebookPages = pages();
    PLATFORM_CHECK(notebookPages.size() == count);
    const auto commands = take(state->shell.commands()->snapshot());
    PLATFORM_CHECK(static_cast<std::size_t>(std::count_if(commands.begin(), commands.end(), [](const auto& row) {
                       return row.id == "notebook.publish";
                   })) == count);
    for (const auto& row : notebookPages) {
        const auto actual = std::find_if(running.begin(), running.end(), [&](const auto& entry) {
            return entry.configuration.plugin == "example.notebook" && entry.instance == row.owner.instance &&
                   entry.epoch == row.owner.epoch;
        });
        PLATFORM_CHECK(actual != running.end() && page(row.owner.instance));
    }
}
void verifySources(const Scene& scene, const std::string& version)
{
    const auto modules = take(scene.state->manager->snapshot());
    PLATFORM_CHECK(modules.size() == 3 && scene.state->pluginRoot() == utf8(scene.store));
    const auto definitions = scene.state->driver->configurationDefinitions();
    PLATFORM_CHECK(definitions &&
                   definitions->plugins() ==
                       (std::vector<std::string>{"example.counter", "example.notebook", "example.reporter"}));
    const auto source = take(parseConfiguration(scene.state->initial));
    PLATFORM_CHECK(source.structure().modules().size() == 3);
    const auto kernel = take(scene.state->kernel->snapshot());
    std::set<RuntimeId> runtimeIds;
    for (const auto& row : modules) {
        PLATFORM_CHECK(row.role == GenerationRole::Current && row.nativeMapped && row.descriptorAccepted &&
                       row.businessAccepted && !row.failure && !row.businessFailure &&
                       row.manifest.moduleVersion == version);
        const bool qt = row.manifest.moduleId == "org.cordis.example.notebook";
        PLATFORM_CHECK(row.manifest.plugins.size() == 1);
        const auto plugin = row.manifest.plugins.front();
        const auto declaration = take(definitions->module(plugin));
        PLATFORM_CHECK(source.structure().modules().at(declaration.alias) == declaration.manifest);
        const auto artifact = take(preflightModule(scene.state->pluginRoot(), declaration.manifest));
        PLATFORM_CHECK(artifact.manifest().moduleId == row.manifest.moduleId &&
                       artifact.manifest().sha256 == row.manifest.sha256 &&
                       artifact.manifestPath() == row.manifestPath);
        const auto path = QDir::cleanPath(QString::fromUtf8(row.manifestPath.c_str()));
        PLATFORM_CHECK(path.startsWith(QDir::cleanPath(scene.store) + '/', Qt::CaseInsensitive) &&
                       path.endsWith('/' + QString::fromStdString(row.manifest.sha256) + "/module.json"));
        PLATFORM_CHECK(row.loadPolicy == (qt ? ModuleLoadPolicy::Resident : ModuleLoadPolicy::ApprovedNative));
        PLATFORM_CHECK(row.manifest.loader == (qt ? ModuleLoader::QtUi : ModuleLoader::CAbi));
        PLATFORM_CHECK(row.manifest.unloadPolicy == (qt ? ModuleUnloadPolicy::Resident : ModuleUnloadPolicy::Explicit));
        const auto id = qt                            ? take(scene.state->notebookRuntime()).id()
                        : plugin == "example.counter" ? take(scene.state->counter())->runtime->id()
                                                      : take(scene.state->reporter())->runtime->id();
        const auto origin = take(scene.state->manager->runtimeOrigin(id));
        PLATFORM_CHECK(origin && origin->runtime == id && origin->generation == row.generation &&
                       origin->moduleId == row.manifest.moduleId && origin->pluginId == plugin);
        PLATFORM_CHECK(
            std::any_of(kernel.names.begin(), kernel.names.end(), [&](const auto& name) { return name.id == id; }));
        runtimeIds.insert(id);
        if (qt)
            PLATFORM_CHECK(take(scene.state->generation()) == row.generation && row.nativePinned);
    }
    PLATFORM_CHECK(runtimeIds.size() == 3 && kernel.runtimes == 4);
    PLATFORM_CHECK(take(scene.state->counter())->runtime->stateSchema() &&
                   take(scene.state->counter())->runtime->stateSchema()->id == "org.cordis.example.counter" &&
                   !take(scene.state->reporter())->runtime->stateSchema());
    PLATFORM_CHECK(take(scene.state->notebookRuntime()).stateSchema() &&
                   take(scene.state->notebookRuntime()).stateSchema()->id == "org.cordis.example.notebook.draft");
}
void installSession(Scene& scene, const QString& sourceBin, const std::string& variant, bool invalidCounter)
{
    PLATFORM_CHECK(variant == "a" || variant == "b");
    std::map<std::string, std::string> locators;
    std::vector<InstalledArtifact> installed;
    for (const auto* alias : {"counter", "reporter", "notebook"}) {
        const auto prefix = std::string(alias) == "notebook" ? "qt-upgrade/" : "native-upgrade/";
        auto artifact = take(installModuleArtifact(
            utf8(sourceBin), prefix + std::string(alias) + '-' + variant + "/module.json", utf8(scene.store)));
        locators.emplace(alias, artifact.artifact().manifest().sha256 + "/module.json");
        installed.push_back(std::move(artifact));
    }
    std::string yaml = "formatVersion: 1\nmodules:\n";
    for (const auto& locator : locators)
        yaml += "  " + locator.first + ": {manifest: " + locator.second + "}\n";
    yaml += "plugins:\n"
            "  - {id: counter, plugin: example.counter, module: counter, config: " +
            std::string(invalidCounter ? "{invalid: true}" : "null") +
            "}\n"
            "  - {id: reporter, plugin: example.reporter, module: reporter, config: null}\n"
            "  - {id: notes, plugin: example.notebook, module: notebook, config: {title: SavedB, text: preserved}}\n";
    take(parseConfiguration(yaml));
    QFile file(scene.file);
    PLATFORM_CHECK(file.open(QIODevice::WriteOnly));
    PLATFORM_CHECK(file.write(QByteArray::fromStdString(yaml)) == static_cast<qint64>(yaml.size()));
    file.close();
}
void Scene::verifyClosed(bool counterDrained) const
{
    const auto closed = take(state->closeSnapshot());
    PLATFORM_CHECK(closed.closed && !closed.retained && !closed.deliveryBlocked && !state->deliveryBlocked());
    PLATFORM_CHECK(closed.session && closed.session->hasValue() && closed.kernel && closed.kernel->hasValue() &&
                   closed.kernel->value().quiescent);
    PLATFORM_CHECK(closed.counter && closed.counter->hasValue() && closed.counter->value().quiescent &&
                   closed.reporter && closed.reporter->hasValue() && closed.reporter->value().quiescent);
    PLATFORM_CHECK(closed.shell && closed.shell->hasValue() && closed.providers && closed.providers->hasValue());
    PLATFORM_CHECK(closed.counterDrain && closed.counterDrain->hasValue() == counterDrained && closed.reporterDrain &&
                   closed.reporterDrain->hasValue());
    PLATFORM_CHECK(!state->kernel && !state->driver && !state->session && !state->configurationManagement);
    PLATFORM_CHECK(!take(state->counter())->runtime && !take(state->reporter())->runtime &&
                   !state->notebookRuntime().hasValue());
    PLATFORM_CHECK(pages().empty() && take(state->shell.commands()->snapshot()).empty());
    const auto readiness = state->ready().tryResult();
    PLATFORM_CHECK(readiness && !readiness->hasValue() && readiness->error().code == ErrorCode::ContextClosing);
    const auto applied = state->apply(state->initial);
    PLATFORM_CHECK(!applied.hasValue() && applied.error().code == ErrorCode::ContextClosing);
    const auto repeated = state->close().tryResult();
    PLATFORM_CHECK(repeated && repeated->hasValue());
    for (const auto& row : take(state->manager->snapshot())) {
        PLATFORM_CHECK(row.nativeMapped && !row.failure && !row.businessFailure);
        if (row.manifest.loader == ModuleLoader::QtUi) {
            PLATFORM_CHECK(row.nativePinned && row.resources.records.size() == 1 &&
                           row.resources.records.front().kind == ResourceKind::QObject &&
                           row.resources.records.front().label == "qt.loader.root");
        } else if (counterDrained || row.manifest.moduleId == "org.cordis.example.reporter")
            PLATFORM_CHECK(row.state == ModuleState::Resident && row.resources.drained());
        else
            PLATFORM_CHECK(!row.resources.drained());
    }
}
void Scene::release()
{
    std::weak_ptr<notebook::Workspace> weak = state;
    state.reset();
    barrier(scheduler);
    PLATFORM_CHECK(weak.expired());
    until([&] { return take(scheduler->snapshot()).queued == 0; });
    const auto queue = take(scheduler->snapshot());
    PLATFORM_CHECK(queue.open && queue.discarded == 0 && queue.callbackErrors == 0);
    take(scheduler->close());
}
} // namespace platform_workspace_test
