/** @file manifest.cpp
 * @brief 严格声明、规范路径和流式 SHA256 预检；不打开任何动态装载器。
 */
#include "path_p.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cordis/module/manifest.hpp>
#include <initializer_list>
#include <limits>
#include <set>

namespace cordis::module {
namespace {
using detail::OpenedPath;
ErrorInfo failure(std::string phase, std::string message, ErrorCode code = ErrorCode::ConfigInvalid)
{
    auto error = makeError(code, std::move(message));
    error.phase = std::move(phase);
    return error;
}
bool validLimits(const PreflightLimits& limits)
{
    return limits.maximumManifestBytes > 0 && limits.maximumManifestBytes <= 1024 * 1024 &&
           limits.maximumLibraryBytes > 0 &&
           limits.maximumLibraryBytes <= static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
}
bool utf8(const std::string& value)
{
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    const auto bytes = QByteArray(value.data(), static_cast<int>(value.size()));
    return QString::fromUtf8(bytes).toUtf8() == bytes;
}
bool exactKeys(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    if (object.size() != static_cast<int>(keys.size()))
        return false;
    return std::all_of(keys.begin(), keys.end(), [&](const char* key) { return object.contains(QLatin1String(key)); });
}
/** @brief Qt 确认文法后检查重复键和版本整数原文，避免 DOM 覆盖或浮点舍入改变契约。 */
bool validTokens(const QByteArray& bytes)
{
    struct Frame {
        bool object;
        bool expectsKey;
        std::set<QString> keys;
    };
    std::vector<Frame> frames;
    for (int i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (c == '{' || c == '[') {
            if (frames.size() >= 16)
                return false;
            frames.push_back({c == '{', c == '{', {}});
        } else if (c == '}' || c == ']')
            frames.pop_back();
        else if (c == ',' && !frames.empty() && frames.back().object)
            frames.back().expectsKey = true;
        else if (c == '"') {
            const int start = i;
            ++i;
            while (i < bytes.size() && bytes[i] != '"') {
                if (bytes[i] == '\\')
                    ++i;
                ++i;
            }
            if (!frames.empty() && frames.back().object && frames.back().expectsKey) {
                const auto key =
                    QJsonDocument::fromJson("[" + bytes.mid(start, i - start + 1) + "]").array()[0].toString();
                if (!frames.back().keys.insert(key).second)
                    return false;
                frames.back().expectsKey = false;
            }
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const int start = i;
            while (i < bytes.size() && bytes[i] != ',' && bytes[i] != ']' && bytes[i] != '}' && bytes[i] != ' ' &&
                   bytes[i] != '\n' && bytes[i] != '\r' && bytes[i] != '\t')
                ++i;
            const auto token = bytes.mid(start, i - start);
            if (token.size() > 10 ||
                std::any_of(token.begin(), token.end(), [](char digit) { return digit < '0' || digit > '9'; }))
                return false;
            bool converted = false;
            const auto number = token.toULongLong(&converted);
            if (!converted || number > std::numeric_limits<std::uint32_t>::max())
                return false;
            --i;
        }
    }
    return frames.empty();
}
bool integer(const QJsonValue& value, std::uint32_t expected)
{
    return value.isDouble() && value.toDouble() == static_cast<double>(expected);
}
bool identifier(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z][A-Za-z0-9_-]*(?:\\.[A-Za-z0-9_-]+)*\\z"));
    return value.size() <= 128 && pattern.match(value).hasMatch();
}
bool libraryName(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z0-9_][A-Za-z0-9_.-]*\\.dll\\z"),
                                            QRegularExpression::CaseInsensitiveOption);
    if (value.size() > 128 || !pattern.match(value).hasMatch())
        return false;
    const auto base = value.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression reserved(QStringLiteral("\\A(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\\z"));
    return !reserved.match(base).hasMatch();
}
/** @brief 当前仅接受三段十进制版本；预发布/比较语义留待版本协调工作包。 */
bool version(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\z"));
    return value.size() <= 64 && pattern.match(value).hasMatch();
}
bool inside(const QString& root, const QString& path)
{
    const auto prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    // 句柄给出的最终路径必须匹配目录边界；精确匹配也保守支持启用大小写敏感的 NTFS 目录。
    return path.startsWith(prefix, Qt::CaseSensitive);
}
} // namespace

Result<ModuleManifest> parseManifest(const std::string& json, PreflightLimits limits)
{
    const auto reject = [](const char* message) {
        return Result<ModuleManifest>::failure(failure("module.manifest", message));
    };
    if (!validLimits(limits))
        return reject("invalid preflight limits (manifest limit must be 1..1048576 bytes)");
    if (json.empty() || json.size() > limits.maximumManifestBytes || !utf8(json))
        return reject("manifest size or UTF-8 is invalid");
    const auto bytes = QByteArray(json.data(), static_cast<int>(json.size()));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return reject("manifest must be one JSON object");
    if (!validTokens(bytes))
        return reject("duplicate JSON key, non-canonical uint32 number or excessive manifest depth");
    const auto root = document.object();
    if (!exactKeys(root, {"manifestVersion", "moduleId", "moduleVersion", "loader", "library", "sha256", "platform",
                          "abi", "plugins", "unloadPolicy"}))
        return reject("missing or unknown manifest field");
    if (!integer(root.value("manifestVersion"), 1))
        return reject("unsupported manifestVersion");
    if (!root.value("moduleId").isString() || !identifier(root.value("moduleId").toString()))
        return reject("invalid moduleId");
    if (!root.value("moduleVersion").isString() || !version(root.value("moduleVersion").toString()))
        return reject("moduleVersion must be three decimal components");
    if (root.value("loader") != QJsonValue(QStringLiteral("c-abi")) &&
        root.value("loader") != QJsonValue(QStringLiteral("qt-ui")))
        return reject("unsupported module loader");
    if (root.value("platform") != QJsonValue(QStringLiteral("windows-x64")))
        return reject("unsupported module platform");
    if (root.value("unloadPolicy") != QJsonValue(QStringLiteral("resident")))
        return reject("only resident policy is supported");
    if (!root.value("abi").isObject())
        return reject("abi must be an object");
    const auto abi = root.value("abi").toObject();
    if (!exactKeys(abi, {"major", "minor"}) || !integer(abi.value("major"), 1) || !integer(abi.value("minor"), 0))
        return Result<ModuleManifest>::failure(
            failure("module.manifest", "unsupported declared ABI (requires 1.0)", ErrorCode::AbiMismatch));
    const auto library = root.value("library");
    if (!library.isString() || !libraryName(library.toString()))
        return reject("library must be a non-device DLL basename");
    static const QRegularExpression digest(QStringLiteral("\\A[0-9a-fA-F]{64}\\z"));
    const auto hash = root.value("sha256");
    if (!hash.isString() || !digest.match(hash.toString()).hasMatch())
        return reject("sha256 must contain 64 hexadecimal digits");
    if (!root.value("plugins").isArray())
        return reject("plugins must be an array");
    const auto plugins = root.value("plugins").toArray();
    if (plugins.isEmpty() || plugins.size() > 64)
        return reject("plugins must contain 1..64 identifiers");
    ModuleManifest result{root.value("moduleId").toString().toStdString(),
                          root.value("moduleVersion").toString().toStdString(),
                          library.toString().toStdString(),
                          hash.toString().toLower().toStdString(),
                          {}};
    std::set<QString> seen;
    result.loader = root.value("loader").toString() == QLatin1String("qt-ui") ? ModuleLoader::QtUi : ModuleLoader::CAbi;
    for (const auto& plugin : plugins) {
        if (!plugin.isString() || !identifier(plugin.toString()) || !seen.insert(plugin.toString()).second)
            return reject("plugin identifiers must be valid and unique");
        result.plugins.push_back(plugin.toString().toStdString());
    }
    return Result<ModuleManifest>::success(std::move(result));
}

Result<PreflightArtifact> preflightModule(const std::string& pluginRoot, const std::string& manifestPath,
                                          PreflightLimits limits)
{
    const auto reject = [](const char* phase, const char* message) {
        return Result<PreflightArtifact>::failure(failure(phase, message));
    };
    if constexpr (sizeof(void*) != 8)
        return reject("module.path", "only Windows x64 preflight is supported");
    if (!validLimits(limits) || !detail::modulePathInput(pluginRoot) || !detail::modulePathInput(manifestPath))
        return reject("module.path", "invalid limits or UTF-8 path");
    const auto rootInput = QString::fromUtf8(pluginRoot.data(), static_cast<int>(pluginRoot.size()));
    if (!detail::fullyQualifiedModulePath(rootInput))
        return reject("module.path", "plugin root requires a drive-qualified path or full UNC share");
    const OpenedPath rootHandle(rootInput, true);
    const auto root = rootHandle.finalPath();
    if (root.isEmpty())
        return reject("module.path", "cannot resolve plugin root directory");
    const auto manifestInput = QString::fromUtf8(manifestPath.data(), static_cast<int>(manifestPath.size()));
    if ((manifestInput.startsWith(QLatin1Char('/')) || manifestInput.startsWith(QLatin1Char('\\')) ||
         manifestInput.contains(QLatin1Char(':'))) &&
        !detail::fullyQualifiedModulePath(manifestInput))
        return reject("module.path", "manifest path cannot depend on the current drive or drive directory");
    const QFileInfo info(QDir(root).absoluteFilePath(manifestInput));
    const OpenedPath file(info.absoluteFilePath(), false);
    if (!file.valid())
        return Result<PreflightArtifact>::failure(
            failure("module.manifest.read",
                    "cannot open stable manifest (Windows error " + std::to_string(file.error()) + ")"));
    const auto canonicalManifest = file.finalPath();
    if (canonicalManifest.isEmpty() || !inside(root, canonicalManifest))
        return reject("module.path", "manifest must resolve to a regular file within plugin root");
    if (file.size() == 0 || file.size() > limits.maximumManifestBytes)
        return reject("module.manifest.read", "manifest file budget exceeded");
    QByteArray text(static_cast<int>(file.size()), '\0');
    DWORD offset = 0;
    while (offset < static_cast<DWORD>(text.size())) {
        DWORD bytes = 0;
        if (!file.read(text.data() + offset, static_cast<DWORD>(text.size()) - offset, bytes) || bytes == 0)
            return reject("module.manifest.read", "manifest read failed or truncated");
        offset += bytes;
    }
    if (!file.unchanged())
        return reject("module.manifest.read", "manifest changed during preflight");
    auto parsed = parseManifest(std::string(text.constData(), static_cast<std::size_t>(text.size())), limits);
    if (!parsed.hasValue())
        return Result<PreflightArtifact>::failure(parsed.error());
    const QFileInfo libraryInfo(
        QDir(QFileInfo(canonicalManifest).absolutePath()).filePath(QString::fromStdString(parsed.value().library)));
    const OpenedPath library(libraryInfo.absoluteFilePath(), false);
    if (!library.valid())
        return Result<PreflightArtifact>::failure(
            failure("module.hash",
                    "cannot open stable module artifact (Windows error " + std::to_string(library.error()) + ")"));
    const auto canonicalLibrary = library.finalPath();
    if (canonicalLibrary.isEmpty() || !inside(root, canonicalLibrary))
        return reject("module.path", "library must resolve to a regular file within plugin root");
    const auto expectedSize = library.size();
    if (expectedSize == 0 || expectedSize > limits.maximumLibraryBytes)
        return reject("module.hash", "module artifact file budget exceeded");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::uint64_t read = 0;
    std::array<char, 64 * 1024> buffer{};
    while (read < expectedSize) {
        DWORD bytes = 0;
        const auto capacity = static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), expectedSize - read));
        if (!library.read(buffer.data(), capacity, bytes) || bytes == 0)
            return reject("module.hash", "module artifact read failed or truncated");
        hash.addData(buffer.data(), static_cast<int>(bytes));
        read += bytes;
    }
    if (!library.unchanged() || !file.unchanged() || library.finalPath() != canonicalLibrary ||
        file.finalPath() != canonicalManifest || rootHandle.finalPath() != root)
        return reject("module.hash", "module artifact changed during preflight");
    if (hash.result().toHex().toStdString() != parsed.value().sha256)
        return reject("module.hash", "module artifact SHA256 mismatch");
    return Result<PreflightArtifact>::success(
        PreflightArtifact(std::move(parsed).value(), canonicalManifest.toUtf8().toStdString(),
                          canonicalLibrary.toUtf8().toStdString(), static_cast<std::uint64_t>(read)));
}
} // namespace cordis::module
