/** @file runtime_log_query.cpp
 * @brief 按已接受事件的快照上界查询归档，复用读写两端的事件解码器。
 */
#include "runtime_log_query_p.hpp"
#include "runtime_log_retention_p.hpp"
#include <QDateTime>
#include <QDir>
#include <QIODevice>
#include <algorithm>
#include <deque>
#include <limits>

namespace cordis::ui::detail
{
namespace
{
quint64 exclusiveAfter(quint64 event)
{
    return event == std::numeric_limits<quint64>::max() ? 0 : event + 1;
}

constexpr std::size_t missingRangeLimit = 1024;

QString logicalChannel(const LogRecord& row)
{
    const auto channel = QString::fromStdString(row.entry.channel);
    if (!row.entry.owner)
        return channel;
    const auto scope = QString("%1/%2/").arg(row.entry.owner->instance.value()).arg(row.entry.owner->epoch);
    for (const auto* transport : {"plugin/", "native/"}) {
        const auto prefix = QString::fromLatin1(transport) + scope;
        if (channel.startsWith(prefix))
            return channel.mid(prefix.size());
    }
    return channel;
}
}

bool logMatches(const LogRecord& row, const LogFilter& filter)
{
    const auto& entry = row.entry;
    return (filter.sourceId.isEmpty() || filter.sourceId == row.sourceId) &&
           (!filter.minimumSeverity || logSeverity(row) >= *filter.minimumSeverity) &&
           (!filter.instance || (entry.owner && entry.owner->instance == *filter.instance)) &&
           (!filter.epoch || (entry.owner && entry.owner->epoch == *filter.epoch)) &&
           (!filter.fromUtcMs || row.utcMs >= *filter.fromUtcMs) &&
           (!filter.throughUtcMs || row.utcMs <= *filter.throughUtcMs) &&
           (filter.moduleVersion.isEmpty() || filter.moduleVersion == row.moduleVersion) &&
           (filter.moduleId.isEmpty() || filter.moduleId == row.moduleId) &&
           (filter.instanceKey.isEmpty() || filter.instanceKey == row.instanceKey) &&
           (filter.channel.isEmpty() || filter.channel == logicalChannel(row)) &&
           (filter.text.isEmpty() || QString::fromStdString(entry.text).contains(filter.text, Qt::CaseInsensitive));
}

QString formatLogText(const LogRecord& row)
{
    const auto& entry = row.entry;
    const char* level = "INFO";
    switch (logSeverity(row)) {
    case LogSeverity::Trace: level = "TRACE"; break;
    case LogSeverity::Debug: level = "DEBUG"; break;
    case LogSeverity::Info: break;
    case LogSeverity::Warning: level = "WARN"; break;
    case LogSeverity::Error: level = "ERROR"; break;
    }
    return QString("[%1] %2 [%3] %4%5  %6")
        .arg(entry.sequence)
        .arg(QDateTime::fromMSecsSinceEpoch(row.utcMs).toString("HH:mm:ss.zzz"), QString::fromLatin1(level),
             row.sourceName,
             entry.owner ? QString(" #%1/%2").arg(entry.owner->instance.value()).arg(entry.owner->epoch) : QString{},
             QString::fromStdString(entry.text));
}

QString LogQuery::scan(const QString& run, quint64 before, const std::function<bool(const LogRecord&)>& visitor) const
{
    return archive_ ? archive_->scanDescending(run, before, visitor) : scanSavedDescending(workspace_, run, before, visitor);
}

quint64 LogQuery::acceptedThrough(const QString& run) const
{
    if (archive_ && run == archive_->currentRun())
        return archive_->acceptedThrough(run);
    quint64 latest = archive_ ? archive_->acceptedThrough(run) : 0;
    if (!archive_) {
        if (const auto metadata = readSavedLogRun(workspace_, run))
            latest = metadata->acceptedThrough;
        scan(run, 0, [&](const LogRecord& row) {
            latest = std::max(latest, row.eventId);
            return false;
        });
    }
    for (const auto& interval : expirations(run, latest, std::numeric_limits<quint64>::max()))
        latest = std::max(latest, interval.last);
    return latest;
}

std::vector<LogLossRange> LogQuery::expirations(const QString& run, quint64 after, quint64 through) const
{
    const auto directory = archive_ ? archive_->directory() : savedLogDirectory(workspace_, run);
    if (directory.isEmpty() || after >= through)
        return {};
    std::vector<LogLossRange> result;
    for (const auto& range : readLogExpirations(QDir(directory), run)) {
        if (range.last > after && range.first <= through)
            result.push_back({std::max(range.first, after + 1), std::min(range.last, through)});
    }
    return result;
}

LogCursor LogQuery::begin(const QString& run, const LogFilter& filter, quint64 before) const
{
    const auto current = acceptedThrough(run);
    const auto through = before ? std::min(current, before - 1) : current;
    return LogCursor(run, filter, through);
}

std::vector<LogRun> LogQuery::runs() const
{
    return archive_ ? archive_->runs() : readSavedLogRuns(workspace_);
}

LogSourceResult LogQuery::sources(const QString& run) const
{
    if (archive_)
        return archive_->sourceCatalog(run);
    LogSourceResult result;
    result.warning = scan(run, 0, [&](const LogRecord& row) {
        collectLogSource(result.sources, row);
        return true;
    });
    return result;
}

LogQueryPage LogQuery::page(const LogCursor& cursor, int limit) const
{
    LogQueryPage result;
    const auto count = static_cast<std::size_t>(std::clamp(limit, 1, maxLogPageSize));
    const auto upper = cursor.before_ ? std::min(cursor.before_ - 1, cursor.through_) : cursor.through_;
    quint64 expected = upper;
    auto gap = [&](quint64 first, quint64 last) {
        if (result.missing.size() == missingRangeLimit)
            result.missingIncomplete = true;
        else
            result.missing.push_back({first, last});
    };
    result.warning = scan(cursor.run_, exclusiveAfter(upper), [&](const LogRecord& row) {
        if (row.eventId > expected)
            return true;
        const auto selected = logMatches(row, cursor.filter_);
        if (selected && result.rows.size() == count) {
            result.older = cursor;
            result.older->before_ = result.rows.back().eventId;
            return false;
        }
        if (row.eventId < expected)
            gap(row.eventId + 1, expected);
        expected = row.eventId - 1;
        if (selected)
            result.rows.push_back(row);
        return true;
    });
    // Include only the expired interval actually traversed by this page.
    const auto boundary = result.older ? result.rows.back().eventId - 1 : 0;
    if (!result.older && expected)
        gap(1, expected);
    while (!result.missing.empty() && result.missing.back().last <= boundary)
        result.missing.pop_back();
    if (!result.missing.empty() && result.missing.back().first <= boundary)
        result.missing.back().first = boundary + 1;
    std::reverse(result.missing.begin(), result.missing.end());
    result.expired = expirations(cursor.run_, boundary, upper);
    std::reverse(result.rows.begin(), result.rows.end());
    return result;
}

LogLookup LogQuery::locate(const QString& run, quint64 eventId) const
{
    LogLookup result;
    if (!eventId)
        return result;
    result.warning = scan(run, exclusiveAfter(eventId), [&](const LogRecord& row) {
        if (row.eventId > eventId)
            return true;
        if (row.eventId == eventId)
            result.record = row;
        return false;
    });
    if (result.record) {
        result.status = LogLookupStatus::Found;
        LogFilter filter;
        filter.sourceId = result.record->sourceId;
        result.neighborhood = LogCursor(run, filter, eventId);
    } else if (!expirations(run, eventId - 1, eventId).empty()) {
        result.status = LogLookupStatus::Expired;
    } else if (!result.warning.isEmpty()) {
        result.status = LogLookupStatus::Unavailable;
    }
    return result;
}

LogUpdate LogQuery::replay(const LogPosition& after, const LogFilter& filter, int limit) const
{
    LogUpdate result;
    result.resume = after;
    const auto through = acceptedThrough(after.run);
    const auto count = static_cast<std::size_t>(std::clamp(limit, 1, maxLogPageSize));
    if (archive_ && after.run == archive_->currentRun()) {
        result.sources = sources(after.run).sources;
        result.persistence = archive_->persistence();
    }
    std::deque<LogRecord> earliest;
    std::deque<LogLossRange> missing;
    auto gap = [&](quint64 first, quint64 last) {
        if (first > last)
            return;
        missing.push_front({first, last});
        if (missing.size() > missingRangeLimit) {
            missing.pop_back();
            result.missingIncomplete = true;
        }
    };
    quint64 next = through;
    if (through > after.through) {
        result.warning = scan(after.run, exclusiveAfter(through), [&](const LogRecord& row) {
            if (row.eventId <= after.through)
                return false;
            if (row.eventId > next)
                return true;
            if (row.eventId < next)
                gap(row.eventId + 1, next);
            next = row.eventId - 1;
            if (!logMatches(row, filter))
                return true;
            earliest.push_front(row);
            if (earliest.size() > count) {
                earliest.pop_back();
                result.more = true;
            }
            return true;
        });
        gap(after.through + 1, next);
        result.resume.through = result.more ? earliest.back().eventId : through;
        result.rows.assign(earliest.begin(), earliest.end());
        for (const auto& interval : missing) {
            if (interval.first > result.resume.through)
                break;
            result.missing.push_back({interval.first, std::min(interval.last, result.resume.through)});
        }
    } else {
        result.warning = scan(after.run, 1, [](const LogRecord&) { return false; });
    }
    result.expired = expirations(after.run, after.through, result.resume.through);
    return result;
}

bool LogQuery::exportText(const LogCursor& cursor, QIODevice& output, QString& error) const
{
    error.clear();
    std::vector<LogCursor> pages;
    std::optional<LogCursor> current = cursor;
    const auto read = [&](const LogCursor& bound) {
        auto batch = page(bound);
        if (!batch.warning.isEmpty())
            error = batch.warning;
        else if (!batch.expired.empty())
            error = QString::fromUtf8("所选范围有已清理的日志。");
        else if (!batch.missing.empty() || batch.missingIncomplete)
            error = QString::fromUtf8("所选范围有缺失记录。");
        return batch;
    };
    // Record cursor boundaries, then stream in forward acceptance order using the same snapshot.
    while (current) {
        const auto batch = read(*current);
        if (!error.isEmpty())
            return false;
        if (batch.rows.empty())
            break;
        pages.push_back(*current);
        current = batch.older;
    }
    for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
        const auto batch = read(*it);
        if (!error.isEmpty())
            return false;
        for (const auto& record : batch.rows) {
            QString header = QString("[%1 / event %2] ").arg(cursor.run_).arg(record.eventId);
            if (!record.diagnosticCode.isEmpty())
                header += QString("[code=%1] ").arg(record.diagnosticCode);
            if (record.operation)
                header += QString("[op=%1] ").arg(record.operation->value());
            const auto data = (header + formatLogText(record) + '\n').toUtf8();
            if (output.write(data) != data.size()) {
                error = output.errorString();
                return false;
            }
        }
    }
    return true;
}

std::optional<LogUpdate> LogSubscription::poll(const LogQuery& query, int limit)
{
    if (cancelled_)
        return {};
    auto update = query.replay(checkpoint_, filter_, limit);
    checkpoint_ = update.resume;
    return update;
}
} // namespace cordis::ui::detail
