/** @file workspace_endurance_round.cpp
 * @brief 原事务、显式保存与纯值基线各自完成；测量不预约或捕获业务状态。
 */
#include "native_image_p.hpp"
#include "workspace_endurance_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSaveFile>
#include <QWidget>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace workspace_endurance_test {
namespace {
using namespace platform_upgrade_test;
using Clock = std::chrono::steady_clock;
using notebook::Workspace;

QString number(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}
QString text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}
std::uint64_t elapsed(Clock::time_point from, Clock::time_point to = Clock::now())
{
    const auto value = std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count();
    PLATFORM_CHECK(value >= 0);
    return static_cast<std::uint64_t>(value);
}
struct Failure final : std::exception {
    ErrorInfo error;
    explicit Failure(ErrorInfo value) : error(std::move(value)) {}
    const char* what() const noexcept override { return error.message.c_str(); }
};
template <class T> T checked(Result<T> result)
{
    if (!result.hasValue())
        throw Failure(result.error());
    return std::move(result).value();
}
void checked(Result<void> result)
{
    if (!result.hasValue())
        throw Failure(result.error());
}
QJsonObject errorValue(const ErrorInfo& error)
{
    QJsonArray causes;
    for (const auto& cause : error.causes)
        causes.append(errorValue(cause));
    QJsonObject value{{"code", number(static_cast<std::uint64_t>(error.code))},
                      {"phase", text(error.phase)},
                      {"message", text(error.message)},
                      {"operation", number(error.operation.value())},
                      {"causes", causes}};
    if (error.instance)
        value.insert("instance", number(error.instance->value()));
    if (error.entryPath)
        value.insert("entry_path", text(*error.entryPath));
    return value;
}
QString phaseName(ReplacementPhase phase)
{
    switch (phase) {
    case ReplacementPhase::Prepared:
        return "Prepared";
    case ReplacementPhase::Capturing:
        return "Capturing";
    case ReplacementPhase::Stopping:
        return "Stopping";
    case ReplacementPhase::Building:
        return "Building";
    case ReplacementPhase::Discarding:
        return "Discarding";
    case ReplacementPhase::Recovering:
        return "Recovering";
    case ReplacementPhase::Committed:
        return "Committed";
    case ReplacementPhase::Restored:
        return "Restored";
    case ReplacementPhase::Failed:
        return "Failed";
    case ReplacementPhase::Cancelled:
        return "Cancelled";
    }
    throw std::runtime_error("unknown original replacement phase");
}
QJsonObject reportValue(const notebook::WorkspaceUpgradeReport& report)
{
    QJsonObject value{{"alias", text(report.alias)},
                      {"previous", number(report.previous.value())},
                      {"candidate", number(report.candidate.value())},
                      {"retired", number(report.retired.value())},
                      {"retained", report.retained},
                      {"delivery_blocked", report.deliveryBlocked}};
    if (report.failure)
        value.insert("failure", errorValue(*report.failure));
    if (report.replacement) {
        QJsonArray errors, mappings;
        for (const auto& error : report.replacement->errors)
            errors.append(errorValue(error));
        for (const auto& mapping : report.replacement->instances)
            mappings.append(QJsonObject{{"previous", number(mapping.previous.value())},
                                        {"current", number(mapping.current.value())}});
        value.insert("replacement",
                     QJsonObject{{"operation", number(report.replacement->operation.value())},
                                 {"outcome", number(static_cast<std::uint64_t>(report.replacement->outcome))},
                                 {"committed", report.replacement->committed},
                                 {"recovered", report.replacement->recovered},
                                 {"resources_retained", report.replacement->resourcesRetained},
                                 {"instances", mappings},
                                 {"errors", errors}});
    }
    return value;
}

/** @brief FIFO 采样只弱借原宿主，记录可观察时点，不能代表内部精确阶段边界。 */
struct PhaseProbe final : std::enable_shared_from_this<PhaseProbe> {
    std::weak_ptr<Workspace> workspace;
    std::weak_ptr<qt::QtScheduler> scheduler;
    Clock::time_point requested;
    OperationId operation;
    QJsonArray observations;
    std::optional<ErrorInfo> failure;
    std::uint64_t samples = 0, maximumQueueDelay = 0;
    bool stopped = false;

    void post()
    {
        const auto target = scheduler.lock();
        if (!target) {
            failure.emplace(makeError(ErrorCode::SchedulerClosed, "phase sampler lost original scheduler"));
            stopped = true;
            return;
        }
        const auto queued = Clock::now();
        const auto self = shared_from_this();
        if (!target->post({[self, queued] { self->observe(queued); },
                           [self] {
                               self->failure.emplace(
                                   makeError(ErrorCode::SchedulerClosed, "phase sampler original FIFO job discarded"));
                               self->stopped = true;
                           }})) {
            failure.emplace(makeError(ErrorCode::SchedulerClosed, "phase sampler original FIFO job rejected"));
            stopped = true;
        }
    }
    void observe(Clock::time_point queued) noexcept
    {
        if (stopped)
            return;
        try {
            const auto owner = workspace.lock();
            PLATFORM_CHECK(owner);
            const auto now = Clock::now();
            maximumQueueDelay = std::max(maximumQueueDelay, elapsed(queued, now));
            ++samples;
            const auto snapshot = checked(owner->upgradeSnapshot());
            if (!snapshot.active) {
                observations.append(
                    QJsonObject{{"phase", "host_settled"}, {"observed_ns", number(elapsed(requested, now))}});
                stopped = true;
                return;
            }
            if (snapshot.active->replacement)
                PLATFORM_CHECK(snapshot.active->replacement->operation == operation);
            const auto phase = snapshot.phase ? phaseName(*snapshot.phase) : QStringLiteral("preparation");
            if (observations.isEmpty() || observations.last().toObject().value("phase").toString() != phase)
                observations.append(QJsonObject{{"phase", phase}, {"observed_ns", number(elapsed(requested, now))}});
            post();
        } catch (const Failure& error) {
            failure.emplace(error.error);
            stopped = true;
        } catch (const std::exception& error) {
            failure.emplace(makeError(ErrorCode::CallbackFailed, error.what()));
            stopped = true;
        } catch (...) {
            failure.emplace(makeError(ErrorCode::CallbackFailed, "unknown phase sampling exception"));
            stopped = true;
        }
    }
};

struct Baseline {
    QJsonObject catalog, objects;
    std::uint32_t handles = 0, gdi = 0, user = 0;
    QJsonObject json(unsigned phase) const
    {
        return {{"phase", number(phase)},
                {"catalog", catalog},
                {"owned_object_shape", objects},
                {"handles_upper", number(handles)},
                {"gdi_upper", number(gdi)},
                {"user_upper", number(user)},
                {"windows_comparison", "per-phase warmup observed upper bounds; no added tolerance"}};
    }
};
struct BusinessBindings {
    BindingId counter, reporter;
};

class Endurance final {
public:
    Endurance(Scene& scene, const QString& source, const QString& output)
        : scene_(scene), source_(source), output_(QFileInfo(output).absoluteFilePath()), lines_(output_ + ".jsonl")
    {
        PLATFORM_CHECK(QDir().mkpath(QFileInfo(output_).absolutePath()));
        PLATFORM_CHECK(lines_.open(QIODevice::WriteOnly | QIODevice::Truncate));
    }
    void run()
    {
        try {
            initialize();
            for (unsigned round = 0; round != 108; ++round)
                oneRound(round);
            PLATFORM_CHECK(expected_ == 163 && operations_.size() == 108 && generations_.size() == 111 &&
                           completed_ == 108);
            current_ = {{"step", "normal_close"}};
            finish(scene_);
            PLATFORM_CHECK(!notebookPage_);
            const auto stopped = sampleDesktop();
            QJsonObject summary{{"passed", true},
                                {"warmup_applied", "8"},
                                {"measured_applied", "100"},
                                {"total_original_applied", "108"},
                                {"final_counter_result", number(expected_)},
                                {"operations", rounds_},
                                {"baselines", baselinesJson()},
                                {"after_close_desktop", desktopValue(stopped)},
                                {"timing_contract", timingContract()},
                                {"initial_desktop", initialDesktop_}};
            writeSummary(summary);
            append({{"event", "complete"}, {"summary", summary}});
        } catch (const Failure& error) {
            failed(error.error);
            throw;
        } catch (const std::exception& error) {
            failed(makeError(ErrorCode::CallbackFailed, error.what()));
            throw;
        }
    }

private:
    Scene& scene_;
    QString source_, output_;
    QFile lines_;
    RunningEntry notebook_;
    QPointer<QWidget> notebookPage_;
    std::string commandTitle_;
    GenerationId notebookGeneration_;
    RuntimeId notebookRuntime_;
    std::set<OperationId> operations_;
    std::set<GenerationId> generations_;
    std::array<std::optional<Baseline>, 4> baselines_;
    QJsonObject current_, initialDesktop_;
    QJsonArray rounds_;
    std::uint64_t expected_ = 1, completed_ = 0;

    static QJsonObject timingContract()
    {
        return {{"clock", "std::chrono::steady_clock; decimal nanoseconds"},
                {"request", "request entry to original wait receipt observed by caller"},
                {"save", "explicit save request to original wait receipt observed by caller"},
                {"phases", "FIFO samples only; unseen phases are not zero duration; no precise downtime/SLA"}};
    }
    static QJsonObject desktopValue(const DesktopSample& value)
    {
        return {{"owned_object_trees", value.ownedObjectTrees},
                {"handles", number(value.handles)},
                {"gdi", number(value.gdi)},
                {"user", number(value.user)}};
    }
    QJsonArray baselinesJson() const
    {
        QJsonArray values;
        for (unsigned phase = 0; phase != baselines_.size(); ++phase)
            if (baselines_[phase])
                values.append(baselines_[phase]->json(phase));
        return values;
    }
    void append(const QJsonObject& value)
    {
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
        PLATFORM_CHECK(lines_.write(bytes) == bytes.size() && lines_.flush());
    }
    void writeSummary(const QJsonObject& value)
    {
        QSaveFile file(output_ + ".json");
        PLATFORM_CHECK(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
        PLATFORM_CHECK(file.write(bytes) == bytes.size() && file.commit());
    }
    void failed(const ErrorInfo& error)
    {
        QJsonObject value{{"passed", false},
                          {"completed_verified_rounds", number(completed_)},
                          {"observed_original_applied", number(operations_.size())},
                          {"failure", errorValue(error)},
                          {"context", current_},
                          {"operations", rounds_},
                          {"baselines", baselinesJson()},
                          {"timing_contract", timingContract()}};
        if (scene_.state) {
            const auto state = scene_.state->upgradeSnapshot();
            if (state.hasValue()) {
                if (state.value().phase)
                    value.insert("actual_phase", phaseName(*state.value().phase));
                if (state.value().active)
                    value.insert("actual_active", reportValue(*state.value().active));
                if (!state.value().history.empty())
                    value.insert("actual_latest", reportValue(state.value().history.back()));
            } else {
                value.insert("actual_snapshot_error", errorValue(state.error()));
            }
        }
        append({{"event", "failure"}, {"report", value}});
        writeSummary(value);
    }
    void settle()
    {
        barrier(scene_.scheduler);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        barrier(scene_.scheduler);
        until([&] {
            const auto queue = checked(scene_.scheduler->snapshot());
            const auto store = checked(scene_.state->session->snapshot()).store;
            return queue.queued == 0 && !queue.running && !store.writing && store.queuedWrites == 0;
        });
        const auto queue = checked(scene_.scheduler->snapshot());
        PLATFORM_CHECK(queue.open && queue.callbackErrors == 0 && queue.discarded == 0);
    }
    ParsedConfiguration disk() const
    {
        const auto snapshot = checked(readConfigurationFile(scene_.file));
        PLATFORM_CHECK(snapshot.version.exists);
        return checked(parseConfiguration(snapshot.text));
    }
    std::uint64_t save()
    {
        const auto revision = checked(scene_.state->session->snapshot()).coordinator.desiredRevision;
        const auto started = Clock::now();
        {
            const auto task = checked(scene_.state->saveConfiguration(revision));
            const auto result = checked(*wait(task, scene_.scheduler));
            PLATFORM_CHECK(result.revision == revision && !result.error &&
                           (result.outcome == SaveOutcome::Written || result.outcome == SaveOutcome::Unchanged));
        }
        const auto duration = elapsed(started);
        settle();
        const auto state = checked(scene_.state->session->snapshot());
        PLATFORM_CHECK(!state.store.dirty && !state.store.writing && state.store.queuedWrites == 0 &&
                       !state.store.lastError && state.store.persistedRevision == revision &&
                       state.store.persisted == state.store.desired);
        return duration;
    }
    void initialize()
    {
        current_ = {{"step", "initialize"}};
        scene_.open(source_);
        scene_.apply(scene_.state->initial);
        scene_.business();
        for (const auto& generation : checked(scene_.state->manager->snapshot()))
            PLATFORM_CHECK(generations_.insert(generation.generation).second);
        PLATFORM_CHECK(generations_.size() == 3);
        notebook_ = running(scene_, "example.notebook");
        notebookGeneration_ = checked(scene_.state->generation());
        notebookRuntime_ = runtimeOf(notebook_.instance);
        notebookPage_ = scene_.page(notebook_.instance);
        PLATFORM_CHECK(notebookPage_);
        const auto commands = checked(scene_.state->shell.commands()->snapshot());
        PLATFORM_CHECK(commands.size() == 1 && commands.front().id == "notebook.publish");
        commandTitle_ = commands.front().title;
        prepareDesktop(scene_);
        scene_.edit(notebook_.instance, QString::fromUtf8("耐久预热准备：真实草稿与命令 😀"));
        settle();
        save();
        reporterValue(scene_, "1");
        initialDesktop_ = desktopValue(sampleDesktop());
        append({{"event", "initialized"}, {"desktop", initialDesktop_}, {"timing_contract", timingContract()}});
    }
    void notebookUnchanged(const QString& draft)
    {
        const auto actual = running(scene_, "example.notebook");
        PLATFORM_CHECK(actual.instance == notebook_.instance && actual.epoch == notebook_.epoch &&
                       runtimeOf(actual.instance) == notebookRuntime_ &&
                       checked(scene_.state->generation()) == notebookGeneration_ && notebookPage_ &&
                       scene_.page(actual.instance) == notebookPage_);
        const auto editor = notebookPage_->findChild<QPlainTextEdit*>("EveNotebookEditor");
        PLATFORM_CHECK(editor && editor->toPlainText() == draft);
        const auto commands = checked(scene_.state->shell.commands()->snapshot());
        PLATFORM_CHECK(commands.size() == 1 && commands.front().owner.instance == notebook_.instance &&
                       commands.front().owner.epoch == notebook_.epoch && commands.front().id == "notebook.publish" &&
                       commands.front().title == commandTitle_ && !commands.front().retiring &&
                       !commands.front().deliveryBlocked && commands.front().activeCalls == 0);
    }
    RuntimeId runtimeOf(InstanceId instance) const
    {
        const auto kernel = checked(scene_.state->kernel->snapshot());
        const auto found = std::find_if(kernel.instances.begin(), kernel.instances.end(),
                                        [instance](const auto& row) { return row.instance == instance; });
        PLATFORM_CHECK(found != kernel.instances.end() && found->state == InstanceState::Active);
        return found->runtime;
    }
    BusinessBindings businessBindings(const RunningEntry& counter, const RunningEntry& reporter)
    {
        const auto graph = checked(scene_.state->kernel->services().dependencyGraph());
        const auto found = std::find_if(graph.bindings.begin(), graph.bindings.end(),
                                        [](const auto& row) { return row.binding.key.name == "example.counter"; });
        PLATFORM_CHECK(found != graph.bindings.end() && found->managedProvider && found->provider == counter.instance &&
                       found->binding.state == BindingState::Published && found->binding.activeCalls == 0 &&
                       found->binding.committedLeases == 1 && found->consumers.size() == 1 &&
                       found->consumers.front().managed && found->consumers.front().instance == reporter.instance &&
                       found->consumers.front().epoch == reporter.epoch);
        const auto report = std::find_if(graph.bindings.begin(), graph.bindings.end(),
                                         [](const auto& row) { return row.binding.key.name == "example.report"; });
        PLATFORM_CHECK(report != graph.bindings.end() && report->managedProvider &&
                       report->provider == reporter.instance && report->binding.state == BindingState::Published &&
                       report->binding.activeCalls == 0 && report->binding.committedLeases == 0 &&
                       report->consumers.empty());
        return {found->binding.id, report->binding.id};
    }
    void checkReport(const notebook::UpgradePreview& preview, const notebook::WorkspaceUpgradeReport& report,
                     const RunningEntry& oldCounter, const RunningEntry& oldReporter, RuntimeId oldCounterRuntime,
                     RuntimeId oldReporterRuntime, BusinessBindings oldBindings)
    {
        PLATFORM_CHECK(report.alias == preview.alias && report.previous == preview.previous &&
                       report.candidate.valid() && report.candidate != report.previous &&
                       report.retired == report.previous && report.replacement &&
                       report.replacement->operation.valid() && report.replacement->previous == report.previous &&
                       report.replacement->candidate == report.candidate && report.replacement->committed &&
                       report.replacement->outcome == ReplacementOutcome::Applied && !report.replacement->recovered &&
                       !report.replacement->resourcesRetained && !report.failure && !report.retained &&
                       !report.deliveryBlocked && report.replacement->errors.empty());
        PLATFORM_CHECK(operations_.insert(report.replacement->operation).second);
        PLATFORM_CHECK(generations_.insert(report.candidate).second);
        const auto current = platform_upgrade_test::module(scene_, report.candidate);
        const auto old = platform_upgrade_test::module(scene_, report.previous);
        PLATFORM_CHECK(
            current.role == GenerationRole::Current && current.manifest.sha256 == preview.artifact.manifest().sha256 &&
            current.manifest.moduleVersion == preview.artifact.manifest().moduleVersion && !current.replacement &&
            !current.failure && !current.businessFailure && old.role == GenerationRole::Retired &&
            old.state == ModuleState::Unloaded && !old.nativeMapped && !old.nativePinned && old.resources.drained() &&
            !old.unloadFailure && !old.replacement && report.nativeClose && report.nativeClose->hasValue() &&
            report.nativeClose->value().quiescent && report.drain && report.drain->hasValue() && report.unload &&
            report.unload->hasValue() && !checked(scene_.state->manager->runtimeOrigin(preview.previousRuntime)));
        const auto counter = running(scene_, "example.counter"), reporter = running(scene_, "example.reporter");
        PLATFORM_CHECK(reporter.instance != oldReporter.instance && reporter.epoch == 1);
        const bool replacingCounter = preview.alias == "counter";
        PLATFORM_CHECK(replacingCounter ? counter.instance != oldCounter.instance && counter.epoch == 1
                                        : counter.instance == oldCounter.instance && counter.epoch == oldCounter.epoch);
        const auto bindings = businessBindings(counter, reporter);
        PLATFORM_CHECK(
            (replacingCounter ? bindings.counter != oldBindings.counter : bindings.counter == oldBindings.counter) &&
            bindings.reporter != oldBindings.reporter);
        const auto counterRuntime = runtimeOf(counter.instance), reporterRuntime = runtimeOf(reporter.instance);
        PLATFORM_CHECK(replacingCounter ? counterRuntime != oldCounterRuntime && reporterRuntime == oldReporterRuntime
                                        : counterRuntime == oldCounterRuntime && reporterRuntime != oldReporterRuntime);
        const auto runtime = replacingCounter ? counterRuntime : reporterRuntime;
        const auto origin = checked(scene_.state->manager->runtimeOrigin(runtime));
        PLATFORM_CHECK(origin && origin->generation == report.candidate && runtime != preview.previousRuntime &&
                       origin->pluginId == "example." + preview.alias &&
                       report.replacement->instances.size() == (replacingCounter ? 2u : 1u));
        for (const auto& mapping : report.replacement->instances)
            PLATFORM_CHECK(
                (mapping.previous == oldReporter.instance && mapping.current == reporter.instance) ||
                (replacingCounter && mapping.previous == oldCounter.instance && mapping.current == counter.instance));
        const auto kernel = checked(scene_.state->kernel->snapshot());
        PLATFORM_CHECK(kernel.runtimes == 4 && kernel.instances.size() == 4 &&
                       checked(scene_.state->kernel->replacements()).empty());
        PLATFORM_CHECK(std::any_of(kernel.names.begin(), kernel.names.end(), [&](const auto& row) {
            return row.name == "example." + preview.alias && row.id == runtime;
        }));
        const auto declaration =
            checked(scene_.state->driver->configurationDefinitions()->module("example." + preview.alias));
        PLATFORM_CHECK(declaration.alias == preview.alias &&
                       declaration.manifest == current.manifest.sha256 + "/module.json");
    }
    void compare(unsigned round, const CatalogSample& catalog, const DesktopSample& desktop)
    {
        const auto phase = round % 4;
        const auto shape = desktop.ownedObjectTrees.value("stable").toObject();
        PLATFORM_CHECK(!catalog.stable.isEmpty() && !shape.isEmpty());
        if (round < 8) {
            if (!baselines_[phase])
                baselines_[phase].emplace();
            auto& baseline = *baselines_[phase];
            baseline.catalog = catalog.stable;
            baseline.objects = shape;
            baseline.handles = std::max(baseline.handles, desktop.handles);
            baseline.gdi = std::max(baseline.gdi, desktop.gdi);
            baseline.user = std::max(baseline.user, desktop.user);
            return;
        }
        const auto& baseline = *baselines_[phase];
        current_.insert("actual_catalog", catalog.stable);
        current_.insert("actual_catalog_detail", catalog.detail);
        current_.insert("actual_desktop", desktopValue(desktop));
        current_.insert("actual_owned_object_shape", shape);
        current_.insert("expected_baseline", baseline.json(phase));
        PLATFORM_CHECK(catalog.stable == baseline.catalog && shape == baseline.objects);
        PLATFORM_CHECK(desktop.handles <= baseline.handles && desktop.gdi <= baseline.gdi &&
                       desktop.user <= baseline.user);
    }
    void oneRound(unsigned round)
    {
        const auto phase = round % 4;
        const std::string alias = phase % 2 == 0 ? "counter" : "reporter";
        const std::string variant = phase < 2 ? "b" : "a";
        current_ = {{"step", "round"},     {"ordinal", number(round + 1)}, {"phase", number(phase)},
                    {"warmup", round < 8}, {"alias", text(alias)},         {"variant", text(variant)}};
        append({{"event", "round_start"}, {"context", current_}});
        const auto draft = QString::fromUtf8("连续替换 %1：Notebook 原实例仍可编辑与输出 😀").arg(round + 1);
        scene_.edit(notebook_.instance, draft); // 原按钮执行原 notebook.publish 命令。
        settle();
        notebookUnchanged(draft);
        const auto oldCounter = running(scene_, "example.counter"), oldReporter = running(scene_, "example.reporter");
        const auto oldBindings = businessBindings(oldCounter, oldReporter);
        const auto oldCounterRuntime = runtimeOf(oldCounter.instance),
                   oldReporterRuntime = runtimeOf(oldReporter.instance);
        const auto before = checked(scene_.state->session->snapshot());
        PLATFORM_CHECK(before.coordinator.desired && !before.store.dirty && !before.store.writing);
        const auto oldModules = disk().structure().modules();
        const auto beforeEvaluations = checked(scene_.state->kernel->services().dependencyEvaluationCount());
        const auto candidate = checked(
            scene_.state->previewUpgrade(alias, source_, "native-upgrade/" + alias + '-' + variant + "/module.json"));
        PLATFORM_CHECK(candidate.desiredRevision == before.coordinator.desiredRevision &&
                       oldModules.at(alias) == candidate.previousManifest.sha256 + "/module.json");
        const Image image(platform_upgrade_test::module(scene_, candidate.previous));
        auto probe = std::make_shared<PhaseProbe>();
        probe->workspace = scene_.state;
        probe->scheduler = scene_.scheduler;
        const auto requested = Clock::now();
        probe->requested = requested;
        const auto report = [&] {
            const auto task = checked(scene_.state->upgrade(candidate));
            const auto started = checked(scene_.state->upgradeSnapshot());
            PLATFORM_CHECK(started.active && started.active->replacement);
            probe->operation = started.active->replacement->operation;
            current_.insert("operation", number(probe->operation.value()));
            probe->post();
            return checked(*wait(task, scene_.scheduler));
        }();
        const auto requestDuration = elapsed(requested);
        probe->stopped = true;
        current_.insert("report", reportValue(report));
        checkReport(candidate, report, oldCounter, oldReporter, oldCounterRuntime, oldReporterRuntime, oldBindings);
        // 只有原 Applied 和静止旧代回执均已确认，完整请求窗口才可包住这两段耗时。
        const QJsonObject durationBounds{
            {"basis", "whole successful Applied request-to-observed-receipt window"},
            {"precision", "conservative duration bounds; no internal phase timing or SLA"},
            {"old_cleanup", QJsonObject{{"lower_ns", "0"}, {"upper_ns", number(requestDuration)}}},
            {"business_unavailability", QJsonObject{{"lower_ns", "0"}, {"upper_ns", number(requestDuration)}}}};
        current_.insert("successful_duration_bounds", durationBounds);
        image.absent(); // 必须在下一次映射同一路径前确认；不保留 HMODULE 或库租约。
        expected_ += alias == "counter" ? 1 : 2;
        reporterValue(scene_, std::to_string(expected_));
        notebookUnchanged(draft);
        const auto applied = checked(scene_.state->session->snapshot());
        const auto newLocator = candidate.artifact.manifest().sha256 + "/module.json";
        PLATFORM_CHECK(applied.coordinator.desired &&
                       applied.coordinator.desiredRevision == candidate.desiredRevision + 1 &&
                       !applied.coordinator.activeRevision && applied.store.dirty && !applied.store.writing &&
                       applied.store.persistedRevision == before.store.persistedRevision &&
                       applied.coordinator.desired->source().structure().modules().at(alias) == newLocator &&
                       disk().structure().modules() == oldModules);
        const auto saveDuration = save();
        auto expectedModules = oldModules;
        expectedModules.at(alias) = newLocator;
        PLATFORM_CHECK(disk().structure().modules() == expectedModules);
        if (probe->failure)
            throw Failure(*probe->failure);
        const auto history = checked(scene_.state->upgradeSnapshot());
        PLATFORM_CHECK(!history.active && history.accepting && !history.deliveryBlocked &&
                       history.history.size() == round + 1 && history.history.back().replacement &&
                       history.history.back().replacement->operation == report.replacement->operation);
        const auto observations = probe->observations;
        const auto probeSamples = probe->samples, probeDelay = probe->maximumQueueDelay;
        PLATFORM_CHECK(probeSamples > 0);
        probe.reset();
        const auto catalog = sampleCatalog(scene_);
        const auto desktop = sampleDesktop();
        PLATFORM_CHECK(catalog.dependencyEvaluations >= beforeEvaluations);
        compare(round, catalog, desktop);
        QJsonObject row{{"event", "round_complete"},
                        {"context", current_},
                        {"report", reportValue(report)},
                        {"expected_counter_result", number(expected_)},
                        {"desired_revision", number(applied.coordinator.desiredRevision)},
                        {"reporter_value_verified", true},
                        {"notebook_draft", draft},
                        {"disk_before", text(oldModules.at(alias))},
                        {"disk_after", text(newLocator)},
                        {"explicit_save_confirmed", true},
                        {"old_image_absent_file_exclusive", true},
                        {"dependency_evaluations", number(catalog.dependencyEvaluations - beforeEvaluations)},
                        {"request_to_observed_receipt_ns", number(requestDuration)},
                        {"successful_duration_bounds", durationBounds},
                        {"save_to_observed_receipt_ns", number(saveDuration)},
                        {"fifo_probe_samples", number(probeSamples)},
                        {"fifo_queue_delay_max_ns", number(probeDelay)},
                        {"observed_phase_samples", observations},
                        {"catalog_stable", catalog.stable},
                        {"catalog_detail", catalog.detail},
                        {"desktop", desktopValue(desktop)}};
        ++completed_;
        rounds_.append(QJsonObject{{"ordinal", number(round + 1)},
                                   {"operation", number(report.replacement->operation.value())},
                                   {"phase", number(phase)},
                                   {"warmup", round < 8},
                                   {"counter_result", number(expected_)}});
        append(row);
        if (round == 7)
            append({{"event", "baselines_frozen"}, {"baselines", baselinesJson()}});
    }
};
} // namespace

void runEndurance(Scene& scene, const QString& sourceBin, const QString& outputPrefix)
{
    Endurance(scene, sourceBin, outputPrefix).run();
}
} // namespace workspace_endurance_test
