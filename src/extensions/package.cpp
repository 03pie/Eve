/** @file package.cpp
 * @brief 离线包预检。解包只向私有目录写入模块契约明确指定的两个文件。
 */
#include "package_p.hpp"
#include <eve/module/qt_manifest.hpp>
#include <miniz.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMap>
#include <QTemporaryDir>
#include <QtEndian>

namespace eve::extensions::detail {
namespace {
constexpr int maxManifest = 64 * 1024;
constexpr int maxIcon = 256 * 1024;
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
        if (!open_ || count < 3 || count > 4)
            return Result<void>::failure(packageError("Expected 3 or 4 regular files in an Eve ZIP package."));
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat{};
            const auto nameSize = mz_zip_reader_get_filename(&zip_, i, nullptr, 0);
            if (!mz_zip_reader_file_stat(&zip_, i, &stat) || nameSize < 2 || nameSize > 256 ||
                !stat.m_is_supported || stat.m_is_directory || stat.m_is_encrypted || !stat.m_uncomp_size ||
                stat.m_uncomp_size > maxLibrary || (stat.m_external_attr & 0x410u))
                return Result<void>::failure(packageError("Unsupported archive entry."));
            const auto type = (stat.m_external_attr >> 16) & 0170000u;
            if (type && type != 0100000u)
                return Result<void>::failure(packageError("Archive links and devices are not accepted."));
            QByteArray name(static_cast<int>(nameSize), '\0');
            mz_zip_reader_get_filename(&zip_, i, name.data(), nameSize);
            name.chop(1);
            if (name.contains('\0') || name.contains('/') || name.contains('\\') || name.contains(':') ||
                entries.contains(name) || !mz_zip_validate_file(&zip_, i, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY))
                return Result<void>::failure(packageError("Invalid or duplicate ZIP entry."));
            entries.insert(name, {i, stat.m_uncomp_size});
        }
        return Result<void>::success();
    }
    Result<QByteArray> read(const QByteArray& name, int limit)
    {
        auto entry = entries.constFind(name);
        if (entry == entries.cend() || entry->bytes > static_cast<mz_uint64>(limit))
            return Result<QByteArray>::failure(packageError("Missing or oversized " + QString::fromUtf8(name)));
        QByteArray data(static_cast<int>(entry->bytes), Qt::Uninitialized);
        if (!mz_zip_reader_extract_to_mem(&zip_, entry->index, data.data(), static_cast<size_t>(data.size()), 0))
            return Result<QByteArray>::failure(packageError("Invalid compressed entry or CRC."));
        return Result<QByteArray>::success(std::move(data));
    }
    Result<void> writeLibrary(const QByteArray& name, QFile& file)
    {
        const auto entry = entries.constFind(name);
        if (entry == entries.cend() || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return Result<void>::failure(packageError("Cannot stage the declared library."));
        auto write = [](void* opaque, mz_uint64 offset, const void* bytes, size_t size) -> size_t {
            auto& stream = *static_cast<QFile*>(opaque);
            if (stream.pos() < 0 || static_cast<mz_uint64>(stream.pos()) != offset ||
                offset > maxLibrary || size > maxLibrary - offset)
                return 0;
            return stream.write(static_cast<const char*>(bytes), static_cast<qint64>(size)) ==
                static_cast<qint64>(size) ? size : 0;
        };
        if (!mz_zip_reader_extract_to_callback(&zip_, entry->index, write, &file, 0) || !file.flush())
            return Result<void>::failure(packageError("Cannot extract the declared library."));
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
bool protectedIdentity(const Package& p)
{
    return p.moduleId.compare("org.eve.desktop", Qt::CaseInsensitive) == 0 ||
        p.moduleId.compare("builtin", Qt::CaseInsensitive) == 0 ||
        p.moduleId.compare("extension", Qt::CaseInsensitive) == 0 ||
        p.pluginId.compare("eve.desktop", Qt::CaseInsensitive) == 0 ||
        p.pluginId.compare("host.configuration", Qt::CaseInsensitive) == 0;
}
Result<void> readPresentation(const QJsonObject& json, Package& p)
{
    if (json.value("formatVersion") != QJsonValue(1))
        return Result<void>::failure(packageError("Unsupported package.json formatVersion."));
    p.kind = json.value("kind").toString();
    p.name = json.value("name").toString();
    p.publisher = json.value("publisher").toString();
    p.description = json.value("description").toString();
    if ((p.kind != "business" && p.kind != "desktop") || !validText(p.name, 128) ||
        !validText(p.publisher, 128) || (json.contains("description") &&
        (!json.value("description").isString() || (!p.description.isEmpty() && !validText(p.description, 2048)))) ||
        (json.contains("defaultConfig") && !json.value("defaultConfig").isObject()))
        return Result<void>::failure(packageError("Invalid extension type, display metadata or default configuration."));
    p.defaultConfig = QString::fromUtf8(QJsonDocument(json.value("defaultConfig").toObject()).toJson(QJsonDocument::Compact));
    return Result<void>::success();
}

Result<Package> unpack(const QString& path, const QString& destination)
{
    using ResultType = Result<Package>;
    QFile file(path);
    if (!path.endsWith(".evepkg", Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly) ||
        file.size() < 1 || file.size() > maxPackage)
        return ResultType::failure(packageError("Cannot read package or size exceeds the package limit."));
    const auto bytes = file.read(maxPackage + 1);
    if (bytes.size() != file.size() || bytes.size() > maxPackage || file.error() != QFileDevice::NoError)
        return ResultType::failure(packageError("Package changed or could not be read."));
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
        return ResultType::failure(packageError("An Eve package requires one resident Qt module with one plugin."));
    p.pluginId = QString::fromStdString(module.plugins.front());
    p.moduleId = QString::fromStdString(module.moduleId);
    if (protectedIdentity(p))
        return ResultType::failure(packageError("This identity belongs to the platform."));
    p.version = QString::fromStdString(module.moduleVersion);
    p.sha256 = QString::fromStdString(module.sha256);
    p.packageHash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const auto library = QByteArray::fromStdString(module.library);
    if (!archive.entries.contains(library) || (archive.entries.size() == 4 && !archive.entries.contains("icon.png")))
        return ResultType::failure(packageError("Package contents differ from the declared payload."));
    if (archive.entries.contains("icon.png")) {
        auto icon = archive.read("icon.png", maxIcon);
        if (!icon.hasValue()) return ResultType::failure(icon.error());
        if (!validIcon(icon.value())) return ResultType::failure(packageError("Expected a PNG icon no larger than 512 x 512."));
        p.icon = std::move(icon).value();
    }
    const QDir dir(destination);
    QFile json(dir.filePath("module.json"));
    if (!json.open(QIODevice::WriteOnly | QIODevice::NewOnly) || json.write(manifest.value()) != manifest.value().size() || !json.flush())
        return ResultType::failure(packageError("Cannot stage module.json.", ErrorCode::ConfigIo));
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
Result<Package> Catalog::preview(const QString& file)
{
    QTemporaryDir dir;
    if (!dir.isValid()) return Result<Package>::failure(detail::packageError("Cannot create a private package staging directory.", ErrorCode::ConfigIo));
    return detail::unpack(file, dir.path());
}
}
