/** @file workspace_endurance_catalog.cpp
 * @brief 从原 Core 和模块目录复制纯值；当前资源形状与递增身份、纯卸载历史分别记录。
 */
#include "workspace_directory_p.hpp"
#include "workspace_endurance_p.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <map>

namespace workspace_endurance_test {
namespace {
using namespace cordis;
using namespace cordis::module;
using platform_workspace_test::take;

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}
QString number(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}
QJsonObject error(const ErrorInfo& value)
{
    QJsonArray causes;
    for (const auto& cause : value.causes)
        causes.append(error(cause));
    return {{"code", static_cast<int>(value.code)},
            {"message", text(value.message)},
            {"phase", text(value.phase)},
            {"operation", number(value.operation.value())},
            {"instance", value.instance ? QJsonValue(number(value.instance->value())) : QJsonValue()},
            {"path", value.entryPath ? QJsonValue(text(*value.entryPath)) : QJsonValue()},
            {"causes", causes}};
}
QJsonValue error(const std::optional<ErrorInfo>& value)
{
    return value ? QJsonValue(error(*value)) : QJsonValue();
}
QJsonArray strings(const std::vector<std::string>& values)
{
    QJsonArray result;
    for (const auto& value : values)
        result.append(text(value));
    return result;
}
using Counts = std::map<QString, std::size_t>;
QJsonObject counts(const Counts& values)
{
    QJsonObject result;
    for (const auto& value : values)
        result.insert(value.first, number(value.second));
    return result;
}
QString key(const ServiceKey& value)
{
    return text(value.name) + "/v" + number(value.interfaceMajor) + "/realm=" + number(value.realm.value());
}
QJsonObject scope(const ScopeSnapshot& value, bool& healthy)
{
    Counts effects;
    std::size_t setups = 0, cleanups = 0, remaining = 0, retained = 0;
    healthy = healthy && !value.closing && !value.deliveryBlocked && value.errors.empty();
    for (const auto& effect : value.effects) {
        ++effects[text(effect.label) + "/state=" + QString::number(static_cast<int>(effect.state))];
        setups += effect.setupPending ? 1u : 0u;
        cleanups += effect.cleanupPending ? 1u : 0u;
        remaining += effect.remainingCleanups;
        retained += effect.retainedCleanups;
        healthy = healthy && !effect.setupPending && !effect.cleanupPending && !effect.deliveryBlocked &&
                  effect.errors.empty() && effect.retainedCleanups == 0;
    }
    return {{"closing", value.closing},
            {"deliveryBlocked", value.deliveryBlocked},
            {"effects", counts(effects)},
            {"setupPending", number(setups)},
            {"cleanupPending", number(cleanups)},
            {"remainingCleanups", number(remaining)},
            {"retainedCleanups", number(retained)}};
}
QJsonObject scopeDetail(const ScopeSnapshot& value)
{
    QJsonArray errors, effects;
    for (const auto& failure : value.errors)
        errors.append(error(failure));
    for (const auto& effect : value.effects) {
        QJsonArray failures;
        for (const auto& failure : effect.errors)
            failures.append(error(failure));
        effects.append(QJsonObject{{"id", number(effect.id.value())},
                                   {"label", text(effect.label)},
                                   {"state", static_cast<int>(effect.state)},
                                   {"deliveryBlocked", effect.deliveryBlocked},
                                   {"errors", failures}});
    }
    return {{"errors", errors}, {"effects", effects}};
}
QJsonObject nativeModule(const NativeModuleSnapshot& value, QJsonArray& identities, bool& healthy)
{
    QJsonArray instances;
    healthy = healthy && !value.closing && !value.closed && !value.faulted && value.instances.size() == 1;
    for (const auto& instance : value.instances) {
        QJsonArray logs;
        for (const auto& log : instance.logs)
            logs.append(QJsonObject{{"level", number(log.level)}, {"text", text(log.text)}});
        identities.append(QJsonObject{{"generation", number(instance.generation.value())},
                                      {"instance", number(instance.instance.value())},
                                      {"epoch", number(instance.epoch)},
                                      {"plugin", text(instance.pluginId)},
                                      {"failure", error(instance.failure)},
                                      {"resources", scopeDetail(instance.resources)},
                                      {"logs", logs}});
        healthy = healthy && instance.state == NativeInstanceState::Started && instance.nativeCreated &&
                  !instance.closing && !instance.uncertain && !instance.deliveryBlocked && !instance.failure &&
                  instance.pendingRequests == 0 && instance.activeHostCalls == 0 && instance.pendingHostOperations == 0;
        instances.append(QJsonObject{{"plugin", text(instance.pluginId)},
                                     {"state", static_cast<int>(instance.state)},
                                     {"nativeCreated", instance.nativeCreated},
                                     {"closing", instance.closing},
                                     {"uncertain", instance.uncertain},
                                     {"deliveryBlocked", instance.deliveryBlocked},
                                     {"requests", number(instance.pendingRequests)},
                                     {"hostCalls", number(instance.activeHostCalls)},
                                     {"hostOperations", number(instance.pendingHostOperations)},
                                     {"committedServices", number(instance.committedServices)},
                                     {"resources", scope(instance.resources, healthy)}});
    }
    return {{"closing", value.closing}, {"closed", value.closed}, {"faulted", value.faulted}, {"instances", instances}};
}
} // namespace

CatalogSample sampleCatalog(const Scene& scene)
{
    PLATFORM_CHECK(scene.scheduler->isCurrentThread() && scene.state && scene.state->kernel && scene.state->session &&
                   scene.state->driver && scene.state->manager);
    CatalogSample sample;
    const auto registry = scene.state->kernel->services();
    sample.dependencyEvaluations = take(registry.dependencyEvaluationCount());
    const auto core = take(scene.state->kernel->snapshot());
    const auto graph = take(registry.dependencyGraph());
    const auto claims = take(scene.state->kernel->replacements());
    const auto queue = take(scene.scheduler->snapshot());
    const auto session = take(scene.state->session->snapshot());
    const auto running = take(scene.state->driver->snapshot());
    const auto upgrade = take(scene.state->upgradeSnapshot());
    const auto values = workspace_directory_test::directory();
    bool healthy = !core.closing && !core.deliveryBlocked && claims.empty() && values.reservations.isEmpty() &&
                   values.readers.isEmpty() && !upgrade.active && upgrade.accepting && !upgrade.deliveryBlocked;

    std::map<RuntimeId, QString> names;
    for (const auto& runtime : core.names)
        names.emplace(runtime.id, text(runtime.name));
    std::map<InstanceId, QString> owners;
    for (const auto& instance : core.instances) {
        const auto name = names.find(instance.runtime);
        PLATFORM_CHECK(name != names.end());
        owners.emplace(instance.instance, name->second);
    }
    const auto owner = [&](InstanceId instance) {
        const auto found = owners.find(instance);
        return found == owners.end() ? QStringLiteral("external") : found->second;
    };
    QJsonObject coreShape;
    QJsonArray coreDetail;
    for (const auto& instance : core.instances) {
        healthy = healthy && instance.state == InstanceState::Active && !instance.deliveryBlocked &&
                  !instance.failure && !instance.finalizing;
        QJsonObject shape{{"state", static_cast<int>(instance.state)},
                          {"children", number(instance.children)},
                          {"committedLeases", number(instance.committedLeases)},
                          {"finalizing", instance.finalizing},
                          {"resources", scope(instance.resources, healthy)}};
        if (instance.finalizers)
            shape.insert("finalizers", scope(*instance.finalizers, healthy));
        PLATFORM_CHECK(!coreShape.contains(owner(instance.instance)));
        coreShape.insert(owner(instance.instance), shape);
        coreDetail.append(QJsonObject{
            {"instance", number(instance.instance.value())},
            {"runtime", number(instance.runtime.value())},
            {"name", owner(instance.instance)},
            {"epoch", number(instance.epoch)},
            {"revision", number(instance.revision)},
            {"failure", error(instance.failure)},
            {"resources", scopeDetail(instance.resources)},
            {"finalizers", instance.finalizers ? QJsonValue(scopeDetail(*instance.finalizers)) : QJsonValue()}});
    }
    sample.stable.insert(
        "kernel",
        QJsonObject{{"runtimes", number(core.runtimes)}, {"instances", coreShape}, {"claims", number(claims.size())}});
    sample.detail.insert("kernel", coreDetail);
    QJsonArray coreClaims;
    for (const auto& claim : claims)
        coreClaims.append(QJsonObject{{"operation", number(claim.operation.value())},
                                      {"state", static_cast<int>(claim.state)},
                                      {"abandoned", claim.abandoned},
                                      {"deliveryBlocked", claim.deliveryBlocked},
                                      {"cancelled", claim.cancelled},
                                      {"failure", error(claim.failure)}});
    sample.detail.insert("coreClaims", coreClaims);

    QJsonObject modules;
    QJsonArray generations;
    std::size_t unloaded = 0, heavy = 0;
    for (const auto& value : values.generations) {
        const auto& row = value.module;
        Counts resources;
        QJsonArray records, runtimes, instances;
        for (const auto& record : row.resources.records) {
            auto label = text(record.label);
            if (row.manifest.loader == ModuleLoader::QtUi && record.kind == ResourceKind::Instance) {
                const auto actual =
                    std::find_if(value.instances.begin(), value.instances.end(), [&](const auto& origin) {
                        return record.label == "qt.instance." + std::to_string(origin.instance.value()) + "." +
                                                   std::to_string(origin.epoch);
                    });
                healthy = healthy && actual != value.instances.end();
                if (actual != value.instances.end())
                    label = QStringLiteral("qt.instance.<current>");
            }
            ++resources[QString::number(static_cast<int>(record.kind)) + ':' + label];
            records.append(QJsonObject{
                {"id", number(record.id)}, {"kind", static_cast<int>(record.kind)}, {"label", text(record.label)}});
            healthy = healthy && record.kind != ResourceKind::Call && record.kind != ResourceKind::QueuePayload;
        }
        for (const auto& runtime : value.runtimes) {
            const auto registered = names.find(runtime.runtime);
            healthy = healthy && registered != names.end() && registered->second == text(runtime.pluginId);
            runtimes.append(
                QJsonObject{{"runtime", number(runtime.runtime.value())}, {"plugin", text(runtime.pluginId)}});
        }
        for (const auto& instance : value.instances) {
            healthy = healthy && instance.managed &&
                      std::any_of(core.instances.begin(), core.instances.end(), [&](const auto& actual) {
                          return actual.instance == instance.instance && actual.runtime == instance.runtime &&
                                 actual.epoch == instance.epoch;
                      });
            instances.append(QJsonObject{{"instance", number(instance.instance.value())},
                                         {"runtime", number(instance.runtime.value())},
                                         {"epoch", number(instance.epoch)},
                                         {"managed", instance.managed},
                                         {"creationOperation", number(instance.operation.value())}});
        }
        generations.append(QJsonObject{
            {"generation", number(row.generation.value())},
            {"module", text(row.manifest.moduleId)},
            {"version", text(row.manifest.moduleVersion)},
            {"sha256", text(row.manifest.sha256)},
            {"state", static_cast<int>(row.state)},
            {"role", static_cast<int>(row.role)},
            {"loading", value.loading},
            {"admissions", number(value.admissions)},
            {"nativeMapped", row.nativeMapped},
            {"nativePinned", row.nativePinned},
            {"imageReference", value.image},
            {"observationImageReference", value.observationImage},
            {"artifactFileGuard", value.artifactFile},
            {"backend", value.backend},
            {"manifestPath", text(row.manifestPath)},
            {"libraryPath", text(row.libraryPath)},
            {"failure", error(row.failure)},
            {"businessFailure", error(row.businessFailure)},
            {"unloadFailure", error(row.unloadFailure)},
            {"records", records},
            {"pinReasons", strings(row.resources.pinReasons)},
            {"runtimes", runtimes},
            {"instances", instances},
            {"reservation", value.reservation ? QJsonValue(number(value.reservation->value())) : QJsonValue()}});
        healthy = healthy && !value.loading && value.admissions == 0 && !value.reservation && !value.observationImage &&
                  !row.failure && !row.businessFailure && !row.unloadFailure;
        if (row.state == ModuleState::Unloaded) {
            ++unloaded;
            healthy = healthy && row.role == GenerationRole::Retired && !row.nativeMapped && !row.nativePinned &&
                      row.resources.drained() && row.resources.pinReasons.empty() && value.runtimes.empty() &&
                      value.instances.empty() && !value.image && !value.artifactFile && !value.backend;
        } else {
            ++heavy;
            healthy = healthy && row.role == GenerationRole::Current && row.nativeMapped && value.image &&
                      value.artifactFile && value.backend && row.resources.accepting && value.runtimes.size() == 1 &&
                      value.instances.size() == 1;
            if (row.manifest.loader == ModuleLoader::CAbi)
                healthy = healthy && row.loadPolicy == ModuleLoadPolicy::ApprovedNative && !row.nativePinned &&
                          row.resources.pinReasons.empty();
            const QJsonObject shape{{"loader", static_cast<int>(row.manifest.loader)},
                                    {"role", static_cast<int>(row.role)},
                                    {"state", static_cast<int>(row.state)},
                                    {"policy", static_cast<int>(row.loadPolicy)},
                                    {"nativeMapped", row.nativeMapped},
                                    {"nativePinned", row.nativePinned},
                                    {"accepting", row.resources.accepting},
                                    {"resources", counts(resources)},
                                    {"pinReasons", strings(row.resources.pinReasons)},
                                    {"origins", number(value.runtimes.size())},
                                    {"instances", number(value.instances.size())},
                                    {"admissions", number(value.admissions)}};
            PLATFORM_CHECK(!modules.contains(text(row.manifest.moduleId)));
            modules.insert(text(row.manifest.moduleId), shape);
        }
    }
    healthy = healthy && heavy == 3 && unloaded <= 128;
    modules.insert("heavyGenerations", number(heavy));
    modules.insert("reservations", number(values.reservations.size()));
    modules.insert("readerModules", number(values.readers.size()));
    modules.insert("readerOperations", number(values.readerOperations));
    sample.stable.insert("modules", modules);
    sample.detail.insert("generations", generations);
    sample.detail.insert("unloadedHistory", number(unloaded));
    sample.detail.insert("moduleReservations", values.reservations);
    sample.detail.insert("moduleReaders", values.readers);

    QJsonObject bindings, declarations;
    QJsonArray bindingDetail;
    for (const auto& entry : graph.bindings) {
        const auto& binding = entry.binding;
        Counts consumers;
        QJsonArray consumerDetail;
        for (const auto& consumer : entry.consumers) {
            healthy = healthy && (!consumer.managed || owners.count(consumer.instance) != 0);
            ++consumers[owner(consumer.instance) + (consumer.managed ? ":managed" : ":unmanaged")];
            consumerDetail.append(QJsonObject{{"instance", number(consumer.instance.value())},
                                              {"epoch", number(consumer.epoch)},
                                              {"managed", consumer.managed}});
        }
        healthy = healthy && binding.state == BindingState::Published && binding.activeCalls == 0 &&
                  !binding.deliveryBlocked && (!entry.managedProvider || owners.count(entry.provider) != 0);
        bindings.insert(key(binding.key), QJsonObject{{"state", static_cast<int>(binding.state)},
                                                      {"provider", owner(entry.provider)},
                                                      {"managedProvider", entry.managedProvider},
                                                      {"calls", number(binding.activeCalls)},
                                                      {"leases", number(binding.committedLeases)},
                                                      {"minor", number(binding.metadata.minor)},
                                                      {"capabilities", strings(binding.metadata.capabilities)},
                                                      {"consumers", counts(consumers)}});
        bindingDetail.append(QJsonObject{{"id", number(binding.id.value())},
                                         {"key", key(binding.key)},
                                         {"provider", number(entry.provider.value())},
                                         {"consumers", consumerDetail}});
    }
    for (const auto& entry : graph.instances) {
        healthy = healthy && !entry.publicationHeld;
        QJsonArray dependencies, outputs;
        for (const auto& dependency : entry.dependencies)
            dependencies.append(key(dependency));
        for (const auto& output : entry.outputs)
            outputs.append(key(output));
        PLATFORM_CHECK(!declarations.contains(owner(entry.instance)));
        declarations.insert(owner(entry.instance), QJsonObject{{"parent", owner(entry.parent)},
                                                               {"dependencies", dependencies},
                                                               {"outputs", outputs},
                                                               {"publicationHeld", entry.publicationHeld}});
    }
    sample.stable.insert("services", QJsonObject{{"bindings", bindings}, {"declarations", declarations}});
    sample.detail.insert("bindings", bindingDetail);

    QJsonObject nativeShape;
    QJsonArray nativeDetail;
    {
        const auto counter = take(scene.state->counter());
        const auto reporter = take(scene.state->reporter());
        PLATFORM_CHECK(counter && counter->session && reporter && reporter->session);
        nativeShape.insert("counter", nativeModule(take(counter->session->snapshot()), nativeDetail, healthy));
        nativeShape.insert("reporter", nativeModule(take(reporter->session->snapshot()), nativeDetail, healthy));
    }
    sample.stable.insert("native", nativeShape);
    sample.detail.insert("native", nativeDetail);
    QJsonObject pages, commands;
    for (const auto& page : take(scene.state->shell.pages()->snapshot())) {
        healthy = healthy && !page.retiring;
        const auto name = owner(page.owner.instance) + ':' + text(page.id);
        PLATFORM_CHECK(!pages.contains(name));
        pages.insert(name, QJsonObject{{"navigation", page.navigationPresent},
                                       {"widget", page.widgetAlive},
                                       {"current", page.current}});
    }
    for (const auto& command : take(scene.state->shell.commands()->snapshot())) {
        healthy = healthy && !command.retiring && !command.deliveryBlocked && command.activeCalls == 0;
        const auto name = owner(command.owner.instance) + ':' + text(command.id);
        PLATFORM_CHECK(!commands.contains(name));
        commands.insert(name, QJsonObject{{"title", text(command.title)},
                                          {"calls", number(command.activeCalls)},
                                          {"retiring", command.retiring},
                                          {"deliveryBlocked", command.deliveryBlocked}});
    }
    sample.stable.insert("ui", QJsonObject{{"pages", pages}, {"commands", commands}});
    healthy = healthy && queue.open && !queue.running && queue.queued == 0 && queue.discarded == 0 &&
              queue.callbackErrors == 0 && queue.accepted == queue.executed;
    sample.stable.insert("queue", QJsonObject{{"open", queue.open},
                                              {"running", queue.running},
                                              {"queued", number(queue.queued)},
                                              {"discarded", number(queue.discarded)},
                                              {"callbackErrors", number(queue.callbackErrors)}});
    sample.detail.insert("queue",
                         QJsonObject{{"accepted", number(queue.accepted)}, {"executed", number(queue.executed)}});
    const auto& coordinator = session.coordinator;
    const auto& store = session.store;
    QJsonObject runningShape;
    QJsonArray runningDetail;
    healthy = healthy && coordinator.running.size() == running.size();
    for (const auto& entry : running) {
        healthy = healthy && entry.state == InstanceState::Active && !entry.waitingForParent && !entry.failure &&
                  std::any_of(coordinator.running.begin(), coordinator.running.end(),
                              [&](const auto& actual) {
                                  return actual.path == entry.path && actual.instance == entry.instance &&
                                         actual.epoch == entry.epoch && actual.state == entry.state && !actual.failure;
                              }) &&
                  std::any_of(core.instances.begin(), core.instances.end(), [&](const auto& actual) {
                      return actual.instance == entry.instance && actual.epoch == entry.epoch;
                  });
        runningShape.insert(text(entry.path), QJsonObject{{"plugin", text(entry.configuration.plugin)},
                                                          {"state", static_cast<int>(entry.state)},
                                                          {"waitingForParent", entry.waitingForParent}});
        runningDetail.append(QJsonObject{{"path", text(entry.path)},
                                         {"instance", number(entry.instance.value())},
                                         {"epoch", number(entry.epoch)},
                                         {"parentEpoch", number(entry.parentEpoch)},
                                         {"failure", error(entry.failure)}});
    }
    healthy = healthy && !session.closing && !session.closed && !session.deliveryBlocked && !session.executionError &&
              !coordinator.activeRevision && !coordinator.closing && !coordinator.unsafe &&
              !coordinator.deliveryBlocked && !coordinator.runningError && !store.writing && store.queuedWrites == 0 &&
              !store.dirty && !store.closing && !store.closed && !store.deliveryBlocked && !store.lastError;
    sample.stable.insert("configuration", QJsonObject{{"running", number(coordinator.running.size())},
                                                      {"paths", runningShape},
                                                      {"writing", store.writing},
                                                      {"queuedWrites", number(store.queuedWrites)},
                                                      {"dirty", store.dirty},
                                                      {"deliveryBlocked", session.deliveryBlocked}});
    sample.detail.insert(
        "configuration",
        QJsonObject{{"desiredRevision", number(coordinator.desiredRevision)},
                    {"persistedRevision",
                     store.persistedRevision ? QJsonValue(number(*store.persistedRevision)) : QJsonValue()},
                    {"executionError", error(session.executionError)},
                    {"runningError", error(coordinator.runningError)},
                    {"storeError", error(store.lastError)},
                    {"running", runningDetail}});
    sample.detail.insert("dependencyEvaluations", number(sample.dependencyEvaluations));
    sample.detail.insert("upgradeHistory", number(upgrade.history.size()));
    sample.detail.insert("outputHistory", number(take(scene.state->shell.output()->snapshot()).size()));
    sample.detail.insert("notificationHistory", number(take(scene.state->shell.notifications()->snapshot()).size()));
    if (!healthy)
        throw std::runtime_error("endurance catalog is not settled: " +
                                 QJsonDocument(QJsonObject{{"stable", sample.stable}, {"detail", sample.detail}})
                                     .toJson(QJsonDocument::Compact)
                                     .toStdString());
    return sample;
}
} // namespace workspace_endurance_test
