#pragma once

#include <QDateTime>
#include <QDir>
#include <vector>

namespace eve::ui::detail
{
struct LogRetentionPolicy {
    int maxAgeDays = 14;
    qint64 maxBytes = 256 * 1024 * 1024;
};

struct LogRetentionResult {
    qint64 bytesBefore = 0;
    qint64 bytesAfter = 0;
    std::size_t removedSegments = 0;
    QString warning;
};

struct LogEventRange {
    quint64 first = 0;
    quint64 last = 0;
};

LogRetentionResult pruneClosedLogRuns(const QDir& root, const LogRetentionPolicy& policy, const QDateTime& now);
std::vector<LogEventRange> readLogExpirations(const QDir& root, const QString& run);
bool readLogExpiration(const QDir& root, const QString& run, quint64 event);
bool hasLogExpirations(const QDir& root, const QString& run);
} // namespace eve::ui::detail
