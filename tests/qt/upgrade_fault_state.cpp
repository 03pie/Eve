/** @file upgrade_fault_state.cpp
 * @brief 真实 Notebook 激活失败/超限捕获及原发布屏障，不修改业务 DLL。
 */
#include "upgrade_fault_p.hpp"
#include <QDir>
#include <algorithm>

namespace upgrade_fault_test {
QString faultPath(const QString& sourceBin, const std::string& name)
{
    return QDir(sourceBin).absoluteFilePath(QString::fromStdString("native-faults/" + name + "/module.json"));
}
QPlainTextEdit* editor(const Scene& scene, InstanceId instance)
{
    auto* widget = scene.page(instance);
    PLATFORM_CHECK(widget);
    auto* value = widget->findChild<QPlainTextEdit*>("CordisNotebookEditor");
    PLATFORM_CHECK(value);
    return value;
}
bool containsError(const ErrorInfo& error, ErrorCode code)
{
    return error.code == code || std::any_of(error.causes.begin(), error.causes.end(),
                                             [&](const auto& cause) { return containsError(cause, code); });
}
bool containsError(const ReplacementReport& report, ErrorCode code)
{
    return std::any_of(report.errors.begin(), report.errors.end(),
                       [&](const auto& error) { return containsError(error, code); });
}
WorkspaceUpgradeReport retained(View& view)
{
    until([&] {
        const auto state = take(view.scene.state->upgradeSnapshot());
        return state.active && state.active->replacement && state.active->retained &&
               state.active->replacement->resourcesRetained && state.phase == ReplacementPhase::Failed;
    });
    barrier(view.scene.scheduler);
    view.refresh();
    const auto state = take(view.scene.state->upgradeSnapshot());
    PLATFORM_CHECK(state.active && state.active->replacement && !state.accepting && !state.deliveryBlocked);
    return *state.active;
}
void retainOwners(Scene& scene)
{
    PLATFORM_CHECK(!take(scene.state->kernel->replacements()).empty());
    std::weak_ptr<notebook::Workspace> owner = scene.state;
    std::weak_ptr<Kernel> kernel = scene.state->kernel;
    std::weak_ptr<KernelReconcileDriver> driver = scene.state->driver;
    std::weak_ptr<ConfigurationSession> session = scene.state->session;
    scene.state.reset();
    barrier(scene.scheduler);
    PLATFORM_CHECK(!owner.expired() && !kernel.expired() && !driver.expired() && !session.expired() &&
                   take(scene.scheduler->snapshot()).callbackErrors == 0);
}
void closeExpectedFailure(Scene& scene, GenerationId expected, const std::string& phase)
{
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    const auto close = take(scene.state->closeSnapshot());
    PLATFORM_CHECK(close.closed && !close.retained && close.session && close.session->hasValue() && close.kernel &&
                   close.kernel->hasValue() && close.kernel->value().quiescent && close.counter &&
                   close.counter->hasValue() && close.counter->value().quiescent && close.reporter &&
                   close.reporter->hasValue() && close.reporter->value().quiescent && close.counterDrain &&
                   close.counterDrain->hasValue() && close.reporterDrain && close.reporterDrain->hasValue() &&
                   close.shell && close.shell->hasValue() && close.providers && close.providers->hasValue());
    bool found = false;
    for (const auto& row : take(scene.state->manager->snapshot())) {
        if (row.generation == expected) {
            found = true;
            PLATFORM_CHECK(row.role == GenerationRole::Candidate && row.nativeMapped && row.nativePinned &&
                           row.loadPolicy == ModuleLoadPolicy::Resident && row.resources.drained() &&
                           ((row.failure && row.failure->phase == phase) ||
                            (row.businessFailure && row.businessFailure->phase == phase)));
        } else if (row.manifest.loader == ModuleLoader::QtUi)
            PLATFORM_CHECK(!row.failure && !row.businessFailure && row.nativePinned &&
                           row.resources.records.size() == 1 &&
                           row.resources.records.front().label == "qt.loader.root");
        else
            PLATFORM_CHECK(!row.failure && !row.businessFailure && row.resources.drained());
    }
    PLATFORM_CHECK(found && !scene.state->kernel && !scene.state->driver && !scene.state->session &&
                   scene.pages().empty() && take(scene.state->shell.commands()->snapshot()).empty());
    scene.release();
}
namespace {
void notebookRestore(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto original = running(scene, "example.notebook");
    const auto previous = take(scene.state->generation());
    const auto source = take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml();
    const auto counter = running(scene, "example.counter"), reporter = running(scene, "example.reporter");
    const auto draft = QString::fromUtf8("激活失败后恢复的最后草稿 😀\nno early startup output");
    QPointer<QWidget> old = scene.page(original.instance);
    editor(scene, original.instance)->setPlainText(draft);
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook", "fail-start"));
    auto* list = commandList();
    auto* proxy = qobject_cast<QAbstractProxyModel*>(list->model());
    PLATFORM_CHECK(proxy && proxy->sourceModel());
    InstanceId candidate;
    bool observed = false;
    std::exception_ptr failure;
    Observation observation;
    observation.connection = QObject::connect(
        proxy->sourceModel(), &QAbstractItemModel::rowsInserted, list, [&](const QModelIndex&, int first, int last) {
            if (observed || failure)
                return;
            try {
                const auto actual = notebookCandidate(scene, previous, true);
                if (!actual)
                    return;
                candidate = actual->instance.instance;
                PLATFORM_CHECK(!old);
                for (int row = first; row <= last; ++row)
                    PLATFORM_CHECK(!proxy->mapFromSource(proxy->sourceModel()->index(row, 0)).isValid());
                for (const auto& page : take(scene.state->shell.pages()->snapshot()))
                    if (page.owner.instance == candidate)
                        PLATFORM_CHECK(!page.navigationPresent && !page.current);
                for (const auto& entry : take(scene.state->shell.output()->snapshot()))
                    PLATFORM_CHECK(!entry.owner || entry.owner->instance != candidate);
                observed = true;
            } catch (...) {
                failure = std::current_exception();
            }
        });
    const auto operation = view.begin();
    const auto report = view.settled();
    QObject::disconnect(observation.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(observed && candidate.valid() && report.replacement && report.replacement->operation == operation &&
                   report.replacement->outcome == ReplacementOutcome::Restored && report.replacement->recovered &&
                   !report.replacement->committed && !report.replacement->resourcesRetained && !report.retained &&
                   containsError(*report.replacement, ErrorCode::ActivationFailed) &&
                   take(scene.state->generation()) == previous &&
                   take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml() == source);
    const auto restored = running(scene, "example.notebook");
    PLATFORM_CHECK(restored.instance != original.instance && restored.instance != candidate &&
                   editor(scene, restored.instance)->toPlainText() == draft && scene.pages().size() == 1 &&
                   running(scene, "example.counter").instance == counter.instance &&
                   running(scene, "example.reporter").instance == reporter.instance);
    for (const auto& row : take(scene.state->shell.output()->snapshot()))
        PLATFORM_CHECK(!row.owner || row.owner->instance != candidate);
    for (const auto& row : take(scene.state->shell.commands()->snapshot()))
        PLATFORM_CHECK(row.owner.instance != original.instance && row.owner.instance != candidate);
    visibleReport(view, report);
    PLATFORM_CHECK(view.reportText->toPlainText().contains("Restored") &&
                   view.reportText->toPlainText().contains("recovered=1"));
    scene.edit(restored.instance, draft);
    finish(scene);
}
void draftLimit(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto original = running(scene, "example.notebook");
    const auto previous = take(scene.state->generation());
    QPointer<QWidget> page = scene.page(original.instance);
    const auto text = QString(65537, QLatin1Char('x'));
    editor(scene, original.instance)->setPlainText(text);
    View view(scene);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    const auto operation = view.begin();
    const auto failed = view.settled();
    PLATFORM_CHECK(failed.replacement && failed.replacement->operation == operation &&
                   failed.replacement->outcome == ReplacementOutcome::Failed && !failed.replacement->committed &&
                   !failed.replacement->recovered && !failed.replacement->resourcesRetained &&
                   containsError(*failed.replacement, ErrorCode::InvalidArgument) && page && !failed.retained &&
                   running(scene, "example.notebook").instance == original.instance &&
                   running(scene, "example.notebook").epoch == original.epoch &&
                   editor(scene, original.instance)->toPlainText() == text &&
                   take(scene.state->generation()) == previous && take(scene.state->kernel->replacements()).empty());
    const auto staged = platform_upgrade_test::module(scene, failed.candidate);
    PLATFORM_CHECK(staged.role == GenerationRole::Candidate && staged.state == ModuleState::Loaded &&
                   staged.resources.accepting && !staged.failure && !staged.businessFailure);
    PLATFORM_CHECK(scene.state->retryRetirement(failed.candidate).error().code == ErrorCode::ModulePinned);
    const auto shortDraft = QString::fromUtf8("缩短后同一候选再试 😀");
    editor(scene, original.instance)->setPlainText(shortDraft);
    view.preview("notebook", candidatePath(sourceBin, "notebook"));
    const auto retry = view.begin();
    const auto applied = view.settled(1);
    PLATFORM_CHECK(applied.candidate == failed.candidate && applied.replacement->operation == retry &&
                   retry != operation);
    committed(scene, applied);
    const auto actual = running(scene, "example.notebook");
    PLATFORM_CHECK(!page && actual.instance != original.instance &&
                   editor(scene, actual.instance)->toPlainText() == shortDraft);
    visibleReport(view, applied);
    finish(scene);
}
} // namespace
bool stateCase(const QString& name, const QString& sourceBin)
{
    if (name == "fault_notebook_restore")
        notebookRestore(sourceBin);
    else if (name == "fault_draft_limit")
        draftLimit(sourceBin);
    else
        return false;
    return true;
}
} // namespace upgrade_fault_test
