/** @file runtime_log_archive.cpp
 * @brief 每次宿主启动独占一个运行目录。只写已发布值，分页读取完整 JSON Lines。
 */
#include "runtime_log_archive_p.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cordis::ui::detail
{
namespace
{
constexpr qint64 segmentSize = 8 * 1024 * 1024;
constexpr int maxLine = 64 * 1024;
QString number(std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); }
bool validRun(const QString& id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
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
                    {"level", static_cast<int>(e.level)},
                    {"text", QString::fromStdString(e.text)}};
    if (e.owner) {
        obj.insert("instance", number(e.owner->instance.value()));
        obj.insert("epoch", number(e.owner->epoch));
    }
    if (record.notification)
        obj.insert("notificationOperation", number(record.notification->value()));
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
    const int level = obj.value("level").toInt(-1);
    if (obj.value("v").toInt() != 1 || !rec.eventId || !rec.entry.sequence || rec.sourceId.isEmpty() || level < 0 ||
        level > static_cast<int>(OutputLevel::Error) || !obj.value("text").isString())
        return {};
    rec.entry.level = static_cast<OutputLevel>(level);
    rec.entry.channel = obj.value("channel").toString().toStdString();
    rec.entry.title = obj.value("title").toString().toStdString();
    rec.entry.text = obj.value("text").toString().toStdString();
    bool validOperation = false;
    const auto operation = obj.value("notificationOperation").toString().toULongLong(&validOperation);
    if (validOperation)
        rec.notification = OperationId{operation};
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
            const auto start = std::max<qint64>(0, end - maxLine);
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

bool matches(const LogRecord& rec, const LogFilter& filter)
{
    const auto& e = rec.entry;
    return (filter.sourceId.isEmpty() || filter.sourceId == rec.sourceId) && e.level >= filter.minimumLevel &&
           (!filter.instance || (e.owner && e.owner->instance == *filter.instance)) &&
           (!filter.epoch || (e.owner && e.owner->epoch == *filter.epoch)) &&
           (filter.text.isEmpty() || QString::fromStdString(e.text).contains(filter.text, Qt::CaseInsensitive));
}
} // namespace

void collectLogSource(LogSourceCatalog& catalog, const LogRecord& row)
{
    auto& source = catalog[row.sourceId];
    source.name = row.sourceName;
    if (const auto& owner = row.entry.owner)
        source.identities.emplace(owner->instance, owner->epoch);
}

struct RuntimeLogArchive::Impl {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDateTime started = QDateTime::currentDateTimeUtc();
    QString version;
    QDir root, run;
    std::unique_ptr<QLockFile> lock;
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::thread writer;
    std::atomic_bool stopping{false};
    bool hasNotifications = false;
    quint64 next = 0, dropped = 0;
    QString problem;
    std::deque<LogRecord> pending, tail;
    LogSourceCatalog currentSources;
    mutable QString cachedRun;
    mutable std::pair<QString, qint64> cachedTail;
    mutable LogSourceCatalog cachedSources;

    bool openDirectory(const QString& path)
    {
        root = QDir(path);
        if (!root.mkpath(".") || !root.mkdir(id))
            return false;
        run = QDir(root.filePath(id));
        lock = std::make_unique<QLockFile>(run.filePath("active.lock"));
        lock->setStaleLockTime(0);
        return lock->tryLock() && writeMetadata(false);
    }
    bool writeMetadata(bool clean)
    {
        QSaveFile file(run.filePath("run.json"));
        QJsonObject data{{"v", 1}, {"run", id}, {"version", version}, {"started", started.toString(Qt::ISODateWithMs)}};
        if (clean) {
            data.insert("ended", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            data.insert("hasNotifications", hasNotifications);
        }
        const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    }
    void writeBatches()
    {
        QFile segment;
        for (;;) {
            std::deque<LogRecord> batch;
            {
                std::unique_lock<std::mutex> guard(mutex);
                ready.wait(guard, [this] { return stopping || !pending.empty(); });
                if (pending.empty() && stopping)
                    return;
                batch.swap(pending);
            }
            std::size_t written = 0;
            for (const auto& row : batch) {
                const auto bytes = encode(row);
                if (segment.isOpen() && segment.pos() + bytes.size() > segmentSize)
                    segment.close();
                if (!segment.isOpen()) {
                    segment.setFileName(run.filePath(QString("events-%1.jsonl").arg(row.eventId, 20, 10, QChar('0'))));
                    if (!segment.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                        break;
                }
                if (segment.write(bytes) != bytes.size())
                    break;
                ++written;
            }
            const bool flushed = segment.isOpen() && segment.flush();
            if (!flushed || written < batch.size()) {
                const auto reason = segment.errorString();
                segment.close();
                std::lock_guard<std::mutex> guard(mutex);
                dropped += flushed ? batch.size() - written : batch.size();
                problem = QString::fromUtf8("日志归档写入受阻：") + reason;
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
            std::deque<LogRecord> recent;
            {
                std::lock_guard<std::mutex> guard(mutex);
                recent = tail;
                warning = problem;
            }
            for (auto it = recent.rbegin(); it != recent.rend(); ++it)
                if ((!before || it->eventId < before) && !visitor(*it))
                    return;
            if (!recent.empty() && (!before || recent.front().eventId < before))
                before = recent.front().eventId;
        }
        const QFileInfo folder(root.filePath(requested));
        if (!folder.isDir() || folder.isSymLink()) {
            warning = QString::fromUtf8("该运行的归档不可用。");
            return;
        }
        visitDisk(QDir(folder.absoluteFilePath()), before, warning, visitor, stop);
    }
};

RuntimeLogArchive::RuntimeLogArchive(const QString& workspace, const QString& version) : impl_(std::make_unique<Impl>())
{
    auto& d = *impl_;
    d.version = version;
    if (!d.openDirectory(QDir(workspace).absoluteFilePath(".cordis/logs"))) {
        const auto key =
            QCryptographicHash::hash(QFileInfo(workspace).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256)
                .toHex();
        const auto fallback = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                  .filePath("logs/" + QString::fromLatin1(key));
        if (!d.openDirectory(fallback)) {
            d.problem = QString::fromUtf8("日志目录不可写，本次仅保留最近记录。");
            return;
        }
        d.problem = QString::fromUtf8("工作区日志目录不可写，已改存到 ") + fallback;
    }
    d.writer = std::thread([this] { impl_->writeBatches(); });
}
RuntimeLogArchive::~RuntimeLogArchive() { finish(false); }
QString RuntimeLogArchive::currentRun() const { return impl_->id; }
QString RuntimeLogArchive::directory() const { return impl_->root.absolutePath(); }
QString RuntimeLogArchive::error() const
{
    std::lock_guard<std::mutex> guard(impl_->mutex);
    if (impl_->dropped)
        return impl_->problem + QString::fromUtf8("（%1 条未确认写入）").arg(impl_->dropped);
    return impl_->problem;
}
quint64 RuntimeLogArchive::append(const OutputEntry& e, const QString& sourceId, const QString& sourceName,
                                  qint64 utcMs, std::optional<OperationId> notification)
{
    auto& d = *impl_;
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.stopping)
        return 0;
    LogRecord row{e, sourceId, sourceName, utcMs, ++d.next, notification};
    if (notification)
        d.hasNotifications = true;
    collectLogSource(d.currentSources, row);
    d.tail.push_back(row);
    if (d.tail.size() > 4096)
        d.tail.pop_front();
    if (d.writer.joinable()) {
        if (d.pending.size() < 4096) {
            d.pending.push_back(std::move(row));
            d.ready.notify_one();
        } else {
            ++d.dropped;
            d.problem = QString::fromUtf8("日志归档队列已满。");
        }
    }
    return d.next;
}
void RuntimeLogArchive::finish(bool clean)
{
    auto& d = *impl_;
    {
        std::lock_guard<std::mutex> guard(d.mutex);
        if (d.stopping)
            return;
        d.stopping = true;
    }
    d.ready.notify_one();
    if (d.writer.joinable()) {
        d.writer.join();
        if (clean && !d.writeMetadata(true)) {
            std::lock_guard<std::mutex> guard(d.mutex);
            d.problem = QString::fromUtf8("日志终态写入失败。");
        }
    }
    d.lock.reset();
}
std::vector<LogRun> RuntimeLogArchive::runs() const
{
    const auto& d = *impl_;
    std::vector<std::pair<QString, LogRun>> history;
    for (const auto& dir : d.root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto key = dir.fileName();
        if (key == d.id || !validRun(key))
            continue;
        QFile file(QDir(dir.absoluteFilePath()).filePath("run.json"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 8192)
            continue;
        const auto meta = QJsonDocument::fromJson(file.readAll()).object();
        if (meta.value("run").toString() != key || meta.value("v").toInt() != 1)
            continue;
        const auto start = meta.value("started").toString();
        const auto stamp =
            QDateTime::fromString(start, Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
        const auto state = meta.contains("ended") ? QString::fromUtf8("正常结束") : QString::fromUtf8("结束未确认");
        history.push_back({start, {key, stamp + " · " + meta.value("version").toString() + " · " + state}});
    }
    std::sort(history.begin(), history.end(), [](const auto& a, const auto& b) {
        return a.first == b.first ? a.second.id > b.second.id : a.first > b.first;
    });
    std::vector<LogRun> list{{d.id, d.started.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") + " · " + d.version}};
    for (auto& run : history)
        list.push_back(std::move(run.second));
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
        QFile file(impl_->root.filePath(run.id + "/run.json"));
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
LogSourceCatalog RuntimeLogArchive::sourceCatalog(const QString& run) const
{
    auto& d = *impl_;
    {
        std::lock_guard<std::mutex> guard(d.mutex);
        if (run == d.id)
            return d.currentSources;
    }
    if (!validRun(run) || QFileInfo(d.root.filePath(run)).isSymLink())
        return {};
    const QDir folder(d.root.filePath(run));
    if (!folder.exists())
        return {};
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
        if (run == d.cachedRun && stamp == d.cachedTail)
            return d.cachedSources;
    }
    LogSourceCatalog values;
    QString warning;
    d.visit(run, 0, warning, [&](const LogRecord& row) {
        collectLogSource(values, row);
        return true;
    });
    if (warning.isEmpty() && (complete || stamp == tail())) {
        std::lock_guard<std::mutex> guard(d.mutex);
        d.cachedRun = run;
        d.cachedTail = stamp;
        d.cachedSources = values;
    }
    return values;
}
LogPage RuntimeLogArchive::query(const QString& run, const LogFilter& filter, quint64 before, int limit) const
{
    LogPage page;
    const auto count = static_cast<std::size_t>(std::clamp(limit, 1, 1000));
    impl_->visit(run, before, page.warning, [&](const LogRecord& row) {
        if (!matches(row, filter))
            return true;
        if (page.rows.size() == count) {
            page.before = page.rows.back().eventId;
            return false;
        }
        page.rows.push_back(row);
        return true;
    });
    std::reverse(page.rows.begin(), page.rows.end());
    return page;
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
} // namespace cordis::ui::detail
