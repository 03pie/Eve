#include <eve/extensions/catalog.hpp>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <stdexcept>
#include <string>

using eve::extensions::Catalog;
namespace {
void expect(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}
template<class T>
T require(eve::Result<T> result)
{
    if (!result.hasValue()) throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(), "write failed");
}
QByteArray read(const QString& path)
{
    QFile file(path);
    expect(file.open(QIODevice::ReadOnly), "read failed");
    return file.readAll();
}
void run(const QString& program, const QStringList& args, const QString& cwd)
{
    QProcess process;
    process.setWorkingDirectory(cwd);
    process.start(program, args);
    expect(process.waitForFinished(30000), "CMake timed out");
    if (process.exitCode() != 0) throw std::runtime_error(process.readAllStandardError().toStdString());
}
void test(const QStringList& args)
{
    QTemporaryDir temp;
    expect(temp.isValid() && args.size() == 5, "fixture arguments");
    QDir dir(temp.path());
    const auto root = dir.filePath("user catalog");
    const auto metadata = dir.filePath("package.json");
    Catalog catalog(root);
    expect(require(catalog.installed()).isEmpty() && !QFileInfo::exists(root), "read-only first launch");
    const auto pack = [&](const QString& dll, const QString& output, const QString& publisher) {
        const QJsonObject descriptor{{"formatVersion", 1}, {"kind", "business"}, {"name", "Notebook"},
            {"publisher", publisher}, {"defaultConfig", QJsonObject{{"label", "Example"}}}};
        write(metadata, QJsonDocument(descriptor).toJson());
        const auto path = dir.filePath(output);
        run(args[1], {"-DLIBRARY=" + dll, "-DPACKAGE_JSON=" + metadata, "-DOUTPUT=" + path, "-P", args[2]}, dir.path());
        return path;
    };
    const auto first = pack(args[3], "first.evepkg", "Test");
    const auto newer = pack(args[4], "newer.evepkg", "Test");
    const auto conflicting = pack(args[3], "conflict.evepkg", "Changed publisher");
    const auto preview = require(Catalog::preview(first));
    const auto update = require(Catalog::preview(newer));
    expect(preview.kind == "business" && preview.version == "1.0.0" && preview.name == "Notebook" &&
        preview.manifestPath.isEmpty() && preview.defaultConfig.contains("Example") && !QFileInfo::exists(root), "preview");
    expect(!catalog.install(newer, preview.packageHash).hasValue(), "confirmation binds exact archive bytes");
    expect(!catalog.install(first, preview.packageHash, "other.module").hasValue(), "repair binds module ID");
    const auto active = require(catalog.install(first, preview.packageHash));
    expect(QFileInfo::exists(active.manifestPath), "content-addressed installation");
    require(catalog.install(first, preview.packageHash));
    expect(require(Catalog(root).installed()).size() == 1, "idempotent installation across sessions");
    const auto conflict = require(Catalog::preview(conflicting));
    expect(!catalog.install(conflicting, conflict.packageHash).hasValue(), "immutable release content");

    // A mounted workspace may continue to own the previous version's read-only module handles.
    QFile pinned(active.manifestPath);
    expect(pinned.open(QIODevice::ReadOnly), "pin the old artifact");
    const auto upgraded = require(catalog.install(newer, update.packageHash));
    expect(upgraded.version == "1.1.0" && require(catalog.installed()).first().sha256 == update.sha256, "select update");
    expect(QFileInfo::exists(active.manifestPath), "preserve in-use artifact");
    expect(!catalog.remove(active.moduleId, active.packageHash).hasValue(), "reject stale uninstall");
    pinned.close();
    require(Catalog(root).installed());
    expect(!QFileInfo::exists(active.manifestPath), "reclaim unpinned old bytes");
    require(catalog.remove(upgraded.moduleId, upgraded.packageHash));
    expect(require(catalog.installed()).isEmpty() && !QFileInfo::exists(upgraded.manifestPath), "uninstall");
    require(catalog.remove(upgraded.moduleId, upgraded.packageHash));
    expect(!catalog.install(conflicting, conflict.packageHash).hasValue(), "release identity survives uninstall");
    const auto restored = require(catalog.install(first, preview.packageHash));
    expect(QFileInfo::exists(restored.manifestPath), "reinstall");

    // Unrecognized package contents never reach the artifact store.
    const auto invalid = dir.filePath("invalid.evepkg");
    const auto dllName = QFileInfo(args[3]).fileName();
    expect(QFile::copy(args[3], dir.filePath(dllName)), "stage fabricated fixture");
    expect(QFile::copy(QFileInfo(args[3]).dir().filePath("module.json"), dir.filePath("module.json")), "stage manifest");
    write(dir.filePath("extra.txt"), "unexpected");
    run(args[1], {"-E", "tar", "cf", invalid, "--format=zip", "--", "module.json", "package.json", dllName,
                  "extra.txt"}, dir.path());
    expect(!Catalog::preview(invalid).hasValue(), "reject unexpected member");
    write(dir.filePath(dllName), "not the declared module");
    run(args[1], {"-E", "tar", "cf", invalid, "--format=zip", "--", "module.json", "package.json", dllName}, dir.path());
    expect(!Catalog::preview(invalid).hasValue(), "verify module DLL");
    write(invalid, read(first).left(32));
    expect(!Catalog::preview(invalid).hasValue(), "reject truncated ZIP");

    // Removing a workspace-independent catalog record still works when the DLL has gone missing.
    expect(QFile::remove(restored.manifestPath), "simulate missing module");
    expect(require(catalog.installed()).first().moduleId == restored.moduleId, "keep missing artifact visible");
    auto repaired = require(catalog.install(first, preview.packageHash, preview.moduleId));
    expect(QFileInfo::exists(repaired.manifestPath), "restore a damaged installation from its reviewed package");
    expect(QFile::remove(repaired.manifestPath), "remove repaired manifest for uninstall check");
    require(catalog.remove(restored.moduleId, restored.packageHash));
    expect(require(catalog.installed()).isEmpty(), "remove missing artifact");
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try { test(app.arguments()); }
    catch (const std::exception& error) { qCritical() << error.what(); return 1; }
    return 0;
}
