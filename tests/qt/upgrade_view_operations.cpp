/** @file upgrade_view_operations.cpp
 * @brief 控件驱动真实 Native/Notebook 替换和原操作取消，不另造事务。
 */
#include "upgrade_view_p.hpp"
#include <QFile>
#include <algorithm>
#include <thread>

namespace upgrade_view_test {
namespace {
QPlainTextEdit* editor(const Scene& scene, InstanceId instance)
{
    auto* page = scene.page(instance);
    PLATFORM_CHECK(page);
    auto* value = page->findChild<QPlainTextEdit*>("EveNotebookEditor");
    PLATFORM_CHECK(value);
    return value;
}
void counter(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto oldCounter = running(scene, "example.counter");
    const auto oldReporter = running(scene, "example.reporter");
    const auto notes = running(scene, "example.notebook");
    QPointer<QWidget> notesWidget = scene.page(notes.instance);
    const auto draft = QString::fromUtf8("Counter 升级期间保留的草稿");
    scene.edit(notes.instance, draft);
    PLATFORM_CHECK(counterValue(scene) == "1");
    View view(scene);
    view.preview("counter", candidatePath(sourceBin, "counter"));
    const auto operation = view.begin();
    PLATFORM_CHECK(!view.start->isEnabled() && !view.source->isEnabled() && !view.browse->isEnabled() &&
                   !view.review->isEnabled() && !view.table->isEnabled());
    view.start->click();
    PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).active->replacement->operation == operation);
    view.window->close();
    PLATFORM_CHECK(view.body && !view.body->isVisible());
    until([&] {
        const auto state = take(scene.state->upgradeSnapshot());
        return !state.active && state.history.size() == 1;
    });
    View reopened(scene);
    PLATFORM_CHECK(reopened.body == view.body && reopened.window == view.window);
    reopened.select("counter");
    const auto report = take(scene.state->upgradeSnapshot()).history.back();
    PLATFORM_CHECK(report.replacement->operation == operation);
    committed(scene, report);
    visibleReport(reopened, report);
    PLATFORM_CHECK(reopened.reportText->toPlainText().contains("Applied"));
    PLATFORM_CHECK(running(scene, "example.counter").instance != oldCounter.instance &&
                   running(scene, "example.reporter").instance != oldReporter.instance &&
                   running(scene, "example.notebook").instance == notes.instance && notesWidget &&
                   scene.page(notes.instance) == notesWidget && editor(scene, notes.instance)->toPlainText() == draft);
    PLATFORM_CHECK(counterValue(scene) == "2");
    reporterValue(scene, "2");
    const auto modules = take(scene.state->modulesSnapshot());
    PLATFORM_CHECK(modules.dirty && !modules.persistedRevision && !QFile::exists(scene.file));
    finish(scene);
}
void notebook(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto old = running(scene, "example.notebook");
    const auto counter = running(scene, "example.counter");
    const auto reporter = running(scene, "example.reporter");
    const auto counterGeneration = take(scene.state->counter())->generation;
    const auto reporterGeneration = take(scene.state->reporter())->generation;
    PLATFORM_CHECK(counterValue(scene) == "1");
    QPointer<QWidget> oldPage = scene.page(old.instance);
    QPointer<QPlainTextEdit> oldEditor = editor(scene, old.instance);
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    const auto draft = QString::fromUtf8("界面最后一次同步编辑 😀\n\"quoted\" \\ tail");
    oldEditor->setPlainText(draft);
    // 最后一次 textChanged 与实际 Start 之间不 pump、barrier 或轮询。
    const auto operation = view.begin();
    const auto report = view.settled();
    committed(scene, report);
    visibleReport(view, report);
    const auto now = running(scene, "example.notebook");
    PLATFORM_CHECK(report.replacement->operation == operation && now.instance != old.instance && !oldPage &&
                   !oldEditor && editor(scene, now.instance)->toPlainText() == draft &&
                   view.reportText->toPlainText().contains("Applied"));
    const auto commands = take(scene.state->shell.commands()->snapshot());
    PLATFORM_CHECK(std::none_of(commands.begin(), commands.end(),
                                [&](const auto& row) { return row.owner.instance == old.instance; }) &&
                   std::count_if(commands.begin(), commands.end(), [&](const auto& row) {
                       return row.id == "notebook.publish" && row.owner.instance == now.instance &&
                              row.owner.epoch == now.epoch;
                   }) == 1);
    PLATFORM_CHECK(running(scene, "example.counter").instance == counter.instance &&
                   running(scene, "example.reporter").instance == reporter.instance &&
                   take(scene.state->counter())->generation == counterGeneration &&
                   take(scene.state->reporter())->generation == reporterGeneration && counterValue(scene) == "1");
    finish(scene);
}
void cancelPrepared(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto original = running(scene, "example.notebook");
    QPointer<QWidget> widget = scene.page(original.instance);
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    const auto first = view.begin();
    PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).phase == ReplacementPhase::Prepared &&
                   view.cancel->isEnabled());
    PLATFORM_CHECK(scene.state->cancelUpgrade({}).error().code == ErrorCode::InvalidArgument);
    bool wrongThread = false;
    std::thread worker([&] {
        const auto rejected = scene.state->cancelUpgrade(first);
        wrongThread = !rejected.hasValue() && rejected.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(wrongThread && !take(scene.state->upgradeSnapshot()).cancellationRequested);
    // 原宿主结算 first；父 timer 已停，界面仍显示 first 的 Prepared/Cancel。
    take(scene.state->cancelUpgrade(first));
    PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).cancellationRequested);
    until([&] {
        const auto snapshot = take(scene.state->upgradeSnapshot());
        return !snapshot.active && snapshot.history.size() == 1;
    });
    const auto cancelled = take(scene.state->upgradeSnapshot()).history.back();
    PLATFORM_CHECK(cancelled.replacement && cancelled.replacement->operation == first &&
                   cancelled.replacement->outcome == ReplacementOutcome::Cancelled &&
                   !cancelled.replacement->committed && !cancelled.replacement->recovered &&
                   cancelled.replacement->instances.empty() && !cancelled.retained && widget &&
                   running(scene, "example.notebook").instance == original.instance);
    PLATFORM_CHECK(view.cancel->isEnabled() &&
                   view.reportText->toPlainText().contains("OperationId #" + QString::number(first.value())) &&
                   view.status->text().contains("Prepared"));
    PLATFORM_CHECK(scene.state->cancelUpgrade(first).error().code == ErrorCode::ContextClosing);
    // 另一真实调用者开始 second；旧显示按钮不能在点击时改用 fresh id 取消它。
    const auto oldCounter = running(scene, "example.counter");
    const auto value = platform_upgrade_test::preview(scene, sourceBin, "counter");
    const auto secondTask = take(scene.state->upgrade(value));
    const auto second = take(scene.state->upgradeSnapshot()).active->replacement->operation;
    PLATFORM_CHECK(second != first && scene.state->cancelUpgrade(first).error().code == ErrorCode::ConfigConflict &&
                   !take(scene.state->upgradeSnapshot()).cancellationRequested);
    view.cancel->click();
    PLATFORM_CHECK(!take(scene.state->upgradeSnapshot()).cancellationRequested && view.cancel->isEnabled() &&
                   view.reportText->toPlainText().contains("OperationId #" + QString::number(second.value())));
    view.cancel->click();
    const auto secondReport = view.settled(1);
    PLATFORM_CHECK(secondReport.replacement->operation == second &&
                   secondReport.replacement->outcome == ReplacementOutcome::Cancelled &&
                   !secondReport.replacement->recovered &&
                   running(scene, "example.counter").instance == oldCounter.instance);
    PLATFORM_CHECK(secondTask.tryResult() && secondTask.tryResult()->hasValue() &&
                   secondTask.tryResult()->value().replacement->operation == second);
    view.select("notebook");
    visibleReport(view, cancelled);
    PLATFORM_CHECK(view.reportText->toPlainText().contains("Cancelled"));
    const auto closing = scene.state->close();
    PLATFORM_CHECK(!closing.isReady() && scene.state->cancelUpgrade(second).error().code == ErrorCode::ContextClosing);
    PLATFORM_CHECK(wait(closing, scene.scheduler)->hasValue());
    PLATFORM_CHECK(scene.state->cancelUpgrade(second).error().code == ErrorCode::ContextClosing);
    finish(scene);
}
void cancelBuilding(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto original = running(scene, "example.notebook");
    const auto previous = take(scene.state->generation());
    const auto locator = take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest;
    const auto draft = QString::fromUtf8("构建中取消后恢复的真实草稿 😀");
    editor(scene, original.instance)->setPlainText(draft);
    QPointer<QWidget> old = scene.page(original.instance);
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    auto* list = commandList();
    auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
    PLATFORM_CHECK(proxy && proxy->sourceModel());
    bool observed = false;
    std::exception_ptr failure;
    OperationId operation;
    Observation observer;
    observer.connection = QObject::connect(proxy->sourceModel(), &QAbstractItemModel::rowsInserted, list, [&] {
        if (observed || failure)
            return;
        try {
            const auto actual = notebookCandidate(scene, previous, true);
            if (!actual)
                return;
            PLATFORM_CHECK(!old && operation.valid() && view.cancel->isEnabled());
            const auto before = take(scene.state->upgradeSnapshot());
            PLATFORM_CHECK(before.phase == ReplacementPhase::Building && before.active &&
                           before.active->replacement->operation == operation);
            observed = true;
            view.cancel->click();
            PLATFORM_CHECK(take(scene.state->upgradeSnapshot()).cancellationRequested);
        } catch (...) {
            failure = std::current_exception();
        }
    });
    operation = view.begin();
    const auto report = view.settled();
    QObject::disconnect(observer.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed && report.replacement && report.replacement->operation == operation &&
                   report.replacement->outcome == ReplacementOutcome::Cancelled && report.replacement->recovered &&
                   !report.replacement->committed && !report.retained && !report.deliveryBlocked &&
                   take(scene.state->generation()) == previous &&
                   platform_upgrade_test::module(scene, previous).role == GenerationRole::Current &&
                   take(scene.state->driver->configurationDefinitions()->module("example.notebook")).manifest ==
                       locator);
    const auto restored = running(scene, "example.notebook");
    PLATFORM_CHECK(restored.instance != original.instance && editor(scene, restored.instance)->toPlainText() == draft);
    visibleReport(view, report);
    PLATFORM_CHECK(view.reportText->toPlainText().contains("Cancelled") &&
                   view.reportText->toPlainText().contains("recovered=1"));
    finish(scene);
}
} // namespace
bool operationsCase(const QString& name, const QString& sourceBin)
{
    if (name == "counter")
        counter(sourceBin);
    else if (name == "notebook")
        notebook(sourceBin);
    else if (name == "cancel_prepared")
        cancelPrepared(sourceBin);
    else if (name == "cancel_building")
        cancelBuilding(sourceBin);
    else
        return false;
    return true;
}
} // namespace upgrade_view_test
