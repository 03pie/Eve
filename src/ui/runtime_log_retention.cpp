#include "runtime_log_retention_p.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <limits>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#elif defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eve::ui::detail
{
namespace
{
constexpr qint64 maxRecordBytes = 512 * 1024;
constexpr qint64 maxProofBytes = 8 * 1024 * 1024;

bool ordinaryEntry(const QFileInfo& entry)
{
    if (entry.isSymLink() || (!entry.isDir() && !entry.isFile()))
        return false;
#ifdef Q_OS_WIN
    const auto path = QDir::toNativeSeparators(entry.absoluteFilePath());
    const DWORD flags = GetFileAttributesW(reinterpret_cast<const wchar_t*>(path.utf16()));
    return flags != INVALID_FILE_ATTRIBUTES && !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return true;
#endif
}

bool ordinaryPath(const QDir& root)
{
    auto path = QDir(root.absolutePath());
    for (;;) {
        if (!ordinaryEntry(QFileInfo(path.absolutePath())))
            return false;
        if (path.isRoot())
            return true;
        if (!path.cdUp())
            return false;
    }
}

class PinnedDirectory {
public:
    explicit PinnedDirectory(const QString& path) : path_(path)
    {
#ifdef Q_OS_WIN
        const auto native = QDir::toNativeSeparators(path);
        handle_ = CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle_ != INVALID_HANDLE_VALUE &&
            (!GetFileInformationByHandle(handle_, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
             (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#elif defined(Q_OS_UNIX)
        fd_ = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#endif
    }

    ~PinnedDirectory()
    {
#ifdef Q_OS_WIN
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#elif defined(Q_OS_UNIX)
        if (fd_ >= 0)
            ::close(fd_);
#endif
    }

    PinnedDirectory(const PinnedDirectory&) = delete;
    PinnedDirectory& operator=(const PinnedDirectory&) = delete;

    bool valid() const
    {
#ifdef Q_OS_WIN
        return handle_ != INVALID_HANDLE_VALUE;
#elif defined(Q_OS_UNIX)
        return fd_ >= 0;
#else
        return ordinaryEntry(QFileInfo(path_));
#endif
    }

    bool removeSegment(const QString& basename) const
    {
#ifdef Q_OS_UNIX
        return ::unlinkat(fd_, QFile::encodeName(basename).constData(), 0) == 0;
#else
        return QFile::remove(QDir(path_).filePath(basename));
#endif
    }

private:
    QString path_;
#ifdef Q_OS_WIN
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#elif defined(Q_OS_UNIX)
    int fd_ = -1;
#endif
};

bool runId(const QString& id)
{
    const QUuid parsed(id);
    return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces) == id;
}

quint64 segmentStart(const QString& name, const QString& prefix, const QString& suffix)
{
    if (!name.startsWith(prefix) || !name.endsWith(suffix) || name.size() != prefix.size() + 20 + suffix.size())
        return 0;
    const auto digits = name.mid(prefix.size(), 20);
    if (std::any_of(digits.cbegin(), digits.cend(), [](QChar digit) { return digit < '0' || digit > '9'; }))
        return 0;
    return digits.toULongLong();
}

bool faultMember(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^fault-events-[0-9]{20}-([0-9a-f-]{36})\\.jsonl$"));
    const auto match = pattern.match(name);
    return match.hasMatch() && runId(match.captured(1));
}

QByteArray readBounded(const QFileInfo& entry, qint64 limit)
{
    if (!ordinaryEntry(entry) || !entry.isFile() || entry.size() > limit)
        return {};
    QFile file(entry.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto data = file.read(limit + 1);
    return data.size() <= limit && file.error() == QFile::NoError ? data : QByteArray{};
}

QJsonObject objectAt(const QDir& directory, const QString& file, qint64 limit)
{
    return QJsonDocument::fromJson(readBounded(QFileInfo(directory.filePath(file)), limit)).object();
}

QDateTime closedAt(const QDir& directory, const QString& id)
{
    const auto metadata = objectAt(directory, QStringLiteral("run.json"), 8192);
    if (metadata.value("v").toInt() != 1 || metadata.value("run").toString() != id ||
        metadata.value("version").toString().isEmpty())
        return {};
    const auto started = QDateTime::fromString(metadata.value("started").toString(), Qt::ISODateWithMs);
    const auto ended = QDateTime::fromString(metadata.value("ended").toString(), Qt::ISODateWithMs);
    return started.isValid() && ended.isValid() && started.timeSpec() != Qt::LocalTime &&
                   ended.timeSpec() != Qt::LocalTime && started <= ended
               ? ended
               : QDateTime{};
}

struct RunInventory {
    QString id;
    QDateTime ended;
    QFileInfoList segments;
    qint64 bytes = 0;
    bool ownedMembers = true;
};

qint64 saturatedAdd(qint64 total, qint64 size)
{
    return total + std::min(std::max<qint64>(size, 0), std::numeric_limits<qint64>::max() - total);
}

RunInventory inventory(const QDir& directory, const QString& id)
{
    RunInventory run;
    run.id = id;
    run.ended = closedAt(directory, id);
    const auto entries = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                 QDir::Name);
    for (const auto& entry : entries) {
        const auto name = entry.fileName();
        if (!ordinaryEntry(entry) || !entry.isFile()) {
            run.ownedMembers = false;
            continue;
        }
        if (segmentStart(name, "events-", ".jsonl")) {
            run.bytes = saturatedAdd(run.bytes, entry.size());
            run.segments.push_back(entry);
        } else if (name == "run.json" || segmentStart(name, "expired-", ".json") || faultMember(name)) {
            run.bytes = saturatedAdd(run.bytes, entry.size());
        } else if (name != "active.lock") {
            run.ownedMembers = false;
        }
    }
    return run;
}

std::vector<RunInventory> scan(const QDir& root, qint64& bytes)
{
    bytes = 0;
    std::vector<RunInventory> runs;
    for (const auto& entry : root.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto id = entry.fileName();
        if (!runId(id) || !ordinaryEntry(entry))
            continue;
        auto run = inventory(QDir(entry.absoluteFilePath()), id);
        bytes = saturatedAdd(bytes, run.bytes);
        if (run.ownedMembers && run.ended.isValid() && !run.segments.empty())
            runs.push_back(std::move(run));
    }
    std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b) {
        return a.ended == b.ended ? a.id < b.id : a.ended < b.ended;
    });
    return runs;
}

bool absent(const QFileInfo& info)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(info.absoluteFilePath());
    const DWORD flags = GetFileAttributesW(reinterpret_cast<const wchar_t*>(native.utf16()));
    return flags == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
#else
    return !info.exists() && !info.isSymLink();
#endif
}

QByteArray segmentProof(const QFileInfo& entry, const QString& run, quint64 first)
{
    if (!ordinaryEntry(entry) || !entry.isFile())
        return {};
    QFile file(entry.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    std::vector<LogEventRange> ranges;
    while (!file.atEnd()) {
        const auto line = file.readLine(maxRecordBytes + 1);
        if (line.isEmpty() || !line.endsWith('\n') || file.error() != QFile::NoError)
            return {};
        const auto object = QJsonDocument::fromJson(line).object();
        const auto event = object.value("event").toString().toULongLong();
        if (object.value("v").toInt() != 1 || !event || !object.value("sequence").toString().toULongLong() ||
            object.value("source").toString().isEmpty() || !object.value("text").isString() ||
            (ranges.empty() ? event != first : event <= ranges.back().last))
            return {};
        digest.addData(line);
        if (!ranges.empty() && event - ranges.back().last == 1) {
            ranges.back().last = event;
        } else {
            if (ranges.size() >= static_cast<std::size_t>(maxProofBytes / 48))
                return {};
            ranges.push_back({event, event});
        }
    }
    if (ranges.empty())
        return {};
    QJsonArray events;
    for (const auto& range : ranges)
        events.append(QJsonArray{QString::number(range.first), QString::number(range.last)});
    return QJsonDocument(QJsonObject{{"v", 1},
                                     {"run", run},
                                     {"first", QString::number(first)},
                                     {"sha256", QString::fromLatin1(digest.result().toHex())},
                                     {"events", events}}).toJson(QJsonDocument::Compact);
}

qint64 writeProof(const QString& path, const QByteArray& proof)
{
    const QFileInfo before(path);
    if (!absent(before))
        return readBounded(before, maxProofBytes) == proof ? 0 : -1;
    if (proof.size() > maxProofBytes)
        return -1;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(proof) == proof.size() && file.commit() ? proof.size() : -1;
}

std::vector<LogEventRange> readProof(const QFileInfo& proof, const QString& run, quint64 first)
{
    const auto metadata = QJsonDocument::fromJson(readBounded(proof, maxProofBytes)).object();
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    if (metadata.value("v").toInt() != 1 || metadata.value("run").toString() != run ||
        metadata.value("first").toString().toULongLong() != first ||
        !hash.match(metadata.value("sha256").toString()).hasMatch())
        return {};
    std::vector<LogEventRange> events;
    for (const auto& encoded : metadata.value("events").toArray()) {
        const auto pair = encoded.toArray();
        if (pair.size() != 2)
            return {};
        const auto begin = pair.at(0).toString().toULongLong();
        const auto end = pair.at(1).toString().toULongLong();
        if (!begin || begin > end || (events.empty() ? begin != first : begin <= events.back().last))
            return {};
        events.push_back({begin, end});
    }
    return events;
}
} // namespace

LogRetentionResult pruneClosedLogRuns(const QDir& root, const LogRetentionPolicy& policy, const QDateTime& now)
{
    LogRetentionResult result;
    if (!now.isValid() || policy.maxAgeDays < 0 || policy.maxBytes < 0 || !ordinaryPath(root)) {
        result.warning = QString::fromUtf8("日志保留策略或归档目录不可用。");
        return result;
    }
    const PinnedDirectory pinnedRoot(root.absolutePath());
    if (!pinnedRoot.valid() || !ordinaryPath(root)) {
        result.warning = QString::fromUtf8("日志归档目录不可用。");
        return result;
    }
    const auto coordination = root.absoluteFilePath("retention.lock");
    const QFileInfo coordinationEntry(coordination);
    if (!absent(coordinationEntry) && (!ordinaryEntry(coordinationEntry) || !coordinationEntry.isFile())) {
        result.warning = QString::fromUtf8("日志清理锁不可用。");
        return result;
    }
    QLockFile maintenance(coordination);
    maintenance.setStaleLockTime(0);
    const bool locked = maintenance.tryLock(0);
    const auto runs = scan(root, result.bytesBefore);
    result.bytesAfter = result.bytesBefore;
    if (!locked) {
        if (result.bytesAfter > policy.maxBytes)
            result.warning = QString::fromUtf8("日志占用 %1 字节，清理锁暂不可用。").arg(result.bytesAfter);
        return result;
    }

    const auto cutoff = now.toUTC().addDays(-policy.maxAgeDays);
    for (const auto& candidate : runs) {
        if (candidate.ended > now || (candidate.ended > cutoff && result.bytesAfter <= policy.maxBytes))
            continue;
        const QDir directory(root.absoluteFilePath(candidate.id));
        const PinnedDirectory pinnedRun(directory.absolutePath());
        if (!pinnedRun.valid() || !ordinaryEntry(QFileInfo(directory.absolutePath())))
            continue;
        const auto activePath = directory.absoluteFilePath("active.lock");
        const QFileInfo activeEntry(activePath);
        if (!absent(activeEntry) && (!ordinaryEntry(activeEntry) || !activeEntry.isFile()))
            continue;
        QLockFile active(activePath);
        active.setStaleLockTime(0);
        if (!active.tryLock(0))
            continue;
        const auto verified = inventory(directory, candidate.id);
        if (!verified.ownedMembers || verified.ended != candidate.ended)
            continue;
        for (const auto& entry : verified.segments) {
            const bool aged = candidate.ended <= cutoff;
            if (!aged && result.bytesAfter <= policy.maxBytes)
                break;
            const auto first = segmentStart(entry.fileName(), "events-", ".jsonl");
            const auto proof = segmentProof(entry, verified.id, first);
            if (proof.isEmpty())
                continue;
            const auto marker = directory.filePath("expired-" + entry.fileName().mid(7, 20) + ".json");
            const qint64 proofBytes = writeProof(marker, proof);
            if (proofBytes < 0)
                continue;
            result.bytesAfter = saturatedAdd(result.bytesAfter, proofBytes);
            const QFileInfo current(entry.absoluteFilePath());
            if (ordinaryEntry(current) && current.isFile() && current.size() == entry.size() &&
                current.lastModified() == entry.lastModified() && pinnedRun.removeSegment(entry.fileName())) {
                result.bytesAfter = std::max<qint64>(0, result.bytesAfter - entry.size());
                ++result.removedSegments;
            }
        }
    }
    (void)scan(root, result.bytesAfter);
    if (result.bytesAfter > policy.maxBytes)
        result.warning = QString::fromUtf8("活动、不可用或保留的日志占用 %1 字节，超出设置容量。").arg(result.bytesAfter);
    return result;
}

std::vector<LogEventRange> readLogExpirations(const QDir& root, const QString& run)
{
    if (!runId(run) || !ordinaryPath(root))
        return {};
    const QDir directory(root.absoluteFilePath(run));
    if (!ordinaryEntry(QFileInfo(directory.absolutePath())) || !closedAt(directory, run).isValid())
        return {};
    std::vector<LogEventRange> result;
    for (const auto& marker : directory.entryInfoList({"expired-*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        const auto first = segmentStart(marker.fileName(), "expired-", ".json");
        if (!first || !absent(QFileInfo(directory.filePath("events-" + marker.fileName().mid(8, 20) + ".jsonl"))))
            continue;
        const auto events = readProof(marker, run, first);
        result.insert(result.end(), events.begin(), events.end());
    }
    return result;
}

bool readLogExpiration(const QDir& root, const QString& run, quint64 event)
{
    const auto ranges = readLogExpirations(root, run);
    return std::any_of(ranges.cbegin(), ranges.cend(), [event](const auto& range) {
        return range.first <= event && event <= range.last;
    });
}

bool hasLogExpirations(const QDir& root, const QString& run)
{
    return !readLogExpirations(root, run).empty();
}
} // namespace eve::ui::detail
