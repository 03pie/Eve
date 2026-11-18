/** @file upgrade_fault_retirement.cpp
 * @brief 外借真实工厂时拒绝排空，释放后由同一高级控件完成物理回收。
 */
#include "native_image_p.hpp"
#include "upgrade_fault_p.hpp"
#include <thread>

namespace upgrade_fault_test {
namespace {
void retirement(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto previous = take(scene.state->counter())->generation;
    const auto qt = take(scene.state->generation());
    const auto image = platform_upgrade_test::Image(platform_upgrade_test::module(scene, previous));
    std::optional<Runtime> borrowed(*take(scene.state->counter())->runtime);
    PLATFORM_CHECK(!scene.state->retryRetirement({}).hasValue() && !scene.state->retryRetirement(previous).hasValue() &&
                   !scene.state->retryRetirement(qt).hasValue());
    View view(scene);
    view.preview("counter", candidatePath(sourceBin, "counter"));
    const auto operation = view.begin();
    const auto rejectedActive = scene.state->retryRetirement(previous);
    PLATFORM_CHECK(!rejectedActive.hasValue() && rejectedActive.error().code == ErrorCode::ConfigConflict);
    const auto original = view.settled();
    committed(scene, original);
    PLATFORM_CHECK(original.replacement->operation == operation && original.drain && !original.drain->hasValue() &&
                   !original.unload && platform_upgrade_test::module(scene, previous).nativeMapped);
    bool wrongThread = false;
    std::thread worker([&] {
        const auto result = scene.state->retryRetirement(previous);
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(wrongThread);
    auto* tabs = view.body->findChild<QTabWidget*>("CordisModuleTabs");
    auto* choices = view.body->findChild<QComboBox*>("CordisRetirementGeneration");
    auto* retry = view.body->findChild<QPushButton*>("CordisRetirementRetry");
    auto* status = view.body->findChild<QLabel*>("CordisRetirementStatus");
    PLATFORM_CHECK(tabs && choices && retry && status);
    tabs->setCurrentIndex(1);
    view.refresh();
    int target = -1;
    for (int row = 0; row != choices->count(); ++row)
        if (choices->itemData(row).toULongLong() == previous.value())
            target = row;
    PLATFORM_CHECK(target >= 0);
    choices->setCurrentIndex(target);
    PLATFORM_CHECK(retry->isEnabled());
    retry->click();
    PLATFORM_CHECK(platform_upgrade_test::module(scene, previous).nativeMapped &&
                   status->text().contains(QString::fromStdString(original.drain->error().message)) &&
                   choices->currentData().toULongLong() == previous.value());
    borrowed.reset();
    barrier(scene.scheduler);
    view.refresh();
    PLATFORM_CHECK(retry->isEnabled());
    retry->click();
    const auto gone = platform_upgrade_test::module(scene, previous);
    PLATFORM_CHECK(gone.state == ModuleState::Unloaded && !gone.nativeMapped && gone.resources.records.empty() &&
                   !gone.unloadFailure && status->text().contains(QString::number(previous.value())));
    image.absent();
    const auto history = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(history.history.size() == 1 && history.history.front().replacement->operation == operation &&
                   history.history.front().replacement->committed && history.history.front().drain &&
                   !history.history.front().drain->hasValue() && !history.history.front().unload && original.drain &&
                   !original.drain->hasValue());
    visibleReport(view, original);
    PLATFORM_CHECK(view.reportText->toPlainText().contains("Applied") && counterValue(scene) == "2");
    reporterValue(scene, "2");
    const auto again = take(scene.state->retryRetirement(previous));
    PLATFORM_CHECK(again.generation == previous && !again.drain && again.unload && again.unload->hasValue());
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    PLATFORM_CHECK(scene.state->retryRetirement(previous).error().code == ErrorCode::ContextClosing);
    finish(scene);
}
} // namespace
bool retirementCase(const QString& name, const QString& sourceBin)
{
    if (name != "fault_retirement")
        return false;
    retirement(sourceBin);
    return true;
}
} // namespace upgrade_fault_test
