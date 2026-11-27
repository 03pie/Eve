#include "runtime_log_archive_p.hpp"
#include "runtime_log_query_p.hpp"
#include "runtime_log_retention_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <iostream>

#define LOG_REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
using eve::ui::OutputEntry;
using eve::ui::OutputLevel;
using namespace eve::ui::detail;
namespace files = eve::testing::modules;

constexpr auto records = 32;
constexpr auto segmentBytes = 1600;

void writeJson(const QString& path, const QJsonObject& object)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    LOG_REQUIRE(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit());
}

QJsonObject readJson(const QString& path)
{
    const auto object = QJsonDocument::fromJson(files::read(path));
    LOG_REQUIRE(object.isObject());
    return object.object();
}

QDir archiveRoot(const QString& workspace)
{
    return QDir(QDir(workspace).filePath(".eve/logs"));
}

std::vector<QString> segments(const QDir& run)
{
    std::vector<QString> names;
    for (const auto& name : run.entryList({"events-*.jsonl"}, QDir::Files, QDir::Name))
        names.push_back(name);
    return names;
}

void awaitFile(const QString& path)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!QFileInfo::exists(path)) {
        LOG_REQUIRE(elapsed.elapsed() < 12000);
        QThread::msleep(10);
    }
}

void produce(const QString& workspace, const QDir& evidence, const QString& marker)
{
    LogArchiveOptions options;
    options.segmentBytes = segmentBytes;
    options.flushMs = 15;
    RuntimeLogArchive archive(workspace, "1.0.0", options);
    const QDir run(archiveRoot(workspace).filePath(archive.currentRun()));
    for (int i = 1; i <= records; ++i) {
        const auto message = marker + "/" + QString::number(i) + ":" + QString(400, 'x');
        OutputEntry entry{static_cast<quint64>(i), {}, "platform", "Platform", message.toStdString(),
                          OutputLevel::Info};
        LOG_REQUIRE(archive.append(entry, "platform", "Platform", QDateTime::currentMSecsSinceEpoch()) ==
                    static_cast<quint64>(i));
    }
    QElapsedTimer elapsed;
    elapsed.start();
    while (!readSavedLogEvent(workspace, archive.currentRun(), records)) {
        LOG_REQUIRE(elapsed.elapsed() < 10000);
        QThread::msleep(10);
    }
    LOG_REQUIRE(segments(run).size() > 2 && run.exists("active.lock"));
    writeJson(evidence.filePath(marker + ".ready.json"),
              {{"pid", QString::number(QCoreApplication::applicationPid())}, {"run", archive.currentRun()}});
    awaitFile(evidence.filePath(marker + ".close"));
    archive.finish(true);
    LOG_REQUIRE(!run.exists("active.lock"));
}

void startWriter(QProcess& process, const QString& workspace, const QDir& evidence, const QString& marker)
{
    process.setStandardOutputFile(evidence.filePath(marker + ".stdout"));
    process.setStandardErrorFile(evidence.filePath(marker + ".stderr"));
    process.start(QCoreApplication::applicationFilePath(), {"--produce", workspace, evidence.path(), marker});
    LOG_REQUIRE(process.waitForStarted(5000));
}

QString utc(const QDateTime& date)
{
    return date.toUTC().toString(Qt::ISODateWithMs);
}

QByteArray eventLine(quint64 id)
{
    QJsonObject values{{"v", 1}, {"event", QString::number(id)}, {"sequence", QString::number(id)},
                       {"utcMs", "10000"}, {"level", 0}, {"source", "platform"}, {"name", "Platform"},
                       {"channel", "platform"}, {"title", "Platform"}, {"text", "Preserved evidence"}};
    return QJsonDocument(values).toJson(QJsonDocument::Compact) + '\n';
}

QString makeClosed(const QDir& root, const QDateTime& end)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    LOG_REQUIRE(root.mkdir(id));
    writeJson(root.filePath(id + "/run.json"),
              {{"v", 1}, {"run", id}, {"started", utc(end.addSecs(-1))}, {"ended", utc(end)}, {"version", "1.0.0"}});
    return id;
}

QString segmentPath(const QDir& directory, quint64 ordinal)
{
    return directory.filePath(QString("events-%1.jsonl").arg(ordinal, 20, 10, QChar('0')));
}

QString digest(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
LogRunState runState(const QString& workspace, const QString& id)
{
    const auto runs = LogQuery(workspace).runs();
    const auto found = std::find_if(runs.cbegin(), runs.cend(), [&](const auto& run) { return run.id == id; });
    LOG_REQUIRE(found != runs.cend());
    return found->state;
}

void verify(const QDir& evidence)
{
    const auto workspace = evidence.filePath("workspace");
    LOG_REQUIRE(QDir().mkpath(workspace));
    QProcess first;
    QProcess second;
    startWriter(first, workspace, evidence, "first");
    startWriter(second, workspace, evidence, "second");
    const auto firstReady = evidence.filePath("first.ready.json");
    const auto secondReady = evidence.filePath("second.ready.json");
    awaitFile(firstReady);
    awaitFile(secondReady);
    const auto firstIdentity = readJson(firstReady);
    const auto secondIdentity = readJson(secondReady);
    const auto firstId = firstIdentity.value("run").toString();
    const auto secondId = secondIdentity.value("run").toString();
    LOG_REQUIRE(!firstId.isEmpty() && firstId != secondId && firstIdentity.value("pid") != secondIdentity.value("pid"));
    const auto root = archiveRoot(workspace);
    const QDir firstRun(root.filePath(firstId));
    const QDir secondRun(root.filePath(secondId));
    LOG_REQUIRE(firstRun.exists("active.lock") && secondRun.exists("active.lock"));
    LOG_REQUIRE(runState(workspace, firstId) == LogRunState::Active &&
                runState(workspace, secondId) == LogRunState::Active);
    const auto firstRows = segments(firstRun);
    const auto secondRows = segments(secondRun);
    const auto firstRecord = readSavedLogEvent(workspace, firstId, 1);
    const auto secondRecord = readSavedLogEvent(workspace, secondId, 1);
    LOG_REQUIRE(firstRecord && firstRecord->entry.text.rfind("first/", 0) == 0);
    LOG_REQUIRE(secondRecord && secondRecord->entry.text.rfind("second/", 0) == 0);
    const auto unlockedOnly = pruneClosedLogRuns(root, {0, 0}, QDateTime::currentDateTimeUtc());
    LOG_REQUIRE(unlockedOnly.removedSegments == 0 && unlockedOnly.bytesBefore > 2 * segmentBytes &&
                unlockedOnly.bytesAfter == unlockedOnly.bytesBefore && !unlockedOnly.warning.isEmpty());
    LOG_REQUIRE(segments(firstRun) == firstRows && segments(secondRun) == secondRows);

    files::write(evidence.filePath("first.close"), "close");
    LOG_REQUIRE(first.waitForFinished(10000) && first.exitStatus() == QProcess::NormalExit && first.exitCode() == 0);
    LOG_REQUIRE(!firstRun.exists("active.lock") && secondRun.exists("active.lock"));
    LOG_REQUIRE(runState(workspace, firstId) == LogRunState::Ended);
    const auto firstEntry = firstRun.filePath(firstRows.front());
    const auto original = files::read(firstEntry);
    const auto proof = firstRun.filePath("expired-00000000000000000001.json");
    const auto quota = pruneClosedLogRuns(root, {1000, unlockedOnly.bytesBefore - 1}, QDateTime::currentDateTimeUtc());
    LOG_REQUIRE(quota.removedSegments == 1 && !QFileInfo::exists(firstEntry) && QFileInfo::exists(proof));
    LOG_REQUIRE(readJson(proof).value("sha256").toString() == digest(original));
    LOG_REQUIRE(segments(secondRun) == secondRows && readLogExpiration(root, firstId, 1));
    LOG_REQUIRE(!readSavedLogEvent(workspace, firstId, 1) && readSavedLogEvent(workspace, firstId, records));

    // Model the interruption between persisting the proof and deleting its physical segment.
    const auto originalProof = files::read(proof);
    files::write(firstEntry, original);
    LOG_REQUIRE(!readLogExpiration(root, firstId, 1));
    LOG_REQUIRE(readSavedLogEvent(workspace, firstId, 1).has_value());

    const auto now = QDateTime::currentDateTimeUtc();
    const auto gapId = makeClosed(root, now.addDays(-30));
    const QDir gap(root.filePath(gapId));
    files::write(segmentPath(gap, 20), eventLine(20) + eventLine(21) + eventLine(25));
    const auto partialId = makeClosed(root, now.addDays(-35));
    const QDir partial(root.filePath(partialId));
    const auto partialRecord = eventLine(90) + "{\"v\":1,\"event\":";
    files::write(segmentPath(partial, 90), partialRecord);
    const auto ordinaryId = makeClosed(root, now.addDays(-38));
    const QDir ordinary(root.filePath(ordinaryId));
    files::write(segmentPath(ordinary, 100), eventLine(100));
    files::write(ordinary.filePath("not-managed.txt"), "Keep this file and skip this run.");

#ifdef Q_OS_WIN
    const QDir external(evidence.filePath("outside-archive"));
    LOG_REQUIRE(QDir().mkpath(external.path()));
    const auto redirectedId = makeClosed(external, now.addDays(-40));
    const QDir redirected(external.filePath(redirectedId));
    const auto redirectedSegment = segmentPath(redirected, 200);
    files::write(redirectedSegment, eventLine(200));
    QProcess junction;
    junction.start(qEnvironmentVariable("ComSpec", "cmd.exe"),
                   {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(root.filePath(redirectedId)),
                    QDir::toNativeSeparators(redirected.path())});
    LOG_REQUIRE(junction.waitForFinished(5000) && junction.exitCode() == 0);
#endif

    const auto swept = pruneClosedLogRuns(root, {0, 0}, now);
    LOG_REQUIRE(swept.removedSegments == firstRows.size() + 1);
    LOG_REQUIRE(files::read(proof) == originalProof);
    LOG_REQUIRE(!QFileInfo::exists(segmentPath(gap, 20)));
    LOG_REQUIRE(readLogExpiration(root, gapId, 20) && readLogExpiration(root, gapId, 21) &&
                !readLogExpiration(root, gapId, 22) && readLogExpiration(root, gapId, 25));
    LOG_REQUIRE(!hasLogExpirations(root, partialId) && readSavedLogEvent(workspace, partialId, 90) &&
                files::read(segmentPath(partial, 90)) == partialRecord);
    LOG_REQUIRE(QFileInfo::exists(segmentPath(ordinary, 100)) && ordinary.exists("not-managed.txt"));
    LOG_REQUIRE(segments(secondRun) == secondRows && second.state() == QProcess::Running);
#ifdef Q_OS_WIN
    LOG_REQUIRE(files::read(redirectedSegment) == eventLine(200));
    LOG_REQUIRE(!hasLogExpirations(root, redirectedId));
#endif

    // Kill only the child started for this test. Its unlock/recovery will not fabricate a terminal state.
    second.kill();
    LOG_REQUIRE(second.waitForFinished(10000) && second.exitStatus() == QProcess::CrashExit);
    const auto unclean = readSavedLogRun(workspace, secondId);
    LOG_REQUIRE(unclean && !unclean->ended && readSavedLogEvent(workspace, secondId, records));
    LOG_REQUIRE(runState(workspace, secondId) == LogRunState::Unconfirmed);
    LOG_REQUIRE(pruneClosedLogRuns(root, {0, 0}, QDateTime::currentDateTimeUtc()).removedSegments == 0);
    LOG_REQUIRE(segments(secondRun) == secondRows);

    QJsonArray archivedFirst;
    for (const auto& segment : firstRows)
        archivedFirst.append(segment);
    QJsonArray archivedSecond;
    for (const auto& segment : secondRows)
        archivedSecond.append(QJsonObject{{"name", segment}, {"sha256", digest(files::read(secondRun.filePath(segment)))}});
    writeJson(evidence.filePath("result.json"),
              {{"passed", true},
               {"normalPid", firstIdentity.value("pid")}, {"normalExit", first.exitCode()}, {"normalRun", firstId},
               {"uncleanPid", secondIdentity.value("pid")}, {"uncleanRun", secondId}, {"uncleanEnded", unclean->ended},
               {"segmentsBefore", archiveRoot(workspace).path()}, {"normalSegments", archivedFirst},
               {"uncleanSegments", archivedSecond}, {"bytesBeforeQuota", QString::number(quota.bytesBefore)},
               {"bytesAfterQuota", QString::number(quota.bytesAfter)}, {"bytesAfterSweep", QString::number(swept.bytesAfter)},
               {"expiredSegmentSha256", digest(original)}, {"expiredProofSha256", digest(originalProof)},
               {"partialSegmentSha256", digest(partialRecord)}, {"gapRun", gapId}});
}
} // namespace

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 5 && args[1] == "--produce") {
            produce(args[2], QDir(args[3]), args[4]);
        } else {
            LOG_REQUIRE(args.size() == 2 && QDir().mkpath(args[1]));
            QTemporaryDir scratch(QDir(args[1]).filePath("logging-l5-XXXXXX"));
            LOG_REQUIRE(scratch.isValid());
            scratch.setAutoRemove(false);
            std::cout << "LOG_RETENTION_EVIDENCE=" << scratch.path().toUtf8().constData() << '\n' << std::flush;
            verify(QDir(scratch.path()));
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "LOG-2 retention: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
