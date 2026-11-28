#include "runtime_log_query_p.hpp"
#include "runtime_log_retention_p.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
using namespace eve;
using namespace eve::ui;
using namespace eve::ui::detail;

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
}

QMap<QString, QByteArray> digests(const QString& directory)
{
    QMap<QString, QByteArray> result;
    const QDir root(directory);
    QDirIterator files(directory, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        QFile file(files.next());
        CHECK(file.open(QIODevice::ReadOnly));
        result.insert(root.relativeFilePath(file.fileName()),
                      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
    }
    return result;
}

std::vector<quint64> ids(const std::vector<LogRecord>& rows)
{
    std::vector<quint64> result;
    for (const auto& row : rows)
        result.push_back(row.eventId);
    return result;
}

LogRecord sample(std::uint64_t sequence, const QString& source, OutputLevel level, Epoch epoch, qint64 time,
                 const char* text)
{
    LogRecord row{{sequence, ContextIdentity{InstanceId{7}, epoch}, "same-channel", "ignored title", text, level},
                  source, source, time};
    row.moduleVersion = "1.2.3";
    return row;
}

void snapshotAndResume(const QString& workspace, bool preserveEvidence)
{
    LogArchiveOptions options;
    options.flushMs = 10;
    options.segmentBytes = 280;
    RuntimeLogArchive archive(workspace, "test", options);
    const LogQuery query(archive);
    const auto run = archive.currentRun();
    const auto active = LogQuery(workspace).runs();
    if (active.size() != 1 || active[0].state != LogRunState::Active) {
        const QDir folder(QDir(archive.directory()).filePath(run));
        std::cerr << "active query observed " << active.size() << " run(s), state "
                  << (active.empty() ? -1 : static_cast<int>(active[0].state)) << ", lock "
                  << folder.exists("active.lock") << ", metadata " << folder.exists("run.json") << '\n';
    }
    CHECK(active.size() == 1 && active[0].id == run && active[0].version == "test" &&
          active[0].state == LogRunState::Active && !active[0].endedUtcMs && active[0].startedUtcMs > 0);
    auto append = [&](quint64 sequence, const QString& source, OutputLevel level, Epoch epoch, qint64 time,
                      const char* text) {
        auto row = sample(sequence, source, level, epoch, time, text);
        if (sequence == 10) {
            row.diagnosticCode = "TEST_WARNING";
            row.operation = OperationId{37};
        }
        return archive.append(std::move(row));
    };
    CHECK(append(10, "alpha", OutputLevel::Warning, 2, 1002, "selected old") == 1);
    CHECK(append(11, "alpha", OutputLevel::Error, 1, 1002, "excluded-private-epoch") == 2);
    CHECK(append(12, "beta", OutputLevel::Warning, 2, 1002, "selected other") == 3);
    CHECK(append(13, "alpha", OutputLevel::Info, 2, 1002, "selected low") == 4);
    CHECK(append(6, "alpha", OutputLevel::Error, 2, 1001, "SELECTED late") == 5);
    CHECK(append(14, "alpha", OutputLevel::Warning, 2, 1004, "selected outside time") == 6);

    LogFilter filter;
    filter.sourceId = "alpha";
    filter.text = "selected";
    filter.minimumSeverity = LogSeverity::Warning;
    filter.instance = InstanceId{7};
    filter.epoch = 2;
    filter.fromUtcMs = 1001;
    filter.throughUtcMs = 1002;
    filter.moduleVersion = "1.2.3";
    const auto cursor = query.begin(run, filter);
    CHECK(cursor.position().run == run && cursor.position().through == 6);
    const auto first = query.page(cursor, 1);
    CHECK(first.warning.isEmpty() && first.older && ids(first.rows) == std::vector<quint64>{5});
    CHECK(first.rows.front().entry.sequence == 6 && first.rows.front().utcMs == 1001);
    LogSubscription subscription(cursor.position(), filter);

    quint64 appended = 0;
    std::thread producer([&] {
        append(16, "alpha", OutputLevel::Error, 2, 1002, "selected new A");
        append(17, "alpha", OutputLevel::Error, 2, 1002, "selected new B");
        appended = append(18, "new-source", OutputLevel::Warning, 2, 1001, "match excluded by source");
    });
    producer.join();
    CHECK(appended == 9);
    const auto older = query.page(*first.older, 1);
    CHECK(!older.older && ids(older.rows) == std::vector<quint64>{1});
    const auto again = query.page(cursor, 100);
    CHECK((ids(again.rows) == std::vector<quint64>{1, 5}));

    const auto live = subscription.poll(query, 1);
    CHECK(live && ids(live->rows) == std::vector<quint64>{7} && live->more && live->resume.through == 7);
    CHECK(live->sources.contains("new-source") && live->persistence && live->persistence->accepted == 9);
    CHECK(live->sources["alpha"].firstEvent == 1 && live->sources["alpha"].lastEvent == 8 &&
          live->sources["alpha"].levels.count(LogSeverity::Error) == 1);
    const auto checkpoint = subscription.checkpoint();
    subscription.cancel();
    CHECK(!subscription.poll(query));
    LogSubscription reconnected(checkpoint, filter);
    const auto resumed = reconnected.poll(query, 1);
    CHECK(resumed && ids(resumed->rows) == std::vector<quint64>{8} && !resumed->more && resumed->resume.through == 9);
    CHECK(resumed->missing.empty());
    CHECK(reconnected.poll(query)->rows.empty());
    archive.finish(true);

    const LogQuery offline(workspace);
    const auto original = digests(archive.directory());
    CHECK(original.size() > 3);
    const auto finished = offline.runs();
    CHECK(finished.size() == 1 && finished[0].state == LogRunState::Ended && finished[0].endedUtcMs &&
          *finished[0].endedUtcMs >= finished[0].startedUtcMs);
    const auto catalog = offline.sources(run);
    CHECK(catalog.warning.isEmpty() && catalog.sources.contains("new-source"));
    const auto located = offline.locate(run, 3);
    CHECK(located.status == LogLookupStatus::Found && located.record && located.record->sourceId == "beta");
    CHECK(located.neighborhood && ids(offline.page(*located.neighborhood).rows) == std::vector<quint64>{3});
    CHECK(offline.locate(run, 10).status == LogLookupStatus::NotFound);
    QBuffer buffer;
    CHECK(buffer.open(QIODevice::WriteOnly));
    QString error;
    CHECK(offline.exportText(cursor, buffer, error) && error.isEmpty());
    const auto exportBytes = buffer.data();
    CHECK(exportBytes.split('\n').size() == 3 && exportBytes.contains(" / event 1]") && exportBytes.contains(" / event 5]"));
    CHECK(exportBytes.indexOf("selected old") < exportBytes.indexOf("SELECTED late") &&
          !exportBytes.contains("excluded-private-epoch") && !exportBytes.contains("selected new"));
    CHECK(exportBytes.contains("[code=TEST_WARNING] [op=37]"));
    CHECK(digests(archive.directory()) == original);
    if (preserveEvidence) {
        write(QDir(workspace).filePath("filtered-export.txt"), exportBytes);
        const QDir runFolder(QDir(archive.directory()).filePath(run));
        const QDir copyFolder(QDir(workspace).filePath("before-retention/" + run));
        CHECK(copyFolder.mkpath("."));
        for (const auto& file : runFolder.entryList(QDir::Files))
            CHECK(QFile::copy(runFolder.filePath(file), copyFolder.filePath(file)));
    }

    const auto retention = pruneClosedLogRuns(QDir(archive.directory()), LogRetentionPolicy{},
                                             QDateTime::currentDateTimeUtc().addDays(20));
    CHECK(retention.removedSegments > 1);
    const auto expired = offline.locate(run, 3);
    CHECK(expired.status == LogLookupStatus::Expired && !expired.record && expired.warning.isEmpty());
    CHECK(offline.locate(run, 10).status == LogLookupStatus::NotFound);
    const auto expiredPage = offline.page(cursor);
    CHECK(expiredPage.rows.empty() && !expiredPage.expired.empty() && expiredPage.warning.isEmpty());
}

void structuredSource(const QString& workspace)
{
    using Ids = std::vector<quint64>;
    RuntimeLogArchive archive(workspace, "test");
    const auto run = archive.currentRun();
    const auto push = [&](quint64 sequence, const char* channel, const char* module, const char* instanceKey,
                          const QString& source = "alpha") {
        auto row = sample(sequence, source, OutputLevel::Info, 2, 1001, "same body");
        row.entry.channel = channel;
        row.moduleId = QString::fromLatin1(module);
        row.instanceKey = QString::fromLatin1(instanceKey);
        CHECK(archive.append(std::move(row)) == sequence);
    };
    push(1, "plugin/7/2/jobs/status", "org.eve.a", "first");
    push(2, "plugin/7/2/jobs/status", "org.eve.b", "first");
    push(3, "plugin/7/2/jobs/status", "org.eve.a", "second");
    push(4, "plugin/7/2/jobs", "org.eve.a", "first");
    push(5, "jobs/status", "org.eve.a", "first"); // A legacy row with no transport prefix.
    push(6, "native/7/2/jobs/status", "org.eve.a", "first", "native/org.eve.a/alpha");
    push(7, "plugin/99/2/jobs/status", "org.eve.a", "first"); // Prefix does not match the recorded owner.

    LogFilter filter;
    filter.moduleId = "org.eve.a";
    filter.instanceKey = "first";
    filter.channel = "jobs/status";
    const LogQuery live(archive);
    const auto cursor = live.begin(run, filter);
    CHECK((ids(live.page(cursor).rows) == Ids{1, 5, 6}));
    archive.finish(true);
    const LogQuery offline(workspace);
    CHECK((ids(offline.page(cursor).rows) == Ids{1, 5, 6}));
    filter.sourceId = "alpha";
    CHECK((ids(offline.page(offline.begin(run, filter)).rows) == Ids{1, 5}));
    filter.sourceId.clear();
    filter.channel = "plugin/99/2/jobs/status";
    CHECK(ids(offline.page(offline.begin(run, filter)).rows) == Ids{7});
}

void mergeTailAndSegments(const QString& workspace)
{
    LogArchiveOptions options;
    options.flushMs = 1;
    options.queueRecords = 5000;
    RuntimeLogArchive archive(workspace, "test-v1", options);
    constexpr quint64 expected = 4200;
    for (quint64 event = 1; event <= expected; ++event)
        CHECK(archive.append(sample(event, "platform", OutputLevel::Info, 1, 6000, "fixture")) == event);
    archive.finish(true);
    CHECK(archive.persistence().confirmed == expected);
    const LogQuery query(archive);
    std::optional<LogCursor> cursor = query.begin(archive.currentRun());
    auto next = expected;
    while (cursor) {
        const auto page = query.page(*cursor, 701);
        CHECK(page.warning.isEmpty() && page.missing.empty() && !page.rows.empty());
        for (auto it = page.rows.rbegin(); it != page.rows.rend(); ++it)
            CHECK(it->eventId == next--);
        cursor = page.older;
    }
    CHECK(next == 0);
}

void checkSourcesCli(const QString& executable, const QString& workspace, const QString& run,
                     int expectedExit, const QByteArray& expectedOutput, const QByteArray& expectedWarning)
{
    QProcess query;
    query.start(executable, {"--workspace", workspace, "--run", run, "--sources"});
    CHECK(query.waitForFinished(8000));
    CHECK(query.exitStatus() == QProcess::NormalExit && query.exitCode() == expectedExit);
    CHECK(query.readAllStandardOutput().trimmed() == expectedOutput);
    CHECK(query.readAllStandardError().trimmed() == expectedWarning);
}

/** @brief 新运行写入 Eve 目录，UI 与命令行仍能读取旧目录；同一运行标识优先读 Eve。 */
void previousWorkspace(const QString& workspace, const QString& executable)
{
    const QString run = "c0cd39b9-7694-4ab3-a382-a05fd5641873";
    const QDir old(QDir(workspace).filePath(".cordis/logs/" + run));
    CHECK(old.mkpath("."));
    const QByteArray meta = QJsonDocument(QJsonObject{{"v", 1}, {"run", run}, {"version", "0.1.5"},
                                                      {"ended", "2026-01-01T00:00:00.000Z"}}).toJson();
    write(old.filePath("run.json"), meta);
    const auto event = [](const char* source) {
        return QJsonDocument(QJsonObject{{"v", 1}, {"event", "1"}, {"sequence", "1"}, {"source", source},
                                        {"level", 0}, {"text", "saved event"}}).toJson(QJsonDocument::Compact) + '\n';
    };
    const QString segment = "events-00000000000000000001.jsonl";
    write(old.filePath(segment), event("previous"));
    const auto originals = digests(old.path());
    RuntimeLogArchive archive(workspace, "eve");
    CHECK(archive.directory() == QDir(workspace).filePath(".eve/logs"));
    const LogQuery live(archive);
    CHECK(live.runs().size() == 2 && live.sources(run).sources.contains("previous"));
    CHECK(live.page(live.begin(run)).rows.at(0).sourceId == "previous");
    checkSourcesCli(executable, workspace, run, EXIT_SUCCESS, "previous\t\t1-1", {});
    const QDir current(QDir(archive.directory()).filePath(run));
    CHECK(current.mkpath("."));
    write(current.filePath("run.json"), meta);
    write(current.filePath(segment), event("current"));
    CHECK(live.runs().size() == 2 && live.sources(run).sources.contains("current"));
    checkSourcesCli(executable, workspace, run, EXIT_SUCCESS, "current\t\t1-1", {});
    archive.finish(true);
    CHECK(digests(old.path()) == originals);
}

void truncatedAndGapped(const QString& workspace, const QString& executable)
{
    const QString run = "87f9eddb-9d82-4d07-bcdd-0a694c571515";
    const QDir directory(QDir(workspace).filePath(".eve/logs/" + run));
    CHECK(directory.mkpath("."));
    const auto metadata = QJsonDocument(QJsonObject{{"v", 1}, {"run", run}, {"version", "1.0.0"},
                                                   {"started", "2026-01-01T00:00:00.000Z"}});
    write(directory.filePath("run.json"), metadata.toJson());
    const auto line = [](int event) {
        return QJsonDocument(QJsonObject{{"v", 1}, {"event", QString::number(event)}, {"sequence", QString::number(event)},
                                        {"utcMs", "9111"}, {"source", "platform"}, {"level", 0},
                                        {"text", "fixture"}}).toJson(QJsonDocument::Compact) + '\n';
    };
    checkSourcesCli(executable, workspace, run, EXIT_SUCCESS, {}, {});
    const auto segment = directory.filePath("events-00000000000000000001.jsonl");
    write(segment, line(1));
    checkSourcesCli(executable, workspace, run, EXIT_SUCCESS, "platform\t\t1-1", {});
    write(segment, line(1) + line(3) + line(4) + "{\"v\":");
    const LogQuery query(workspace);
    const auto catalog = query.sources(run);
    CHECK(!catalog.warning.isEmpty() && catalog.sources.contains("platform"));
    checkSourcesCli(executable, workspace, run, EXIT_FAILURE, "platform\t\t1-4", catalog.warning.toUtf8());
    const auto runs = query.runs();
    CHECK(runs.size() == 1 && runs[0].state == LogRunState::Unconfirmed && !runs[0].endedUtcMs);
    const auto cursor = query.begin(run);
    const auto page = query.page(cursor);
    CHECK((ids(page.rows) == std::vector<quint64>{1, 3, 4}) && !page.warning.isEmpty() && page.expired.empty());
    CHECK(page.rows.front().sourceName.isEmpty());
    CHECK(formatLogText(page.rows.front()).contains(QStringLiteral("[INFO] Platform")));
    const auto replay = query.replay({run, 1}, {}, 1);
    CHECK(replay.more && replay.resume.through == 3 && ids(replay.rows) == std::vector<quint64>{3});
    CHECK(replay.missing.size() == 1 && replay.missing[0].first == 2 && replay.missing[0].last == 2);
    const auto next = query.replay(replay.resume, {}, 1);
    CHECK(!next.more && next.resume.through == 4 && ids(next.rows) == std::vector<quint64>{4} && next.missing.empty());
    CHECK(query.locate(run, 2).status == LogLookupStatus::Unavailable);
    CHECK(query.locate(run, 1).status == LogLookupStatus::Found);
    QBuffer destination;
    CHECK(destination.open(QIODevice::WriteOnly));
    QString error;
    CHECK(!query.exportText(cursor, destination, error) && !error.isEmpty() && destination.data().isEmpty());
}
}

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    try {
        const auto arguments = app.arguments();
        CHECK(arguments.size() <= 3);
        const bool preserveEvidence = arguments.size() >= 2;
        const QDir artifactParent(preserveEvidence ? arguments.at(1) : QDir::tempPath());
        CHECK(artifactParent.mkpath("."));
        QTemporaryDir workspace(artifactParent.filePath("logging-l6-XXXXXX"));
        CHECK(workspace.isValid());
        workspace.setAutoRemove(!preserveEvidence);
        if (preserveEvidence)
            std::cout << "L6 evidence: " << workspace.path().toUtf8().constData() << std::endl;
        snapshotAndResume(workspace.filePath("snapshot"), preserveEvidence);
        mergeTailAndSegments(workspace.filePath("merge"));
        const auto cli = arguments.value(2, QDir(app.applicationDirPath()).filePath("eve_log_query.exe"));
        truncatedAndGapped(workspace.filePath("truncated"), cli);
        previousWorkspace(workspace.filePath("previous"), cli);
        structuredSource(workspace.filePath("sources"));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "L6 log query: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
