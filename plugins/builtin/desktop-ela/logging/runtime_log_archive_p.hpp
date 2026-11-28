/** @file runtime_log_archive_p.hpp
 * @brief Desktop logging storage, retention and offline readers.
 */
#pragma once
#include "desktop_archive.hpp"

class QFile;
namespace eve::ui { class ElaShell; }
namespace eve::ui::detail {
struct SavedLogRun {
    QString version;
    bool ended = false;
    quint64 acceptedThrough = 0;
};
LogSeverity logSeverity(const LogRecord& record);
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

class RuntimeLogArchive final : public DesktopArchive {
public:
    explicit RuntimeLogArchive(const QString& workspace, const QString& version, LogArchiveOptions options = {});
    ~RuntimeLogArchive() override;
    RuntimeLogArchive(const RuntimeLogArchive&) = delete;
    RuntimeLogArchive& operator=(const RuntimeLogArchive&) = delete;
    Result<std::uint64_t> writePlatform(std::string text, OutputLevel level = OutputLevel::Info,
                                        OutputDiagnostic diagnostic = {});
    QString currentRun() const override;
    QString directory() const override;
    /** @brief 返回指定运行所在的归档根。已清理或缺失的历史运行返回空。 */
    QString directoryFor(const QString& run) const override;
    /** @brief 返回该工作区可能的当前和旧版归档根，供导出前保护归档文件。 */
    const QStringList& readDirectories() const override;
    QString error() const override;
    LogPersistence persistence() const override;
    QString scanDescending(const QString& run, quint64 beforeExclusive,
                                   const std::function<bool(const LogRecord&)>& visitor) const override;
    quint64 acceptedThrough(const QString& run) const override;
    std::vector<LogRun> runs() const override;
    quint64 append(const OutputEntry& entry, const QString& sourceId, const QString& sourceName, qint64 utcMs,
                           std::optional<OperationId> notification = {}) override;
    quint64 append(LogRecord record) override;
    /** Only one live desktop projection may own the sequence cursor at a time. */
    Result<std::uint64_t> attachOutput(PlatformWriter writer) override;
    void detachOutput(std::uint64_t lastSequence) override;
    std::vector<std::pair<QString, LogRecord>> recentNotifications(std::size_t limit) const override;
    LogSourceResult sourceCatalog(const QString& run) const override;
    std::optional<LogRecord> locate(const QString& run, std::uint64_t sourceSequence) const override;
    void finish(bool clean);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Result<void> attachDesktopArchive(ElaShell& shell, const DesktopArchiveHandle& archive);
} // namespace eve::ui::detail
