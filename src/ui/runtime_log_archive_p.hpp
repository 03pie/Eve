/** @file runtime_log_archive_p.hpp
 * @brief 工作台读取宿主的已发布事件。文件路径和事件游标由归档持有。
 */
#pragma once
#include <cordis/ui/output.hpp>
#include <QString>
#include <QMap>
#include <memory>
#include <set>
#include <vector>

namespace cordis::ui::detail
{
struct LogRun {
    QString id, label;
};
struct LogRecord {
    OutputEntry entry;
    QString sourceId, sourceName;
    qint64 utcMs = 0;
    quint64 eventId = 0;
    std::optional<OperationId> notification{};
};
struct LogSource {
    QString name;
    std::set<std::pair<InstanceId, Epoch>> identities;
};
using LogSourceCatalog = QMap<QString, LogSource>;
void collectLogSource(LogSourceCatalog& catalog, const LogRecord& record);
struct LogFilter {
    QString sourceId, text;
    std::optional<InstanceId> instance{};
    std::optional<Epoch> epoch{};
    OutputLevel minimumLevel = OutputLevel::Info;
};
struct LogPage {
    // Oldest first for presentation. Pass before to query an earlier page; zero means at the beginning.
    std::vector<LogRecord> rows;
    quint64 before = 0;
    QString warning;
};
class RuntimeLogArchive
{
public:
    explicit RuntimeLogArchive(const QString& workspace, const QString& version);
    ~RuntimeLogArchive();
    RuntimeLogArchive(const RuntimeLogArchive&) = delete;
    RuntimeLogArchive& operator=(const RuntimeLogArchive&) = delete;
    QString currentRun() const;
    QString directory() const;
    QString error() const;
    std::vector<LogRun> runs() const;
    quint64 append(const OutputEntry& entry, const QString& sourceId, const QString& sourceName, qint64 utcMs,
                   std::optional<OperationId> notification = {});
    std::vector<std::pair<QString, LogRecord>> recentNotifications(std::size_t limit) const;
    LogSourceCatalog sourceCatalog(const QString& run) const;
    LogPage query(const QString& run, const LogFilter& filter = {}, quint64 before = 0, int limit = 200) const;
    std::optional<LogRecord> locate(const QString& run, std::uint64_t sourceSequence) const;
    void finish(bool clean);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cordis::ui::detail
