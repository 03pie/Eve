/** @file qt_file.cpp
 * @brief 使用有界读取、原始字节哈希、协作锁及禁止直接写回退的 QSaveFile 完成文件事务。
 */
#include <cordis/loader/qt_file.hpp>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace cordis::loader {
namespace {
/** @brief 文件阶段错误不混同于 YAML/schema 校验；不输出配置负载。 */
ErrorInfo fileError(ErrorCode code, std::string phase, std::string message)
{
    auto error = makeError(code, std::move(message));
    error.phase = std::move(phase);
    return error;
}
/** @brief 限额兼容 Qt 5 QByteArray 的 int 长度，并为超限探测保留一字节。 */
Result<void> checkLimits(ConfigurationFileLimits limits)
{
    if (!limits.maximumBytes || limits.maximumBytes >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return Result<void>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.limits", "invalid file byte limit"));
    return Result<void>::success();
}
/** @brief 解析现有父目录，拒绝相对路径、资源路径、NUL 和备用数据流；不创建目录。 */
Result<QString> resolve(const QString& source)
{
    if (source.isEmpty() || source.contains(QChar(0)) || source.startsWith(QLatin1Char(':')) || !QDir::isAbsolutePath(source))
        return Result<QString>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.path", "configuration requires a local absolute path"));
#ifdef Q_OS_WIN
    const auto native = QDir::fromNativeSeparators(source);
    const auto drive = native.front().toUpper();
    if (native.size() < 3 || drive < QLatin1Char('A') || drive > QLatin1Char('Z') ||
        native.at(1) != QLatin1Char(':') || native.at(2) != QLatin1Char('/'))
        return Result<QString>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.path", "configuration requires a drive-qualified local path"));
#endif
    const QFileInfo info(source);
    const auto name = info.fileName();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char(':')))
        return Result<QString>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.path", "invalid configuration file name"));
#ifdef Q_OS_WIN
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return Result<QString>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.path", "ambiguous Windows configuration file name"));
    const auto stem = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    const bool portDigit = stem.size() == 4 && ((stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9')) ||
        stem.at(3) == QChar(0x00b9) || stem.at(3) == QChar(0x00b2) || stem.at(3) == QChar(0x00b3));
    if (stem == QStringLiteral("CON") || stem == QStringLiteral("PRN") || stem == QStringLiteral("AUX") ||
        stem == QStringLiteral("NUL") || stem == QStringLiteral("CONIN$") || stem == QStringLiteral("CONOUT$") ||
        (portDigit && (stem.startsWith(QStringLiteral("COM")) || stem.startsWith(QStringLiteral("LPT")))))
        return Result<QString>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.path", "reserved Windows device name"));
#endif
    const auto parent = info.dir().canonicalPath();
    if (parent.isEmpty())
        return Result<QString>::failure(fileError(ErrorCode::ConfigIo, "loader.file.path", "configuration parent directory is unavailable"));
    return Result<QString>::success(QDir(parent).filePath(name));
}
/** @brief 同一路径内容版本忽略修改时间，原文或存在性改变都会产生冲突。 */
bool sameVersion(const ConfigurationFileVersion& left, const ConfigurationFileVersion& right)
{
    return left.path == right.path && left.exists == right.exists && left.sha256 == right.sha256;
}
/** @brief 从普通文件读取实际原文，拒绝符号链接和非普通文件，不对内容进行隐式修复。 */
Result<ConfigurationFileSnapshot> read(const QString& path, ConfigurationFileLimits limits)
{
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigIo, "loader.file.read", "configuration is not a regular non-symlink file"));
    ConfigurationFileSnapshot snapshot{{path.toUtf8().toStdString(), false, {}}, {}};
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        // Qt 的 OpenError 不区分缺失和权限错误，使用非抛出状态查询确认缺失。
        std::error_code error;
#ifdef Q_OS_WIN
        const auto status = std::filesystem::symlink_status(std::filesystem::path(path.toStdWString()), error);
#else
        const auto status = std::filesystem::symlink_status(std::filesystem::u8path(path.toUtf8().toStdString()), error);
#endif
        if (status.type() == std::filesystem::file_type::not_found &&
            (!error || error == std::errc::no_such_file_or_directory))
            return Result<ConfigurationFileSnapshot>::success(std::move(snapshot));
        return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigIo, "loader.file.read", "cannot open configuration file"));
    }
    const auto size = input.size();
    if (size < 0)
        return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigIo, "loader.file.read", "cannot query configuration file size"));
    if (static_cast<quint64>(size) > limits.maximumBytes)
        return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigInvalid, "loader.file.read", "configuration file byte limit exceeded"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char, 65536> buffer{};
    for (;;) {
        const auto remaining = limits.maximumBytes - snapshot.text.size();
        const auto request = std::min(buffer.size(), remaining + 1);
        const auto count = input.read(buffer.data(), static_cast<qint64>(request));
        if (count < 0 || (count == 0 && input.error() != QFileDevice::NoError))
            return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigIo, "loader.file.read", "cannot read configuration file"));
        if (count == 0) break;
        if (static_cast<quint64>(count) > remaining)
            return Result<ConfigurationFileSnapshot>::failure(fileError(ErrorCode::ConfigInvalid, "loader.file.read", "configuration file byte limit exceeded"));
        snapshot.text.append(buffer.data(), static_cast<std::size_t>(count));
        hash.addData(buffer.data(), static_cast<int>(count));
    }
    snapshot.version.exists = true;
    snapshot.version.sha256 = hash.result().toHex().toStdString();
    return Result<ConfigurationFileSnapshot>::success(std::move(snapshot));
}
/** @brief 返回被观察到的冲突原文，用户目标仍由调用方持有，不自动采用外部内容。 */
Result<ConfigurationFileCommit> conflict(ConfigurationFileSnapshot observed)
{
    return Result<ConfigurationFileCommit>::success({FileCommitOutcome::Conflict, std::move(observed),
        fileError(ErrorCode::ConfigConflict, "loader.file.compare", "configuration changed on disk")});
}
} // namespace

/** @brief 文件读取不依赖应用对象、scheduler 或 GUI，可由宿主安排到专用线程。 */
Result<ConfigurationFileSnapshot> readConfigurationFile(const QString& path, ConfigurationFileLimits limits)
{
    auto checked = checkLimits(limits);
    if (!checked.hasValue()) return Result<ConfigurationFileSnapshot>::failure(checked.error());
    auto resolved = resolve(path);
    if (!resolved.hasValue()) return Result<ConfigurationFileSnapshot>::failure(resolved.error());
    return read(resolved.value(), limits);
}
/** @brief 所有写入验证先于创建临时文件；提交前再次检查磁盘，失败由 RAII 丢弃临时文件。 */
Result<ConfigurationFileCommit> commitConfigurationFile(const QString& path, const ValidatedCandidate& target,
    const ConfigurationFileVersion& expected, ConfigurationFileLimits limits)
{
    auto checked = checkLimits(limits);
    if (!checked.hasValue()) return Result<ConfigurationFileCommit>::failure(checked.error());
    if (!target.source().isValid())
        return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::ConfigInvalid, "loader.file.target", "candidate was moved from"));
    const auto& text = target.source().normalizedYaml();
    if (text.size() > limits.maximumBytes)
        return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::ConfigInvalid, "loader.file.target", "configuration output byte limit exceeded"));
    auto resolved = resolve(path);
    if (!resolved.hasValue()) return Result<ConfigurationFileCommit>::failure(resolved.error());
    const auto& destination = resolved.value();
    const auto nativePath = destination.toUtf8().toStdString();
    const bool validHash = expected.sha256.size() == 64 && std::all_of(expected.sha256.begin(), expected.sha256.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
    if (expected.path != nativePath || (expected.exists ? !validHash : !expected.sha256.empty()))
        return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::InvalidArgument, "loader.file.baseline", "invalid or differently bound disk baseline"));
    QLockFile lock(destination + QStringLiteral(".cordis.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return Result<ConfigurationFileCommit>::failure(fileError(lock.error() == QLockFile::LockFailedError ? ErrorCode::ConfigConflict : ErrorCode::ConfigIo,
            "loader.file.lock", "configuration write lock is unavailable"));
    auto observed = read(destination, limits);
    if (!observed.hasValue()) return Result<ConfigurationFileCommit>::failure(observed.error());
    if (!sameVersion(expected, observed.value().version)) return conflict(std::move(observed).value());
    if (observed.value().version.exists && observed.value().text == text)
        return Result<ConfigurationFileCommit>::success({FileCommitOutcome::Unchanged, std::move(observed).value(), {}});
    // 提交后只移动已准备的值，避免提交成功后才复制整份配置和分配报告。
    ConfigurationFileCommit saved{FileCommitOutcome::Committed,
        {{nativePath, true, QCryptographicHash::hash(QByteArray::fromRawData(text.data(), static_cast<int>(text.size())), QCryptographicHash::Sha256).toHex().toStdString()}, text}, {}};
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::ConfigIo, "loader.file.write", "cannot create configuration temporary file"));
    std::size_t written = 0;
    while (written < text.size()) {
        const auto count = output.write(text.data() + written, static_cast<qint64>(text.size() - written));
        if (count <= 0) {
            output.cancelWriting();
            return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::ConfigIo, "loader.file.write", "cannot write complete configuration"));
        }
        written += static_cast<std::size_t>(count);
    }
    auto beforeCommit = read(destination, limits);
    if (!beforeCommit.hasValue()) return Result<ConfigurationFileCommit>::failure(beforeCommit.error());
    if (!sameVersion(expected, beforeCommit.value().version)) {
        output.cancelWriting();
        return conflict(std::move(beforeCommit).value());
    }
    if (!output.commit())
        return Result<ConfigurationFileCommit>::failure(fileError(ErrorCode::ConfigIo, "loader.file.commit", "configuration temporary file commit failed"));
    return Result<ConfigurationFileCommit>::success(std::move(saved));
}
} // namespace cordis::loader
