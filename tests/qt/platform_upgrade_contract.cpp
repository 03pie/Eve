/** @file platform_upgrade_contract.cpp
 * @brief 预览无副作用、重验及关闭在途操作的真实入口检查。
 */
#include "platform_upgrade_p.hpp"
#include <QDir>
#include <QFile>
#include <thread>

namespace platform_upgrade_test {
namespace {
void admission(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto old = running(scene, "example.counter");
    const auto revision = take(scene.state->session->snapshot()).coordinator.desiredRevision;
    PLATFORM_CHECK(
        !scene.state->previewUpgrade("unknown", sourceBin, "native-upgrade/counter-b/module.json").hasValue());
    PLATFORM_CHECK(
        !scene.state->previewUpgrade("counter", sourceBin, "native-upgrade/reporter-b/module.json").hasValue());
    PLATFORM_CHECK(
        !scene.state->previewUpgrade("counter", sourceBin, "native-upgrade/counter-a/module.json").hasValue());
    const auto value = preview(scene, sourceBin, "counter");
    bool wrongThread = false;
    std::thread worker([&] {
        const auto generation = scene.state->generation();
        const auto counter = scene.state->counter();
        const auto reporter = scene.state->reporter();
        const auto notebook = scene.state->notebookRuntime();
        const auto snapshot = scene.state->upgradeSnapshot();
        const auto rejected = scene.state->upgrade(value);
        wrongThread = !generation.hasValue() && generation.error().code == ErrorCode::WrongThread &&
                      !counter.hasValue() && counter.error().code == ErrorCode::WrongThread && !reporter.hasValue() &&
                      reporter.error().code == ErrorCode::WrongThread && !notebook.hasValue() &&
                      notebook.error().code == ErrorCode::WrongThread && !snapshot.hasValue() &&
                      snapshot.error().code == ErrorCode::WrongThread && !rejected.hasValue() &&
                      rejected.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    PLATFORM_CHECK(wrongThread);
    const auto candidateFile =
        QDir(scene.store).filePath(QString::fromStdString(value.artifact.manifest().sha256) + "/module.json");
    PLATFORM_CHECK(take(scene.state->manager->snapshot()).size() == 3 && !QFile::exists(candidateFile) &&
                   take(scene.state->kernel->replacements()).empty() &&
                   running(scene, "example.counter").instance == old.instance &&
                   take(scene.state->session->snapshot()).coordinator.desiredRevision == revision);
    auto stale = value;
    stale.previous = GenerationId{};
    PLATFORM_CHECK(!scene.state->upgrade(stale).hasValue());
    auto forged = value;
    forged.impact.members.clear();
    PLATFORM_CHECK(!scene.state->upgrade(forged).hasValue());
    PLATFORM_CHECK(!QFile::exists(candidateFile));
    const auto task = take(scene.state->upgrade(value));
    PLATFORM_CHECK(!scene.state->upgrade(value).hasValue());
    const auto report = take(*wait(task, scene.scheduler));
    PLATFORM_CHECK(QFile::exists(candidateFile));
    applied(scene, value, report);
    finish(scene);
}
void staleRevision(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto value = preview(scene, sourceBin, "counter");
    scene.apply(scene.state->initial);
    PLATFORM_CHECK(take(scene.state->session->snapshot()).coordinator.desiredRevision > value.desiredRevision);
    const auto rejected = scene.state->upgrade(value);
    PLATFORM_CHECK(
        !rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict &&
        !QFile::exists(
            QDir(scene.store).filePath(QString::fromStdString(value.artifact.manifest().sha256) + "/module.json")) &&
        take(scene.state->manager->snapshot()).size() == 3 && take(scene.state->kernel->replacements()).empty());
    finish(scene);
}
void changedSource(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto source = take(preflightModule(utf8(sourceBin), "native-upgrade/counter-b/module.json"));
    const auto copyRoot = scene.scratch.filePath("source-copy");
    PLATFORM_CHECK(QDir().mkpath(copyRoot));
    PLATFORM_CHECK(
        QFile::copy(QString::fromUtf8(source.manifestPath().c_str()), QDir(copyRoot).filePath("module.json")));
    const auto copiedLibrary = QDir(copyRoot).filePath(QString::fromUtf8(source.manifest().library.c_str()));
    PLATFORM_CHECK(QFile::copy(QString::fromUtf8(source.libraryPath().c_str()), copiedLibrary));
    const auto value = take(scene.state->previewUpgrade("counter", copyRoot, "module.json"));
    {
        QFile library(copiedLibrary);
        PLATFORM_CHECK(library.open(QIODevice::Append) && library.write("changed", 7) == 7);
    }
    PLATFORM_CHECK(
        !scene.state->upgrade(value).hasValue() && take(scene.state->manager->snapshot()).size() == 3 &&
        !QFile::exists(
            QDir(scene.store).filePath(QString::fromStdString(value.artifact.manifest().sha256) + "/module.json")) &&
        take(scene.state->kernel->replacements()).empty());
    finish(scene);
}
void closeInflight(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto value = preview(scene, sourceBin, "counter");
    const auto task = take(scene.state->upgrade(value));
    const auto closing = scene.state->close();
    PLATFORM_CHECK(
        !scene.state->previewUpgrade("counter", sourceBin, "native-upgrade/counter-b/module.json").hasValue());
    const auto result = wait(task, scene.scheduler);
    PLATFORM_CHECK(result->hasValue() && !result->value().retained && !result->value().deliveryBlocked &&
                   result->value().replacement && !result->value().replacement->resourcesRetained &&
                   result->value().replacement->outcome == ReplacementOutcome::Cancelled &&
                   !result->value().replacement->committed && !result->value().replacement->recovered &&
                   result->value().replacement->instances.empty());
    PLATFORM_CHECK(wait(closing, scene.scheduler)->hasValue());
    finish(scene);
}
} // namespace
bool contractCase(const QString& name, const QString& sourceBin)
{
    if (name == "admission")
        admission(sourceBin);
    else if (name == "stale_revision")
        staleRevision(sourceBin);
    else if (name == "changed_source")
        changedSource(sourceBin);
    else if (name == "close_inflight")
        closeInflight(sourceBin);
    else
        return false;
    return true;
}
} // namespace platform_upgrade_test
