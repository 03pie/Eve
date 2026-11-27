/** @file runtime_log_query_p.hpp
 * @brief 日志快照、独立回读、续读与导出使用同一值契约。
 */
#pragma once
#include "runtime_log_archive_p.hpp"

class QIODevice;

namespace eve::ui::detail
{
inline constexpr int defaultLogPageSize = 200;
inline constexpr int maxLogPageSize = 1000;

struct LogPosition {
    QString run;
    quint64 through = 0;
};

class LogCursor {
public:
    LogPosition position() const { return {run_, through_}; }
private:
    friend class LogQuery;
    LogCursor(QString run, LogFilter filter, quint64 through)
        : run_(std::move(run)), filter_(std::move(filter)), through_(through) {}

    QString run_;
    LogFilter filter_;
    quint64 through_;
    quint64 before_ = 0;
};

struct LogQueryPage {
    std::vector<LogRecord> rows;
    std::optional<LogCursor> older;
    std::vector<LogLossRange> missing, expired;
    QString warning;
    bool missingIncomplete = false;
};

enum class LogLookupStatus { Found, NotFound, Expired, Unavailable };
struct LogLookup {
    LogLookupStatus status = LogLookupStatus::NotFound;
    std::optional<LogRecord> record;
    std::optional<LogCursor> neighborhood;
    QString warning;
};

struct LogUpdate {
    std::vector<LogRecord> rows;
    LogPosition resume;
    LogSourceCatalog sources;
    std::vector<LogLossRange> missing, expired;
    std::optional<LogPersistence> persistence;
    QString warning;
    bool more = false;
    bool missingIncomplete = false;
};

bool logMatches(const LogRecord& row, const LogFilter& filter);
QString formatLogText(const LogRecord& row);

class LogQuery {
public:
    /** 在线查询借用归档，调用者须保证归档存活。离线查询只持有工作区路径。 */
    explicit LogQuery(const RuntimeLogArchive& archive) : archive_(&archive) {}
    explicit LogQuery(QString workspace) : workspace_(std::move(workspace)) {}
    /** 固定当前运行的事件上界，后续分页和导出共享同一快照。 */
    LogCursor begin(const QString& run, const LogFilter& filter = {}, quint64 before = 0) const;
    std::vector<LogRun> runs() const;
    /** 已解码的来源和扫描告警可同时返回；调用者据此区分完整目录和部分目录。 */
    LogSourceResult sources(const QString& run) const;
    LogQueryPage page(const LogCursor& cursor, int limit = defaultLogPageSize) const;
    LogLookup locate(const QString& run, quint64 eventId) const;
    LogUpdate replay(const LogPosition& after, const LogFilter& filter = {}, int limit = defaultLogPageSize) const;
    bool exportText(const LogCursor& cursor, QIODevice& output, QString& error) const;

private:
    QString scan(const QString& run, quint64 before, const std::function<bool(const LogRecord&)>& visitor) const;
    quint64 acceptedThrough(const QString& run) const;
    std::vector<LogLossRange> expirations(const QString& run, quint64 after, quint64 through) const;
    const RuntimeLogArchive* archive_ = nullptr;
    QString workspace_;
};

// 在订阅者的调度器上从上次游标续读。取消后没有需要释放的回调或宿主引用。
class LogSubscription {
public:
    explicit LogSubscription(LogPosition checkpoint, LogFilter filter = {})
        : checkpoint_(std::move(checkpoint)), filter_(std::move(filter)) {}
    std::optional<LogUpdate> poll(const LogQuery& query, int limit = defaultLogPageSize);
    LogPosition checkpoint() const { return checkpoint_; }
    void cancel() { cancelled_ = true; }
private:
    LogPosition checkpoint_;
    LogFilter filter_;
    bool cancelled_ = false;
};
} // namespace eve::ui::detail
