/** @file platform_upgrade_qt.cpp
 * @brief 真实 Notebook 替换、恢复及模型发布边界，不开放私有插件句柄或测试导出。
 */
#include "platform_upgrade_p.hpp"
#include <ElaWindow.h>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVariant>
#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

namespace platform_upgrade_test {
namespace {
struct Observation {
    QMetaObject::Connection connection;
    ~Observation() { QObject::disconnect(connection); }
};
struct NativeIdentity {
    GenerationId counter, reporter;
    RunningEntry counterInstance, reporterInstance;
};
NativeIdentity nativeIdentity(const Scene& scene)
{
    return {take(scene.state->counter())->generation, take(scene.state->reporter())->generation,
            running(scene, "example.counter"), running(scene, "example.reporter")};
}
void unchangedNative(const Scene& scene, const NativeIdentity& previous)
{
    const auto current = nativeIdentity(scene);
    PLATFORM_CHECK(current.counter == previous.counter && current.reporter == previous.reporter &&
                   current.counterInstance.instance == previous.counterInstance.instance &&
                   current.counterInstance.epoch == previous.counterInstance.epoch &&
                   current.reporterInstance.instance == previous.reporterInstance.instance &&
                   current.reporterInstance.epoch == previous.reporterInstance.epoch);
    PLATFORM_CHECK(module(scene, current.counter).role == GenerationRole::Current &&
                   module(scene, current.reporter).role == GenerationRole::Current);
}
QListView* commandList()
{
    for (auto* window : QApplication::topLevelWidgets())
        if (auto* list = window->findChild<QListView*>("EveCommandList"))
            return list;
    throw std::runtime_error("Workspace command list missing");
}
ElaWindow* shellWindow()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (auto* window = qobject_cast<ElaWindow*>(widget))
            return window;
    throw std::runtime_error("Workspace Ela window missing");
}
QPlainTextEdit* editor(const Scene& scene, InstanceId instance)
{
    auto* widget = scene.page(instance);
    PLATFORM_CHECK(widget);
    auto* value = widget->findChild<QPlainTextEdit*>("EveNotebookEditor");
    PLATFORM_CHECK(value);
    return value;
}
void currentCommand(const Scene& scene, const RunningEntry& current, const RunningEntry& previous)
{
    const auto commands = take(scene.state->shell.commands()->snapshot());
    PLATFORM_CHECK(std::count_if(commands.begin(), commands.end(), [&](const auto& command) {
                       return command.id == "notebook.publish" && command.owner.instance == current.instance &&
                              command.owner.epoch == current.epoch && !command.retiring;
                   }) == 1);
    PLATFORM_CHECK(std::none_of(commands.begin(), commands.end(), [&](const auto& command) {
        return command.owner.instance == previous.instance && command.owner.epoch == previous.epoch;
    }));
    PLATFORM_CHECK(scene.pages().size() == 1 && scene.pages().front().owner.instance == current.instance);
}
void notebookUpgrade(const QString& sourceBin, bool restore)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto original = running(scene, "example.notebook");
    const auto natives = nativeIdentity(scene);
    const auto count = counterValue(scene);
    const auto generation = take(scene.state->generation());
    const auto locator = take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest;
    const auto target = take(scene.state->session->snapshot()).coordinator.desired;
    PLATFORM_CHECK(target);
    QPointer<QWidget> oldPage = scene.page(original.instance);
    QPointer<QPlainTextEdit> oldEditor = editor(scene, original.instance);
    QPointer<QPushButton> oldPublish = oldPage->findChild<QPushButton*>("EveNotebookPublish");
    const auto key = oldPage->property("ElaPageKey").toString();
    PLATFORM_CHECK(oldPublish && !key.isEmpty());
    const auto draft = QString::fromUtf8("真实升级草稿 中文 😀\nlast synchronous edit \"quoted\" \\ tail");
    oldEditor->setPlainText(draft);
    // 不 pump/barrier；实际同步 textChanged 必须已经进入插件自己的草稿状态。
    const auto value = preview(scene, sourceBin, "notebook", restore ? "fail-start" : "b");
    PLATFORM_CHECK(value.impact.members.size() == 1 && value.impact.members.front().instance == original.instance);
    const auto report = perform(scene, value);
    if (restore) {
        PLATFORM_CHECK(
            report.previous == generation && report.candidate.valid() && report.candidate != generation &&
            report.retired == report.candidate && report.replacement && report.replacement->operation.valid() &&
            report.replacement->outcome == ReplacementOutcome::Restored && !report.replacement->committed &&
            report.replacement->recovered && !report.replacement->resourcesRetained &&
            !report.replacement->errors.empty() && !report.failure && !report.retained && !report.deliveryBlocked);
        PLATFORM_CHECK(take(scene.state->generation()) == generation &&
                       module(scene, generation).role == GenerationRole::Current &&
                       module(scene, report.candidate).role == GenerationRole::Candidate &&
                       module(scene, report.candidate).manifest.moduleVersion == "1.3.0");
        const auto restored = take(scene.state->session->snapshot());
        PLATFORM_CHECK(restored.coordinator.desired &&
                       restored.coordinator.desired->source().normalizedYaml() == target->source().normalizedYaml() &&
                       restored.coordinator.desired->source().structure().modules().at("notebook") == locator &&
                       take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest ==
                           locator);
        PLATFORM_CHECK(take(scene.state->kernel->replacements()).empty());
    } else
        applied(scene, value, report);
    const auto current = running(scene, "example.notebook");
    PLATFORM_CHECK(current.instance != original.instance && !oldPage && !oldEditor && !oldPublish);
    PLATFORM_CHECK(editor(scene, current.instance)->toPlainText() == draft);
    currentCommand(scene, current, original);
    auto* window = shellWindow();
    PLATFORM_CHECK(!window->isNodePresentable(key));
    window->navigation(key);
    barrier(scene.scheduler);
    PLATFORM_CHECK(scene.pages().size() == 1 && !scene.page(original.instance));
    scene.edit(current.instance, draft);
    unchangedNative(scene, natives);
    PLATFORM_CHECK(counterValue(scene) == count);
    finish(scene);
}
/** @brief 只从真实贡献所有者、Core 实例与模块来源定位本次候选。 */
std::optional<std::pair<InstanceSnapshot, ModuleRuntimeOrigin>> candidate(const Scene& scene, GenerationId previous,
                                                                          bool held)
{
    const auto commands = take(scene.state->shell.commands()->snapshot());
    const auto core = take(scene.state->kernel->snapshot());
    const auto graph = take(scene.state->kernel->services().dependencyGraph());
    for (const auto& command : commands) {
        if (command.id != "notebook.publish" || command.retiring)
            continue;
        const auto instance = std::find_if(core.instances.begin(), core.instances.end(), [&](const auto& row) {
            return row.instance == command.owner.instance && row.epoch == command.owner.epoch;
        });
        if (instance == core.instances.end())
            continue;
        const auto origin = take(scene.state->manager->runtimeOrigin(instance->runtime));
        if (!origin || origin->generation == previous || origin->moduleId != "org.eve.example.notebook" ||
            origin->pluginId != "example.notebook")
            continue;
        const auto node = std::find_if(graph.instances.begin(), graph.instances.end(),
                                       [&](const auto& row) { return row.instance == instance->instance; });
        if (node != graph.instances.end() && node->publicationHeld == held)
            return std::make_pair(*instance, *origin);
    }
    return {};
}
void heldProtection(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto previous = take(scene.state->generation());
    auto* list = commandList();
    auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
    PLATFORM_CHECK(proxy && proxy->sourceModel());
    bool observed = false;
    std::exception_ptr failure;
    GenerationId observedGeneration;
    InstanceId observedInstance;
    Observation observation;
    observation.connection = QObject::connect(proxy->sourceModel(), &QAbstractItemModel::rowsInserted, list, [&] {
        if (observed || failure)
            return;
        try {
            const auto actual = candidate(scene, previous, true);
            if (!actual)
                return;
            PLATFORM_CHECK(module(scene, actual->second.generation).role == GenerationRole::Candidate);
            const auto diagnostics = take(scene.state->shell.diagnostics()->snapshot());
            const auto row =
                std::find_if(diagnostics.instances.begin(), diagnostics.instances.end(),
                             [&](const auto& value) { return value.instance.instance == actual->first.instance; });
            PLATFORM_CHECK(row == diagnostics.instances.end() || (!row->canStop && !row->canRetry && !row->canLoad));
            PLATFORM_CHECK(!scene.state->shell.management()->stop(actual->first.instance).hasValue() &&
                           !scene.state->shell.management()->retry(actual->first.instance).hasValue() &&
                           !scene.state->shell.management()->load(actual->first.instance).hasValue());
            observedGeneration = actual->second.generation;
            observedInstance = actual->first.instance;
            observed = true;
        } catch (...) {
            failure = std::current_exception();
        }
    });
    const auto value = preview(scene, sourceBin, "notebook");
    const auto report = perform(scene, value);
    QObject::disconnect(observation.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed && report.candidate == observedGeneration &&
                   running(scene, "example.notebook").instance == observedInstance);
    applied(scene, value, report);
    finish(scene);
}
void committedDiscard(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto previous = take(scene.state->generation());
    const auto natives = nativeIdentity(scene);
    auto* list = commandList();
    auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
    PLATFORM_CHECK(proxy);
    bool observed = false;
    std::exception_ptr failure;
    GenerationId committedGeneration;
    RuntimeId committedRuntime;
    OperationId operation;
    Observation observation;
    observation.connection = QObject::connect(proxy, &QAbstractItemModel::rowsInserted, list, [&] {
        if (observed || failure)
            return;
        try {
            const auto actual = candidate(scene, previous, false);
            if (!actual)
                return;
            PLATFORM_CHECK(module(scene, actual->second.generation).role == GenerationRole::Current);
            const auto snapshot = take(scene.state->upgradeSnapshot());
            PLATFORM_CHECK(snapshot.active && snapshot.active->replacement && snapshot.active->replacement->committed &&
                           snapshot.active->replacement->candidate == actual->second.generation);
            PLATFORM_CHECK(take(scene.state->generation()) == actual->second.generation &&
                           take(scene.state->notebookRuntime()).id() == actual->first.runtime);
            const auto locator =
                take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest;
            const auto session = take(scene.state->session->snapshot());
            PLATFORM_CHECK(session.coordinator.desired &&
                           session.coordinator.desired->source().structure().modules().at("notebook") == locator &&
                           locator == module(scene, actual->second.generation).manifest.sha256 + "/module.json");
            unchangedNative(scene, natives);
            PLATFORM_CHECK(take(scene.state->scheduler->snapshot()).queued > 0);
            committedGeneration = actual->second.generation;
            committedRuntime = actual->first.runtime;
            operation = snapshot.active->replacement->operation;
            observed = true;
            take(scene.scheduler->close());
        } catch (...) {
            failure = std::current_exception();
        }
    });
    const auto value = preview(scene, sourceBin, "notebook");
    const auto task = take(scene.state->upgrade(value));
    until([&] { return observed || failure || task.isReady(); });
    QObject::disconnect(observation.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed && operation.valid());
    const auto result = task.tryResult();
    PLATFORM_CHECK(result && !result->hasValue() && result->error().code == ErrorCode::SchedulerClosed);
    const auto snapshot = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(!snapshot.accepting && snapshot.deliveryBlocked && snapshot.active && snapshot.active->retained &&
                   snapshot.active->deliveryBlocked && snapshot.active->replacement &&
                   snapshot.active->replacement->committed && snapshot.active->replacement->operation == operation);
    PLATFORM_CHECK(take(scene.state->generation()) == committedGeneration &&
                   take(scene.state->notebookRuntime()).id() == committedRuntime &&
                   module(scene, committedGeneration).role == GenerationRole::Current &&
                   module(scene, previous).role == GenerationRole::Retired);
    // 发布投影已可见时原 claims 可以已经解除；故障不补造 claims，也不清 pin。
    PLATFORM_CHECK(take(scene.state->kernel->replacements()).empty());
    PLATFORM_CHECK(!module(scene, committedGeneration).replacement && !module(scene, previous).replacement);
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    scene.state.reset();
    PLATFORM_CHECK(!workspace.expired() && !kernel.expired() && !driver.expired() && !session.expired());
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(!queue.open && queue.discarded > 0 && queue.callbackErrors == 0);
    // 独立故障进程仅保留原执行与真实资源，不走 finish 或伪造成功退出。
}
} // namespace
bool qtCase(const QString& name, const QString& sourceBin)
{
    if (name == "notebook")
        notebookUpgrade(sourceBin, false);
    else if (name == "notebook_restore")
        notebookUpgrade(sourceBin, true);
    else if (name == "held_protection")
        heldProtection(sourceBin);
    else if (name == "committed_discard")
        committedDiscard(sourceBin);
    else
        return false;
    return true;
}
} // namespace platform_upgrade_test
