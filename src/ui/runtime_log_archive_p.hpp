/** @file runtime_log_archive_p.hpp
 * @brief 工作台读取宿主的已发布事件。文件路径和事件游标由归档持有。
 */
#pragma once
#include <cordis/ui/output.hpp>
#include <QString>
#include <QMap>
#include <functional>
#include <memory>
#include <set>
#include <vector>

class QFile;

namespace cordis::ui::detail
{
enum class LogRunState { Active, Ended, Unconfirmed };
struct LogRun {
    QString id, label;
    QString version;
    qint64 startedUtcMs = 0;
    std::optional<qint64> endedUtcMs;
    LogRunState state = LogRunState::Unconfirmed;
};
struct SavedLogRun {
    QString version;
    bool ended = false;
    quint64 acceptedThrough = 0;
};
enum class LogSeverity : int { Trace = -2, Debug = -1, Info = 0, Warning = 1, Error = 2 };
struct LogRecord {
    OutputEntry entry;
    QString sourceId, sourceName;
    qint64 utcMs = 0;
    quint64 eventId = 0;
    std::optional<OperationId> notification{};
    std::optional<LogSeverity> severity{};
    QString moduleId{}, moduleVersion{}, instanceKey{}, diagnosticCode{};
    std::optional<std::uint64_t> generation{};
    std::optional<std::uint64_t> sourceSequence{};
    std::optional<OperationId> operation{};
};
LogSeverity logSeverity(const LogRecord& record);
struct LogSource {
    QString name;
    std::set<std::pair<InstanceId, Epoch>> identities;
    std::set<QString> instanceKeys;
    std::set<LogSeverity> levels;
    quint64 firstEvent = 0, lastEvent = 0;
};
using LogSourceCatalog = QMap<QString, LogSource>;
struct LogSourceResult {
    LogSourceCatalog sources;
    QString warning;  // 非空时仍可使用成功解码的来源。
};
void collectLogSource(LogSourceCatalog& catalog, const LogRecord& record);
struct LogFilter {
    QString sourceId, text;
    std::optional<InstanceId> instance{};
    std::optional<Epoch> epoch{};
    std::optional<qint64> fromUtcMs, throughUtcMs;
    std::optional<LogSeverity> minimumSeverity;
    QString moduleVersion;
    QString moduleId, instanceKey, channel;
};

struct LogLossRange {
    quint64 first = 0, last = 0;
};
struct LogPersistence {
    quint64 accepted = 0, written = 0, confirmed = 0, discarded = 0, unconfirmed = 0;
    quint64 queued = 0, queuedBytes = 0, peakQueued = 0, peakQueuedBytes = 0;
    qint64 retainedBytes = 0;
    quint64 prunedSegments = 0;
    bool lossRangesIncomplete = false;
    std::vector<LogLossRange> losses;
    QString problem;
    QString retentionNotice;
};
struct LogArchiveOptions {
    quint64 queueRecords = 4096;
    quint64 queueBytes = 8 * 1024 * 1024;
    int batchRecords = 128;
    int flushMs = 250;
    int retryMs = 250;
    qint64 segmentBytes = 8 * 1024 * 1024;
    // Internal fault seam. A test may perform a real short write and return the byte count.
    std::function<qint64(QFile&, const QByteArray&)> write;
};
// Read from a workspace without opening a writer or acquiring an activity lock.
QString savedLogDirectory(const QString& workspace, const QString& run);
QString scanSavedDescending(const QString& workspace, const QString& run, quint64 beforeExclusive,
                            const std::function<bool(const LogRecord&)>& visitor);
std::optional<SavedLogRun> readSavedLogRun(const QString& workspace, const QString& run);
std::vector<LogRun> readSavedLogRuns(const QString& workspace);
std::optional<LogRecord> readSavedLogEvent(const QString& workspace, const QString& run, quint64 eventId);

class RuntimeLogArchive
{
public:
    explicit RuntimeLogArchive(const QString& workspace, const QString& version, LogArchiveOptions options = {});
    ~RuntimeLogArchive();
    RuntimeLogArchive(const RuntimeLogArchive&) = delete;
    RuntimeLogArchive& operator=(const RuntimeLogArchive&) = delete;
    QString currentRun() const;
    QString directory() const;
    QString error() const;
    LogPersistence persistence() const;
    QString scanDescending(const QString& run, quint64 beforeExclusive,
                           const std::function<bool(const LogRecord&)>& visitor) const;
    quint64 acceptedThrough(const QString& run) const;
    std::vector<LogRun> runs() const;
    quint64 append(const OutputEntry& entry, const QString& sourceId, const QString& sourceName, qint64 utcMs,
                   std::optional<OperationId> notification = {});
    quint64 append(LogRecord record);
    std::vector<std::pair<QString, LogRecord>> recentNotifications(std::size_t limit) const;
    LogSourceResult sourceCatalog(const QString& run) const;
    std::optional<LogRecord> locate(const QString& run, std::uint64_t sourceSequence) const;
    void finish(bool clean);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cordis::ui::detail
