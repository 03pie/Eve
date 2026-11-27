/** @file platform_workspace_close.cpp
 * @brief 真实保存冲突、显式队列失败与分阶段关闭；不伪造清理或物理卸载证据。
 */
#include "platform_workspace_p.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QWidget>
#include <algorithm>
#include <thread>

namespace platform_workspace_test {
namespace {
std::vector<QPointer<QWidget>> newShellWindows(const QWidgetList& previous)
{
    std::vector<QPointer<QWidget>> result;
    for (auto* window : QApplication::topLevelWidgets())
        if (window->inherits("ElaWindow") && std::find(previous.begin(), previous.end(), window) == previous.end())
            result.emplace_back(window);
    return result;
}
void failedShellsGone(const std::vector<QPointer<QWidget>>& windows, const std::shared_ptr<qt::QtScheduler>& scheduler)
{
    until([&] {
        return std::all_of(windows.begin(), windows.end(), [](const auto& window) { return window.isNull(); }) &&
               take(scheduler->snapshot()).queued == 0;
    });
}
std::string failureConfiguration(const Scene& scene)
{
    const auto source = take(parseConfiguration(scene.state->initial));
    std::optional<ParsedConfiguration> counter;
    for (const auto& entry : source.structure().entries())
        if (entry.second.plugin == "example.counter") {
            counter.emplace(take(setEntryConfiguration(source, entry.first, "{failCleanup: true}")));
            break;
        }
    PLATFORM_CHECK(counter);
    for (const auto& entry : counter->structure().entries())
        if (entry.second.plugin == "example.notebook")
            return take(setEntryConfiguration(*counter, entry.first,
                                              "{title: Retained, text: failure draft, failCleanup: true}"))
                .normalizedYaml();
    throw std::runtime_error("qualification Notebook configuration missing");
}
void saveConflict(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    const auto accepted = scene.apply(scene.state->initial);
    scene.business();
    const auto notebook = scene.pages().front().owner.instance;
    QPointer<QWidget> oldPage = scene.page(notebook);
    QFile external(scene.file);
    PLATFORM_CHECK(external.open(QIODevice::WriteOnly));
    PLATFORM_CHECK(external.write("external file\n") == 14);
    external.close();
    const auto failed = wait(scene.state->close(), scene.scheduler);
    PLATFORM_CHECK(!failed->hasValue() && failed->error().code == ErrorCode::ConfigConflict);
    PLATFORM_CHECK(oldPage && scene.state->session && !take(scene.state->session->snapshot()).closing);
    const auto before = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(!before.closed && !before.retained && before.save && !before.session && !before.kernel &&
                   !before.counter && !before.reporter && !before.shell);
    scene.business();
    scene.edit(notebook, "still running after save conflict");
    take(scene.state->session->rebaseExternal(accepted.revision));
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    PLATFORM_CHECK(!oldPage);
    const auto saved = take(readConfigurationFile(scene.file));
    PLATFORM_CHECK(saved.version.exists && parseConfiguration(saved.text).hasValue() &&
                   saved.text != "external file\n");
    scene.verifyClosed();
    scene.release();
}
void openFailure(const QString& sourceBin)
{
    Scene scene;
    const auto noQueue = notebook::Workspace::openQualification(sourceBin, scene.store, scene.file, {});
    PLATFORM_CHECK(!noQueue.hasValue() && noQueue.error().code == ErrorCode::InvalidArgument);
    bool wrongThread = false;
    std::thread worker([&] {
        const auto result = notebook::Workspace::openQualification(sourceBin, scene.store, scene.file, scene.scheduler);
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(wrongThread);
    {
        const auto closedQueue = std::make_shared<qt::QtScheduler>();
        take(closedQueue->close());
        const auto result = notebook::Workspace::openQualification(sourceBin, scene.store, scene.file, closedQueue);
        PLATFORM_CHECK(!result.hasValue() && result.error().code == ErrorCode::SchedulerClosed);
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto before = QApplication::topLevelWidgets();
        const auto result = notebook::Workspace::openQualification(scene.scratch.filePath("missing"), scene.store,
                                                                   scene.file, scene.scheduler);
        PLATFORM_CHECK(!result.hasValue());
        const auto windows = newShellWindows(before);
        barrier(scene.scheduler);
        failedShellsGone(windows, scene.scheduler);
    }
    ModuleManager observer;
    PLATFORM_CHECK(take(observer.snapshot()).empty() && scene.scheduler->isOpen());
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    scene.business();
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    scene.verifyClosed();
    scene.release();
}
void retainedRuntime(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto id = take(scene.state->counter())->generation;
    const auto manager = scene.state->manager;
    auto retained = take(scene.state->counter())->runtime;
    PLATFORM_CHECK(retained);
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    scene.verifyClosed(false);
    const auto history = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(history.counterDrain && !history.counterDrain->hasValue() &&
                   history.counterDrain->error().code == ErrorCode::ContextClosing);
    barrier(scene.scheduler);
    PLATFORM_CHECK(kernel.expired() && driver.expired() && session.expired());
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    scene.state.reset();
    barrier(scene.scheduler);
    PLATFORM_CHECK(workspace.expired());
    retained.reset();
    barrier(scene.scheduler);
    take(manager->drain(id));
    const auto modules = take(manager->snapshot());
    const auto counter =
        std::find_if(modules.begin(), modules.end(), [&](const auto& row) { return row.generation == id; });
    PLATFORM_CHECK(counter != modules.end() && counter->resources.drained() && counter->nativeMapped);
    PLATFORM_CHECK(!history.counterDrain->hasValue());
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(queue.discarded == 0 && queue.callbackErrors == 0);
    take(scene.scheduler->close());
}
void partialOpenFailure(const QString& sourceBin)
{
    Scene scene;
    installSession(scene, sourceBin, "a", true);
    const auto before = take(readConfigurationFile(scene.file));
    const auto beforeWindows = QApplication::topLevelWidgets();
    const auto result = notebook::Workspace::openQualification(sourceBin, scene.store, scene.file, scene.scheduler);
    PLATFORM_CHECK(!result.hasValue() && result.error().code == ErrorCode::ConfigInvalid);
    const auto windows = newShellWindows(beforeWindows);
    ModuleManager observer;
    barrier(scene.scheduler);
    until([&] {
        const auto rows = take(observer.snapshot());
        return rows.size() == 3 && std::all_of(rows.begin(), rows.end(), [](const auto& row) {
                   return row.manifest.loader == ModuleLoader::QtUi
                              ? row.resources.records.size() == 1 &&
                                    row.resources.records.front().kind == ResourceKind::QObject &&
                                    row.resources.records.front().label == "qt.loader.root"
                              : row.state == ModuleState::Resident && row.resources.drained();
               });
    });
    for (const auto& row : take(observer.snapshot()))
        PLATFORM_CHECK(row.nativeMapped && row.descriptorAccepted && row.businessAccepted && !row.failure &&
                       !row.businessFailure);
    PLATFORM_CHECK(take(readConfigurationFile(scene.file)).text == before.text && scene.scheduler->isOpen());
    barrier(scene.scheduler);
    failedShellsGone(windows, scene.scheduler);
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(queue.discarded == 0 && queue.callbackErrors == 0);
    take(scene.scheduler->close());
    // 只覆盖 schema 校验这个已到达的半装配点；不复活已 drain 的 Native 当前代。
}
void discardedClose(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    scene.business();
    const auto manager = scene.state->manager;
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    const auto closing = scene.state->close();
    scene.state.reset();
    take(scene.scheduler->close());
    const auto result = closing.tryResult();
    PLATFORM_CHECK(result && !result->hasValue() && result->error().code == ErrorCode::SchedulerClosed);
    PLATFORM_CHECK(take(scene.scheduler->snapshot()).discarded > 0 && !workspace.expired() && !kernel.expired() &&
                   !driver.expired() && !session.expired());
    const auto retained = workspace.lock();
    const auto report = take(retained->closeSnapshot());
    PLATFORM_CHECK(retained->deliveryBlocked() && report.deliveryBlocked && !report.closed && report.retained);
    const auto modules = take(manager->snapshot());
    PLATFORM_CHECK(modules.size() == 3 && std::all_of(modules.begin(), modules.end(), [](const auto& row) {
                       return row.nativeMapped && !row.resources.records.empty();
                   }));
    PLATFORM_CHECK(take(scene.scheduler->snapshot()).callbackErrors == 0);
    // 独立故障进程保留未确认的真实拥有者；队列关闭不等于业务静止。
}
void cleanupFailure(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(failureConfiguration(scene));
    scene.business();
    const auto failed = wait(scene.state->close(), scene.scheduler);
    PLATFORM_CHECK(!failed->hasValue() && failed->error().code == ErrorCode::CleanupFailed);
    const auto report = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(report.retained && !report.closed && !report.deliveryBlocked && report.session &&
                   !report.session->hasValue());
    PLATFORM_CHECK(report.kernel && report.kernel->hasValue() && !report.kernel->value().quiescent);
    PLATFORM_CHECK(report.counter && report.counter->hasValue() && !report.counter->value().quiescent);
    PLATFORM_CHECK(report.reporter && report.reporter->hasValue() && report.reporter->value().quiescent);
    PLATFORM_CHECK(!report.shell && !report.providers && !report.counterDrain && !report.reporterDrain);
    PLATFORM_CHECK(scene.state->kernel && scene.state->driver && scene.state->session &&
                   take(scene.state->counter())->runtime && take(scene.state->reporter())->runtime &&
                   scene.state->notebookRuntime().hasValue());
    PLATFORM_CHECK(scene.pages().empty() && take(scene.state->shell.commands()->snapshot()).empty());
    const auto modules = take(scene.state->manager->snapshot());
    for (const auto& row : modules)
        if (row.manifest.moduleId != "org.eve.example.reporter")
            PLATFORM_CHECK(std::any_of(row.resources.records.begin(), row.resources.records.end(),
                                       [](const auto& record) { return record.kind == ResourceKind::Instance; }));
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    scene.state.reset();
    barrier(scene.scheduler);
    PLATFORM_CHECK(!workspace.expired() && take(scene.scheduler->snapshot()).callbackErrors == 0);
    // failCleanup 没有复位开关；保留资源与 owner，不能伪造补偿或强行销毁。
    take(scene.scheduler->close());
}
} // namespace
bool closeCase(const QString& name, const QString& sourceBin)
{
    if (name == "save_conflict")
        saveConflict(sourceBin);
    else if (name == "open_failure")
        openFailure(sourceBin);
    else if (name == "partial_open_failure")
        partialOpenFailure(sourceBin);
    else if (name == "retained_runtime")
        retainedRuntime(sourceBin);
    else if (name == "discarded_close")
        discardedClose(sourceBin);
    else if (name == "cleanup_failure")
        cleanupFailure(sourceBin);
    else
        return false;
    return true;
}
} // namespace platform_workspace_test
