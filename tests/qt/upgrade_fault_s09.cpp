/** @file upgrade_fault_s09.cpp
 * @brief 同平台真实消费者第二次启动失败；FIFO 原 ready 见证候选 Active/Held。
 */
#include "upgrade_fault_p.hpp"
#include <algorithm>
#include <eve/module/install.hpp>
#include <map>

namespace upgrade_fault_test {
namespace {
void openReporterFailure(Scene& scene, const QString& sourceBin, bool permanent)
{
    std::map<std::string, std::string> locators;
    std::vector<InstalledArtifact> installations;
    for (const auto* alias : {"counter", "reporter", "notebook"}) {
        const auto path = std::string(alias) == "reporter"
                              ? "native-faults/" +
                                    std::string(permanent ? "reporter-fail-recovery" : "reporter-fail-once") +
                                    "/module.json"
                              : std::string(std::string(alias) == "notebook" ? "qt-upgrade/" : "native-upgrade/") +
                                    alias + "-a/module.json";
        auto artifact = take(installModuleArtifact(utf8(sourceBin), path, utf8(scene.store)));
        locators.emplace(alias, artifact.artifact().manifest().sha256 + "/module.json");
        installations.push_back(std::move(artifact));
    }
    std::string yaml = "formatVersion: 1\nmodules:\n";
    for (const auto& row : locators)
        yaml += "  " + row.first + ": {manifest: " + row.second + "}\n";
    yaml +=
        "plugins:\n  - {id: counter, plugin: example.counter, module: counter, config: null}\n"
        "  - {id: reporter, plugin: example.reporter, module: reporter, config: null}\n"
        "  - {id: notes, plugin: example.notebook, module: notebook, config: {title: Independent, text: S09 draft}}\n";
    writeDisk(scene, QByteArray::fromStdString(yaml));
    scene.open(sourceBin); // 安装移交期间保留原真实 artifact guards；不把故障源加入批准名单。
}
class CounterWitness final : public std::enable_shared_from_this<CounterWitness> {
public:
    CounterWitness(Scene& scene, GenerationId previous)
        : owner_(scene.state), scheduler_(scene.scheduler), previous_(previous)
    {
    }
    bool loading = false, activeHeld = false;
    std::exception_ptr failure;
    InstanceId instance;
    GenerationId generation;
    void post()
    {
        const auto self = shared_from_this();
        PLATFORM_CHECK(scheduler_->post({[self] { self->inspect(); },
                                         [self] {
                                             self->failure = std::make_exception_ptr(
                                                 std::runtime_error("S09 witness continuation discarded"));
                                         }}));
    }

private:
    std::weak_ptr<notebook::Workspace> owner_;
    std::shared_ptr<qt::QtScheduler> scheduler_;
    GenerationId previous_;
    std::size_t attempts_ = 0;
    void inspect() noexcept
    {
        try {
            const auto owner = owner_.lock();
            PLATFORM_CHECK(owner && ++attempts_ < 10000);
            for (const auto& row : take(owner->kernel->snapshot()).instances) {
                const auto origin = take(owner->manager->runtimeOrigin(row.runtime));
                if (!origin || origin->pluginId != "example.counter" || origin->generation == previous_)
                    continue;
                if (row.state == InstanceState::Pending) {
                    post();
                    return;
                }
                PLATFORM_CHECK(row.state == InstanceState::Loading);
                loading = true;
                instance = row.instance;
                generation = origin->generation;
                const auto actual = take(owner->kernel->findInstance(instance));
                const auto self = shared_from_this();
                actual.ready().thenOn(scheduler_, [self](const Result<void>& result) { self->ready(result); });
                return;
            }
            const auto state = take(owner->upgradeSnapshot());
            PLATFORM_CHECK(state.history.empty() && (!state.active || !state.active->retained));
            post();
        } catch (...) {
            failure = std::current_exception();
        }
    }
    void ready(const Result<void>& result) noexcept
    {
        try {
            PLATFORM_CHECK(result.hasValue());
            const auto owner = owner_.lock();
            PLATFORM_CHECK(owner);
            const auto actual = take(owner->kernel->findInstance(instance)).snapshot();
            PLATFORM_CHECK(actual.hasValue() && actual.value().state == InstanceState::Active);
            const auto graph = take(owner->kernel->services().dependencyGraph());
            const auto node = std::find_if(graph.instances.begin(), graph.instances.end(),
                                           [&](const auto& row) { return row.instance == instance; });
            const auto binding = std::find_if(graph.bindings.begin(), graph.bindings.end(), [&](const auto& row) {
                return row.provider == instance && row.binding.key.name == "example.counter";
            });
            PLATFORM_CHECK(node != graph.instances.end() && node->publicationHeld && binding != graph.bindings.end() &&
                           binding->managedProvider && binding->binding.state == BindingState::Held);
            PLATFORM_CHECK(std::none_of(graph.bindings.begin(), graph.bindings.end(), [](const auto& row) {
                return row.binding.key.name == "example.counter" && row.binding.state == BindingState::Published;
            }));
            activeHeld = true;
        } catch (...) {
            failure = std::current_exception();
        }
    }
};
void s09(const QString& sourceBin, bool permanent)
{
    Scene scene;
    openReporterFailure(scene, sourceBin, permanent);
    scene.apply(scene.state->initial);
    scene.business();
    const auto previous = take(scene.state->counter())->generation;
    const auto reporterGeneration = take(scene.state->reporter())->generation;
    const auto oldCounter = running(scene, "example.counter"), oldReporter = running(scene, "example.reporter");
    const auto notes = running(scene, "example.notebook");
    QPointer<QWidget> notesWidget = scene.page(notes.instance);
    const auto draft = QString::fromUtf8("独立活跃 Notebook 不参与 S09 服务升级 😀");
    editor(scene, notes.instance)->setPlainText(draft);
    const auto original = take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml();
    View view(scene);
    view.preview("counter", candidatePath(sourceBin, "counter"));
    const auto witness = std::make_shared<CounterWitness>(scene, previous);
    witness->post();
    const auto operation = view.begin();
    WorkspaceUpgradeReport report = permanent ? retained(view) : view.settled();
    if (witness->failure)
        std::rethrow_exception(witness->failure);
    PLATFORM_CHECK(witness->loading && witness->activeHeld && witness->generation == report.candidate &&
                   report.replacement && report.replacement->operation == operation && !report.replacement->committed &&
                   take(scene.state->counter())->generation == previous &&
                   take(scene.state->reporter())->generation == reporterGeneration &&
                   take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml() == original &&
                   notesWidget && scene.page(notes.instance) == notesWidget &&
                   running(scene, "example.notebook").instance == notes.instance &&
                   editor(scene, notes.instance)->toPlainText() == draft);
    visibleReport(view, report);
    if (permanent) {
        PLATFORM_CHECK(report.replacement->outcome == ReplacementOutcome::Failed &&
                       containsError(*report.replacement, ErrorCode::RecoveryFailed) &&
                       report.replacement->resourcesRetained && !report.replacement->recovered &&
                       view.reportText->toPlainText().contains("RecoveryFailed"));
        retainOwners(scene);
    } else {
        PLATFORM_CHECK(report.replacement->outcome == ReplacementOutcome::Restored && report.replacement->recovered &&
                       !report.replacement->resourcesRetained && !report.retained &&
                       !containsError(*report.replacement, ErrorCode::RecoveryFailed) &&
                       running(scene, "example.counter").instance != oldCounter.instance &&
                       running(scene, "example.reporter").instance != oldReporter.instance &&
                       view.reportText->toPlainText().contains("Restored"));
        PLATFORM_CHECK(counterValue(scene) == "2");
        reporterValue(scene, "2");
        scene.edit(notes.instance, draft);
        finish(scene);
    }
}
} // namespace
bool s09Case(const QString& name, const QString& sourceBin)
{
    if (name == "fault_s09_restored")
        s09(sourceBin, false);
    else if (name == "fault_s09_recovery_failed")
        s09(sourceBin, true);
    else
        return false;
    return true;
}
} // namespace upgrade_fault_test
