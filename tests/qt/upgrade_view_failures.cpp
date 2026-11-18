/** @file upgrade_view_failures.cpp
 * @brief Applied 与显式保存、原共同提交后的真实回执丢弃相互独立。
 */
#include "upgrade_view_p.hpp"
#include <QFile>
#include <thread>

namespace upgrade_view_test {
namespace {
void saveConflict(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    view.select("counter");
    view.saveNow();
    const auto baseline = diskText(scene);
    const auto before = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(!before.dirty && before.persistedRevision == before.desiredRevision &&
                   parseConfiguration(baseline).hasValue());
    view.preview("counter", candidatePath(sourceBin, "counter"));
    const auto operation = view.begin();
    const auto report = view.settled();
    committed(scene, report);
    visibleReport(view, report);
    const auto current = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(report.replacement->operation == operation && current.dirty &&
                   current.desiredRevision == before.desiredRevision + 1 &&
                   current.persistedRevision == before.persistedRevision && diskText(scene) == baseline &&
                   view.reportText->toPlainText().contains("Applied"));
    const auto stale = scene.state->saveConfiguration(before.desiredRevision);
    PLATFORM_CHECK(!stale.hasValue() && stale.error().code == ErrorCode::ConfigConflict);
    bool wrongThread = false;
    std::thread worker([&] {
        const auto rejected = scene.state->saveConfiguration(current.desiredRevision);
        wrongThread = !rejected.hasValue() && rejected.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(wrongThread);
    writeDisk(scene, "external file\n");
    view.saveNow();
    const auto failed = take(scene.state->session->snapshot());
    PLATFORM_CHECK(failed.store.dirty && failed.store.lastError &&
                   failed.store.lastError->code == ErrorCode::ConfigConflict &&
                   failed.store.persistedRevision == before.persistedRevision && diskText(scene) == "external file\n" &&
                   view.saveStatus->text().contains("Conflict") && view.reportText->toPlainText().contains("Applied"));
    PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).history.size() == 1 &&
                   platform_upgrade_test::module(scene, report.candidate).role == GenerationRole::Current &&
                   counterValue(scene) == "2");
    reporterValue(scene, "2");
    take(scene.state->session->rebaseExternal(current.desiredRevision));
    view.saveNow();
    const auto saved = take(scene.state->modulesSnapshot());
    const auto disk = take(parseConfiguration(diskText(scene)));
    PLATFORM_CHECK(!saved.dirty && !saved.writing && saved.persistedRevision == saved.desiredRevision &&
                   disk.structure().modules().at("counter") ==
                       platform_upgrade_test::module(scene, report.candidate).manifest.sha256 + "/module.json" &&
                   (view.saveStatus->text().contains("Written") || view.saveStatus->text().contains("Unchanged")));
    const auto closing = scene.state->close();
    PLATFORM_CHECK(!closing.isReady() &&
                   scene.state->saveConfiguration(saved.desiredRevision).error().code == ErrorCode::ContextClosing);
    PLATFORM_CHECK(wait(closing, scene.scheduler)->hasValue());
    PLATFORM_CHECK(scene.state->saveConfiguration(saved.desiredRevision).error().code == ErrorCode::ContextClosing);
    finish(scene);
}
void committedDiscard(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto previous = take(scene.state->generation());
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    auto* list = commandList();
    auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
    PLATFORM_CHECK(proxy);
    bool observed = false;
    std::exception_ptr failure;
    OperationId operation;
    GenerationId generation;
    RuntimeId runtime;
    Observation observer;
    observer.connection = QObject::connect(proxy, &QAbstractItemModel::rowsInserted, list, [&] {
        if (observed || failure)
            return;
        try {
            const auto candidate = notebookCandidate(scene, previous, false);
            if (!candidate)
                return;
            const auto snapshot = take(scene.state->upgradeSnapshot());
            PLATFORM_CHECK(
                operation.valid() && snapshot.active && snapshot.active->replacement &&
                snapshot.active->replacement->committed && snapshot.active->replacement->operation == operation &&
                platform_upgrade_test::module(scene, candidate->origin.generation).role == GenerationRole::Current &&
                take(scene.state->generation()) == candidate->origin.generation &&
                take(scene.state->notebookRuntime()).id() == candidate->instance.runtime);
            const auto locator =
                take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest;
            const auto session = take(scene.state->session->snapshot());
            PLATFORM_CHECK(session.coordinator.desired &&
                           session.coordinator.desired->source().structure().modules().at("notebook") == locator &&
                           locator ==
                               platform_upgrade_test::module(scene, candidate->origin.generation).manifest.sha256 +
                                   "/module.json" &&
                           take(scene.scheduler->snapshot()).queued > 0);
            generation = candidate->origin.generation;
            runtime = candidate->instance.runtime;
            observed = true;
            take(scene.scheduler->close());
        } catch (...) {
            failure = std::current_exception();
        }
    });
    operation = view.begin();
    until([&] { return observed || static_cast<bool>(failure); });
    QObject::disconnect(observer.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed);
    const auto retained = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(!retained.accepting && retained.deliveryBlocked && retained.active && retained.active->retained &&
                   retained.active->deliveryBlocked && retained.active->replacement &&
                   retained.active->replacement->committed && retained.active->replacement->operation == operation &&
                   take(scene.state->generation()) == generation &&
                   take(scene.state->notebookRuntime()).id() == runtime &&
                   platform_upgrade_test::module(scene, generation).role == GenerationRole::Current &&
                   platform_upgrade_test::module(scene, previous).role == GenerationRole::Retired);
    // 关队列后视图屏障拒绝写入；只验证实际点击不能取消已提交原操作，不声称最终界面已刷新。
    view.cancel->click();
    PLATFORM_CHECK(
        scene.state->cancelUpgrade(operation).error().code == ErrorCode::ContextClosing &&
        !scene.state->previewUpgrade("notebook", sourceBin, "qt-upgrade/notebook-b/module.json").hasValue() &&
        take(scene.state->upgradeSnapshot()).active->replacement->committed);
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(!queue.open && queue.discarded > 0 && queue.callbackErrors == 0 &&
                   take(scene.state->kernel->replacements()).empty());
    std::weak_ptr<notebook::Workspace> workspace = scene.state;
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    scene.state.reset();
    PLATFORM_CHECK(!workspace.expired() && !kernel.expired() && !driver.expired() && !session.expired() && view.body &&
                   view.window);
    // 独立故障进程保留原执行及其真实拥有者；不清 pin 或伪造 finish。
}
} // namespace
bool failuresCase(const QString& name, const QString& sourceBin)
{
    if (name == "save_conflict")
        saveConflict(sourceBin);
    else if (name == "committed_discard")
        committedDiscard(sourceBin);
    else
        return false;
    return true;
}
} // namespace upgrade_view_test
