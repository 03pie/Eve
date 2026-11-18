/** @file workspace_stress_observe.cpp
 * @brief 按实际目标、运行身份和来源账目交叉核对；不推进队列或持有业务能力。
 */
#include "workspace_directory_p.hpp"
#include "workspace_stress_p.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace workspace_stress_test {
namespace {
using namespace platform_upgrade_test;
using workspace_directory_test::DirectoryValues;
using workspace_directory_test::GenerationValues;

template <class T> T checked(Result<T> result)
{
    if (!result.hasValue())
        throw ProductError(result.error());
    return std::move(result).value();
}
QString text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}
QJsonValue failure(const std::optional<ErrorInfo>& value)
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
QJsonArray failures(const std::vector<ErrorInfo>& values)
{
    QJsonArray result;
    for (const auto& value : values)
        result.append(error(value));
    return result;
}
QJsonObject key(const ServiceKey& value)
{
    return {
        {"name", text(value.name)}, {"major", decimal(value.interfaceMajor)}, {"realm", decimal(value.realm.value())}};
}
QJsonArray keys(const std::vector<ServiceKey>& values)
{
    QJsonArray result;
    for (const auto& value : values)
        result.append(key(value));
    return result;
}
QJsonObject identity(ContextIdentity value)
{
    return {{"instance", decimal(value.instance.value())}, {"epoch", decimal(value.epoch)}};
}
QJsonObject configuration(const EntryValue& value)
{
    QJsonObject realms;
    for (const auto& entry : value.realms)
        realms.insert(text(entry.first), text(entry.second));
    QJsonArray inject;
    for (const auto& entry : value.inject)
        inject.append(QJsonObject{{"name", text(entry.name)},
                                  {"major", decimal(entry.major)},
                                  {"minimum_minor", decimal(entry.minimumMinor)},
                                  {"capabilities", strings(entry.capabilities)}});
    // config 保留原解析器的数字原文；不得经 QJsonDocument 数值往返。
    return {{"plugin", text(value.plugin)},
            {"module", text(value.module)},
            {"disabled", value.disabled},
            {"effective_disabled", value.effectiveDisabled},
            {"config", text(value.config)},
            {"realms", realms},
            {"inject", inject}};
}
QJsonObject scope(const ScopeSnapshot& value)
{
    QJsonArray effects;
    for (const auto& effect : value.effects)
        effects.append(QJsonObject{{"id", decimal(effect.id.value())},
                                   {"label", text(effect.label)},
                                   {"state", static_cast<int>(effect.state)},
                                   {"setup_pending", effect.setupPending},
                                   {"cleanup_pending", effect.cleanupPending},
                                   {"remaining_cleanups", decimal(effect.remainingCleanups)},
                                   {"retained_cleanups", decimal(effect.retainedCleanups)},
                                   {"delivery_blocked", effect.deliveryBlocked},
                                   {"errors", failures(effect.errors)}});
    return {{"closing", value.closing},
            {"delivery_blocked", value.deliveryBlocked},
            {"effects", effects},
            {"errors", failures(value.errors)}};
}
QJsonObject binding(const BindingSnapshot& value)
{
    return {{"id", decimal(value.id.value())},
            {"key", key(value.key)},
            {"state", static_cast<int>(value.state)},
            {"calls", decimal(value.activeCalls)},
            {"leases", decimal(value.committedLeases)},
            {"minor", decimal(value.metadata.minor)},
            {"capabilities", strings(value.metadata.capabilities)},
            {"delivery_blocked", value.deliveryBlocked}};
}
QJsonObject running(const RunningEntry& value)
{
    return {{"path", text(value.path)},
            {"configuration", configuration(value.configuration)},
            {"state", static_cast<int>(value.state)},
            {"instance", decimal(value.instance.value())},
            {"epoch", decimal(value.epoch)},
            {"waiting_for_parent", value.waitingForParent},
            {"parent_epoch", decimal(value.parentEpoch)},
            {"failure", failure(value.failure)}};
}
struct Values {
    KernelSnapshot core;
    ServiceGraphSnapshot graph;
    std::vector<ReplacementScopeSnapshot> claims;
    std::vector<RunningEntry> driver;
    SessionSnapshot session;
    DirectoryValues directory;
    std::vector<std::pair<std::string, NativeModuleSnapshot>> native;
    std::vector<PageSnapshot> pages;
    std::vector<CommandSnapshot> commands;
    std::vector<OutputEntry> output;
    std::vector<NotificationSnapshot> notices;
    qt::SchedulerSnapshot queue;
    notebook::WorkspaceUpgradeSnapshot upgrade;
    notebook::WorkspaceModulesSnapshot modules;
    std::uint64_t evaluations;
};
Values capture(const Scene& scene)
{
    PLATFORM_CHECK(scene.state && scene.scheduler && scene.scheduler->isCurrentThread());
    auto& state = *scene.state;
    PLATFORM_CHECK(state.kernel && state.driver && state.session && state.manager);
    Values result{checked(state.kernel->snapshot()),
                  checked(state.kernel->services().dependencyGraph()),
                  checked(state.kernel->replacements()),
                  checked(state.driver->snapshot()),
                  checked(state.session->snapshot()),
                  workspace_directory_test::directory(),
                  {},
                  checked(state.shell.pages()->snapshot()),
                  checked(state.shell.commands()->snapshot()),
                  checked(state.shell.output()->snapshot()),
                  checked(state.shell.notifications()->snapshot()),
                  checked(scene.scheduler->snapshot()),
                  checked(state.upgradeSnapshot()),
                  checked(state.modulesSnapshot()),
                  checked(state.kernel->services().dependencyEvaluationCount())};
    // 真实会话句柄只在这个词法块内借用；返回值不保留 CurrentModule/Runtime。
    {
        const auto counter = checked(state.counter());
        const auto reporter = checked(state.reporter());
        PLATFORM_CHECK(counter && counter->session && reporter && reporter->session);
        result.native.emplace_back("example.counter", checked(counter->session->snapshot()));
        result.native.emplace_back("example.reporter", checked(reporter->session->snapshot()));
    }
    return result;
}
QJsonObject generation(const GenerationValues& value)
{
    const auto& row = value.module;
    QJsonArray records, runtimes, instances;
    for (const auto& resource : row.resources.records)
        records.append(QJsonObject{
            {"id", decimal(resource.id)}, {"kind", static_cast<int>(resource.kind)}, {"label", text(resource.label)}});
    for (const auto& origin : value.runtimes)
        runtimes.append(QJsonObject{{"runtime", decimal(origin.runtime.value())},
                                    {"generation", decimal(origin.generation.value())},
                                    {"plugin", text(origin.pluginId)}});
    for (const auto& origin : value.instances)
        instances.append(QJsonObject{{"instance", decimal(origin.instance.value())},
                                     {"runtime", decimal(origin.runtime.value())},
                                     {"epoch", decimal(origin.epoch)},
                                     {"managed", origin.managed},
                                     {"operation", decimal(origin.operation.value())}});
    return {{"generation", decimal(row.generation.value())},
            {"module", text(row.manifest.moduleId)},
            {"version", text(row.manifest.moduleVersion)},
            {"sha256", text(row.manifest.sha256)},
            {"loader", static_cast<int>(row.manifest.loader)},
            {"policy", static_cast<int>(row.loadPolicy)},
            {"state", static_cast<int>(row.state)},
            {"role", static_cast<int>(row.role)},
            {"manifest_path", text(row.manifestPath)},
            {"library_path", text(row.libraryPath)},
            {"native_mapped", row.nativeMapped},
            {"native_pinned", row.nativePinned},
            {"loading", value.loading},
            {"admissions", decimal(value.admissions)},
            {"image_reference", value.image},
            {"observation_image_reference", value.observationImage},
            {"file_guard", value.artifactFile},
            {"backend", value.backend},
            {"reservation", value.reservation ? QJsonValue(decimal(value.reservation->value())) : QJsonValue()},
            {"accepting", row.resources.accepting},
            {"records", records},
            {"pin_reasons", strings(row.resources.pinReasons)},
            {"runtime_origins", runtimes},
            {"instance_origins", instances},
            {"failure", failure(row.failure)},
            {"business_failure", failure(row.businessFailure)},
            {"unload_failure", failure(row.unloadFailure)}};
}
QJsonObject native(const std::pair<std::string, NativeModuleSnapshot>& source)
{
    QJsonArray instances;
    for (const auto& row : source.second.instances) {
        QJsonArray logs, services;
        for (const auto& log : row.logs)
            logs.append(QJsonObject{{"level", decimal(log.level)}, {"text", text(log.text)}});
        for (const auto& service : row.services)
            services.append(binding(service));
        instances.append(QJsonObject{{"generation", decimal(row.generation.value())},
                                     {"instance", decimal(row.instance.value())},
                                     {"epoch", decimal(row.epoch)},
                                     {"plugin", text(row.pluginId)},
                                     {"state", static_cast<int>(row.state)},
                                     {"native_created", row.nativeCreated},
                                     {"closing", row.closing},
                                     {"uncertain", row.uncertain},
                                     {"delivery_blocked", row.deliveryBlocked},
                                     {"requests", decimal(row.pendingRequests)},
                                     {"host_calls", decimal(row.activeHostCalls)},
                                     {"host_operations", decimal(row.pendingHostOperations)},
                                     {"committed_services", decimal(row.committedServices)},
                                     {"failure", failure(row.failure)},
                                     {"resources", scope(row.resources)},
                                     {"services", services},
                                     {"logs", logs}});
    }
    return {{"plugin", text(source.first)},     {"generation", decimal(source.second.generation.value())},
            {"closing", source.second.closing}, {"closed", source.second.closed},
            {"faulted", source.second.faulted}, {"instances", instances}};
}
QJsonObject upgrade(const notebook::WorkspaceUpgradeReport& value)
{
    QJsonObject result{{"alias", text(value.alias)},
                       {"previous", decimal(value.previous.value())},
                       {"candidate", decimal(value.candidate.value())},
                       {"retired", decimal(value.retired.value())},
                       {"retained", value.retained},
                       {"delivery_blocked", value.deliveryBlocked},
                       {"failure", failure(value.failure)}};
    if (value.replacement)
        result.insert("replacement", QJsonObject{{"operation", decimal(value.replacement->operation.value())},
                                                 {"outcome", static_cast<int>(value.replacement->outcome)},
                                                 {"committed", value.replacement->committed},
                                                 {"recovered", value.replacement->recovered},
                                                 {"resources_retained", value.replacement->resourcesRetained},
                                                 {"errors", failures(value.replacement->errors)}});
    if (value.nativeClose)
        result.insert("native_close", value.nativeClose->hasValue()
                                          ? QJsonObject{{"quiescent", value.nativeClose->value().quiescent},
                                                        {"errors", failures(value.nativeClose->value().errors)}}
                                          : QJsonObject{{"failure", error(value.nativeClose->error())}});
    if (value.drain)
        result.insert("drain_error", value.drain->hasValue() ? QJsonValue() : QJsonValue(error(value.drain->error())));
    if (value.unload)
        result.insert("unload_error",
                      value.unload->hasValue() ? QJsonValue() : QJsonValue(error(value.unload->error())));
    return result;
}
QJsonObject serialize(const Values& values)
{
    QJsonArray core, names, claims, driver, coordinator, bindings, declarations, generations, natives, pages, commands,
        output, notices, paths;
    for (const auto& row : values.core.instances)
        core.append(QJsonObject{{"instance", decimal(row.instance.value())},
                                {"runtime", decimal(row.runtime.value())},
                                {"epoch", decimal(row.epoch)},
                                {"revision", decimal(row.revision)},
                                {"state", static_cast<int>(row.state)},
                                {"children", decimal(row.children)},
                                {"leases", decimal(row.committedLeases)},
                                {"delivery_blocked", row.deliveryBlocked},
                                {"finalizing", row.finalizing},
                                {"failure", failure(row.failure)},
                                {"resources", scope(row.resources)},
                                {"finalizers", row.finalizers ? QJsonValue(scope(*row.finalizers)) : QJsonValue()}});
    for (const auto& row : values.core.names)
        names.append(QJsonObject{{"runtime", decimal(row.id.value())}, {"name", text(row.name)}});
    for (const auto& row : values.claims)
        claims.append(QJsonObject{{"operation", decimal(row.operation.value())},
                                  {"state", static_cast<int>(row.state)},
                                  {"abandoned", row.abandoned},
                                  {"delivery_blocked", row.deliveryBlocked},
                                  {"cancelled", row.cancelled},
                                  {"failure", failure(row.failure)}});
    for (const auto& row : values.driver)
        driver.append(running(row));
    for (const auto& row : values.session.coordinator.running)
        coordinator.append(running(row));
    for (const auto& row : values.graph.bindings) {
        auto value = binding(row.binding);
        QJsonArray consumers;
        for (const auto& consumer : row.consumers) {
            auto child = identity({consumer.instance, consumer.epoch});
            child.insert("managed", consumer.managed);
            consumers.append(child);
        }
        value.insert("provider", decimal(row.provider.value()));
        value.insert("managed_provider", row.managedProvider);
        value.insert("consumers", consumers);
        bindings.append(value);
    }
    for (const auto& row : values.graph.instances)
        declarations.append(QJsonObject{{"instance", decimal(row.instance.value())},
                                        {"parent", decimal(row.parent.value())},
                                        {"dependencies", keys(row.dependencies)},
                                        {"outputs", keys(row.outputs)},
                                        {"publication_held", row.publicationHeld}});
    for (const auto& row : values.directory.generations)
        generations.append(generation(row));
    for (const auto& row : values.native)
        natives.append(native(row));
    for (const auto& row : values.pages) {
        auto value = identity(row.owner);
        value.insert("id", text(row.id));
        value.insert("navigation", row.navigationPresent);
        value.insert("widget", row.widgetAlive);
        value.insert("current", row.current);
        value.insert("retiring", row.retiring);
        pages.append(value);
    }
    for (const auto& row : values.commands) {
        auto value = identity(row.owner);
        value.insert("id", text(row.id));
        value.insert("title", text(row.title));
        value.insert("calls", decimal(row.activeCalls));
        value.insert("retiring", row.retiring);
        value.insert("delivery_blocked", row.deliveryBlocked);
        commands.append(value);
    }
    for (const auto& row : values.output)
        output.append(QJsonObject{{"sequence", decimal(row.sequence)},
                                  {"owner", row.owner ? QJsonValue(identity(*row.owner)) : QJsonValue()},
                                  {"channel", text(row.channel)},
                                  {"title", text(row.title)},
                                  {"text", text(row.text)},
                                  {"level", static_cast<int>(row.level)}});
    for (const auto& row : values.notices)
        notices.append(QJsonObject{{"sequence", decimal(row.sequence)},
                                   {"owner", identity(row.owner)},
                                   {"level", static_cast<int>(row.notification.level)},
                                   {"operation", decimal(row.notification.operation.value())},
                                   {"text", text(row.notification.text)}});
    const auto& session = values.session;
    QJsonObject desired, modules;
    if (session.coordinator.desired) {
        for (const auto& row : session.coordinator.desired->source().structure().entries())
            desired.insert(text(row.first), configuration(row.second));
        for (const auto& row : session.coordinator.desired->source().structure().modules())
            modules.insert(text(row.first), text(row.second));
    }
    for (const auto& row : values.modules.current) {
        QJsonArray configuredPaths;
        for (const auto& path : row.paths)
            configuredPaths.append(text(path));
        paths.append(QJsonObject{{"alias", text(row.alias)},
                                 {"plugin", text(row.plugin)},
                                 {"generation", decimal(row.generation.value())},
                                 {"runtime", decimal(row.runtime.value())},
                                 {"paths", configuredPaths}});
    }
    const auto& store = session.store;
    QJsonArray history, planEntries;
    for (const auto& row : values.upgrade.history)
        history.append(upgrade(row));
    QJsonValue latestPlan;
    if (session.latestPlan) {
        for (const auto& row : session.latestPlan->entries)
            planEntries.append(QJsonObject{{"path", text(row.operation.path)},
                                           {"kind", static_cast<int>(row.operation.kind)},
                                           {"status", static_cast<int>(row.status)},
                                           {"failure", failure(row.error)}});
        latestPlan = QJsonObject{{"revision", decimal(session.latestPlan->revision)},
                                 {"outcome", static_cast<int>(session.latestPlan->outcome)},
                                 {"entries", planEntries},
                                 {"errors", failures(session.latestPlan->errors)}};
    }
    return {
        {"desired_revision", decimal(session.coordinator.desiredRevision)},
        {"active_revision",
         session.coordinator.activeRevision ? QJsonValue(decimal(*session.coordinator.activeRevision)) : QJsonValue()},
        {"desired", desired},
        {"module_locators", modules},
        {"driver", driver},
        {"coordinator_running", coordinator},
        {"latest_plan", latestPlan},
        {"session", QJsonObject{{"closing", session.closing},
                                {"closed", session.closed},
                                {"delivery_blocked", session.deliveryBlocked},
                                {"unsafe", session.coordinator.unsafe},
                                {"execution_error", failure(session.executionError)},
                                {"running_error", failure(session.coordinator.runningError)}}},
        {"store", QJsonObject{{"desired_revision", decimal(store.desiredRevision)},
                              {"persisted_revision",
                               store.persistedRevision ? QJsonValue(decimal(*store.persistedRevision)) : QJsonValue()},
                              {"dirty", store.dirty},
                              {"writing", store.writing},
                              {"queued_writes", decimal(store.queuedWrites)},
                              {"closing", store.closing},
                              {"closed", store.closed},
                              {"delivery_blocked", store.deliveryBlocked},
                              {"last_error", failure(store.lastError)},
                              {"baseline", store.baseline ? QJsonValue(text(store.baseline->text)) : QJsonValue()},
                              {"external", store.external ? QJsonValue(text(store.external->text)) : QJsonValue()}}},
        {"kernel", QJsonObject{{"closing", values.core.closing},
                               {"delivery_blocked", values.core.deliveryBlocked},
                               {"runtimes", decimal(values.core.runtimes)},
                               {"names", names},
                               {"instances", core},
                               {"claims", claims}}},
        {"services", QJsonObject{{"bindings", bindings},
                                 {"declarations", declarations},
                                 {"dependency_evaluations", decimal(values.evaluations)}}},
        {"directory", QJsonObject{{"generations", generations},
                                  {"reservations", values.directory.reservations},
                                  {"readers", values.directory.readers},
                                  {"reader_operations", decimal(values.directory.readerOperations)}}},
        {"module_paths", paths},
        {"module_errors", failures(values.modules.configurationErrors)},
        {"native", natives},
        {"pages", pages},
        {"commands", commands},
        {"output", output},
        {"notifications", notices},
        {"queue", QJsonObject{{"open", values.queue.open},
                              {"running", values.queue.running},
                              {"queued", decimal(values.queue.queued)},
                              {"accepted", decimal(values.queue.accepted)},
                              {"executed", decimal(values.queue.executed)},
                              {"discarded", decimal(values.queue.discarded)},
                              {"callback_errors", decimal(values.queue.callbackErrors)}}},
        {"upgrade",
         QJsonObject{
             {"accepting", values.upgrade.accepting},
             {"active", values.upgrade.active ? QJsonValue(upgrade(*values.upgrade.active)) : QJsonValue()},
             {"phase", values.upgrade.phase ? QJsonValue(static_cast<int>(*values.upgrade.phase)) : QJsonValue()},
             {"history", history},
             {"delivery_blocked", values.upgrade.deliveryBlocked}}}};
}

using Core = std::map<InstanceId, const InstanceSnapshot*>;
std::string parentPath(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}
void verifyScope(const ScopeSnapshot& value)
{
    PLATFORM_CHECK(!value.deliveryBlocked && value.errors.empty());
    for (const auto& effect : value.effects)
        PLATFORM_CHECK(effect.state == EffectState::Live && !effect.setupPending && !effect.cleanupPending &&
                       !effect.deliveryBlocked && effect.retainedCleanups == 0 && effect.errors.empty());
}
bool sameKey(const ServiceKey& a, const ServiceKey& b)
{
    return !(a < b) && !(b < a);
}
bool supports(const BindingSnapshot& value, const InjectSpec& requirement)
{
    return value.state == BindingState::Published && value.metadata.minor >= requirement.minimumMinor &&
           std::all_of(requirement.capabilities.begin(), requirement.capabilities.end(), [&](const auto& capability) {
               return std::find(value.metadata.capabilities.begin(), value.metadata.capabilities.end(), capability) !=
                      value.metadata.capabilities.end();
           });
}
void verify(const Scene& scene, const Values& values)
{
    const auto& coordinator = values.session.coordinator;
    const auto& store = values.session.store;
    PLATFORM_CHECK(coordinator.desired && !coordinator.activeRevision && !coordinator.closing && !coordinator.unsafe &&
                   !coordinator.deliveryBlocked && !coordinator.runningError);
    PLATFORM_CHECK(!values.session.closing && !values.session.closed && !values.session.deliveryBlocked);
    PLATFORM_CHECK(!store.writing && store.queuedWrites == 0 && !store.closing && !store.closed &&
                   !store.deliveryBlocked);
    PLATFORM_CHECK(store.desired && store.desiredRevision == coordinator.desiredRevision &&
                   store.desired == coordinator.desired);
    PLATFORM_CHECK(!values.core.closing && !values.core.deliveryBlocked && values.claims.empty());
    PLATFORM_CHECK(values.directory.reservations.isEmpty() && values.directory.readers.isEmpty() &&
                   values.directory.readerOperations == 0);
    PLATFORM_CHECK(values.queue.open && !values.queue.running && values.queue.queued == 0 &&
                   values.queue.discarded == 0 && values.queue.callbackErrors == 0 &&
                   values.queue.accepted == values.queue.executed);
    PLATFORM_CHECK(!values.upgrade.active && values.upgrade.accepting && !values.upgrade.deliveryBlocked);
    auto desired = coordinator.desired->source().structure().entries();
    for (auto& entry : desired) {
        const auto parent = parentPath(entry.first);
        if (!parent.empty()) {
            auto local = std::move(entry.second.realms);
            entry.second.realms = desired.at(parent).realms;
            for (const auto& realm : local)
                entry.second.realms.insert_or_assign(realm.first, std::move(realm.second));
        }
    }
    std::map<RuntimeId, std::string> names;
    for (const auto& name : values.core.names)
        PLATFORM_CHECK(names.emplace(name.id, name.name).second);
    PLATFORM_CHECK(names.size() == values.core.runtimes);
    Core core;
    for (const auto& row : values.core.instances) {
        PLATFORM_CHECK(core.emplace(row.instance, &row).second && names.count(row.runtime) == 1 &&
                       !row.deliveryBlocked && !row.finalizing);
        PLATFORM_CHECK(row.state == InstanceState::Active || row.state == InstanceState::Pending ||
                       row.state == InstanceState::Failed);
        verifyScope(row.resources);
        if (row.finalizers)
            verifyScope(*row.finalizers);
        if (row.state != InstanceState::Active)
            PLATFORM_CHECK(!row.hasRetainedResources());
        if (row.state == InstanceState::Failed)
            PLATFORM_CHECK(row.failure);
    }
    std::map<InstanceId, const RunningEntry*> owners;
    std::map<std::string, const RunningEntry*> paths;
    PLATFORM_CHECK(values.driver.size() == coordinator.running.size());
    for (const auto& row : values.driver) {
        PLATFORM_CHECK(paths.emplace(row.path, &row).second && desired.count(row.path) == 1);
        const auto& target = desired.at(row.path);
        PLATFORM_CHECK(!target.effectiveDisabled && configuration(row.configuration) == configuration(target));
        const auto reported = std::find_if(coordinator.running.begin(), coordinator.running.end(),
                                           [&](const auto& entry) { return entry.path == row.path; });
        PLATFORM_CHECK(reported != coordinator.running.end() && running(*reported) == running(row));
        if (!row.instance.valid()) {
            PLATFORM_CHECK(!parentPath(row.path).empty() && row.epoch == 0 && (row.waitingForParent || row.failure));
            continue;
        }
        PLATFORM_CHECK(core.count(row.instance) == 1 && owners.emplace(row.instance, &row).second);
        const auto& actual = *core.at(row.instance);
        PLATFORM_CHECK(actual.state == row.state && actual.epoch == row.epoch &&
                       names.at(actual.runtime) == row.configuration.plugin);
        PLATFORM_CHECK(failure(actual.failure) == failure(row.failure));
        if (row.state == InstanceState::Failed) {
            const auto config = QJsonDocument::fromJson(QByteArray::fromStdString(target.config));
            PLATFORM_CHECK(config.isObject() && config.object().value("failStart").toBool());
        }
        const auto parent = parentPath(row.path);
        if (parent.empty())
            PLATFORM_CHECK(row.parentEpoch == 0);
        else {
            PLATFORM_CHECK(paths.count(parent) == 1 && paths.at(parent)->instance.valid());
            PLATFORM_CHECK(paths.at(parent)->epoch == row.parentEpoch);
        }
    }
    for (const auto& target : desired)
        PLATFORM_CHECK(target.second.effectiveDisabled ? paths.count(target.first) == 0
                                                       : paths.count(target.first) == 1);
    std::size_t host = 0;
    for (const auto& row : core)
        if (!owners.count(row.first)) {
            PLATFORM_CHECK(names.at(row.second->runtime) == "host.notebook" &&
                           row.second->state == InstanceState::Active);
            ++host;
        }
    PLATFORM_CHECK(host == 1);
    std::map<InstanceId, const ServiceInstanceSnapshot*> declarations;
    std::map<std::string, RealmId> aliases;
    auto checkRealm = [&](const ServiceKey& service, const EntryValue& target) {
        PLATFORM_CHECK(checked(scene.state->kernel->services().ownsRealm(service.realm)));
        const auto override = target.realms.find(service.name);
        if (override == target.realms.end())
            PLATFORM_CHECK(service.realm == RealmId{1});
        else {
            PLATFORM_CHECK(override->second == "second" && service.realm != RealmId{1});
            const auto inserted = aliases.emplace(override->second, service.realm);
            PLATFORM_CHECK(inserted.first->second == service.realm);
        }
    };
    for (const auto& row : values.graph.instances) {
        PLATFORM_CHECK(core.count(row.instance) == 1 && declarations.emplace(row.instance, &row).second &&
                       !row.publicationHeld);
        if (!owners.count(row.instance))
            continue;
        const auto& owner = *owners.at(row.instance);
        const auto& target = desired.at(owner.path);
        const auto parent = parentPath(owner.path);
        PLATFORM_CHECK(row.parent == (parent.empty() ? InstanceId{} : paths.at(parent)->instance));
        const auto& requirements = coordinator.desired->effectiveInject().at(owner.path);
        PLATFORM_CHECK(row.dependencies.size() == requirements.size());
        bool missing = false;
        for (const auto& dependency : row.dependencies) {
            checkRealm(dependency, target);
            const auto requirement = std::find_if(requirements.begin(), requirements.end(), [&](const auto& item) {
                return item.name == dependency.name && item.major == dependency.interfaceMajor;
            });
            PLATFORM_CHECK(requirement != requirements.end());
            const auto provider =
                std::find_if(values.graph.bindings.begin(), values.graph.bindings.end(), [&](const auto& item) {
                    return sameKey(item.binding.key, dependency) && supports(item.binding, *requirement);
                });
            missing = missing || provider == values.graph.bindings.end();
        }
        for (const auto& output : row.outputs)
            checkRealm(output, target);
        if (target.plugin == "example.notebook")
            PLATFORM_CHECK(row.outputs.empty());
        else {
            const auto output = target.plugin == "example.counter" ? "example.counter" : "example.report";
            PLATFORM_CHECK(row.outputs.size() == 1 && row.outputs.front().name == output &&
                           row.outputs.front().interfaceMajor == 1);
            if (owner.state == InstanceState::Active)
                PLATFORM_CHECK(
                    std::count_if(values.graph.bindings.begin(), values.graph.bindings.end(), [&](const auto& item) {
                        return item.managedProvider && item.provider == row.instance &&
                               sameKey(item.binding.key, row.outputs.front());
                    }) == 1);
        }
        if (owner.state == InstanceState::Pending && !owner.waitingForParent)
            PLATFORM_CHECK(missing);
        if (owner.state == InstanceState::Active)
            PLATFORM_CHECK(!missing);
    }
    PLATFORM_CHECK(declarations.size() == core.size());
    std::map<BindingId, const ServiceBindingSnapshot*> bindings;
    for (const auto& row : values.graph.bindings) {
        PLATFORM_CHECK(bindings.emplace(row.binding.id, &row).second && row.binding.state == BindingState::Published &&
                       row.binding.activeCalls == 0 && !row.binding.deliveryBlocked);
        PLATFORM_CHECK(row.binding.committedLeases == row.consumers.size());
        if (row.managedProvider) {
            PLATFORM_CHECK(core.count(row.provider) == 1 && core.at(row.provider)->state == InstanceState::Active);
            const auto& outputs = declarations.at(row.provider)->outputs;
            PLATFORM_CHECK(std::any_of(outputs.begin(), outputs.end(),
                                       [&](const auto& item) { return sameKey(item, row.binding.key); }));
        }
        for (const auto& consumer : row.consumers) {
            PLATFORM_CHECK(consumer.managed && core.count(consumer.instance) == 1 &&
                           core.at(consumer.instance)->state == InstanceState::Active &&
                           core.at(consumer.instance)->epoch == consumer.epoch);
            const auto& requirements = declarations.at(consumer.instance)->dependencies;
            PLATFORM_CHECK(std::any_of(requirements.begin(), requirements.end(),
                                       [&](const auto& item) { return sameKey(item, row.binding.key); }));
        }
    }
    for (const auto& row : core) {
        std::size_t leases = 0, children = 0;
        for (const auto& provider : values.graph.bindings)
            leases += static_cast<std::size_t>(
                std::count_if(provider.consumers.begin(), provider.consumers.end(), [&](const auto& item) {
                    return item.managed && item.instance == row.first && item.epoch == row.second->epoch;
                }));
        for (const auto& child : values.graph.instances)
            children += child.parent == row.first ? 1u : 0u;
        PLATFORM_CHECK(row.second->committedLeases == leases && row.second->children == children);
    }
    std::map<GenerationId, const GenerationValues*> generations;
    std::map<RuntimeId, GenerationId> origins;
    std::map<std::pair<InstanceId, Epoch>, GenerationId> instances;
    for (const auto& row : values.directory.generations) {
        PLATFORM_CHECK(generations.emplace(row.module.generation, &row).second && !row.loading && row.admissions == 0 &&
                       !row.reservation && !row.observationImage);
        for (const auto& origin : row.runtimes)
            PLATFORM_CHECK(origins.emplace(origin.runtime, origin.generation).second &&
                           origin.generation == row.module.generation && names.count(origin.runtime) == 1 &&
                           names.at(origin.runtime) == origin.pluginId);
        for (const auto& origin : row.instances) {
            PLATFORM_CHECK(origin.managed && core.count(origin.instance) == 1 &&
                           core.at(origin.instance)->state == InstanceState::Active);
            PLATFORM_CHECK(core.at(origin.instance)->runtime == origin.runtime &&
                           core.at(origin.instance)->epoch == origin.epoch);
            PLATFORM_CHECK(
                instances.emplace(std::make_pair(origin.instance, origin.epoch), row.module.generation).second);
        }
        std::size_t instanceRecords = 0;
        for (const auto& resource : row.module.resources.records) {
            PLATFORM_CHECK(resource.kind != ResourceKind::Call && resource.kind != ResourceKind::SetupTask &&
                           resource.kind != ResourceKind::CleanupTask && resource.kind != ResourceKind::Worker &&
                           resource.kind != ResourceKind::QueuePayload);
            instanceRecords += resource.kind == ResourceKind::Instance ? 1u : 0u;
        }
        PLATFORM_CHECK(instanceRecords == row.instances.size());
        if (row.module.role != GenerationRole::Current) {
            PLATFORM_CHECK(row.runtimes.empty() && row.instances.empty());
            for (const auto& resource : row.module.resources.records)
                PLATFORM_CHECK(row.module.manifest.loader == ModuleLoader::QtUi &&
                               resource.kind == ResourceKind::QObject && resource.label == "qt.loader.root");
        }
        if (row.module.state == ModuleState::Unloaded)
            PLATFORM_CHECK(row.module.resources.drained() && row.module.resources.pinReasons.empty() &&
                           !row.module.nativeMapped && !row.module.nativePinned && !row.image && !row.artifactFile &&
                           !row.backend);
    }
    std::set<GenerationId> current;
    for (const auto& source : values.modules.current) {
        PLATFORM_CHECK(generations.count(source.generation) == 1 && origins.count(source.runtime) == 1 &&
                       origins.at(source.runtime) == source.generation);
        PLATFORM_CHECK(current.insert(source.generation).second);
        const auto& row = *generations.at(source.generation);
        PLATFORM_CHECK(row.module.role == GenerationRole::Current && names.at(source.runtime) == source.plugin);
        PLATFORM_CHECK(!row.module.failure && !row.module.businessFailure && !row.module.unloadFailure &&
                       row.module.nativeMapped && row.module.resources.accepting && row.image && row.artifactFile &&
                       row.backend);
        if (row.module.manifest.loader == ModuleLoader::CAbi)
            PLATFORM_CHECK(row.module.loadPolicy == ModuleLoadPolicy::ApprovedNative && !row.module.nativePinned &&
                           row.module.resources.pinReasons.empty());
        else
            PLATFORM_CHECK(row.module.loadPolicy == ModuleLoadPolicy::Resident && row.module.nativePinned);
        PLATFORM_CHECK(coordinator.desired->source().structure().modules().at(source.alias) ==
                       row.module.manifest.sha256 + "/module.json");
        std::set<std::string> expected;
        for (const auto& target : desired)
            if (target.second.plugin == source.plugin)
                expected.insert(target.first);
        PLATFORM_CHECK(std::set<std::string>(source.paths.begin(), source.paths.end()) == expected);
    }
    PLATFORM_CHECK(current.size() == static_cast<std::size_t>(std::count_if(
                                         values.directory.generations.begin(), values.directory.generations.end(),
                                         [](const auto& row) { return row.module.role == GenerationRole::Current; })));
    for (const auto& row : core)
        if (owners.count(row.first)) {
            PLATFORM_CHECK(origins.count(row.second->runtime) == 1);
            PLATFORM_CHECK(instances.count({row.first, row.second->epoch}) ==
                           (row.second->state == InstanceState::Active ? 1u : 0u));
        }
    std::set<std::pair<InstanceId, Epoch>> nativeInstances;
    for (const auto& source : values.native) {
        const auto& module = source.second;
        PLATFORM_CHECK(generations.count(module.generation) == 1 &&
                       generations.at(module.generation)->module.role == GenerationRole::Current);
        PLATFORM_CHECK(!module.closing && !module.closed && !module.faulted);
        for (const auto& row : module.instances) {
            PLATFORM_CHECK(row.pluginId == source.first && row.generation == module.generation &&
                           row.state == NativeInstanceState::Started && row.nativeCreated && !row.closing &&
                           !row.uncertain && !row.deliveryBlocked && !row.failure);
            PLATFORM_CHECK(row.pendingRequests == 0 && row.activeHostCalls == 0 && row.pendingHostOperations == 0);
            PLATFORM_CHECK(core.count(row.instance) == 1 && core.at(row.instance)->epoch == row.epoch &&
                           core.at(row.instance)->state == InstanceState::Active);
            PLATFORM_CHECK(instances.at({row.instance, row.epoch}) == module.generation &&
                           nativeInstances.emplace(row.instance, row.epoch).second);
            verifyScope(row.resources);
            std::size_t consumers = 0;
            for (const auto& provider : values.graph.bindings)
                consumers += static_cast<std::size_t>(
                    std::count_if(provider.consumers.begin(), provider.consumers.end(), [&](const auto& item) {
                        return item.managed && item.instance == row.instance && item.epoch == row.epoch;
                    }));
            PLATFORM_CHECK(row.committedServices == consumers);
            for (const auto& service : row.services)
                PLATFORM_CHECK(bindings.count(service.id) == 1 && bindings.at(service.id)->provider == row.instance);
        }
    }
    // 按实际会话、工厂来源和实例来源计算完整账目，拒绝额外 Callback/ServiceObject 等滞留。
    using Resources = std::map<std::pair<ResourceKind, std::string>, std::size_t>;
    for (const auto& generation : values.directory.generations) {
        Resources actual, expected;
        for (const auto& resource : generation.module.resources.records)
            ++actual[{resource.kind, resource.label}];
        for (const auto& origin : generation.runtimes)
            ++expected[{ResourceKind::ExternalHandle, "runtime.definition." + origin.pluginId}];
        if (generation.module.manifest.loader == ModuleLoader::QtUi && generation.backend)
            ++expected[{ResourceKind::QObject, "qt.loader.root"}];
        for (const auto& origin : generation.instances) {
            const auto plugin = names.at(origin.runtime);
            const auto label =
                generation.module.manifest.loader == ModuleLoader::QtUi
                    ? "qt.instance." + std::to_string(origin.instance.value()) + "." + std::to_string(origin.epoch)
                    : plugin;
            ++expected[{ResourceKind::Instance, label}];
        }
        for (const auto& source : values.native)
            if (source.second.generation == generation.module.generation) {
                ++expected[{ResourceKind::ExternalHandle, "native.host"}];
                for (const auto& row : source.second.instances) {
                    expected[{ResourceKind::Callback, "native.effect.cleanup"}] += row.resources.effects.size();
                    expected[{ResourceKind::ServiceObject, "native.service.object"}] += row.services.size();
                }
            }
        for (auto it = expected.begin(); it != expected.end();)
            if (it->second == 0)
                it = expected.erase(it);
            else
                ++it;
        PLATFORM_CHECK(actual == expected);
    }
    std::set<std::pair<InstanceId, Epoch>> pageOwners, commandOwners;
    for (const auto& row : values.pages) {
        PLATFORM_CHECK(core.count(row.owner.instance) == 1 && core.at(row.owner.instance)->epoch == row.owner.epoch &&
                       core.at(row.owner.instance)->state == InstanceState::Active && !row.retiring && row.widgetAlive);
        if (owners.count(row.owner.instance)) {
            PLATFORM_CHECK(owners.at(row.owner.instance)->configuration.plugin == "example.notebook" &&
                           row.id == "notebook" && row.navigationPresent);
            PLATFORM_CHECK(pageOwners.emplace(row.owner.instance, row.owner.epoch).second);
        }
    }
    for (const auto& row : values.commands) {
        PLATFORM_CHECK(core.count(row.owner.instance) == 1 && core.at(row.owner.instance)->epoch == row.owner.epoch &&
                       core.at(row.owner.instance)->state == InstanceState::Active && !row.retiring &&
                       !row.deliveryBlocked && row.activeCalls == 0);
        PLATFORM_CHECK(owners.count(row.owner.instance) == 1 &&
                       owners.at(row.owner.instance)->configuration.plugin == "example.notebook" &&
                       row.id == "notebook.publish");
        PLATFORM_CHECK(commandOwners.emplace(row.owner.instance, row.owner.epoch).second);
    }
    PLATFORM_CHECK(pageOwners == commandOwners);
    PLATFORM_CHECK(values.pages.size() == pageOwners.size() + 2 && values.commands.size() == commandOwners.size());
    for (const auto& owner : owners) {
        const auto token = std::make_pair(owner.first, owner.second->epoch);
        const bool active = owner.second->state == InstanceState::Active;
        if (owner.second->configuration.plugin == "example.notebook")
            PLATFORM_CHECK(pageOwners.count(token) == (active ? 1u : 0u));
        else
            PLATFORM_CHECK(nativeInstances.count(token) == (active ? 1u : 0u));
    }
}
} // namespace

QJsonObject observe(const Scene& scene)
{
    return serialize(capture(scene));
}
void verifySettled(const Scene& scene)
{
    verify(scene, capture(scene));
}
} // namespace workspace_stress_test
