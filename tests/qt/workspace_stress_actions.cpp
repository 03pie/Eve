/** @file workspace_stress_actions.cpp
 * @brief 原 Session 配置动作、明确保存及唯一有限动作字段校验。
 */
#include "workspace_stress_actions_p.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <map>

namespace workspace_stress_test {
namespace {
using namespace detail;

using Fields = std::map<QString, bool>; // true: bool; false: string
const std::map<QString, Fields>& schemas()
{
    static const std::map<QString, Fields> values{
        {"mount", {{"id", false}, {"configuration", false}}},
        {"configure", {{"path", false}, {"configuration", false}}},
        {"enable", {{"path", false}, {"enabled", true}}},
        {"withdraw", {{"path", false}}},
        {"realmPair", {{"present", true}}},
        {"nativeUpgrade", {{"alias", false}, {"variant", false}}},
        {"notebookUpgrade", {{"variant", false}}},
        {"draft", {{"path", false}, {"text", false}}},
        {"preparedQueuedCancel",
         {{"variant", false}, {"path", false}, {"firstConfiguration", false}, {"secondConfiguration", false}}},
        {"buildingCancelNotebook", {{"variant", false}}},
        {"saveConflictRebase", {{"text", false}}},
        {"save", {}}};
    return values;
}
std::string quote(const std::string& value)
{
    // 只编码元数据字符串；config 的规范 JSON 片段从不进入 QJson 数字模型。
    const auto bytes = QJsonDocument(QJsonArray{QString::fromUtf8(value.data(), static_cast<int>(value.size()))})
                           .toJson(QJsonDocument::Compact);
    return bytes.mid(1, bytes.size() - 2).toStdString();
}
std::string parent(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}
EntrySpec rebuildEntry(const std::string& path, const std::map<std::string, EntryValue>& entries)
{
    const auto& value = entries.at(path);
    EntrySpec result;
    result.id = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    result.plugin = value.plugin;
    result.module = value.module;
    result.disabled = value.disabled;
    result.config = value.config;
    result.realms = value.realms;
    result.inject = value.inject;
    for (const auto& child : entries)
        if (parent(child.first) == path)
            result.children.push_back(rebuildEntry(child.first, entries));
    return result;
}
Document document(const ParsedConfiguration& source)
{
    Document result;
    result.modules = source.structure().modules();
    const auto& entries = source.structure().entries();
    for (const auto& value : entries)
        if (parent(value.first).empty())
            result.plugins.push_back(rebuildEntry(value.first, entries));
    return result;
}
std::string serializeEntry(const EntrySpec& value)
{
    std::string result = "{\"id\":" + quote(value.id) + ",\"plugin\":" + quote(value.plugin) +
                         ",\"module\":" + quote(value.module) + ",\"disabled\":" + (value.disabled ? "true" : "false") +
                         ",\"config\":" + value.config + ",\"realms\":{";
    bool first = true;
    for (const auto& realm : value.realms) {
        if (!first)
            result += ',';
        first = false;
        result += quote(realm.first) + ':' + quote(realm.second);
    }
    result += "},\"inject\":[";
    first = true;
    for (const auto& inject : value.inject) {
        if (!first)
            result += ',';
        first = false;
        result += "{\"name\":" + quote(inject.name) + ",\"major\":" + std::to_string(inject.major) +
                  ",\"minimumMinor\":" + std::to_string(inject.minimumMinor) + ",\"capabilities\":[";
        bool firstCapability = true;
        for (const auto& capability : inject.capabilities) {
            if (!firstCapability)
                result += ',';
            firstCapability = false;
            result += quote(capability);
        }
        result += "]}";
    }
    result += "],\"children\":[";
    first = true;
    for (const auto& child : value.children) {
        if (!first)
            result += ',';
        first = false;
        result += serializeEntry(child);
    }
    return result + "]}";
}
std::string serialize(const Document& value)
{
    std::string result = "{\"formatVersion\":1,\"modules\":{";
    bool first = true;
    for (const auto& module : value.modules) {
        if (!first)
            result += ',';
        first = false;
        result += quote(module.first) + ":{\"manifest\":" + quote(module.second) + '}';
    }
    result += "},\"plugins\":[";
    first = true;
    for (const auto& plugin : value.plugins) {
        if (!first)
            result += ',';
        first = false;
        result += serializeEntry(plugin);
    }
    return result + "]}";
}
void eraseEntry(std::vector<EntrySpec>& entries, const std::string& path)
{
    const auto slash = path.find('/');
    const auto id = path.substr(0, slash);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& value) { return value.id == id; });
    require(found != entries.end(), "withdraw path is absent: " + path);
    if (slash == std::string::npos)
        entries.erase(found);
    else
        eraseEntry(found->children, path.substr(slash + 1));
}
QJsonObject applyDocument(Scene& scene, const Document& value)
{
    checked(validateDocument(value));
    const auto text = serialize(value);
    return accepted(scene, scene.state->session->apply(checked(scene.state->session->preview(text))));
}
QJsonObject diskValue(const ConfigurationFileSnapshot& value)
{
    return {{"path", QString::fromStdString(value.version.path)},
            {"exists", value.version.exists},
            {"sha256", QString::fromStdString(value.version.sha256)},
            {"text", QString::fromStdString(value.text)}};
}
QJsonObject saveValue(const SaveReport& value)
{
    return {{"revision", decimal(value.revision)},
            {"outcome", static_cast<int>(value.outcome)},
            {"error", value.error ? QJsonValue(error(*value.error)) : QJsonValue()},
            {"external", value.external ? QJsonValue(diskValue(*value.external)) : QJsonValue()}};
}
QJsonObject save(Scene& scene)
{
    const auto revision = session(scene).coordinator.desiredRevision;
    const auto result = await(checked(scene.state->saveConfiguration(revision)), scene);
    PLATFORM_CHECK(result.revision == revision &&
                   (result.outcome == SaveOutcome::Written || result.outcome == SaveOutcome::Unchanged) &&
                   !result.error && !result.external);
    const auto disk = checked(readConfigurationFile(scene.file));
    const auto state = session(scene);
    PLATFORM_CHECK(state.coordinator.desired && state.store.persistedRevision == revision && !state.store.dirty &&
                   disk.text == state.coordinator.desired->source().normalizedYaml());
    return {{"save", saveValue(result)}, {"disk", diskValue(disk)}};
}
QJsonObject saveConflict(Scene& scene, const Action& action)
{
    const auto before = session(scene);
    const auto running = checked(scene.state->driver->snapshot());
    require(before.store.dirty && !before.store.writing && before.store.queuedWrites == 0,
            "save conflict action requires a dirty, idle Store");
    const auto revision = before.coordinator.desiredRevision;
    const auto text = argument(action, "text");
    require(before.coordinator.desired && text != before.coordinator.desired->source().normalizedYaml(),
            "conflict text must differ from the accepted target");
    require(!before.store.baseline || text != before.store.baseline->text,
            "conflict text must differ from the current file baseline");
    {
        QFile file(scene.file);
        PLATFORM_CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        PLATFORM_CHECK(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
    }
    const auto external = checked(readConfigurationFile(scene.file));
    const auto failed = await(checked(scene.state->saveConfiguration(revision)), scene);
    PLATFORM_CHECK(failed.revision == revision && failed.outcome == SaveOutcome::Conflict && failed.error &&
                   failed.error->code == ErrorCode::ConfigConflict && failed.external &&
                   failed.external->text == text && failed.external->version.sha256 == external.version.sha256 &&
                   checked(readConfigurationFile(scene.file)).text == text);
    const auto conflict = session(scene);
    PLATFORM_CHECK(conflict.coordinator.desired == before.coordinator.desired &&
                   conflict.coordinator.desiredRevision == revision && conflict.store.dirty &&
                   conflict.store.external && conflict.store.external->text == text);
    const auto afterRunning = checked(scene.state->driver->snapshot());
    PLATFORM_CHECK(afterRunning.size() == running.size());
    for (const auto& row : running)
        PLATFORM_CHECK(std::any_of(afterRunning.begin(), afterRunning.end(), [&](const auto& current) {
            return current.path == row.path && current.instance == row.instance && current.epoch == row.epoch &&
                   current.state == row.state;
        }));
    checked(scene.state->session->rebaseExternal(revision));
    const auto rebased = session(scene);
    PLATFORM_CHECK(rebased.coordinator.desired == before.coordinator.desired && rebased.store.dirty &&
                   !rebased.store.external && rebased.store.baseline && rebased.store.baseline->text == text &&
                   checked(readConfigurationFile(scene.file)).text == text);
    return {{"conflict", saveValue(failed)}, {"external", diskValue(external)}, {"after_rebase_save", save(scene)}};
}
} // namespace

void validateAction(const Action& action)
{
    detail::require(!action.id.isEmpty(), "action id is empty");
    const auto schema = schemas().find(action.kind);
    detail::require(schema != schemas().end(), "unknown action: " + action.kind.toStdString());
    detail::require(action.args.size() == static_cast<int>(schema->second.size()), "wrong action argument count");
    for (const auto& field : schema->second) {
        const auto found = action.args.find(field.first);
        detail::require(found != action.args.end() && (field.second ? found->isBool() : found->isString()),
                        "missing or incorrectly typed argument: " + field.first.toStdString());
        if (!field.second && field.first != "text")
            detail::require(!found->toString().isEmpty(), "empty argument: " + field.first.toStdString());
    }
    if (action.kind == "nativeUpgrade") {
        const auto alias = action.args.value("alias").toString();
        detail::require(alias == "counter" || alias == "reporter", "unsupported Native module alias");
    }
    if (action.kind == "nativeUpgrade" || action.kind == "preparedQueuedCancel") {
        const auto variant = action.args.value("variant").toString();
        detail::require(variant == "a" || variant == "b", "unsupported Native artifact variant");
    }
    if (action.kind == "notebookUpgrade" || action.kind == "buildingCancelNotebook")
        detail::require(action.args.value("variant") == QJsonValue("b"), "unsupported Notebook artifact variant");
}

void settle(Scene& scene)
{
    platform_workspace_test::barrier(scene.scheduler);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    platform_workspace_test::barrier(scene.scheduler);
    platform_workspace_test::until([&] {
        const auto queue = detail::checked(scene.scheduler->snapshot());
        const auto current = detail::session(scene);
        return queue.queued == 0 && !queue.running && !current.store.writing && current.store.queuedWrites == 0 &&
               !current.coordinator.activeRevision;
    });
    const auto queue = detail::checked(scene.scheduler->snapshot());
    PLATFORM_CHECK(queue.open && queue.callbackErrors == 0 && queue.discarded == 0);
}

namespace detail {
QJsonObject plan(const PlanReport& value)
{
    QJsonArray entries, errors;
    for (const auto& row : value.entries)
        entries.append(QJsonObject{{"path", QString::fromStdString(row.operation.path)},
                                   {"kind", static_cast<int>(row.operation.kind)},
                                   {"status", static_cast<int>(row.status)},
                                   {"error", row.error ? QJsonValue(error(*row.error)) : QJsonValue()}});
    for (const auto& failure : value.errors)
        errors.append(error(failure));
    return {{"revision", decimal(value.revision)},
            {"outcome", static_cast<int>(value.outcome)},
            {"entries", entries},
            {"errors", errors}};
}
QJsonObject accepted(Scene& scene, Result<AcceptedTarget> value)
{
    const auto target = checked(std::move(value));
    const auto report = await(target.settled, scene);
    PLATFORM_CHECK(report.revision == target.revision);
    return {{"accepted_revision", decimal(target.revision)}, {"plan", plan(report)}};
}
} // namespace detail

QJsonObject execute(Scene& scene, const QString& sourceBin, const Action& action)
{
    using namespace detail;
    validateAction(action);
    require(scene.state && scene.state->session && scene.state->driver && scene.scheduler->isOpen(),
            "action requires an open original Workspace");
    if (action.kind == "save")
        return save(scene);
    if (action.kind == "saveConflictRebase")
        return saveConflict(scene, action);
    if (action.kind == "nativeUpgrade" || action.kind == "notebookUpgrade" || action.kind == "preparedQueuedCancel" ||
        action.kind == "buildingCancelNotebook" || action.kind == "draft")
        return upgradeAction(scene, sourceBin, action);
    const auto target = desired(scene);
    const auto revision = session(scene).coordinator.desiredRevision;
    const auto path = argument(action, "path");
    if (action.kind == "mount") {
        const auto id = argument(action, "id");
        require(target->source().structure().entries().count(id) == 0, "mount id already exists: " + id);
        return accepted(scene, scene.state->session->addInstance(id, "example.notebook",
                                                                 argument(action, "configuration"), revision));
    }
    if (action.kind == "configure") {
        entry(target, path);
        const auto parsed = checked(setEntryConfiguration(target->source(), path, argument(action, "configuration")));
        return accepted(scene,
                        scene.state->session->apply(checked(scene.state->session->preview(parsed.normalizedYaml()))));
    }
    if (action.kind == "enable") {
        const auto enabled = action.args.value("enabled").toBool();
        require(entry(target, path).disabled == enabled, "enable action must change the local enabled flag");
        return accepted(scene, scene.state->session->setEnabled(path, enabled, revision));
    }
    auto value = document(target->source());
    if (action.kind == "withdraw") {
        entry(target, path);
        eraseEntry(value.plugins, path);
        return applyDocument(scene, value);
    }
    if (action.kind == "realmPair") {
        const auto& entries = target->source().structure().entries();
        const bool present = action.args.value("present").toBool();
        require((entries.count("counter2") == 0) == present && (entries.count("reporter2") == 0) == present,
                "realmPair requires both fixed paths to be absent or both present");
        if (present) {
            EntrySpec counter;
            counter.id = "counter2";
            counter.plugin = "example.counter";
            counter.module = "counter";
            counter.config = "null";
            counter.realms = {{"example.counter", "second"}};
            EntrySpec reporter;
            reporter.id = "reporter2";
            reporter.plugin = "example.reporter";
            reporter.module = "reporter";
            reporter.config = "null";
            reporter.realms = {{"example.counter", "second"}, {"example.report", "second"}};
            value.plugins.push_back(std::move(counter));
            value.plugins.push_back(std::move(reporter));
        } else {
            require(entry(target, "counter2").plugin == "example.counter" &&
                        entry(target, "reporter2").plugin == "example.reporter",
                    "realmPair paths belong to unrelated plugins");
            eraseEntry(value.plugins, "reporter2");
            eraseEntry(value.plugins, "counter2");
        }
        return applyDocument(scene, value);
    }
    throw InvalidTrace("unsupported configuration action");
}
} // namespace workspace_stress_test
