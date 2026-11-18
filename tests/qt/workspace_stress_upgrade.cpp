/** @file workspace_stress_upgrade.cpp
 * @brief 原事务的有限升级、真实取消窗口与 Notebook 草稿边界；不拥有第二事务。
 */
#include "native_image_p.hpp"
#include "workspace_stress_actions_p.hpp"
#include <QAbstractProxyModel>
#include <QApplication>
#include <QJsonArray>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QWidget>
#include <algorithm>
#include <cordis_abi/state.h>
#include <exception>
#include <optional>
#include <vector>

namespace workspace_stress_test::detail {
namespace {
using namespace cordis::module;
using notebook::UpgradePreview;
using notebook::WorkspaceUpgradeReport;

RunningEntry runningAt(const Scene& scene, const std::string& path)
{
    const auto rows = checked(scene.state->driver->snapshot());
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& value) { return value.path == path; });
    require(found != rows.end() && found->state == InstanceState::Active && !found->waitingForParent,
            "action requires an active configuration path: " + path);
    return *found;
}
QPlainTextEdit* editor(const Scene& scene, const RunningEntry& row)
{
    auto* page = scene.page(row.instance);
    require(page != nullptr, "active Notebook page is missing");
    auto* value = page->findChild<QPlainTextEdit*>("CordisNotebookEditor");
    require(value != nullptr, "active Notebook editor is missing");
    return value;
}
struct NotebookValue {
    RunningEntry row;
    QPointer<QWidget> page;
    QString draft;
};
std::vector<NotebookValue> notebooks(const Scene& scene)
{
    std::vector<NotebookValue> result;
    for (const auto& row : checked(scene.state->driver->snapshot()))
        if (row.configuration.plugin == "example.notebook" && row.state == InstanceState::Active)
            result.push_back({row, scene.page(row.instance), editor(scene, row)->toPlainText()});
    require(!result.empty(), "Notebook transaction requires at least one active Notebook");
    return result;
}
QJsonObject identities(const std::vector<NotebookValue>& values)
{
    QJsonObject result;
    for (const auto& value : values)
        result.insert(QString::fromStdString(value.row.path),
                      QJsonObject{{"instance", decimal(value.row.instance.value())},
                                  {"epoch", decimal(value.row.epoch)},
                                  {"draft", value.draft}});
    return result;
}
void sameNotebooks(const Scene& scene, const std::vector<NotebookValue>& before, bool rebuilt)
{
    for (const auto& value : before) {
        const auto current = runningAt(scene, value.row.path);
        PLATFORM_CHECK(editor(scene, current)->toPlainText() == value.draft);
        if (rebuilt)
            PLATFORM_CHECK(current.instance != value.row.instance && !value.page);
        else
            PLATFORM_CHECK(current.instance == value.row.instance && current.epoch == value.row.epoch && value.page &&
                           scene.page(current.instance) == value.page);
    }
}
QJsonValue resultValue(const std::optional<Result<void>>& value)
{
    if (!value)
        return {};
    return QJsonObject{{"has_value", value->hasValue()},
                       {"error", value->hasValue() ? QJsonValue() : QJsonValue(error(value->error()))}};
}
QJsonObject reportValue(const WorkspaceUpgradeReport& value)
{
    QJsonObject result{{"alias", QString::fromStdString(value.alias)},
                       {"previous", decimal(value.previous.value())},
                       {"candidate", decimal(value.candidate.value())},
                       {"retired", decimal(value.retired.value())},
                       {"failure", value.failure ? QJsonValue(error(*value.failure)) : QJsonValue()},
                       {"retained", value.retained},
                       {"delivery_blocked", value.deliveryBlocked},
                       {"drain", resultValue(value.drain)},
                       {"unload", resultValue(value.unload)}};
    if (value.nativeClose) {
        QJsonObject close{{"has_value", value.nativeClose->hasValue()}};
        if (value.nativeClose->hasValue()) {
            QJsonArray errors;
            for (const auto& failure : value.nativeClose->value().errors)
                errors.append(error(failure));
            close.insert("quiescent", value.nativeClose->value().quiescent);
            close.insert("errors", errors);
        } else
            close.insert("error", error(value.nativeClose->error()));
        result.insert("native_close", close);
    } else
        result.insert("native_close", QJsonValue());
    if (value.replacement) {
        const auto& report = *value.replacement;
        QJsonArray instances, errors;
        for (const auto& mapping : report.instances)
            instances.append(QJsonObject{{"previous", decimal(mapping.previous.value())},
                                         {"current", decimal(mapping.current.value())}});
        for (const auto& failure : report.errors)
            errors.append(error(failure));
        result.insert("replacement", QJsonObject{{"operation", decimal(report.operation.value())},
                                                 {"previous", decimal(report.previous.value())},
                                                 {"candidate", decimal(report.candidate.value())},
                                                 {"outcome", static_cast<int>(report.outcome)},
                                                 {"committed", report.committed},
                                                 {"recovered", report.recovered},
                                                 {"resources_retained", report.resourcesRetained},
                                                 {"instances", instances},
                                                 {"errors", errors}});
    } else
        result.insert("replacement", QJsonValue());
    return result;
}
void checkReport(const WorkspaceUpgradeReport& report, const UpgradePreview& preview)
{
    PLATFORM_CHECK(report.alias == preview.alias && report.previous == preview.previous && report.candidate.valid() &&
                   report.candidate != preview.previous && report.replacement &&
                   report.replacement->operation.valid() && report.replacement->previous == report.previous &&
                   report.replacement->candidate == report.candidate && !report.failure && !report.retained &&
                   !report.deliveryBlocked && !report.replacement->resourcesRetained);
}
UpgradePreview preview(Scene& scene, const QString& sourceBin, const std::string& alias, const std::string& variant)
{
    const auto upgrade = checked(scene.state->upgradeSnapshot());
    require(upgrade.accepting && !upgrade.active, "an upgrade is already active or closed");
    const auto state = session(scene);
    require(state.coordinator.desired && !state.coordinator.activeRevision, "upgrade requires a settled configuration");
    const auto rows = checked(scene.state->manager->snapshot());
    const bool qt = alias == "notebook";
    std::size_t count = 0;
    for (const auto& row : rows)
        if ((row.manifest.loader == ModuleLoader::QtUi) == qt)
            ++count;
    require(qt ? count == 1 : count < 18, qt ? "only one Notebook upgrade attempt is permitted per process"
                                             : "Native upgrade attempt budget is exhausted");
    const auto modules = checked(scene.state->modulesSnapshot());
    const auto current = std::find_if(modules.current.begin(), modules.current.end(),
                                      [&](const auto& value) { return value.alias == alias; });
    require(current != modules.current.end(), "upgrade alias has no current module");
    const auto old = std::find_if(rows.begin(), rows.end(),
                                  [&](const auto& value) { return value.generation == current->generation; });
    require(old != rows.end() && old->role == GenerationRole::Current && old->nativeMapped,
            "upgrade source is not a mapped Current generation");
    const auto path = (qt ? "qt-upgrade/" : "native-upgrade/") + alias + '-' + variant + "/module.json";
    const auto artifact = checked(preflightModule(platform_workspace_test::utf8(sourceBin), path));
    require(old->manifest.sha256 != artifact.manifest().sha256, "upgrade artifact is already current");
    if (!qt)
        for (const auto& row : rows)
            if (row.manifest.sha256 == artifact.manifest().sha256)
                require(row.state == ModuleState::Unloaded && !row.nativeMapped,
                        "Native A/B content cannot be reused before actual unload");
    return checked(scene.state->previewUpgrade(alias, sourceBin, path));
}
void verifyCurrent(Scene& scene, const UpgradePreview& value, const WorkspaceUpgradeReport& report, bool committed)
{
    const auto generation = committed ? report.candidate : report.previous;
    const auto modules = checked(scene.state->modulesSnapshot());
    const auto current = std::find_if(modules.current.begin(), modules.current.end(),
                                      [&](const auto& row) { return row.alias == value.alias; });
    PLATFORM_CHECK(current != modules.current.end() && current->generation == generation && current->runtime.valid());
    const auto origin = checked(scene.state->manager->runtimeOrigin(current->runtime));
    PLATFORM_CHECK(origin && origin->generation == generation && origin->pluginId == "example." + value.alias);
    PLATFORM_CHECK((current->runtime != value.previousRuntime) == committed);
    const auto state = session(scene);
    PLATFORM_CHECK(state.coordinator.desired &&
                   state.coordinator.desired->source().structure().modules().at(value.alias) ==
                       (committed ? value.artifact.manifest().sha256 + "/module.json"
                                  : value.previousManifest.sha256 + "/module.json"));
    PLATFORM_CHECK(
        checked(scene.state->driver->configurationDefinitions()->module("example." + value.alias)).manifest ==
        state.coordinator.desired->source().structure().modules().at(value.alias));
    PLATFORM_CHECK(checked(scene.state->kernel->replacements()).empty());
    const auto retired = platform_upgrade_test::module(scene, report.retired);
    PLATFORM_CHECK(!retired.replacement);
}
void reclaimed(Scene& scene, const WorkspaceUpgradeReport& report, const platform_upgrade_test::Image& image)
{
    PLATFORM_CHECK(report.nativeClose && report.nativeClose->hasValue() && report.nativeClose->value().quiescent &&
                   report.drain && report.drain->hasValue() && report.unload && report.unload->hasValue());
    const auto old = platform_upgrade_test::module(scene, report.retired);
    PLATFORM_CHECK(old.state == ModuleState::Unloaded && !old.nativeMapped && old.resources.drained());
    image.absent();
}
bool captureLimit(const ErrorInfo& failure)
{
    if (failure.code == ErrorCode::InvalidArgument && failure.phase == "module.state" &&
        failure.message == "state operation rejected, status " + std::to_string(CORDIS_E_LIMIT))
        return true;
    return std::any_of(failure.causes.begin(), failure.causes.end(), captureLimit);
}
QJsonObject upgrade(Scene& scene, const QString& sourceBin, const Action& action)
{
    const bool native = action.kind == "nativeUpgrade";
    const auto alias = native ? argument(action, "alias") : std::string("notebook");
    const auto value = preview(scene, sourceBin, alias, argument(action, "variant"));
    const auto before = session(scene);
    const auto notes = notebooks(scene);
    const std::optional<platform_upgrade_test::Image> image =
        native ? std::optional<platform_upgrade_test::Image>(platform_upgrade_test::module(scene, value.previous))
               : std::nullopt;
    const auto report = await(checked(scene.state->upgrade(value)), scene);
    checkReport(report, value);
    QJsonObject evidence{{"base_revision", decimal(value.desiredRevision)},
                         {"report", reportValue(report)},
                         {"notebooks_before", identities(notes)}};
    const auto& replacement = *report.replacement;
    if (replacement.outcome == ReplacementOutcome::Applied) {
        PLATFORM_CHECK(replacement.committed && !replacement.recovered && report.retired == report.previous);
        verifyCurrent(scene, value, report, true);
        PLATFORM_CHECK(session(scene).coordinator.desiredRevision == value.desiredRevision + 1);
        sameNotebooks(scene, notes, !native);
        if (native) {
            reclaimed(scene, report, *image);
            PLATFORM_CHECK(!checked(scene.state->manager->runtimeOrigin(value.previousRuntime)));
            evidence.insert("old_image_absent_file_exclusive", true);
        }
        return evidence;
    }
    // d2b 的目标来自真实原 capture 拒绝；不抛测试伪造错误，也不把任意 Failed 当目标。
    const bool limit = !native && replacement.outcome == ReplacementOutcome::Failed &&
                       std::any_of(replacement.errors.begin(), replacement.errors.end(), captureLimit);
    if (limit) {
        PLATFORM_CHECK(!replacement.committed && !replacement.recovered && replacement.instances.empty() &&
                       report.retired == report.candidate &&
                       session(scene).coordinator.desiredRevision == before.coordinator.desiredRevision &&
                       session(scene).coordinator.desired == before.coordinator.desired);
        bool oversized = false;
        for (const auto& note : notes)
            oversized = oversized || note.draft.toUtf8().size() > 65536;
        PLATFORM_CHECK(oversized);
        sameNotebooks(scene, notes, false);
        verifyCurrent(scene, value, report, false);
        evidence.insert("failure_signature",
                        "notebook.capture.limit/InvalidArgument/module.state/unchanged-before-stop");
        evidence.insert("notebooks_after", identities(notebooks(scene)));
        return evidence;
    }
    // 原报告已经保存在返回证据中，入口将此签名判为真实失败而不是成功动作。
    evidence.insert("failure_signature", "unexpected.replacement.outcome");
    return evidence;
}

QJsonObject queuedCancel(Scene& scene, const QString& sourceBin, const Action& action)
{
    const auto path = argument(action, "path");
    const auto target = desired(scene);
    require(entry(target, path).plugin == "example.notebook", "queued revision path must be an unrelated Notebook");
    require(entry(target, "counter").plugin == "example.counter", "fixed counter path is absent");
    const auto oldCounter = runningAt(scene, "counter");
    const auto value = preview(scene, sourceBin, "counter", argument(action, "variant"));
    const auto task = checked(scene.state->upgrade(value));
    const auto snapshot = checked(scene.state->upgradeSnapshot());
    require(snapshot.phase == ReplacementPhase::Prepared && snapshot.active && snapshot.active->replacement,
            "saved Prepared window was not observed");
    const auto operation = snapshot.active->replacement->operation;
    const platform_upgrade_test::Image candidateImage(platform_upgrade_test::module(scene, snapshot.active->candidate));
    const auto related = checked(setEntryConfiguration(target->source(), "counter", "{failCleanup: true}"));
    const auto rejected = scene.state->session->preview(related.normalizedYaml());
    PLATFORM_CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict &&
                   rejected.error().phase == "loader.replacement.claim" && rejected.error().operation == operation &&
                   session(scene).coordinator.desiredRevision == value.desiredRevision);
    const auto firstSource =
        checked(setEntryConfiguration(desired(scene)->source(), path, argument(action, "firstConfiguration")));
    const auto first =
        checked(scene.state->session->apply(checked(scene.state->session->preview(firstSource.normalizedYaml()))));
    const auto secondSource =
        checked(setEntryConfiguration(desired(scene)->source(), path, argument(action, "secondConfiguration")));
    const auto second =
        checked(scene.state->session->apply(checked(scene.state->session->preview(secondSource.normalizedYaml()))));
    // 到这里没有等待、barrier 或 pump；两个修订均通过原 CAS 并排入同一 Coordinator。
    require(checked(scene.state->upgradeSnapshot()).phase == ReplacementPhase::Prepared,
            "Prepared window changed before saved cancellation");
    checked(scene.state->cancelUpgrade(operation));
    PLATFORM_CHECK(checked(scene.state->upgradeSnapshot()).cancellationRequested);
    const auto report = await(task, scene);
    const auto firstReport = await(first.settled, scene);
    const auto secondReport = await(second.settled, scene);
    checkReport(report, value);
    PLATFORM_CHECK(report.replacement->operation == operation &&
                   report.replacement->outcome == ReplacementOutcome::Cancelled && !report.replacement->committed &&
                   !report.replacement->recovered && report.replacement->instances.empty() &&
                   first.revision == value.desiredRevision + 1 && second.revision == first.revision + 1 &&
                   firstReport.outcome == PlanOutcome::Superseded && secondReport.outcome == PlanOutcome::Applied &&
                   session(scene).coordinator.desiredRevision == second.revision &&
                   runningAt(scene, "counter").instance == oldCounter.instance &&
                   runningAt(scene, "counter").epoch == oldCounter.epoch);
    verifyCurrent(scene, value, report, false);
    const auto cancelled = platform_upgrade_test::module(scene, report.candidate);
    PLATFORM_CHECK(cancelled.state == ModuleState::Unloaded && !cancelled.nativeMapped &&
                   cancelled.resources.drained() && report.nativeClose && report.nativeClose->hasValue() &&
                   report.nativeClose->value().quiescent && report.drain && report.drain->hasValue() && report.unload &&
                   report.unload->hasValue());
    candidateImage.absent();
    return {{"observed_phase", static_cast<int>(ReplacementPhase::Prepared)},
            {"operation", decimal(operation.value())},
            {"related_rejection", error(rejected.error())},
            {"first_accepted_revision", decimal(first.revision)},
            {"second_accepted_revision", decimal(second.revision)},
            {"first_plan", plan(firstReport)},
            {"second_plan", plan(secondReport)},
            {"candidate_image_absent_file_exclusive", true},
            {"report", reportValue(report)}};
}

QAbstractItemModel* commandSource()
{
    for (auto* window : QApplication::topLevelWidgets())
        if (auto* list = window->findChild<QListView*>("CordisCommandList")) {
            auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
            require(proxy && proxy->sourceModel(), "real command source model is unavailable");
            return proxy->sourceModel();
        }
    throw InvalidTrace("real command list is unavailable");
}
bool heldCandidate(const Scene& scene, GenerationId previous)
{
    const auto commands = checked(scene.state->shell.commands()->snapshot());
    const auto core = checked(scene.state->kernel->snapshot());
    const auto graph = checked(scene.state->kernel->services().dependencyGraph());
    for (const auto& command : commands) {
        if (command.id != "notebook.publish" || command.retiring)
            continue;
        const auto instance = std::find_if(core.instances.begin(), core.instances.end(), [&](const auto& row) {
            return row.instance == command.owner.instance && row.epoch == command.owner.epoch;
        });
        if (instance == core.instances.end())
            continue;
        const auto origin = checked(scene.state->manager->runtimeOrigin(instance->runtime));
        if (!origin || origin->generation == previous || origin->pluginId != "example.notebook")
            continue;
        const auto node = std::find_if(graph.instances.begin(), graph.instances.end(),
                                       [&](const auto& row) { return row.instance == instance->instance; });
        if (node != graph.instances.end() && node->publicationHeld)
            return true;
    }
    return false;
}
QJsonObject buildingCancel(Scene& scene, const QString& sourceBin, const Action& action)
{
    const auto notes = notebooks(scene);
    const auto before = session(scene);
    const auto value = preview(scene, sourceBin, "notebook", argument(action, "variant"));
    auto* model = commandSource();
    bool observed = false;
    std::exception_ptr failure;
    OperationId operation;
    struct Observer {
        QMetaObject::Connection connection;
        ~Observer() { QObject::disconnect(connection); }
    } observer;
    observer.connection = QObject::connect(model, &QAbstractItemModel::rowsInserted, model, [&] {
        if (observed || failure)
            return;
        try {
            if (!heldCandidate(scene, value.previous))
                return;
            const auto snapshot = checked(scene.state->upgradeSnapshot());
            require(operation.valid() && snapshot.phase == ReplacementPhase::Building && snapshot.active &&
                        snapshot.active->replacement && snapshot.active->replacement->operation == operation,
                    "saved Building cancellation window does not match its operation");
            observed = true;
            checked(scene.state->cancelUpgrade(operation));
            PLATFORM_CHECK(checked(scene.state->upgradeSnapshot()).cancellationRequested);
        } catch (...) {
            failure = std::current_exception();
        }
    });
    const auto task = checked(scene.state->upgrade(value));
    const auto initial = checked(scene.state->upgradeSnapshot());
    require(initial.active && initial.active->replacement, "Notebook operation was not created");
    operation = initial.active->replacement->operation;
    const auto report = await(task, scene);
    QObject::disconnect(observer.connection);
    if (failure)
        std::rethrow_exception(failure);
    require(observed, "saved Building cancellation window was not observed");
    checkReport(report, value);
    PLATFORM_CHECK(report.replacement->operation == operation &&
                   report.replacement->outcome == ReplacementOutcome::Cancelled && !report.replacement->committed &&
                   report.replacement->recovered && report.retired == report.candidate &&
                   session(scene).coordinator.desiredRevision == before.coordinator.desiredRevision &&
                   session(scene).coordinator.desired == before.coordinator.desired);
    sameNotebooks(scene, notes, true);
    verifyCurrent(scene, value, report, false);
    return {{"observed_phase", static_cast<int>(ReplacementPhase::Building)},
            {"operation", decimal(operation.value())},
            {"report", reportValue(report)},
            {"notebooks_before", identities(notes)},
            {"notebooks_after", identities(notebooks(scene))}};
}
} // namespace

QJsonObject upgradeAction(Scene& scene, const QString& sourceBin, const Action& action)
{
    if (action.kind == "draft") {
        const auto row = runningAt(scene, argument(action, "path"));
        require(row.configuration.plugin == "example.notebook", "draft target is not a Notebook");
        const auto text = action.args.value("text").toString();
        auto* value = editor(scene, row);
        value->setPlainText(text);
        PLATFORM_CHECK(value->toPlainText() == text);
        return {{"instance", decimal(row.instance.value())},
                {"epoch", decimal(row.epoch)},
                {"utf8_bytes", decimal(static_cast<std::uint64_t>(text.toUtf8().size()))},
                {"text", text}};
    }
    if (action.kind == "preparedQueuedCancel")
        return queuedCancel(scene, sourceBin, action);
    if (action.kind == "buildingCancelNotebook")
        return buildingCancel(scene, sourceBin, action);
    return upgrade(scene, sourceBin, action);
}
} // namespace workspace_stress_test::detail
