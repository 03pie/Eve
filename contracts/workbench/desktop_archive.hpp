#pragma once
#include <eve/ui/output.hpp>
#include <QMap>
#include <QStringList>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace eve::ui::detail {
enum class LogRunState { Active, Ended, Unconfirmed };
struct LogRun {
    QString id, label;
    QString version;
    qint64 startedUtcMs = 0;
    std::optional<qint64> endedUtcMs;
    LogRunState state = LogRunState::Unconfirmed;
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

/** Desktop-facing view of one run. Calls dispatch to the plugin that owns the writer. */
class DesktopArchive {
public:
    virtual ~DesktopArchive() = default;
    virtual QString currentRun() const = 0;
    virtual QString directory() const = 0;
    /** @brief 返回指定运行所在的归档根。已清理或缺失的历史运行返回空。 */
    virtual QString directoryFor(const QString& run) const = 0;
    /** @brief 返回该工作区可能的当前和旧版归档根，供导出前保护归档文件。 */
    virtual const QStringList& readDirectories() const = 0;
    virtual QString error() const = 0;
    virtual LogPersistence persistence() const = 0;
    virtual QString scanDescending(const QString& run, quint64 beforeExclusive,
                                   const std::function<bool(const LogRecord&)>& visitor) const = 0;
    virtual quint64 acceptedThrough(const QString& run) const = 0;
    virtual std::vector<LogRun> runs() const = 0;
    virtual quint64 append(const OutputEntry& entry, const QString& sourceId, const QString& sourceName, qint64 utcMs,
                           std::optional<OperationId> notification = {}) = 0;
    virtual quint64 append(LogRecord record) = 0;
    using PlatformWriter = std::function<Result<std::uint64_t>(std::string, OutputLevel, OutputDiagnostic)>;
    /** Only one live desktop projection may own the sequence cursor at a time. */
    virtual Result<std::uint64_t> attachOutput(PlatformWriter writer) = 0;
    virtual void detachOutput(std::uint64_t lastSequence) = 0;
    virtual std::vector<std::pair<QString, LogRecord>> recentNotifications(std::size_t limit) const = 0;
    virtual LogSourceResult sourceCatalog(const QString& run) const = 0;
    virtual std::optional<LogRecord> locate(const QString& run, std::uint64_t sourceSequence) const = 0;
};
using DesktopArchiveHandle = std::shared_ptr<DesktopArchive>;
}
