/** @file upgrade_fault_native.cpp
 * @brief UI 真正执行已过预览的缺符号 DLL；准备失败不伪造原 OperationId。
 */
#include "upgrade_fault_p.hpp"
#include <QFileInfo>
#include <algorithm>

namespace upgrade_fault_test {
namespace {
void missing(const QString& sourceBin, bool discovery)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto oldCounter = running(scene, "example.counter");
    const auto oldReporter = running(scene, "example.reporter");
    const auto notes = running(scene, "example.notebook");
    QPointer<QWidget> page = scene.page(notes.instance);
    const auto current = take(scene.state->counter())->generation;
    const auto source = take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml();
    View view(scene);
    const auto path = faultPath(sourceBin, discovery ? "counter-missing-query" : "counter-missing-business");
    view.preview("counter", path);
    const auto artifact = take(preflightModule(utf8(QFileInfo(path).absolutePath()), "module.json"));
    PLATFORM_CHECK(artifact.manifest().unloadPolicy == ModuleUnloadPolicy::Resident &&
                   take(scene.state->manager->snapshot()).size() == 3);
    // stage/ABI 准备会真实拒绝；此时尚未创建 ReplacementOperation，不能套 View.begin。
    view.start->click();
    const auto report = view.settled();
    const std::string phase = discovery ? "module.query" : "module.abi.query";
    PLATFORM_CHECK(report.failure && report.failure->code == ErrorCode::AbiMismatch && report.failure->phase == phase &&
                   !report.replacement && report.candidate.valid() && report.previous == current &&
                   report.retired == report.candidate && !report.retained && !report.deliveryBlocked &&
                   take(scene.state->kernel->replacements()).empty());
    const auto candidate = platform_upgrade_test::module(scene, report.candidate);
    PLATFORM_CHECK(candidate.role == GenerationRole::Candidate && candidate.predecessor == current &&
                   candidate.manifest.sha256 == artifact.manifest().sha256 && candidate.nativeMapped &&
                   candidate.nativePinned && candidate.loadPolicy == ModuleLoadPolicy::Resident &&
                   !candidate.businessAccepted);
    if (discovery)
        PLATFORM_CHECK(candidate.state == ModuleState::Failed && !candidate.descriptorAccepted && candidate.failure &&
                       candidate.failure->phase == phase);
    else
        PLATFORM_CHECK(candidate.descriptorAccepted && !candidate.failure && candidate.businessFailure &&
                       candidate.businessFailure->phase == phase);
    PLATFORM_CHECK(take(scene.state->counter())->generation == current &&
                   running(scene, "example.counter").instance == oldCounter.instance &&
                   running(scene, "example.counter").epoch == oldCounter.epoch &&
                   running(scene, "example.reporter").instance == oldReporter.instance &&
                   take(scene.state->session->snapshot()).coordinator.desired->source().normalizedYaml() == source &&
                   page && scene.page(notes.instance) == page);
    const auto text = view.reportText->toPlainText();
    PLATFORM_CHECK(text.contains(QString::fromStdString(phase)) &&
                   text.contains(QString::number(report.candidate.value())) && !text.contains("Restored") &&
                   !text.contains("OperationId #"));
    scene.business();
    PLATFORM_CHECK(report.unload && !report.unload->hasValue() &&
                   report.unload->error().code == ErrorCode::ModulePinned);
    closeExpectedFailure(scene, report.candidate, phase);
}
} // namespace
bool nativeCase(const QString& name, const QString& sourceBin)
{
    if (name == "fault_missing_query")
        missing(sourceBin, true);
    else if (name == "fault_missing_business")
        missing(sourceBin, false);
    else
        return false;
    return true;
}
} // namespace upgrade_fault_test
