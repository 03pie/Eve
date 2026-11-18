/** @file platform_product.cpp
 * @brief 受审产品名单经原 Workspace 准入、替换、物理退役及跨进程保存恢复。
 */
#include "native_catalog_p.hpp"
#include "native_image_p.hpp"
#include "platform_upgrade_p.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QWidget>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

namespace {
using namespace platform_upgrade_test;

QByteArray read(const QString& path)
{
    QFile file(path);
    PLATFORM_CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}
std::map<QString, QByteArray> storeTree(const QString& root)
{
    std::map<QString, QByteArray> result;
    QDirIterator entries(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        const auto path = entries.next();
        const auto relative = QDir(root).relativeFilePath(path);
        result.emplace((entries.fileInfo().isDir() ? "directory/" : "file/") + relative,
                       entries.fileInfo().isDir() ? QByteArray() : QByteArray::fromStdString(sha256(read(path))));
    }
    return result;
}
void openProduct(Scene& scene, const QString& sourceBin)
{
    scene.state = take(notebook::Workspace::openPlatform(sourceBin, scene.store, scene.file, scene.scheduler,
                                                         notebook::detail::productNativeApprovals()));
    PLATFORM_CHECK(scene.state->scheduler == scene.scheduler && scene.state->kernel && scene.state->driver &&
                   scene.state->session && scene.state->kernel->usesScheduler(scene.scheduler) &&
                   scene.state->session->usesScheduler(scene.scheduler));
    PLATFORM_CHECK(wait(scene.state->ready(), scene.scheduler)->hasValue());
}
void catalog(const QString& sourceBin)
{
    const auto approvals = notebook::detail::productNativeApprovals();
    PLATFORM_CHECK(approvals.size() == 4);
    std::set<std::pair<std::string, std::string>> expected, actual;
    for (const auto* alias : {"counter", "reporter"})
        for (const auto* variant : {"a", "b"}) {
            const auto artifact = take(preflightModule(utf8(sourceBin), "native-upgrade/" + std::string(alias) + '-' +
                                                                            variant + "/module.json"));
            const auto& value = artifact.manifest();
            PLATFORM_CHECK(value.moduleId == "org.cordis.example." + std::string(alias) &&
                           value.loader == ModuleLoader::CAbi && value.unloadPolicy == ModuleUnloadPolicy::Explicit &&
                           value.plugins == std::vector<std::string>{"example." + std::string(alias)} &&
                           value.moduleVersion == (std::string(variant) == "a" ? "1.1.0" : "1.2.0"));
            PLATFORM_CHECK(expected.emplace(value.moduleId, value.sha256).second);
        }
    for (const auto& approval : approvals)
        PLATFORM_CHECK(actual.emplace(approval.moduleId, approval.sha256).second);
    // Exact equality also excludes Qt, fault fixtures, resident variants and other profile hashes.
    PLATFORM_CHECK(actual == expected);
}
QPlainTextEdit* notebookEditor(const Scene& scene, InstanceId instance)
{
    auto* widget = scene.page(instance);
    PLATFORM_CHECK(widget);
    auto* editor = widget->findChild<QPlainTextEdit*>("CordisNotebookEditor");
    PLATFORM_CHECK(editor);
    return editor;
}
void reclaimed(Scene& scene, const UpgradePreview& value, const WorkspaceUpgradeReport& report, const Image& image)
{
    PLATFORM_CHECK(report.nativeClose && report.nativeClose->hasValue() && report.nativeClose->value().quiescent &&
                   report.drain && report.drain->hasValue() && report.unload && report.unload->hasValue());
    const auto old = platform_upgrade_test::module(scene, value.previous);
    PLATFORM_CHECK(old.role == GenerationRole::Retired && old.state == ModuleState::Unloaded && !old.nativeMapped &&
                   !old.nativePinned && old.resources.records.empty() && !old.unloadFailure &&
                   !take(scene.state->manager->runtimeOrigin(value.previousRuntime)));
    image.absent();
}
void roundtrip(const QString& sourceBin)
{
    Scene scene;
    openProduct(scene, sourceBin);
    scene.apply(scene.state->initial);
    scene.business();
    verifySources(scene, "1.1.0");
    const auto notes = running(scene, "example.notebook");
    const auto notebookRuntime = take(scene.state->notebookRuntime()).id();
    const QPointer<QWidget> widget = scene.page(notes.instance);
    const auto draft = QString::fromUtf8("产品 Native 往返保留草稿 😀");
    scene.edit(notes.instance, draft);
    unsigned counter = 1;
    for (const auto* variant : {"b", "a"})
        for (const auto* alias : {"counter", "reporter"}) {
            const auto oldCounter = running(scene, "example.counter");
            const auto oldReporter = running(scene, "example.reporter");
            const auto value = preview(scene, sourceBin, alias, variant);
            const Image image(platform_upgrade_test::module(scene, value.previous));
            const auto report = perform(scene, value);
            applied(scene, value, report);
            reclaimed(scene, value, report, image);
            const bool provider = std::string(alias) == "counter";
            counter += provider ? 1u : 2u;
            PLATFORM_CHECK(counterValue(scene) == std::to_string(counter));
            reporterValue(scene, std::to_string(counter));
            const auto nowCounter = running(scene, "example.counter");
            const auto nowReporter = running(scene, "example.reporter");
            const auto nowNotes = running(scene, "example.notebook");
            PLATFORM_CHECK(nowReporter.instance != oldReporter.instance &&
                           (nowCounter.instance != oldCounter.instance) == provider &&
                           nowNotes.instance == notes.instance && nowNotes.epoch == notes.epoch &&
                           take(scene.state->notebookRuntime()).id() == notebookRuntime && widget &&
                           scene.page(notes.instance) == widget &&
                           notebookEditor(scene, notes.instance)->toPlainText() == draft);
        }
    PLATFORM_CHECK(counter == 7 && take(scene.state->upgradeSnapshot()).history.size() == 4);
    finish(scene);
}
void rejectedOpen(const QString& sourceBin, bool wrongHash)
{
    Scene scene;
    installSession(scene, sourceBin, "a");
    const auto disk = read(scene.file);
    const auto tree = storeTree(scene.store);
    const auto before = QApplication::topLevelWidgets();
    auto approvals = notebook::detail::productNativeApprovals();
    PLATFORM_CHECK(approvals.size() == 4);
    if (wrongHash)
        for (auto& row : approvals)
            row.sha256 = std::string(64, '0');
    else
        approvals.clear();
    const auto opened =
        notebook::Workspace::openPlatform(sourceBin, scene.store, scene.file, scene.scheduler, std::move(approvals));
    PLATFORM_CHECK(!opened.hasValue() && opened.error().code == ErrorCode::ModulePinned);
    std::vector<QPointer<QWidget>> windows;
    for (auto* window : QApplication::topLevelWidgets())
        if (window->inherits("ElaWindow") && std::find(before.begin(), before.end(), window) == before.end())
            windows.emplace_back(window);
    barrier(scene.scheduler);
    until([&] {
        return std::all_of(windows.begin(), windows.end(), [](const auto& window) { return window.isNull(); }) &&
               take(scene.scheduler->snapshot()).queued == 0;
    });
    ModuleManager observer;
    PLATFORM_CHECK(take(observer.snapshot()).empty() && read(scene.file) == disk && storeTree(scene.store) == tree);
    const auto queue = take(scene.scheduler->snapshot());
    PLATFORM_CHECK(queue.open && queue.running == 0 && queue.discarded == 0 && queue.callbackErrors == 0);
    take(scene.scheduler->close());
}
void changedCandidate(const QString& sourceBin)
{
    Scene scene;
    openProduct(scene, sourceBin);
    scene.apply(scene.state->initial);
    scene.business();
    const auto source = take(preflightModule(utf8(sourceBin), "native-upgrade/counter-b/module.json"));
    const auto copy = scene.scratch.filePath("changed-source");
    PLATFORM_CHECK(QDir().mkpath(copy));
    const auto library = QDir(copy).filePath(QString::fromStdString(source.manifest().library));
    PLATFORM_CHECK(QFile::copy(QString::fromStdString(source.libraryPath()), library));
    {
        QFile file(library);
        PLATFORM_CHECK(file.open(QIODevice::Append) && file.write("product-unapproved-overlay", 26) == 26);
    }
    const auto digest = sha256(read(library));
    PLATFORM_CHECK(digest != source.manifest().sha256);
    auto manifest = QJsonDocument::fromJson(read(QString::fromStdString(source.manifestPath()))).object();
    manifest.insert("sha256", QString::fromStdString(digest));
    {
        QFile file(QDir(copy).filePath("module.json"));
        const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
        PLATFORM_CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
    }
    // This must reach the whitelist: the changed real PE and its updated manifest pass normal preflight.
    const auto changed = preflightModule(utf8(copy), "module.json");
    PLATFORM_CHECK(changed.hasValue() && changed.value().manifest().sha256 == digest &&
                   changed.value().manifest().moduleId == source.manifest().moduleId &&
                   changed.value().manifest().moduleVersion == source.manifest().moduleVersion &&
                   changed.value().manifest().plugins == source.manifest().plugins &&
                   changed.value().manifest().loader == ModuleLoader::CAbi &&
                   changed.value().manifest().unloadPolicy == ModuleUnloadPolicy::Explicit);
    const auto before = take(scene.state->session->snapshot());
    const auto runningBefore = take(scene.state->driver->snapshot());
    const auto coreBefore = take(scene.state->kernel->snapshot());
    const auto disk = take(readConfigurationFile(scene.file));
    const auto tree = storeTree(scene.store);
    const auto rejected = scene.state->previewUpgrade("counter", copy, "module.json");
    PLATFORM_CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ModulePinned);
    const auto after = take(scene.state->session->snapshot());
    const auto history = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(after.coordinator.desired == before.coordinator.desired &&
                   after.coordinator.desiredRevision == before.coordinator.desiredRevision &&
                   after.store.desired == before.store.desired && after.store.dirty == before.store.dirty &&
                   after.store.persistedRevision == before.store.persistedRevision && history.history.empty() &&
                   !history.active && history.accepting && take(scene.state->kernel->replacements()).empty() &&
                   take(scene.state->manager->snapshot()).size() == 3 && storeTree(scene.store) == tree);
    const auto runningAfter = take(scene.state->driver->snapshot());
    PLATFORM_CHECK(runningAfter.size() == runningBefore.size());
    for (const auto& old : runningBefore) {
        const auto row = std::find_if(runningAfter.begin(), runningAfter.end(),
                                      [&](const auto& value) { return value.path == old.path; });
        PLATFORM_CHECK(row != runningAfter.end() && row->instance == old.instance && row->epoch == old.epoch &&
                       row->state == old.state);
    }
    const auto coreAfter = take(scene.state->kernel->snapshot());
    PLATFORM_CHECK(coreAfter.instances.size() == coreBefore.instances.size());
    for (const auto& old : coreBefore.instances) {
        const auto row = std::find_if(coreAfter.instances.begin(), coreAfter.instances.end(),
                                      [&](const auto& value) { return value.instance == old.instance; });
        PLATFORM_CHECK(row != coreAfter.instances.end() && row->runtime == old.runtime && row->epoch == old.epoch &&
                       row->state == old.state);
    }
    const auto unchangedDisk = take(readConfigurationFile(scene.file));
    PLATFORM_CHECK(unchangedDisk.version.exists == disk.version.exists && unchangedDisk.text == disk.text);
    scene.business();
    finish(scene);
}
void reopen(const QString& sourceBin, const QString& file, const QString& store)
{
    Scene scene;
    scene.file = file;
    scene.store = store;
    const auto disk = read(file);
    const auto source = take(parseConfiguration(disk.toStdString()));
    for (const auto* alias : {"counter", "reporter"}) {
        const auto expected =
            take(preflightModule(utf8(sourceBin), "native-upgrade/" + std::string(alias) + "-b/module.json"));
        PLATFORM_CHECK(source.structure().modules().at(alias) == expected.manifest().sha256 + "/module.json");
    }
    openProduct(scene, sourceBin);
    PLATFORM_CHECK(scene.state->initial == disk.toStdString());
    scene.apply(scene.state->initial);
    // Runtime state is not a disk persistence contract: the new OS process starts Counter at one.
    scene.business();
    PLATFORM_CHECK(counterValue(scene) == "1");
    for (const auto& row : take(scene.state->manager->snapshot()))
        if (row.manifest.loader == ModuleLoader::CAbi)
            PLATFORM_CHECK(row.manifest.moduleVersion == "1.2.0" && row.loadPolicy == ModuleLoadPolicy::ApprovedNative);
    finish(scene);
    PLATFORM_CHECK(read(file) == disk);
    std::cout << "Product reopen pid=" << QCoreApplication::applicationPid() << '\n';
}
void restart(const QString& sourceBin)
{
    Scene scene;
    openProduct(scene, sourceBin);
    scene.apply(scene.state->initial);
    for (const auto* alias : {"counter", "reporter"}) {
        const auto value = preview(scene, sourceBin, alias);
        const Image image(platform_upgrade_test::module(scene, value.previous));
        const auto report = perform(scene, value);
        applied(scene, value, report);
        reclaimed(scene, value, report, image);
    }
    PLATFORM_CHECK(counterValue(scene) == "4");
    finish(scene);
    const auto disk = read(scene.file);
    const auto tree = storeTree(scene.store);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    const auto executable = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    process.start(executable, {sourceBin, "reopen", scene.file, scene.store}, QIODevice::ReadOnly);
    PLATFORM_CHECK(process.waitForStarted(10000));
    const auto child = process.processId();
    PLATFORM_CHECK(child && child != QCoreApplication::applicationPid());
    if (!process.waitForFinished(60000)) {
        process.kill();
        PLATFORM_CHECK(process.waitForFinished(10000));
        throw std::runtime_error("product reopen child exceeded its process budget");
    }
    const auto output = process.readAll();
    std::cout << "Product restart parent=" << QCoreApplication::applicationPid() << " child=" << child << '\n'
              << output.toStdString();
    PLATFORM_CHECK(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
                   output.contains("Product reopen pid=" + QByteArray::number(child)) && read(scene.file) == disk &&
                   storeTree(scene.store) == tree);
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    try {
        const auto args = application.arguments();
        PLATFORM_CHECK((args.size() == 3 && args[2] != "reopen") || (args.size() == 5 && args[2] == "reopen"));
        const auto sourceBin = QDir(args[1]).absolutePath();
        const auto name = args[2];
        if (name == "catalog")
            catalog(sourceBin);
        else if (name == "roundtrip")
            roundtrip(sourceBin);
        else if (name == "empty" || name == "wrong_hash")
            rejectedOpen(sourceBin, name == "wrong_hash");
        else if (name == "changed_candidate")
            changedCandidate(sourceBin);
        else if (name == "restart")
            restart(sourceBin);
        else if (name == "reopen")
            reopen(sourceBin, args[3], args[4]);
        else
            throw std::runtime_error("unknown product case");
        std::cout << "Platform product passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
