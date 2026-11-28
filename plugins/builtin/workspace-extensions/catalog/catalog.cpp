/** @file catalog.cpp
 * @brief Atomically publish the desired per-user extensions. Running modules are owned by their hosts.
 */
#include "package_p.hpp"
#include <eve/module/install.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace eve::extensions {
namespace {
constexpr int maxIndexBytes = 32 * 1024 * 1024;
constexpr int writerWaitMs = 1000;
const QString indexName = QStringLiteral("extensions.json");
struct Index {
    QMap<QString, Package> current;
    // A released module/version cannot acquire a different payload even after an uninstall.
    QJsonObject releases;
    QSet<QString> retired;
};

ErrorInfo indexError(const QString& message, ErrorCode code = ErrorCode::ConfigInvalid)
{
    auto result = makeError(code, message.toStdString());
    result.phase = "extensions.catalog";
    return result;
}

enum class ReleaseFields { Current, BeforeResources };

QJsonObject serialize(const Package& p, ReleaseFields fields = ReleaseFields::Current)
{
    QJsonObject json{{"formatVersion", 1}, {"kind", p.kind}, {"name", p.name}, {"publisher", p.publisher},
        {"description", p.description}, {"defaultConfig", QJsonDocument::fromJson(p.defaultConfig.toUtf8()).object()},
        {"moduleId", p.moduleId}, {"pluginId", p.pluginId}, {"version", p.version}, {"sha256", p.sha256},
        {"packageHash", p.packageHash}, {"icon", QString::fromLatin1(p.icon.toBase64())}};
    // Omit absent metadata to retain the fingerprints of previously published releases.
    for (const auto& member : detail::markdownMembers)
        if (!(p.*member.text).isEmpty()) json.insert(member.catalogKey, p.*member.text);
    if (fields == ReleaseFields::BeforeResources) return json;
    if (!p.categories.isEmpty()) json.insert("categories", QJsonArray::fromStringList(p.categories));
    if (!p.homepage.isEmpty()) json.insert("homepage", p.homepage);
    if (!p.repository.isEmpty()) json.insert("repository", p.repository);
    if (!p.issues.isEmpty()) json.insert("bugs", p.issues);
    if (!p.license.isEmpty()) json.insert("license", p.license);
    return json;
}

QString fingerprint(const Package& p, ReleaseFields fields = ReleaseFields::Current)
{
    auto json = serialize(p, fields);
    json.remove("packageHash"); // A ZIP timestamp alone must not change release identity.
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(json).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

QString releaseKey(const Package& p) { return p.moduleId + '/' + p.version; }
bool digest(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    return pattern.match(value).hasMatch();
}

Result<Package> deserialize(const QJsonObject& object, const QDir& root)
{
    Package p;
    const auto read = detail::readPresentation(object, p);
    if (!read.hasValue()) return Result<Package>::failure(read.error());
    for (const auto& member : detail::markdownMembers) {
        if (!object.contains(member.catalogKey)) continue;
        const auto text = detail::decodeMarkdown(object.value(member.catalogKey).toString().toUtf8(), member);
        if (!text.hasValue()) return Result<Package>::failure(text.error());
        p.*member.text = text.value();
    }
    p.moduleId = object.value("moduleId").toString();
    p.pluginId = object.value("pluginId").toString();
    p.version = object.value("version").toString();
    p.packageHash = object.value("packageHash").toString();
    p.sha256 = object.value("sha256").toString();
    static const QRegularExpression id(QStringLiteral("\\A[A-Za-z][A-Za-z0-9_-]*(?:\\.[A-Za-z0-9_-]+)*\\z"));
    static const QRegularExpression version(QStringLiteral("\\A(?:0|[1-9][0-9]*)(?:\\.(?:0|[1-9][0-9]*)){2}\\z"));
    if (!digest(p.sha256) || !digest(p.packageHash) || p.moduleId.size() > 128 || p.pluginId.size() > 128 ||
        !id.match(p.moduleId).hasMatch() || !id.match(p.pluginId).hasMatch() || p.version.size() > 64 ||
        !version.match(p.version).hasMatch() || isReservedIdentity(p.moduleId, {}) ||
        (isReservedIdentity({}, p.pluginId) && !isLegacyPluginConflict(p.pluginId)))
        return Result<Package>::failure(indexError("Invalid extension identity in the catalog."));
    const auto encoded = object.value("icon").toString().toLatin1();
    if (!encoded.isEmpty()) {
        p.icon = QByteArray::fromBase64(encoded);
        if (!detail::validIcon(p.icon) || p.icon.toBase64() != encoded)
            return Result<Package>::failure(indexError("Invalid icon in the extension catalog."));
    }
    if (object.contains("packageBytes")) {
        p.packageBytes = object.value("packageBytes").toInt(-1);
        if (p.packageBytes <= 0)
            return Result<Package>::failure(indexError("Invalid archive size in the extension catalog."));
    }
    if (object.contains("installedAt")) {
        p.installedAt = QDateTime::fromString(object.value("installedAt").toString(), Qt::ISODateWithMs);
        if (!p.installedAt.isValid() || p.installedAt.timeSpec() != Qt::UTC)
            return Result<Package>::failure(indexError("Invalid UTC installation time in the extension catalog."));
    }
    // Only a validated digest, never a path from mutable catalog JSON, forms the artifact location.
    p.manifestPath = root.filePath("artifacts/" + p.sha256 + "/module.json");
    return Result<Package>::success(std::move(p));
}

Result<Index> load(const QDir& root)
{
    using Output = Result<Index>;
    if (!root.isAbsolute()) return Output::failure(indexError("An absolute extension directory is required."));
    QFile file(root.filePath(indexName));
    if (!file.exists()) return Output::success({});
    if (!file.open(QIODevice::ReadOnly)) return Output::failure(indexError(file.errorString(), ErrorCode::ConfigIo));
    const auto bytes = file.read(maxIndexBytes + 1);
    if (bytes.size() > maxIndexBytes || file.error() != QFileDevice::NoError)
        return Output::failure(indexError("Could not read extension catalog within its size limit."));
    const auto json = QJsonDocument::fromJson(bytes).object();
    if (json.value("formatVersion") != QJsonValue(1) || !json.value("extensions").isArray() ||
        !json.value("releases").isObject()) return Output::failure(indexError("Invalid extension catalog."));
    Index index;
    index.releases = json.value("releases").toObject();
    if (json.contains("retired") && !json.value("retired").isArray())
        return Output::failure(indexError("Invalid retired artifact list."));
    for (const auto item : json.value("retired").toArray()) {
        const auto hash = item.toString();
        if (!digest(hash)) return Output::failure(indexError("Invalid retired artifact digest."));
        index.retired.insert(hash);
    }
    QSet<QString> plugins;
    for (const auto entry : json.value("extensions").toArray()) {
        auto parsed = deserialize(entry.toObject(), root);
        if (!parsed.hasValue()) return Output::failure(parsed.error());
        auto p = std::move(parsed).value();
        if (index.current.contains(p.moduleId) || plugins.contains(p.pluginId) || index.retired.contains(p.sha256) ||
            index.releases.value(releaseKey(p)).toString() != fingerprint(p))
            return Output::failure(indexError("Inconsistent identity or release record in the extension catalog."));
        plugins.insert(p.pluginId);
        index.current.insert(p.moduleId, p);
    }
    return Output::success(std::move(index));
}

Result<void> save(const QDir& root, const Index& index)
{
    QJsonArray records;
    for (const auto& p : index.current) {
        auto record = serialize(p);
        // Local installation facts must not change release identity.
        if (p.packageBytes > 0) record.insert("packageBytes", static_cast<double>(p.packageBytes));
        if (p.installedAt.isValid()) record.insert("installedAt", p.installedAt.toString(Qt::ISODateWithMs));
        records.append(record);
    }
    QJsonArray retired;
    for (const auto& hash : index.retired) retired.append(hash);
    const auto bytes = QJsonDocument(QJsonObject{{"formatVersion", 1}, {"extensions", records},
                                                 {"releases", index.releases}, {"retired", retired}}).toJson(QJsonDocument::Compact);
    QSaveFile output(root.filePath(indexName));
    if (bytes.size() > maxIndexBytes || !output.open(QIODevice::WriteOnly) ||
        output.write(bytes) != bytes.size() || !output.commit())
        return Result<void>::failure(indexError("Cannot atomically publish the extension catalog.", ErrorCode::ConfigIo));
    return Result<void>::success();
}

void collect(const QDir& root, Index& index)
{
    // The queue is durable before the first deletion. Losing this housekeeping write only retries a missing hash.
    bool changed = false;
    for (auto it = index.retired.begin(); it != index.retired.end();) {
        if (detail::reclaimArtifact(root.filePath("artifacts"), *it)) {
            it = index.retired.erase(it);
            changed = true;
        } else ++it;
    }
    if (changed) (void)save(root, index);
}
}

QString Catalog::defaultRoot()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString{} : QDir(base).filePath("extensions");
}

Catalog::Catalog(QString root) : root_(QDir::cleanPath(root)) {}

Result<QVector<Package>> Catalog::installed() const
{
    const QDir root(root_);
    const auto index = load(root);
    if (!index.hasValue()) return Result<QVector<Package>>::failure(index.error());
    if (!index.value().retired.isEmpty()) {
        QLockFile lock(root.filePath("catalog.lock"));
        if (lock.tryLock()) {
            auto latest = load(root);
            if (!latest.hasValue()) return Result<QVector<Package>>::failure(latest.error());
            collect(root, latest.value());
            return Result<QVector<Package>>::success(latest.value().current.values().toVector());
        }
    }
    return Result<QVector<Package>>::success(index.value().current.values().toVector());
}

Result<Package> Catalog::install(const QString& file, const QString& expectedPackageHash, const QString& expectedModuleId,
                                 std::optional<QString> expectedInstalledHash)
{
    using Output = Result<Package>;
    QTemporaryDir staging;
    if (!staging.isValid()) return Output::failure(indexError("Cannot create staging directory.", ErrorCode::ConfigIo));
    auto unpacked = detail::unpack(file, staging.path());
    if (!unpacked.hasValue()) return Output::failure(unpacked.error());
    auto p = std::move(unpacked).value();
    if (p.packageHash != expectedPackageHash) return Output::failure(indexError("Package changed since preview."));
    if (!expectedModuleId.isEmpty() && p.moduleId != expectedModuleId)
        return Output::failure(indexError("The selected package has a different module ID."));

    const QDir root(root_);
    if (!root.isAbsolute() || !root.mkpath("artifacts"))
        return Output::failure(indexError("Cannot open extension storage.", ErrorCode::ConfigIo));
    QLockFile lock(root.filePath("catalog.lock"));
    if (!lock.tryLock(writerWaitMs)) return Output::failure(indexError("Extension catalog is busy.", ErrorCode::ConfigIo));
    auto loaded = load(root);
    if (!loaded.hasValue()) return Output::failure(loaded.error());
    auto index = std::move(loaded).value();
    const auto selected = index.current.constFind(p.moduleId);
    if (expectedInstalledHash && *expectedInstalledHash !=
            (selected == index.current.cend() ? QString{} : selected->packageHash))
        return Output::failure(indexError(QStringLiteral("已安装的版本在预览后发生变化，请重新核对安装包。"), ErrorCode::ConfigConflict));
    const auto identity = fingerprint(p);
    const auto published = index.releases.value(releaseKey(p));
    if (!published.isUndefined() && published.toString() != identity) {
        // load() has already checked the selected record against this release fingerprint.
        // An identical archive may now expose fields that the previous reader didn't record.
        const bool sameLegacyRelease = selected != index.current.cend() && selected->version == p.version &&
            selected->packageHash == p.packageHash && published.toString() == fingerprint(p, ReleaseFields::BeforeResources);
        if (!sameLegacyRelease)
            return Output::failure(indexError("This module version was already published with different contents."));
    }
    for (const auto& current : index.current) {
        if (current.moduleId == p.moduleId && (current.pluginId != p.pluginId || current.kind != p.kind))
            return Output::failure(indexError("An update cannot change the extension's plugin ID or kind."));
        if (current.pluginId == p.pluginId && current.moduleId != p.moduleId)
            return Output::failure(indexError("The plugin ID already belongs to another installed extension."));
    }
    const auto store = root.filePath("artifacts");
    const auto install = [&] { return module::installModuleArtifact(staging.path().toStdString(), "module.json", store.toStdString()); };
    const auto artifact = [&] {
        auto first = install();
        if (first.hasValue() || !index.current.contains(p.moduleId) || index.current.value(p.moduleId).sha256 != p.sha256)
            return first;
        const auto& phase = first.error().phase;
        if (phase != "module.install.conflict" && phase != "module.hash" && phase != "module.manifest")
            return first;
        QFile manifest(QDir(staging.path()).filePath("module.json"));
        if (!manifest.open(QIODevice::ReadOnly)) return first;
        const auto library = QJsonDocument::fromJson(manifest.readAll()).object().value("library").toString();
        if (library.isEmpty() || !detail::preserveDamagedArtifact(store, p.sha256, library)) return first;
        return install();
    }();
    if (!artifact.hasValue()) return Output::failure(artifact.error());
    p.manifestPath = QString::fromStdString(artifact.value().artifact().manifestPath());
    const auto previous = index.current.constFind(p.moduleId);
    const bool sameArchive = previous != index.current.cend() && previous->packageHash == p.packageHash;
    p.installedAt = sameArchive && previous->installedAt.isValid() ? previous->installedAt : QDateTime::currentDateTimeUtc();
    if (previous != index.current.cend() && previous->sha256 != p.sha256) index.retired.insert(previous->sha256);
    index.retired.remove(p.sha256);
    index.releases.insert(releaseKey(p), identity);
    index.current.insert(p.moduleId, p);
    const auto committed = save(root, index);
    if (!committed.hasValue()) return Output::failure(committed.error());
    collect(root, index);
    return Output::success(std::move(p));
}

Result<void> Catalog::remove(const QString& moduleId, const QString& expectedPackageHash)
{
    const QDir root(root_);
    if (!root.isAbsolute()) return Result<void>::failure(indexError("An absolute extension directory is required."));
    if (!root.exists()) return Result<void>::success();
    QLockFile lock(root.filePath("catalog.lock"));
    if (!lock.tryLock(writerWaitMs)) return Result<void>::failure(indexError("Extension catalog is busy.", ErrorCode::ConfigIo));
    auto loaded = load(root);
    if (!loaded.hasValue()) return Result<void>::failure(loaded.error());
    auto index = std::move(loaded).value();
    auto current = index.current.find(moduleId);
    if (current == index.current.end()) return Result<void>::success();
    if (current->packageHash != expectedPackageHash)
        return Result<void>::failure(indexError(QStringLiteral("已安装的版本发生变化，请核对当前版本后重新确认卸载。"),
                                                ErrorCode::ConfigConflict));
    // Commit desired membership first. Other workspaces can keep their mapped image until exit.
    index.retired.insert(current->sha256);
    index.current.erase(current);
    auto committed = save(root, index);
    if (committed.hasValue()) collect(root, index);
    return committed;
}
} // namespace eve::extensions
