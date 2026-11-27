/** @file runtime_log_archive.cpp
 * @brief 每次宿主启动独占一个运行目录。只写已发布值，分页读取完整 JSON Lines。
 */
#include "runtime_log_archive_p.hpp"
#include "runtime_log_retention_p.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <tuple>
#ifdef Q_OS_WIN
#include <io.h>
#include <qt_windows.h>
#else
#include <unistd.h>
#endif

namespace eve::ui::detail
{
namespace
{
constexpr int diskBlockBytes = 64 * 1024;
constexpr int maxLine = 512 * 1024;
constexpr quint64 tailByteLimit = 8 * 1024 * 1024;
constexpr std::size_t tailRecordLimit = 4096;
constexpr std::size_t lossRangeLimit = 128;
struct QueuedLog {
    quint64 id;
    QByteArray bytes;
};
struct TailLog {
    LogRecord row;
    quint64 bytes;
};
bool syncFile(QFile& file)
{
    if (!file.flush())
        return false;
#ifdef Q_OS_WIN
    const auto handle = _get_osfhandle(file.handle());
    return handle != -1 && FlushFileBuffers(reinterpret_cast<HANDLE>(handle));
#else
    return fsync(file.handle()) == 0;
#endif
}
QString number(std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
bool validRun(const QString& id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}
QString workspaceLogsPath(const QString& workspace)
{
    return QDir(workspace).absoluteFilePath(".eve/logs");
}
QString fallbackLogsPath(const QString& workspace, const QString& dataPath)
{
    const auto key = QCryptographicHash::hash(QFileInfo(workspace).absoluteFilePath().toUtf8(),
                                              QCryptographicHash::Sha256).toHex();
    return QDir(dataPath).filePath("logs/" + QString::fromLatin1(key));
}
QFileInfo runDirectory(const QDir& root, const QString& id)
{
    if (!validRun(id))
        return {};
    const QFileInfo folder(root.filePath(id));
    return folder.isDir() && !folder.isSymLink() ? folder : QFileInfo{};
}
QJsonObject readRunMetadata(const QFileInfo& folder, const QString& id)
{
    if (folder.filePath().isEmpty())
        return {};
    const QFileInfo metadata(QDir(folder.absoluteFilePath()).filePath("run.json"));
    if (metadata.isSymLink())
        return {};
    QFile file(metadata.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8192)
        return {};
    const auto object = QJsonDocument::fromJson(file.read(8193)).object();
    return object.value("run").toString() == id && object.value("v").toInt() == 1 ? object : QJsonObject{};
}
QByteArray encode(const LogRecord& record)
{
    const auto& e = record.entry;
    QJsonObject obj{{"v", 1},
                    {"event", number(record.eventId)},
                    {"sequence", number(e.sequence)},
                    {"utcMs", QString::number(record.utcMs)},
                    {"source", record.sourceId},
                    {"name", record.sourceName},
                    {"channel", QString::fromStdString(e.channel)},
                    {"title", QString::fromStdString(e.title)},
                    {"level", static_cast<int>(logSeverity(record))},
                    {"text", QString::fromStdString(e.text)}};
    if (e.owner) {
        obj.insert("instance", number(e.owner->instance.value()));
        obj.insert("epoch", number(e.owner->epoch));
    }
    if (record.notification)
        obj.insert("notificationOperation", number(record.notification->value()));
    if (record.operation)
        obj.insert("operation", number(record.operation->value()));
    if (record.generation)
        obj.insert("generation", number(*record.generation));
    if (record.sourceSequence)
        obj.insert("sourceSequence", number(*record.sourceSequence));
    if (!record.moduleId.isEmpty())
        obj.insert("module", record.moduleId);
    if (!record.moduleVersion.isEmpty())
        obj.insert("moduleVersion", record.moduleVersion);
    if (!record.instanceKey.isEmpty())
        obj.insert("instanceKey", record.instanceKey);
    if (!record.diagnosticCode.isEmpty())
        obj.insert("code", record.diagnosticCode);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}
std::optional<LogRecord> decode(const QByteArray& line)
{
    const auto obj = QJsonDocument::fromJson(line).object();
    LogRecord rec{};
    rec.eventId = obj.value("event").toString().toULongLong();
    rec.entry.sequence = obj.value("sequence").toString().toULongLong();
    rec.utcMs = obj.value("utcMs").toString().toLongLong();
    rec.sourceId = obj.value("source").toString();
    rec.sourceName = obj.value("name").toString();
    const int level = obj.value("level").toInt(std::numeric_limits<int>::min());
    if (obj.value("v").toInt() != 1 || !rec.eventId || !rec.entry.sequence || rec.sourceId.isEmpty() ||
        level < static_cast<int>(LogSeverity::Trace) || level > static_cast<int>(LogSeverity::Error) ||
        !obj.value("text").isString())
        return {};
    rec.entry.level = static_cast<OutputLevel>(std::max(level, static_cast<int>(LogSeverity::Info)));
    if (level < static_cast<int>(LogSeverity::Info))
        rec.severity = static_cast<LogSeverity>(level);
    rec.entry.channel = obj.value("channel").toString().toStdString();
    rec.entry.title = obj.value("title").toString().toStdString();
    rec.entry.text = obj.value("text").toString().toStdString();
    bool validOperation = false;
    const auto operation = obj.value("notificationOperation").toString().toULongLong(&validOperation);
    if (validOperation)
        rec.notification = OperationId{operation};
    const auto related = obj.value("operation").toString().toULongLong(&validOperation);
    if (validOperation)
        rec.operation = OperationId{related};
    const auto generation = obj.value("generation").toString().toULongLong(&validOperation);
    if (validOperation)
        rec.generation = generation;
    const auto sourceSequence = obj.value("sourceSequence").toString().toULongLong(&validOperation);
    if (validOperation)
        rec.sourceSequence = sourceSequence;
    rec.moduleId = obj.value("module").toString();
    rec.moduleVersion = obj.value("moduleVersion").toString();
    rec.instanceKey = obj.value("instanceKey").toString();
    rec.diagnosticCode = obj.value("code").toString();
    if (obj.contains("instance"))
        rec.entry.owner = ContextIdentity{InstanceId(obj.value("instance").toString().toULongLong()),
                                          obj.value("epoch").toString().toULongLong()};
    return rec;
}
// Walk backwards in blocks. An uncommitted last line is skipped; older complete lines remain readable.
template <class Visitor>
void visitDisk(const QDir& run, quint64 before, QString& warning, Visitor visit, const std::atomic_bool* stop = nullptr)
{
    const auto segments =
        run.entryInfoList({"events-*.jsonl"}, QDir::Files | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
    for (const auto& segment : segments) {
        bool valid = false;
        const auto first = segment.completeBaseName().mid(7).toULongLong(&valid);
        if (!valid || (before && first >= before))
            continue;
        QFile file(segment.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            warning = file.errorString();
            continue;
        }
        qint64 end = file.size();
        QByteArray suffix;
        bool skipFragment = false, firstBlock = true;
        while (end > 0) {
            if (stop && stop->load(std::memory_order_relaxed))
                return;
            const auto start = std::max<qint64>(0, end - diskBlockBytes);
            if (!file.seek(start)) {
                warning = file.errorString();
                break;
            }
            auto block = file.read(end - start);
            if (block.size() != end - start) {
                warning = file.errorString();
                break;
            }
            if (firstBlock && !block.endsWith('\n')) {
                warning = QString::fromUtf8("日志末行不完整，已保留可读记录。");
                skipFragment = true;
            }
            firstBlock = false;
            block += suffix;
            int pos = block.size();
            while (pos > 0) {
                const int previous = block.lastIndexOf('\n', pos - 1);
                if (previous < 0 && start > 0)
                    break;
                const auto length = pos - previous - 1;
                if (length && !skipFragment) {
                    const auto record = decode(block.mid(previous + 1, length));
                    if (!record)
                        warning = QString::fromUtf8("日志中有无法读取的记录。");
                    else if ((!before || record->eventId < before) && !visit(*record))
                        return;
                }
                skipFragment = false;
                pos = std::max(0, previous);
            }
            suffix = block.left(pos);
            if (suffix.size() > maxLine) {
                suffix.clear();
                skipFragment = true;
                warning = QString::fromUtf8("日志行过长。");
            }
            end = start;
        }
    }
}


QStringList savedLogRoots(const QString& workspace)
{
    if (workspace.isEmpty())
        return {};
    // 旧目录仅用于读取历史，新运行始终写入当前版本的工作区或备用目录。
    QStringList roots{workspaceLogsPath(workspace), QDir(workspace).absoluteFilePath(".cordis/logs")};
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!data.isEmpty())
        roots.push_back(fallbackLogsPath(workspace, data));
    const auto localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!localData.isEmpty()) {
        roots.push_back(fallbackLogsPath(workspace, QDir(localData).filePath("EvePluginWorkbench")));
        roots.push_back(fallbackLogsPath(workspace, QDir(localData).filePath("CordisPluginWorkbench")));
    }
    roots.removeDuplicates();
    return roots;
}

bool activeLockOwner(const QFileInfo& folder, const QDateTime& started, const QJsonObject& metadata)
{
    const QFileInfo info(QDir(folder.absoluteFilePath()).filePath("active.lock"));
    if (!info.isFile() || info.isSymLink())
        return false;
    bool validPid = false;
    qint64 pid = metadata.value("pid").toString().toLongLong(&validPid);
    if (!validPid) {
        const QLockFile lock(info.absoluteFilePath());
        QString host, app;
        if (!lock.getLockInfo(&pid, &host, &app) || host != QSysInfo::machineHostName())
            return false;
    }
    if (pid <= 0)
        return false;
    if (pid == QCoreApplication::applicationPid())
        return true;
#ifdef Q_OS_WIN
    if (!started.isValid() || pid > std::numeric_limits<DWORD>::max())
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    DWORD status = 0;
    const bool alive = GetExitCodeProcess(process, &status) && status == STILL_ACTIVE &&
                       GetProcessTimes(process, &created, &exited, &kernel, &user);
    CloseHandle(process);
    if (!alive)
        return false;
    ULARGE_INTEGER fileTime{};
    fileTime.HighPart = created.dwHighDateTime;
    fileTime.LowPart = created.dwLowDateTime;
    const auto createdUtcMs = qint64(fileTime.QuadPart / 10'000ULL) - 11'644'473'600'000LL;
    return createdUtcMs <= started.toMSecsSinceEpoch();
#else
    // A foreign lock without verifiable process identity remains unconfirmed.
    (void)started;
    return false;
#endif
}

LogRun describeRun(const QFileInfo& folder, const QString& id, const QJsonObject& metadata)
{
    const auto started = QDateTime::fromString(metadata.value("started").toString(), Qt::ISODateWithMs);
    const auto ended = QDateTime::fromString(metadata.value("ended").toString(), Qt::ISODateWithMs);
    LogRun result;
    result.id = id;
    result.version = metadata.value("version").toString();
    result.startedUtcMs = started.isValid() ? started.toMSecsSinceEpoch() : 0;
    result.label = started.isValid() ? started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                                     : QString::fromUtf8("开始时间未知");
    result.label += " · " + result.version;
    if (ended.isValid()) {
        result.endedUtcMs = ended.toMSecsSinceEpoch();
        result.state = LogRunState::Ended;
        result.label += QString::fromUtf8(" · 正常结束");
    } else if (activeLockOwner(folder, started, metadata)) {
        result.state = LogRunState::Active;
        result.label += QString::fromUtf8(" · 正在运行");
    } else {
        result.label += QString::fromUtf8(" · 结束未确认");
    }
    return result;
}

bool newerRun(const LogRun& a, const LogRun& b)
{
    return std::tie(a.startedUtcMs, a.id) > std::tie(b.startedUtcMs, b.id);
}

std::vector<LogRun> listLogRuns(const QStringList& roots, const QString& except = {})
{
    std::set<QString> seen;
    std::vector<LogRun> result;
    for (const auto& root : roots)
        for (const auto& item : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            const auto id = item.fileName();
            if (id == except || seen.count(id) || !validRun(id))
                continue;
            const auto metadata = readRunMetadata(item, id);
            if (metadata.isEmpty())
                continue;
            seen.insert(id);
            result.push_back(describeRun(item, id, metadata));
        }
    std::sort(result.begin(), result.end(), newerRun);
    return result;
}
QString findLogRoot(const QStringList& roots, const QString& run)
{
    if (!validRun(run))
        return {};
    for (const auto& root : roots)
        if (!readRunMetadata(runDirectory(QDir(root), run), run).isEmpty())
            return root;
    return {};
}
} // namespace
QString savedLogDirectory(const QString& workspace, const QString& run)
{
    return findLogRoot(savedLogRoots(workspace), run);
}
QString scanSavedDescending(const QString& workspace, const QString& run, quint64 beforeExclusive,
                            const std::function<bool(const LogRecord&)>& visitor)
{
    const auto root = savedLogDirectory(workspace, run);
    if (root.isEmpty())
        return QString::fromUtf8("该运行的归档不可用。");
    QString warning;
    visitDisk(QDir(QDir(root).filePath(run)), beforeExclusive, warning, visitor);
    return warning;
}
std::optional<SavedLogRun> readSavedLogRun(const QString& workspace, const QString& run)
{
    const auto root = savedLogDirectory(workspace, run);
    if (root.isEmpty())
        return {};
    const auto metadata = readRunMetadata(runDirectory(QDir(root), run), run);
    if (metadata.isEmpty())
        return {};
    const auto accepted = metadata.value("persistence").toObject().value("accepted").toString().toULongLong();
    const auto ended = QDateTime::fromString(metadata.value("ended").toString(), Qt::ISODateWithMs);
    return SavedLogRun{metadata.value("version").toString(), ended.isValid(), accepted};
}
std::vector<LogRun> readSavedLogRuns(const QString& workspace)
{
    return listLogRuns(savedLogRoots(workspace));
}
std::optional<LogRecord> readSavedLogEvent(const QString& workspace, const QString& run, quint64 eventId)
{
    if (workspace.isEmpty() || !eventId)
        return {};
    std::optional<LogRecord> event;
    const quint64 before = eventId == std::numeric_limits<quint64>::max() ? 0 : eventId + 1;
    scanSavedDescending(workspace, run, before, [&](const LogRecord& row) {
        if (row.eventId == eventId)
            event = row;
        return false;
    });
    return event;
}

LogSeverity logSeverity(const LogRecord& record)
{
    return record.severity.value_or(static_cast<LogSeverity>(record.entry.level));
}

void collectLogSource(LogSourceCatalog& catalog, const LogRecord& row)
{
    auto& source = catalog[row.sourceId];
    source.name = row.sourceName;
    if (const auto& owner = row.entry.owner)
        source.identities.emplace(owner->instance, owner->epoch);
    if (!row.instanceKey.isEmpty())
        source.instanceKeys.insert(row.instanceKey);
    source.levels.insert(logSeverity(row));
    source.firstEvent = source.firstEvent ? std::min(source.firstEvent, row.eventId) : row.eventId;
    source.lastEvent = std::max(source.lastEvent, row.eventId);
}

struct RuntimeLogArchive::Impl {
    struct Segment {
        QFile file;
        qint64 confirmedOffset = 0;
        std::size_t attempted = 0;
        bool needsRecovery = false;
        quint64 savedFault = 0;
    };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime started = QDateTime::currentDateTimeUtc();
    QString version;
    QDir root, run;
    QStringList readRoots;
    QString directoryFor(const QString& requested) const
    {
        return requested == id ? root.absolutePath() : findLogRoot(readRoots, requested);
    }
    std::unique_ptr<QLockFile> lock;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::thread writer;
    std::atomic_bool stopping{false};
    bool hasNotifications = false, cleanRequested = false;
    bool urgent = false;
    LogArchiveOptions options;
    LogPersistence state;
    QString fallbackNotice;
    std::deque<QueuedLog> pending;
    std::deque<TailLog> tail;
    quint64 tailBytes = 0;
    LogSourceCatalog currentSources;
    mutable QString cachedRunPath;
    mutable std::pair<QString, qint64> cachedTail;
    mutable LogSourceCatalog cachedSources;

    void selectDirectory(const QString& path)
    {
        root = QDir(path);
        run = QDir(root.filePath(id));
    }
    bool openDirectory()
    {
        if (!root.mkpath(".") || (!root.mkdir(id) && runDirectory(root, id).filePath().isEmpty()))
            return false;
        auto candidate = std::make_unique<QLockFile>(run.filePath("active.lock"));
        candidate->setStaleLockTime(0);
        if (!candidate->tryLock() || !writeMetadata(false))
            return false;
        lock = std::move(candidate);
        return true;
    }

    bool writeMetadata(bool clean)
    {
        QSaveFile file(run.filePath("run.json"));
        QJsonObject data{{"v", 1}, {"run", id}, {"version", version}, {"started", started.toString(Qt::ISODateWithMs)},
                         {"pid", number(static_cast<quint64>(QCoreApplication::applicationPid()))}};
        if (clean) {
            data.insert("ended", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            data.insert("hasNotifications", hasNotifications);
            QJsonArray losses;
            for (const auto& range : state.losses)
                losses.append(QJsonArray{number(range.first), number(range.last)});
            data.insert("persistence", QJsonObject{{"accepted", number(state.accepted)},
                {"written", number(state.written)}, {"confirmed", number(state.confirmed)},
                {"discarded", number(state.discarded)}, {"unconfirmed", number(state.unconfirmed)},
                {"losses", losses}, {"lossRangesIncomplete", state.lossRangesIncomplete}});
        }
        const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    }
    void addLoss(quint64 event)
    {
        // Concurrent queue overflow can be newer than an abandoned batch.
        auto& ranges = state.losses;
        auto it = std::lower_bound(ranges.begin(), ranges.end(), event,
                                  [](const LogLossRange& range, quint64 id) { return range.last < id - 1; });
        if (it != ranges.end() && it->first - 1 <= event) {
            it->first = std::min(it->first, event);
            it->last = std::max(it->last, event);
            const auto next = std::next(it);
            if (next != ranges.end() && next->first - 1 <= it->last) {
                it->last = next->last;
                ranges.erase(next);
            }
        } else if (ranges.size() < lossRangeLimit) {
            ranges.insert(it, {event, event});
        } else {
            state.lossRangesIncomplete = true;
        }
    }

    void registerWritten(std::size_t rows)
    {
        std::lock_guard<std::mutex> guard(mutex);
        state.written = std::max(state.written, state.confirmed + static_cast<quint64>(rows));
    }

    void confirm(std::deque<QueuedLog>& batch, std::size_t rows)
    {
        std::lock_guard<std::mutex> guard(mutex);
        state.confirmed += rows;
        state.written = std::max(state.written, state.confirmed);
        for (std::size_t i = 0; i < rows; ++i) {
            state.queuedBytes -= static_cast<quint64>(batch.front().bytes.size());
            --state.queued;
            batch.pop_front();
        }
    }

    bool reconcile(Segment& segment, std::deque<QueuedLog>& batch)
    {
        // The previous flush failed or the tail may contain a short write.
        // Read and sync the existing complete prefix before attempting the suffix.
        QFile existing(segment.file.fileName());
        if (!existing.open(QIODevice::ReadWrite | QIODevice::Unbuffered) ||
            existing.size() < segment.confirmedOffset || !existing.seek(segment.confirmedOffset))
            return false;
        std::size_t recovered = 0;
        for (; recovered < segment.attempted; ++recovered) {
            const auto& expected = batch[recovered].bytes;
            if (existing.read(expected.size()) != expected)
                break;
        }
        if (existing.error() != QFileDevice::NoError || (recovered && !syncFile(existing)))
            return false;
        const bool empty = existing.size() == 0;
        existing.close();
        if (!segment.confirmedOffset && !recovered) {
            const QString original = existing.fileName();
            if (!empty && segment.savedFault != batch.front().id) {
                const auto saved = run.filePath("fault-" + QFileInfo(original).completeBaseName() + "-" +
                                                QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jsonl");
                if (!QFile::rename(original, saved))
                    return false;
                segment.savedFault = batch.front().id;
            } else if (!QFile::remove(original)) {
                return false;
            }
        }
        confirm(batch, recovered);
        segment.needsRecovery = false;
        segment.confirmedOffset = 0;
        segment.attempted = 0;
        return true;
    }

    bool writeChunk(Segment& segment, std::deque<QueuedLog>& batch)
    {
        if (segment.needsRecovery && !reconcile(segment, batch))
            return false;
        if (batch.empty())
            return true;
        if (segment.file.isOpen() && segment.file.pos() + batch.front().bytes.size() > options.segmentBytes) {
            segment.file.close();
            applyRetention();
        }
        if (!segment.file.isOpen()) {
            segment.file.setFileName(run.filePath(QString("events-%1.jsonl").arg(batch.front().id, 20, 10, QChar('0'))));
            if (!segment.file.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Unbuffered))
                return false;
            segment.confirmedOffset = 0;
        }
        std::size_t written = 0;
        segment.attempted = 0;
        bool complete = true;
        for (const auto& row : batch) {
            if (written && segment.file.pos() + row.bytes.size() > options.segmentBytes)
                break;
            ++segment.attempted;
            const auto size = options.write ? options.write(segment.file, row.bytes) : segment.file.write(row.bytes);
            if (size != row.bytes.size()) {
                complete = false;
                break;
            }
            ++written;
        }
        registerWritten(written);
        if (complete && syncFile(segment.file)) {
            segment.confirmedOffset = segment.file.pos();
            confirm(batch, written);
            return true;
        }
        segment.file.close();
        segment.needsRecovery = true;
        return false;
    }

    void abandon(std::deque<QueuedLog>& batch)
    {
        const auto discard = [this](auto& records) {
            for (const auto& record : records)
                addLoss(record.id);
            state.unconfirmed += records.size();
            records.clear();
        };
        discard(batch);
        discard(pending);
        state.queued = state.queuedBytes = 0;
    }

    void applyRetention()
    {
        const auto result = pruneClosedLogRuns(root, {}, QDateTime::currentDateTimeUtc());
        std::lock_guard<std::mutex> guard(mutex);
        state.retainedBytes = result.bytesAfter;
        state.prunedSegments += result.removedSegments;
        state.retentionNotice = result.warning;
    }

    void writeBatches()
    {
        Segment segment;
        std::deque<QueuedLog> batch;
        if (lock)
            applyRetention();
        for (;;) {
            if (batch.empty()) {
                std::unique_lock<std::mutex> guard(mutex);
                ready.wait(guard, [this] { return stopping || !pending.empty(); });
                if (pending.empty() && stopping)
                    break;
                ready.wait_for(guard, std::chrono::milliseconds(options.flushMs), [this] {
                    return stopping || urgent || pending.size() >= static_cast<std::size_t>(options.batchRecords);
                });
                pending.swap(batch);
                urgent = false;
            }
            if ((lock || openDirectory()) && writeChunk(segment, batch)) {
                std::lock_guard<std::mutex> guard(mutex);
                state.problem.clear();
                continue;
            }
            std::unique_lock<std::mutex> guard(mutex);
            state.problem = QString::fromUtf8("日志归档写入受阻，正在等待恢复。");
            if (stopping) {
                abandon(batch);
                break;
            }
            ready.wait_for(guard, std::chrono::milliseconds(options.retryMs), [this] { return stopping.load(); });
        }
        segment.file.close();
        if (cleanRequested && lock) {
            bool committed = writeMetadata(true);
            // Windows readers may briefly hold the old metadata entry open during atomic replacement.
            for (int retry = 0; !committed && retry < 2; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                committed = writeMetadata(true);
            }
            if (committed) {
                lock.reset();
                applyRetention();
            } else {
                std::lock_guard<std::mutex> guard(mutex);
                state.problem = QString::fromUtf8("日志终态写入失败。");
            }
        }
    }
    template <class Visitor>
    void visit(const QString& requested, quint64 before, QString& warning, Visitor visitor,
               const std::atomic_bool* stop = nullptr) const
    {
        if (!validRun(requested)) {
            warning = QString::fromUtf8("运行标识无效。");
            return;
        }
        if (requested == id) {
            std::deque<TailLog> recent;
            {
                std::lock_guard<std::mutex> guard(mutex);
                recent = tail;
                warning = state.problem;
            }
            for (auto it = recent.rbegin(); it != recent.rend(); ++it)
                if ((!before || it->row.eventId < before) && !visitor(it->row))
                    return;
            if (!recent.empty() && (!before || recent.front().row.eventId < before))
                before = recent.front().row.eventId;
        }
        const auto directory = directoryFor(requested);
        const auto folder = directory.isEmpty() ? QFileInfo{} : runDirectory(QDir(directory), requested);
        if (folder.filePath().isEmpty()) {
            warning = QString::fromUtf8("该运行的归档不可用。");
            return;
        }
        visitDisk(QDir(folder.absoluteFilePath()), before, warning, visitor, stop);
    }
};

RuntimeLogArchive::RuntimeLogArchive(const QString& workspace, const QString& version, LogArchiveOptions options)
    : impl_(std::make_unique<Impl>())
{
    auto& d = *impl_;
    d.options = std::move(options);
    d.version = version;
    const auto primary = workspaceLogsPath(workspace);
    d.selectDirectory(primary);
    if (!d.openDirectory()) {
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!data.isEmpty()) {
            d.selectDirectory(fallbackLogsPath(workspace, data));
            if (d.openDirectory())
                d.fallbackNotice = QString::fromUtf8("工作区日志目录不可写，已改存到 ") + d.root.absolutePath();
        }
        if (!d.lock) {
            d.selectDirectory(primary);
            d.state.problem = QString::fromUtf8("日志目录不可写，正在等待恢复。");
        }
    }
    d.readRoots = savedLogRoots(workspace);
    d.writer = std::thread([this] { impl_->writeBatches(); });
}
RuntimeLogArchive::~RuntimeLogArchive() { finish(false); }
QString RuntimeLogArchive::currentRun() const { return impl_->id; }
QString RuntimeLogArchive::directory() const { return impl_->root.absolutePath(); }
QString RuntimeLogArchive::directoryFor(const QString& run) const { return impl_->directoryFor(run); }
const QStringList& RuntimeLogArchive::readDirectories() const { return impl_->readRoots; }
QString RuntimeLogArchive::error() const
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    const auto& state = impl_->state;
    auto message = state.problem.isEmpty() ? impl_->fallbackNotice : state.problem;
    if (message.isEmpty())
        message = state.retentionNotice;
    if (state.discarded || state.unconfirmed) {
        if (!message.isEmpty())
            message += " ";
        message += QString::fromUtf8("已丢弃 %1 条，另有 %2 条未确认写入。").arg(state.discarded).arg(state.unconfirmed);
    }
    return message;
}
LogPersistence RuntimeLogArchive::persistence() const
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->state;
}
QString RuntimeLogArchive::scanDescending(const QString& run, quint64 beforeExclusive,
                                          const std::function<bool(const LogRecord&)>& visitor) const
{
    QString warning;
    impl_->visit(run, beforeExclusive, warning, visitor);
    return warning;
}
quint64 RuntimeLogArchive::acceptedThrough(const QString& run) const
{
    if (run == currentRun()) {
        std::lock_guard<std::mutex> guard(impl_->mutex);
        return impl_->state.accepted;
    }
    const auto directory = impl_->directoryFor(run);
    if (directory.isEmpty())
        return 0;
    const auto saved = readRunMetadata(runDirectory(QDir(directory), run), run);
    const auto accepted = saved.value("persistence").toObject().value("accepted").toString().toULongLong();
    if (accepted)
        return accepted;
    quint64 latest = 0;
    scanDescending(run, 0, [&](const LogRecord& record) {
        latest = record.eventId;
        return false;
    });
    return latest;
}
quint64 RuntimeLogArchive::append(const OutputEntry& e, const QString& sourceId, const QString& sourceName,
                                  qint64 utcMs, std::optional<OperationId> notification)
{
    return append(LogRecord{e, sourceId, sourceName, utcMs, 0, notification});
}
quint64 RuntimeLogArchive::append(LogRecord row)
{
    auto& d = *impl_;
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.stopping || d.state.accepted == std::numeric_limits<quint64>::max())
        return 0;
    const auto id = ++d.state.accepted;
    row.eventId = id;
    auto bytes = encode(row);
    const auto size = static_cast<quint64>(bytes.size());
    const bool urgent = logSeverity(row) == LogSeverity::Error;
    if (row.notification)
        d.hasNotifications = true;
    collectLogSource(d.currentSources, row);
    d.tail.push_back({std::move(row), size});
    d.tailBytes += size;
    while (d.tail.size() > tailRecordLimit || d.tailBytes > tailByteLimit) {
        d.tailBytes -= d.tail.front().bytes;
        d.tail.pop_front();
    }
    const auto countLimit = d.options.queueRecords - (urgent ? 0 : d.options.queueRecords / 8);
    const auto byteLimit = d.options.queueBytes - (urgent ? 0 : d.options.queueBytes / 8);
    if (size > maxLine || d.state.queued >= countLimit || size > byteLimit || d.state.queuedBytes > byteLimit - size) {
        ++d.state.discarded;
        d.addLoss(id);
        return id;
    }
    d.pending.push_back({id, std::move(bytes)});
    ++d.state.queued;
    d.state.queuedBytes += size;
    d.state.peakQueued = std::max(d.state.peakQueued, d.state.queued);
    d.state.peakQueuedBytes = std::max(d.state.peakQueuedBytes, d.state.queuedBytes);
    d.urgent = d.urgent || urgent;
    d.ready.notify_one();
    return id;
}
void RuntimeLogArchive::finish(bool clean)
{
    auto& d = *impl_;
    {
        std::lock_guard<std::mutex> guard(d.mutex);
        if (d.stopping)
            return;
        d.cleanRequested = clean;
        d.stopping = true;
    }
    d.ready.notify_one();
    if (d.writer.joinable())
        d.writer.join();
    d.lock.reset();
}
std::vector<LogRun> RuntimeLogArchive::runs() const
{
    const auto& d = *impl_;
    auto list = listLogRuns(d.readRoots, d.id);
    const auto folder = runDirectory(d.root, d.id);
    const auto metadata = readRunMetadata(folder, d.id);
    LogRun current;
    if (!metadata.isEmpty())
        current = describeRun(folder, d.id, metadata);
    else {
        current = {d.id, d.started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") + " · " + d.version,
                   d.version, d.started.toMSecsSinceEpoch(), {}, d.stopping ? LogRunState::Unconfirmed : LogRunState::Active};
    }
    const auto point = std::lower_bound(list.cbegin(), list.cend(), current, newerRun);
    list.insert(point, std::move(current));
    return list;
}
std::vector<std::pair<QString, LogRecord>> RuntimeLogArchive::recentNotifications(std::size_t limit) const
{
    std::vector<std::pair<QString, LogRecord>> result;
    for (const auto& run : runs()) {
        if (result.size() >= limit || impl_->stopping.load(std::memory_order_relaxed))
            break;
        if (run.id == currentRun())
            continue;
        const auto directory = impl_->directoryFor(run.id);
        if (directory.isEmpty())
            continue;
        QFile file(QDir(directory).filePath(run.id + "/run.json"));
        if (file.open(QIODevice::ReadOnly) && file.size() <= 8192) {
            const auto meta = QJsonDocument::fromJson(file.readAll()).object();
            if (meta.contains("ended") && !meta.value("hasNotifications").toBool())
                continue;
        }
        QString warning;
        impl_->visit(
            run.id, 0, warning,
            [&](const LogRecord& record) {
                if (record.notification)
                    result.emplace_back(run.id, record);
                return result.size() < limit;
            },
            &impl_->stopping);
    }
    std::reverse(result.begin(), result.end());
    return result;
}
LogSourceResult RuntimeLogArchive::sourceCatalog(const QString& run) const
{
    auto& d = *impl_;
    {
        std::lock_guard<std::mutex> guard(d.mutex);
        if (run == d.id)
            return {d.currentSources, {}};
    }
    const auto directory = d.directoryFor(run);
    if (directory.isEmpty())
        return {{}, QString::fromUtf8("该运行的归档不可用。")};
    const QDir folder(QDir(directory).filePath(run));
    if (!folder.exists())
        return {{}, QString::fromUtf8("该运行的归档不可用。")};
    QFile meta(folder.filePath("run.json"));
    const bool complete = meta.open(QIODevice::ReadOnly) && meta.size() <= 8192 &&
                          QJsonDocument::fromJson(meta.readAll()).object().contains("ended");
    // A neighboring process can still append or rotate its last segment.
    // Earlier complete segments are immutable, including on an unclean exit.
    const auto tail = [&]() -> std::pair<QString, qint64> {
        const auto files = QDir(folder.absolutePath()).entryInfoList({"events-*.jsonl"}, QDir::Files | QDir::NoSymLinks,
                                                                    QDir::Name | QDir::Reversed);
        return files.empty() ? std::make_pair(QString{}, qint64{0})
                             : std::make_pair(files.front().fileName(), files.front().size());
    };
    const auto stamp = complete ? std::make_pair(QString{}, qint64{-1}) : tail();
    {
        std::lock_guard<std::mutex> guard(d.mutex);
        if (folder.absolutePath() == d.cachedRunPath && stamp == d.cachedTail)
            return {d.cachedSources, {}};
    }
    LogSourceCatalog values;
    QString warning;
    d.visit(run, 0, warning, [&](const LogRecord& row) {
        collectLogSource(values, row);
        return true;
    });
    if (warning.isEmpty() && (complete || stamp == tail())) {
        std::lock_guard<std::mutex> guard(d.mutex);
        d.cachedRunPath = folder.absolutePath();
        d.cachedTail = stamp;
        d.cachedSources = values;
    }
    return {std::move(values), std::move(warning)};
}
std::optional<LogRecord> RuntimeLogArchive::locate(const QString& run, std::uint64_t sourceSequence) const
{
    std::optional<LogRecord> found;
    QString warning;
    impl_->visit(run, 0, warning, [&](const LogRecord& row) {
        if (row.entry.sequence != sourceSequence)
            return true;
        found = row;
        return false;
    });
    return found;
}
} // namespace eve::ui::detail
