#include <eve/extensions/catalog.hpp>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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
    const auto pack = [&](const QString& dll, const QString& output, const QString& publisher,
                          const QString& readme = {}, const QJsonObject& fields = {}, const QString& changelog = {}) {
        QJsonObject descriptor{{"formatVersion", 1}, {"kind", "business"}, {"name", "Notebook"},
            {"publisher", publisher}, {"defaultConfig", QJsonObject{{"label", "Example"}}}};
        for (auto field = fields.begin(); field != fields.end(); ++field)
            descriptor.insert(field.key(), field.value());
        write(metadata, QJsonDocument(descriptor).toJson());
        const auto path = dir.filePath(output);
        run(args[1], {"-DLIBRARY=" + dll, "-DPACKAGE_JSON=" + metadata, "-DREADME=" + readme,
                      "-DCHANGELOG=" + changelog, "-DOUTPUT=" + path, "-P", args[2]}, dir.path());
        return path;
    };
    const auto first = pack(args[3], "first.evepkg", "Test");
    const auto readmeFile = dir.filePath("notes.md");
    const auto markdown = QStringLiteral("# 使用说明\n\n- Open the **Notebook** panel.\n");
    write(readmeFile, markdown.toUtf8());
    const auto changesFile = dir.filePath("release-notes.md");
    const auto changes = QStringLiteral("## 1.1.0\n\n- Added **notes**.\n");
    write(changesFile, changes.toUtf8());
    const QJsonObject resources{{"categories", QJsonArray{"Notes", " Workbench ", "notes"}},
        {"homepage", "https://example.org"}, {"repository", QJsonObject{{"url", "https://example.org/source"}}},
        {"bugs", QJsonObject{{"url", "https://example.org/issues"}}}, {"license", "MIT OR Apache-2.0"}};
    const auto newer = pack(args[4], "newer.evepkg", "Test", readmeFile, resources, changesFile);
    const auto documentedFirst = pack(args[3], "documented.evepkg", "Test", readmeFile);
    const auto conflicting = pack(args[3], "conflict.evepkg", "Changed publisher");
    const auto preview = require(Catalog::preview(first));
    const auto update = require(Catalog::preview(newer));
    expect(preview.readme.isEmpty() && update.readme == markdown, "preview optional package README");
    expect(preview.changelog.isEmpty() && update.changelog == changes, "preview optional release notes");
    expect(preview.categories.isEmpty() && preview.license.isEmpty() &&
        update.categories.join('|') == "Notes|Workbench" && update.repository == "https://example.org/source" &&
        update.issues == "https://example.org/issues" && update.homepage == "https://example.org" &&
        update.license == "MIT OR Apache-2.0", "preview optional package resources");
    for (const auto& fields : {QJsonObject{{"homepage", "file:///local/path"}},
                               QJsonObject{{"bugs", "https://user:pass@example.org/"}},
                               QJsonObject{{"repository", QJsonObject{{"type", "git"}}}},
                               QJsonObject{{"categories", QJsonArray{"Notes", 1}}},
                               QJsonObject{{"license", "javascript:alert(1)"}}}) {
        const auto invalid = pack(args[3], "bad-resource.evepkg", "Test", {}, fields);
        expect(!Catalog::preview(invalid).hasValue(), "reject invalid resource declaration");
    }
    expect(preview.kind == "business" && preview.version == "1.0.0" && preview.name == "Notebook" &&
        preview.manifestPath.isEmpty() && preview.defaultConfig.contains("Example") && !QFileInfo::exists(root), "preview");
    expect(preview.packageBytes == QFileInfo(first).size() && !preview.installedAt.isValid(),
           "preview reports exact archive bytes and no local installation time");
    expect(!catalog.install(newer, preview.packageHash).hasValue(), "confirmation binds exact archive bytes");
    expect(!catalog.install(first, preview.packageHash, "other.module").hasValue(), "repair binds module ID");
    const auto active = require(catalog.install(first, preview.packageHash));
    expect(QFileInfo::exists(active.manifestPath), "content-addressed installation");
    const auto installed = require(Catalog(root).installed());
    expect(installed.size() == 1 && installed.first().packageBytes == preview.packageBytes &&
           active.installedAt.isValid() && active.installedAt.timeSpec() == Qt::UTC &&
           installed.first().installedAt == active.installedAt, "persist and reopen local installation facts");

    const auto indexPath = QDir(root).filePath("extensions.json");
    auto index = QJsonDocument::fromJson(read(indexPath)).object();
    auto record = index.value("extensions").toArray().first().toObject();
    for (const auto plugin : {"Eve.Desktop", "HOST.CONFIGURATION"}) {
        auto reserved = record;
        reserved.insert("pluginId", plugin);
        index.insert("extensions", QJsonArray{reserved});
        write(indexPath, QJsonDocument(index).toJson());
        const auto rejected = catalog.installed();
        expect(!rejected.hasValue() && rejected.error().message == "Invalid extension identity in the catalog.",
               "catalog cannot claim an always-reserved identity");
    }
    const auto knownTime = QDateTime::fromString(QStringLiteral("2001-12-02T03:04:05.678Z"), Qt::ISODateWithMs);
    record.insert("installedAt", knownTime.toString(Qt::ISODateWithMs));
    index.insert("extensions", QJsonArray{record});
    write(indexPath, QJsonDocument(index).toJson());
    const auto repeated = require(catalog.install(first, preview.packageHash));
    expect(repeated.installedAt == knownTime && require(Catalog(root).installed()).size() == 1,
           "exact same archive preserves its known time");

    // Older catalog entries have the same release identity with neither local installation field.
    record.remove("installedAt");
    record.remove("packageBytes");
    index.insert("extensions", QJsonArray{record});
    write(indexPath, QJsonDocument(index).toJson());
    const auto legacy = require(Catalog(root).installed()).first();
    expect(legacy.packageBytes == -1 && !legacy.installedAt.isValid(), "legacy local installation facts stay unknown");
    const auto conflict = require(Catalog::preview(conflicting));
    expect(!catalog.install(conflicting, conflict.packageHash).hasValue(), "immutable release content");
    const auto addedReadme = require(Catalog::preview(documentedFirst));
    expect(!catalog.install(documentedFirst, addedReadme.packageHash).hasValue(), "README is part of published release");
    const auto changelogOnly = pack(args[3], "changelog.evepkg", "Test", {}, {}, changesFile);
    const auto notesOnly = require(Catalog::preview(changelogOnly));
    expect(notesOnly.readme.isEmpty() && notesOnly.changelog == changes &&
           !catalog.install(changelogOnly, notesOnly.packageHash).hasValue(), "standalone changelog changes release identity");
    const auto reclassified = pack(args[3], "resources.evepkg", "Test", {}, resources);
    const auto resourceHash = require(Catalog::preview(reclassified)).packageHash;
    expect(!catalog.install(reclassified, resourceHash).hasValue(),
           "resource declarations are part of release identity");

    // Simulate the record an earlier reader wrote for this exact resource-bearing archive.
    // All the old descriptor fields and its release fingerprint are identical to the first fixture.
    const auto earlierRoot = dir.filePath("older catalog");
    expect(dir.mkdir("older catalog"), "create isolated older catalog");
    auto earlierRecord = record;
    earlierRecord.insert("installedAt", knownTime.toString(Qt::ISODateWithMs));
    auto earlierIndex = index;
    const auto earlierFile = QDir(earlierRoot).filePath("extensions.json");
    Catalog earlier(earlierRoot);
    earlierIndex.insert("extensions", QJsonArray{});
    write(earlierFile, QJsonDocument(earlierIndex).toJson());
    expect(!earlier.install(reclassified, resourceHash).hasValue(), "historical fingerprint alone cannot migrate");
    earlierRecord.insert("packageHash", conflict.packageHash);
    earlierIndex.insert("extensions", QJsonArray{earlierRecord});
    write(earlierFile, QJsonDocument(earlierIndex).toJson());
    expect(!earlier.install(conflicting, conflict.packageHash).hasValue(), "archive hash cannot override old descriptor fields");
    earlierRecord.insert("packageHash", resourceHash);
    earlierIndex.insert("extensions", QJsonArray{earlierRecord});
    write(earlierFile, QJsonDocument(earlierIndex).toJson());
    expect(require(earlier.installed()).first().categories.isEmpty(), "legacy reader omitted resources");
    const auto migrated = require(earlier.install(reclassified, resourceHash));
    const auto migratedRecord = require(Catalog(earlierRoot).installed()).first();
    expect(migrated.installedAt == knownTime && migratedRecord.installedAt == knownTime &&
        migratedRecord.categories.join('|') == "Notes|Workbench" && migratedRecord.repository == update.repository,
        "reinstall exact original archive migrates its ignored metadata and preserves installation time");
    expect(!earlier.install(first, preview.packageHash).hasValue(), "upgraded fingerprint rejects stripped metadata");

    // A mounted workspace may continue to own the previous version's read-only module handles.
    QFile pinned(active.manifestPath);
    expect(pinned.open(QIODevice::ReadOnly), "pin the old artifact");
    const auto upgraded = require(catalog.install(newer, update.packageHash));
    expect(upgraded.version == "1.1.0" && require(catalog.installed()).first().sha256 == update.sha256, "select update");
    const auto reopened = require(Catalog(root).installed()).first();
    expect(reopened.changelog == changes, "read installed changelog after reopening catalog");
    expect(reopened.categories.join('|') == "Notes|Workbench" && reopened.homepage == update.homepage &&
        reopened.repository == update.repository && reopened.issues == update.issues && reopened.license == update.license,
        "reopen installed resources offline");
    expect(reopened.readme == markdown && reopened.packageBytes == QFileInfo(newer).size() &&
           upgraded.installedAt.isValid() && reopened.installedAt == upgraded.installedAt,
           "reopen installed README and local installation facts offline");
    expect(QFileInfo::exists(active.manifestPath), "preserve in-use artifact");
    const auto staleRemoval = catalog.remove(active.moduleId, active.packageHash);
    expect(!staleRemoval.hasValue() && staleRemoval.error().code == eve::ErrorCode::ConfigConflict,
           "reject stale uninstall as a conflict");
    pinned.close();
    require(Catalog(root).installed());
    expect(!QFileInfo::exists(active.manifestPath), "reclaim unpinned old bytes");
    const auto reselected = require(catalog.install(first, preview.packageHash, upgraded.moduleId));
    const auto selected = require(Catalog(root).installed()).first();
    expect(selected.version == preview.version && selected.packageBytes == preview.packageBytes &&
           reselected.installedAt.isValid() && selected.installedAt == reselected.installedAt,
           "reselect a previous release from its original archive");
    expect(QFileInfo::exists(reselected.manifestPath) && !QFileInfo::exists(upgraded.manifestPath),
           "version selection reinstalls the retained release from its original archive");
    require(catalog.remove(reselected.moduleId, reselected.packageHash));
    expect(require(catalog.installed()).isEmpty() && !QFileInfo::exists(reselected.manifestPath), "uninstall");
    require(catalog.remove(reselected.moduleId, reselected.packageHash));
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
    const auto malformedDocument = [&](const QString& filename, const QByteArray& contents) {
        write(dir.filePath(filename), contents);
        run(args[1], {"-E", "tar", "cf", invalid, "--format=zip", "--", "module.json", "package.json", dllName,
                      filename}, dir.path());
        return !Catalog::preview(invalid).hasValue();
    };
    for (const auto& name : {QStringLiteral("README.md"), QStringLiteral("CHANGELOG.md")}) {
        expect(malformedDocument(name, QByteArray::fromHex("61c0af")), "reject non-UTF-8 package Markdown");
        expect(malformedDocument(name, QByteArray(256 * 1024 + 1, 'a')), "bound decompressed Markdown");
    }
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
