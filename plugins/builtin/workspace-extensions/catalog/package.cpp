/** @file package.cpp
 * @brief 离线包预检。解包只向私有目录写入模块契约明确指定的两个文件。
 */
#include "desktop_text.hpp"
#include "package_p.hpp"
#include "builtin_ids.hpp"
#include <eve/module/qt_manifest.hpp>
#include <miniz.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEndian>

namespace eve::extensions::detail {
namespace {
constexpr int maxManifest = 64 * 1024;
constexpr int maxIcon = 256 * 1024;
constexpr int maxMarkdown = 256 * 1024;
constexpr int maxLibrary = 256 * 1024 * 1024;
constexpr int maxPackage = maxLibrary + 2 * 1024 * 1024;
struct Entry { mz_uint index; mz_uint64 bytes; };

class Archive {
public:
    explicit Archive(const QByteArray& bytes)
        : open_(mz_zip_reader_init_mem(&zip_, bytes.constData(), static_cast<size_t>(bytes.size()), 0) != 0) {}
    ~Archive() { if (open_) mz_zip_reader_end(&zip_); }
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;
    Result<void> index()
    {
        const auto count = mz_zip_reader_get_num_files(&zip_);
        if (!open_ || count < 3 || count > 6)
            return Result<void>::failure(packageError(desktopText("extensions.expectedModuleFilesWithOptionalIconPng")));
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat{};
            const auto nameSize = mz_zip_reader_get_filename(&zip_, i, nullptr, 0);
            if (!mz_zip_reader_file_stat(&zip_, i, &stat) || nameSize < 2 || nameSize > 256 ||
                !stat.m_is_supported || stat.m_is_directory || stat.m_is_encrypted || !stat.m_uncomp_size ||
                stat.m_uncomp_size > maxLibrary || (stat.m_external_attr & 0x410u))
                return Result<void>::failure(packageError(desktopText("extensions.unsupportedArchiveEntry")));
            const auto type = (stat.m_external_attr >> 16) & 0170000u;
            if (type && type != 0100000u)
                return Result<void>::failure(packageError(desktopText("extensions.archiveLinksAndDevicesAreNotAccepted")));
            QByteArray name(static_cast<int>(nameSize), '\0');
            mz_zip_reader_get_filename(&zip_, i, name.data(), nameSize);
            name.chop(1);
            if (name.contains('\0') || name.contains('/') || name.contains('\\') || name.contains(':') ||
                entries.contains(name) || !mz_zip_validate_file(&zip_, i, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY))
                return Result<void>::failure(packageError(desktopText("extensions.invalidOrDuplicateZIPEntry")));
            entries.insert(name, {i, stat.m_uncomp_size});
        }
        return Result<void>::success();
    }
    Result<QByteArray> read(const QByteArray& name, int limit)
    {
        auto entry = entries.constFind(name);
        if (entry == entries.cend() || entry->bytes > static_cast<mz_uint64>(limit))
            return Result<QByteArray>::failure(packageError(desktopText("extensions.missingOrOversized") + QString::fromUtf8(name)));
        QByteArray data(static_cast<int>(entry->bytes), Qt::Uninitialized);
        if (!mz_zip_reader_extract_to_mem(&zip_, entry->index, data.data(), static_cast<size_t>(data.size()), 0))
            return Result<QByteArray>::failure(packageError(desktopText("extensions.invalidCompressedEntryOrCRC")));
        return Result<QByteArray>::success(std::move(data));
    }
    Result<void> writeLibrary(const QByteArray& name, QFile& file)
    {
        const auto entry = entries.constFind(name);
        if (entry == entries.cend() || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return Result<void>::failure(packageError(desktopText("extensions.cannotStageTheDeclaredLibrary")));
        auto write = [](void* opaque, mz_uint64 offset, const void* bytes, size_t size) -> size_t {
            auto& stream = *static_cast<QFile*>(opaque);
            if (stream.pos() < 0 || static_cast<mz_uint64>(stream.pos()) != offset ||
                offset > maxLibrary || size > maxLibrary - offset)
                return 0;
            return stream.write(static_cast<const char*>(bytes), static_cast<qint64>(size)) ==
                static_cast<qint64>(size) ? size : 0;
        };
        if (!mz_zip_reader_extract_to_callback(&zip_, entry->index, write, &file, 0) || !file.flush())
            return Result<void>::failure(packageError(desktopText("extensions.cannotExtractTheDeclaredLibrary")));
        file.close();
        return Result<void>::success();
    }
    QMap<QByteArray, Entry> entries;
private:
    mz_zip_archive zip_{};
    bool open_;
};

bool validText(const QString& value, int limit)
{
    if (value.trimmed().isEmpty() || value.size() > limit) return false;
    for (const auto ch : value) if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format) return false;
    return true;
}

bool webLink(const QString& text)
{
    if (!validText(text, 2048)) return false;
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") &&
           !url.host().isEmpty() && !url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'));
}

bool readLink(QJsonValue value, QString& link, bool acceptsObject)
{
    if (value.isUndefined()) return true;
    if (acceptsObject && value.isObject()) value = value.toObject().value("url");
    link = value.toString();
    return webLink(link);
}

Result<void> readResources(const QJsonObject& json, Package& p)
{
    if (json.contains("categories")) {
        const auto categories = json.value("categories");
        if (!categories.isArray() || categories.toArray().size() > 32)
            return Result<void>::failure(packageError(desktopText("extensions.categoriesMustBeAnArrayOfAt")));
        for (const auto& entry : categories.toArray()) {
            const auto category = entry.toString().trimmed();
            if (!validText(entry.toString(), 64))
                return Result<void>::failure(packageError(desktopText("extensions.invalidExtensionCategory")));
            if (!p.categories.contains(category, Qt::CaseInsensitive)) p.categories.append(category);
        }
    }
    if (!readLink(json.value("homepage"), p.homepage, false) ||
        !readLink(json.value("repository"), p.repository, true) ||
        !readLink(json.value("bugs"), p.issues, true))
        return Result<void>::failure(packageError(desktopText("extensions.extensionResourceLinksMustBeHTTPS")));
    if (json.contains("license")) {
        p.license = json.value("license").toString();
        // Allow printable license identifiers and SPDX expressions, or a link to the license text.
        static const QRegularExpression expression(QStringLiteral("\\A[A-Za-z0-9(+][A-Za-z0-9 .()+-]*\\z"));
        if (!(validText(p.license, 256) && expression.match(p.license).hasMatch()) && !webLink(p.license))
            return Result<void>::failure(packageError(desktopText("extensions.licenseMustBeALicenseExpressionOr")));
    }
    return Result<void>::success();
}
}

ErrorInfo packageError(const QString& message, ErrorCode code)
{
    auto value = makeError(code, message.toStdString());
    value.phase = "extensions.package";
    return value;
}
bool validIcon(const QByteArray& data)
{
    if (data.size() < 33 || data.size() > maxIcon || !data.startsWith("\x89PNG\r\n\x1a\n") ||
        data.mid(12, 4) != "IHDR") return false;
    const auto width = qFromBigEndian<quint32>(data.constData() + 16);
    const auto height = qFromBigEndian<quint32>(data.constData() + 20);
    return width && height && width <= 512 && height <= 512;
}
Result<QString> decodeMarkdown(const QByteArray& bytes, const MarkdownMember& member)
{
    const auto name = QString::fromLatin1(member.filename);
    if (bytes.size() > maxMarkdown)
        return Result<QString>::failure(packageError(name + " exceeds 256 KiB."));
    const auto utf8 = bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes;
    const auto text = QString::fromUtf8(utf8.constData(), utf8.size());
    if (text.trimmed().isEmpty() || text.toUtf8() != utf8)
        return Result<QString>::failure(packageError(name + " must contain UTF-8 text."));
    for (const auto ch : text) {
        if (ch.category() == QChar::Other_Control && ch != '\t' && ch != '\r' && ch != '\n')
            return Result<QString>::failure(packageError(desktopText("extensions.unexpectedControlCharacterIn") + name));
    }
    return Result<QString>::success(text);
}
Result<void> readPresentation(const QJsonObject& json, Package& p)
{
    if (json.value("formatVersion") != QJsonValue(1))
        return Result<void>::failure(packageError(desktopText("extensions.unsupportedPackageJsonFormatVersion")));
    p.kind = json.value("kind").toString();
    p.name = json.value("name").toString();
    p.publisher = json.value("publisher").toString();
    p.description = json.value("description").toString();
    if ((p.kind != "business" && p.kind != "desktop") || !validText(p.name, 128) ||
        !validText(p.publisher, 128) || (json.contains("description") &&
        (!json.value("description").isString() || (!p.description.isEmpty() && !validText(p.description, 2048)))) ||
        (json.contains("defaultConfig") && !json.value("defaultConfig").isObject()))
        return Result<void>::failure(packageError(desktopText("extensions.invalidExtensionTypeDisplayMetadataOrDefault")));
    p.defaultConfig = QString::fromUtf8(QJsonDocument(json.value("defaultConfig").toObject()).toJson(QJsonDocument::Compact));
    return readResources(json, p);
}

Result<Package> unpack(const QString& path, const QString& destination)
{
    using ResultType = Result<Package>;
    QFile file(path);
    if (!path.endsWith(".evepkg", Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly) ||
        file.size() < 1 || file.size() > maxPackage)
        return ResultType::failure(packageError(desktopText("extensions.cannotReadPackageOrSizeExceedsThe")));
    const auto bytes = file.read(maxPackage + 1);
    if (bytes.size() != file.size() || bytes.size() > maxPackage || file.error() != QFileDevice::NoError)
        return ResultType::failure(packageError(desktopText("extensions.packageChangedOrCouldNotBeRead")));
    Archive archive(bytes);
    const auto indexed = archive.index();
    if (!indexed.hasValue()) return ResultType::failure(indexed.error());
    const auto descriptor = archive.read("package.json", maxManifest);
    if (!descriptor.hasValue()) return ResultType::failure(descriptor.error());
    Package p;
    const auto presentation = readPresentation(QJsonDocument::fromJson(descriptor.value()).object(), p);
    if (!presentation.hasValue()) return ResultType::failure(presentation.error());
    const auto manifest = archive.read("module.json", maxManifest);
    if (!manifest.hasValue()) return ResultType::failure(manifest.error());
    const auto parsed = module::parseManifest(manifest.value().toStdString());
    if (!parsed.hasValue()) return ResultType::failure(parsed.error());
    const auto& module = parsed.value();
    if (module.loader != module::ModuleLoader::QtUi || module.unloadPolicy != module::ModuleUnloadPolicy::Resident ||
        module.plugins.size() != 1)
        return ResultType::failure(packageError(desktopText("extensions.anEvePackageRequiresOneResidentQt")));
    p.pluginId = QString::fromStdString(module.plugins.front());
    p.moduleId = QString::fromStdString(module.moduleId);
    if (isReservedIdentity(p.moduleId, p.pluginId))
        return ResultType::failure(packageError(desktopText("extensions.thisIdentityBelongsToThePlatform")));
    p.version = QString::fromStdString(module.moduleVersion);
    p.sha256 = QString::fromStdString(module.sha256);
    p.packageHash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    p.packageBytes = bytes.size();
    const auto library = QByteArray::fromStdString(module.library);
    const bool hasIcon = archive.entries.contains("icon.png");
    int expectedMembers = 3 + int(hasIcon);
    for (const auto& member : markdownMembers) {
        if (!archive.entries.contains(member.filename)) continue;
        ++expectedMembers;
        auto bytesRead = archive.read(member.filename, maxMarkdown);
        if (!bytesRead.hasValue()) return ResultType::failure(bytesRead.error());
        auto text = decodeMarkdown(bytesRead.value(), member);
        if (!text.hasValue()) return ResultType::failure(text.error());
        p.*member.text = std::move(text).value();
    }
    if (!archive.entries.contains(library) || archive.entries.size() != expectedMembers)
        return ResultType::failure(packageError(desktopText("extensions.packageContentsDifferFromTheDeclaredPayload")));
    if (hasIcon) {
        auto icon = archive.read("icon.png", maxIcon);
        if (!icon.hasValue()) return ResultType::failure(icon.error());
        if (!validIcon(icon.value())) return ResultType::failure(packageError(desktopText("extensions.expectedAPNGIconNoLargerThan")));
        p.icon = std::move(icon).value();
    }
    const QDir dir(destination);
    QFile json(dir.filePath("module.json"));
    if (!json.open(QIODevice::WriteOnly | QIODevice::NewOnly) || json.write(manifest.value()) != manifest.value().size() || !json.flush())
        return ResultType::failure(packageError(desktopText("extensions.cannotStageModuleJson"), ErrorCode::ConfigIo));
    json.close();
    QFile dll(dir.filePath(QString::fromUtf8(library)));
    const auto extraction = archive.writeLibrary(library, dll);
    if (!extraction.hasValue()) return ResultType::failure(extraction.error());
    const auto inspected = module::inspectQtModule(destination.toStdString(), "module.json");
    if (!inspected.hasValue()) return ResultType::failure(inspected.error());
    return ResultType::success(std::move(p));
}
}

namespace eve::extensions {
bool isLegacyPluginConflict(const QString& pluginId)
{
    return pluginId.compare(QLatin1String(workbench::desktopLogPluginId), Qt::CaseInsensitive) == 0 ||
           pluginId.compare(QLatin1String(workbench::workspaceExtensionsId), Qt::CaseInsensitive) == 0;
}

bool isReservedIdentity(const QString& moduleId, const QString& pluginId, ExtensionSource source)
{
    if (moduleId.compare(QLatin1String(workbench::desktopModuleId), Qt::CaseInsensitive) == 0) return true;
    if (source == ExtensionSource::InstalledPackage &&
        (moduleId.compare(QStringLiteral("builtin"), Qt::CaseInsensitive) == 0 ||
         moduleId.compare(QStringLiteral("extension"), Qt::CaseInsensitive) == 0)) return true;
    for (const char* id : {workbench::desktopId, workbench::configurationPluginId})
        if (pluginId.compare(QLatin1String(id), Qt::CaseInsensitive) == 0) return true;
    return isLegacyPluginConflict(pluginId);
}

Result<Package> Catalog::preview(const QString& file)
{
    QTemporaryDir dir;
    if (!dir.isValid()) return Result<Package>::failure(detail::packageError(desktopText("extensions.cannotCreateAPrivatePackageStagingDirectory"), ErrorCode::ConfigIo));
    return detail::unpack(file, dir.path());
}
}
