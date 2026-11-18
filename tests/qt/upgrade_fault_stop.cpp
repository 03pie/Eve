/** @file upgrade_fault_stop.cpp
 * @brief 原真实 failCleanup 停止失败不能冒称已恢复；未确认链留在独立进程。
 */
#include "upgrade_fault_p.hpp"
#include <algorithm>

namespace upgrade_fault_test {
namespace {
void stopFailure(const QString& sourceBin, bool notebookFailure)
{
    Scene scene;
    scene.open(sourceBin);
    const auto source = take(parseConfiguration(scene.state->initial));
    const auto path = notebookFailure ? "notes" : "counter";
    const auto config =
        notebookFailure ? "{title: StopFailure, text: retained draft, failCleanup: true}" : "{failCleanup: true}";
    scene.apply(take(setEntryConfiguration(source, path, config)).normalizedYaml());
    const auto alias = std::string(notebookFailure ? "notebook" : "counter");
    const auto plugin = "example." + alias;
    const auto original = running(scene, plugin);
    const auto notes = running(scene, "example.notebook");
    QPointer<QWidget> oldPage = scene.page(notes.instance);
    const auto oldDraft = editor(scene, notes.instance)->toPlainText();
    const auto before = take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml();
    const auto locator = take(scene.state->driver->configurationDefinitions()->module(plugin)).manifest;
    const auto generation =
        notebookFailure ? take(scene.state->generation()) : take(scene.state->counter())->generation;
    View view(scene);
    view.preview(alias, candidatePath(sourceBin, alias));
    const auto operation = view.begin();
    const auto report = retained(view);
    PLATFORM_CHECK(
        report.replacement->operation == operation && report.replacement->outcome == ReplacementOutcome::Failed &&
        !report.replacement->committed && !report.replacement->recovered && report.replacement->resourcesRetained &&
        !containsError(*report.replacement, ErrorCode::RecoveryFailed) && report.replacement->instances.empty() &&
        platform_upgrade_test::module(scene, generation).role == GenerationRole::Current &&
        take(scene.state->driver->configurationDefinitions()->module(plugin)).manifest == locator &&
        take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml() == before);
    const auto scopes = take(scene.state->kernel->replacements());
    PLATFORM_CHECK(scopes.size() == 1 && scopes.front().operation == operation && scopes.front().cleanup &&
                   !scopes.front().cleanup->quiescent && !scopes.front().cleanup->errors.empty());
    PLATFORM_CHECK(std::any_of(scopes.front().closure.members.begin(), scopes.front().closure.members.end(),
                               [&](const auto& row) { return row.instance == original.instance; }));
    const auto candidate = platform_upgrade_test::module(scene, report.candidate);
    const auto core = take(scene.state->kernel->snapshot());
    for (const auto& row : core.instances) {
        const auto origin = take(scene.state->manager->runtimeOrigin(row.runtime));
        PLATFORM_CHECK(!origin || origin->generation != candidate.generation);
    }
    visibleReport(view, report);
    PLATFORM_CHECK(view.reportText->toPlainText().contains("Failed") &&
                   !view.reportText->toPlainText().contains("RecoveryFailed") &&
                   view.reportText->toPlainText().contains("resourcesRetained=1"));
    if (notebookFailure) {
        PLATFORM_CHECK(!oldPage && scene.pages().empty() && take(scene.state->shell.commands()->snapshot()).empty());
    } else {
        PLATFORM_CHECK(oldPage && scene.page(notes.instance) == oldPage &&
                       running(scene, "example.notebook").instance == notes.instance &&
                       editor(scene, notes.instance)->toPlainText() == oldDraft);
    }
    PLATFORM_CHECK(!scene.state->retryRetirement(generation).hasValue());
    // 原停止失败已经不是可继续业务的旧 Active，不能用旧页存在或成功 close 伪造恢复。
    retainOwners(scene);
}
} // namespace
bool stopCase(const QString& name, const QString& sourceBin)
{
    if (name == "fault_stop_counter")
        stopFailure(sourceBin, false);
    else if (name == "fault_stop_notebook")
        stopFailure(sourceBin, true);
    else
        return false;
    return true;
}
} // namespace upgrade_fault_test
