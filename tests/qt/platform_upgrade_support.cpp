/** @file platform_upgrade_support.cpp
 * @brief 以原目录、配置和 Frozen 能力核对真实升级，不增加插件测试导出。
 */
#include "../../src/core/instance_state_p.hpp"
#include "platform_upgrade_p.hpp"
#include <QFile>
#include <algorithm>

namespace platform_upgrade_test {
RunningEntry running(const Scene& scene, const std::string& plugin)
{
    const auto rows = take(scene.state->driver->snapshot());
    const auto row =
        std::find_if(rows.begin(), rows.end(), [&](const auto& value) { return value.configuration.plugin == plugin; });
    PLATFORM_CHECK(row != rows.end() && row->state == InstanceState::Active);
    return *row;
}
ModuleGenerationSnapshot module(const Scene& scene, GenerationId generation)
{
    const auto rows = take(scene.state->manager->snapshot());
    const auto row =
        std::find_if(rows.begin(), rows.end(), [&](const auto& value) { return value.generation == generation; });
    PLATFORM_CHECK(row != rows.end());
    return *row;
}
UpgradePreview preview(Scene& scene, const QString& sourceBin, const std::string& alias, const std::string& variant)
{
    const auto prefix = alias == "notebook" ? "qt-upgrade/" : "native-upgrade/";
    return take(scene.state->previewUpgrade(alias, sourceBin, prefix + alias + '-' + variant + "/module.json"));
}
WorkspaceUpgradeReport perform(Scene& scene, const UpgradePreview& value)
{
    const auto task = take(scene.state->upgrade(value));
    const auto result = wait(task, scene.scheduler);
    return take(*result);
}
void applied(Scene& scene, const UpgradePreview& value, const WorkspaceUpgradeReport& report)
{
    PLATFORM_CHECK(report.alias == value.alias && report.previous == value.previous && report.candidate.valid() &&
                   report.candidate != report.previous && report.retired == report.previous);
    PLATFORM_CHECK(report.replacement && report.replacement->operation.valid() &&
                   report.replacement->previous == report.previous &&
                   report.replacement->candidate == report.candidate &&
                   report.replacement->outcome == ReplacementOutcome::Applied && report.replacement->committed &&
                   !report.replacement->recovered && !report.replacement->resourcesRetained && !report.failure &&
                   !report.retained && !report.deliveryBlocked);
    const auto current = module(scene, report.candidate);
    const auto old = module(scene, report.previous);
    PLATFORM_CHECK(current.role == GenerationRole::Current && old.role == GenerationRole::Retired &&
                   current.manifest.sha256 == value.artifact.manifest().sha256 && !current.replacement &&
                   !old.replacement && !current.businessFailure && !current.failure);
    const auto plugin = "example." + value.alias;
    const auto runtime = value.alias == "notebook"  ? take(scene.state->notebookRuntime()).id()
                         : value.alias == "counter" ? take(scene.state->counter())->runtime->id()
                                                    : take(scene.state->reporter())->runtime->id();
    const auto origin = take(scene.state->manager->runtimeOrigin(runtime));
    PLATFORM_CHECK(origin && origin->generation == report.candidate && origin->pluginId == plugin &&
                   runtime != value.previousRuntime);
    const auto core = take(scene.state->kernel->snapshot());
    PLATFORM_CHECK(std::any_of(core.names.begin(), core.names.end(),
                               [&](const auto& row) { return row.name == plugin && row.id == runtime; }));
    PLATFORM_CHECK(take(scene.state->kernel->replacements()).empty());
    const auto locator = take(scene.state->driver->configurationDefinitions()->module(plugin)).manifest;
    PLATFORM_CHECK(locator == current.manifest.sha256 + "/module.json");
    const auto session = take(scene.state->session->snapshot());
    PLATFORM_CHECK(session.coordinator.desired && session.coordinator.desiredRevision == value.desiredRevision + 1 &&
                   !session.coordinator.activeRevision &&
                   session.store.desiredRevision == session.coordinator.desiredRevision &&
                   session.store.desired == session.coordinator.desired);
    PLATFORM_CHECK(session.coordinator.desired->source().structure().modules().at(value.alias) == locator);
    const auto actual = running(scene, plugin);
    PLATFORM_CHECK(
        std::any_of(session.coordinator.running.begin(), session.coordinator.running.end(), [&](const auto& row) {
            return row.instance == actual.instance && row.epoch == actual.epoch && row.path == actual.path;
        }));
    const auto diagnostic = take(scene.state->shell.diagnostics()->snapshot());
    const auto managed = std::find_if(diagnostic.instances.begin(), diagnostic.instances.end(),
                                      [&](const auto& row) { return row.instance.instance == actual.instance; });
    PLATFORM_CHECK(managed == diagnostic.instances.end() ||
                   (!managed->canStop && !managed->canRetry && !managed->canLoad));
    PLATFORM_CHECK(!scene.state->shell.management()->stop(actual.instance).hasValue() &&
                   !scene.state->shell.management()->retry(actual.instance).hasValue() &&
                   !scene.state->shell.management()->load(actual.instance).hasValue());
    const auto saved = wait(take(scene.state->session->save(session.coordinator.desiredRevision)), scene.scheduler);
    PLATFORM_CHECK(saved->hasValue() && (saved->value().outcome == SaveOutcome::Written ||
                                         saved->value().outcome == SaveOutcome::Unchanged));
    QFile file(scene.file);
    PLATFORM_CHECK(file.open(QIODevice::ReadOnly));
    const auto disk = take(parseConfiguration(file.readAll().toStdString()));
    PLATFORM_CHECK(disk.structure().modules().at(value.alias) == locator);
    const auto snapshot = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(snapshot.accepting && !snapshot.active && !snapshot.deliveryBlocked && !snapshot.history.empty() &&
                   snapshot.history.back().replacement->operation == report.replacement->operation);
}
void finish(Scene& scene)
{
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    const auto closed = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(closed.closed && !closed.retained && !closed.deliveryBlocked && closed.kernel &&
                   closed.kernel->hasValue() && closed.kernel->value().quiescent && closed.session &&
                   closed.session->hasValue() && closed.counter && closed.counter->hasValue() &&
                   closed.counter->value().quiescent && closed.reporter && closed.reporter->hasValue() &&
                   closed.reporter->value().quiescent && closed.shell && closed.shell->hasValue() && closed.providers &&
                   closed.providers->hasValue());
    PLATFORM_CHECK(!scene.state->kernel && !scene.state->driver && !scene.state->session && scene.pages().empty() &&
                   take(scene.state->shell.commands()->snapshot()).empty());
    for (const auto& row : take(scene.state->manager->snapshot())) {
        PLATFORM_CHECK(!row.replacement && !row.failure && !row.businessFailure);
        if (row.state == ModuleState::Unloaded) {
            PLATFORM_CHECK(!row.nativeMapped && row.resources.records.empty());
        } else if (row.manifest.loader == ModuleLoader::QtUi) {
            PLATFORM_CHECK(row.nativeMapped && row.nativePinned && row.resources.records.size() == 1 &&
                           row.resources.records.front().label == "qt.loader.root");
        } else {
            PLATFORM_CHECK(row.state == ModuleState::Resident && row.resources.drained());
        }
    }
    scene.release();
}
std::string counterValue(Scene& scene)
{
    std::string json;
    {
        const auto id = take(scene.state->counter())->runtime->id();
        const auto actual = running(scene, "example.counter");
        const auto instance = take(scene.state->kernel->findInstance(actual.instance));
        auto scope = take(scene.state->kernel->reserveReplacement({id}));
        take(scope.freeze());
        const auto captured = take(cordis::detail::InstanceStateAccess::capture(scope, instance));
        PLATFORM_CHECK(captured && captured->schema.id == "org.cordis.example.counter");
        json = captured->json;
        take(scope.release());
    }
    barrier(scene.scheduler);
    return json;
}
void reporterValue(const Scene& scene, const std::string& value)
{
    const auto actual = running(scene, "example.reporter");
    const auto rows = take(take(scene.state->reporter())->session->snapshot());
    const auto row = std::find_if(rows.instances.begin(), rows.instances.end(),
                                  [&](const auto& instance) { return instance.instance == actual.instance; });
    PLATFORM_CHECK(row != rows.instances.end() && row->state == NativeInstanceState::Started && !row->logs.empty() &&
                   row->logs.back().text == "start result {\"value\":" + value + '}');
}
} // namespace platform_upgrade_test
