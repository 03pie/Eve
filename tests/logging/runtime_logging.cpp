/** @brief Verify actual GUI-host writes with an independent, QCoreApplication-only archive reader. */
#include "runtime_log_archive_p.hpp"
#include "workbench.hpp"
#include "../support/module_artifacts.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstring>
#include <iostream>

#define LOG_REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace {
using eve::ui::OutputLevel;
using eve::ui::detail::readSavedLogEvent;
using eve::ui::detail::readSavedLogRun;
namespace files = eve::testing::modules;

QJsonObject parse(const QByteArray& bytes)
{
    const auto document = QJsonDocument::fromJson(bytes);
    LOG_REQUIRE(document.isObject());
    return document.object();
}

template<class T>
typename eve::Task<T>::Snapshot settle(const eve::Task<T>& task)
{
    QElapsedTimer timeout;
    timeout.start();
    while (!task.isReady()) {
        LOG_REQUIRE(timeout.elapsed() < 8000);
        QTest::qWait(5);
    }
    const auto result = task.tryResult();
    LOG_REQUIRE(result);
    return result;
}
template<class T>
typename eve::Task<T>::Snapshot wait(const eve::Task<T>& task)
{
    auto result = settle(task);
    LOG_REQUIRE(result->hasValue());
    return result;
}

struct Producer {
    eve::workbench::Workbench host;
    bool closed = false;

    void close()
    {
        wait(host.close());
        LOG_REQUIRE(host.scheduler->close().hasValue());
        LOG_REQUIRE(!host.kernel && !host.session);
        const auto scheduler = host.scheduler->snapshot();
        LOG_REQUIRE(scheduler.hasValue() && !scheduler.value().open && scheduler.value().queued == 0 &&
                    scheduler.value().callbackErrors == 0);
        closed = true;
    }

    ~Producer()
    {
        if (closed)
            return;
        try {
            close();
        } catch (const std::exception& error) {
            std::cerr << "cleanup: " << error.what() << '\n';
        }
    }
};

void produce(const QString& manifest, const QString& workspace, const QDir& evidence, bool conflict)
{
    const auto applicationSuffix = QCryptographicHash::hash(workspace.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    QCoreApplication::setApplicationName("EveLogging-" + QString::fromLatin1(applicationSuffix));
    QStandardPaths::setTestModeEnabled(true);
    Producer producer;
    wait(producer.host.open(manifest, workspace));
    const auto result = wait(producer.host.applyInitial().settled);
    LOG_REQUIRE(result->value().outcome == eve::loader::PlanOutcome::Applied);
    const auto snapshot = producer.host.desktop()->output()->snapshot();
    LOG_REQUIRE(snapshot.hasValue());
    const auto& rows = snapshot.value();
    const auto rejected = [](const auto& row) { return row.text.rfind("TEST_OPERATION_REJECTED:", 0) == 0; };
    LOG_REQUIRE(std::count_if(rows.begin(), rows.end(), rejected) == 1);
    const auto& event = *std::find_if(rows.begin(), rows.end(), rejected);
    LOG_REQUIRE(event.level == OutputLevel::Error && event.owner && event.sequence > 0);
    if (conflict) {
        const auto beforeClose = producer.host.session->snapshot();
        LOG_REQUIRE(beforeClose.hasValue() && beforeClose.value().store.dirty);
        const auto revision = beforeClose.value().coordinator.desiredRevision;
        const auto config = QDir(workspace).filePath("session.yaml");
        files::write(config, "# External edit before close\n");
        const auto failed = settle(producer.host.close());
        LOG_REQUIRE(!failed->hasValue() && failed->error().code == eve::ErrorCode::ConfigConflict);
        LOG_REQUIRE(producer.host.canRetryClose());
        LOG_REQUIRE(producer.host.desktop()->cancelCloseRequest().hasValue());
        LOG_REQUIRE(producer.host.writeOutput("TEST_AFTER_CANCELLED_CLOSE").hasValue());
        LOG_REQUIRE(producer.host.session->rebaseExternal(revision).hasValue());
    }
    producer.close();
    const QJsonObject receipt{{"pid", QString::number(QCoreApplication::applicationPid())},
        {"sequence", QString::number(event.sequence)}, {"instance", QString::number(event.owner->instance.value())},
        {"epoch", QString::number(event.owner->epoch)}, {"eventText", QString::fromStdString(event.text)},
        {"normalClose", true}};
    files::write(evidence.filePath("plugin-emission.json"), QJsonDocument(receipt).toJson());
}

QJsonObject archiveInventory(const QDir& logs)
{
    QJsonObject entries;
    QDirIterator it(logs.path(), QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto absolute = it.next();
        const auto info = it.fileInfo();
        LOG_REQUIRE(!info.isSymLink());
        const auto relative = logs.relativeFilePath(absolute);
        if (info.isDir())
            entries.insert(relative + '/', QStringLiteral("directory"));
        else
            entries.insert(relative, QString::fromLatin1(
                QCryptographicHash::hash(files::read(absolute), QCryptographicHash::Sha256).toHex()));
    }
    return entries;
}

qint64 launch(const QDir& evidence, const QString& program, const QStringList& arguments,
              const QString& stage, int expectedExit)
{
    QProcess child;
    child.setProgram(program);
    child.setArguments(arguments);
    child.setStandardOutputFile(evidence.filePath(stage + ".stdout.raw"));
    child.setStandardErrorFile(evidence.filePath(stage + ".stderr.raw"));
    child.start();
    LOG_REQUIRE(child.waitForStarted());
    const auto pid = child.processId();
    LOG_REQUIRE(child.waitForFinished(20000));
    const QJsonObject receipt{{"pid", QString::number(pid)}, {"exitCode", child.exitCode()},
        {"normalExit", child.exitStatus() == QProcess::NormalExit}, {"program", program},
        {"argv", QJsonArray::fromStringList(arguments)}};
    files::write(evidence.filePath(stage + "-process.json"), QJsonDocument(receipt).toJson());
    LOG_REQUIRE(child.exitStatus() == QProcess::NormalExit && child.exitCode() == expectedExit);
    return pid;
}

QJsonObject verifyRun(const QString& workspace, const QString& diagnostic, const QString& source,
                      const QList<QByteArray>& excluded = {})
{
    const QDir logs(QDir(workspace).filePath(".eve/logs"));
    const auto runs = logs.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    LOG_REQUIRE(runs.size() == 1);
    const auto run = runs.front();
    const QDir folder(logs.filePath(run));
    LOG_REQUIRE(!folder.exists("active.lock"));
    const auto metadata = parse(files::read(folder.filePath("run.json")));
    LOG_REQUIRE(metadata.value("run") == run && !metadata.value("version").toString().isEmpty());
    LOG_REQUIRE(QDateTime::fromString(metadata.value("ended").toString(), Qt::ISODateWithMs).isValid());

    QJsonObject expected;
    quint64 previous = 0;
    quint64 cleanup = 0;
    const auto segments = folder.entryList({"events-*.jsonl"}, QDir::Files, QDir::Name);
    LOG_REQUIRE(!segments.isEmpty());
    for (const auto& segment : segments) {
        const auto bytes = files::read(folder.filePath(segment));
        LOG_REQUIRE(bytes.endsWith('\n'));
        for (const auto& value : excluded)
            LOG_REQUIRE(!bytes.contains(value));
        for (const auto& line : bytes.chopped(1).split('\n')) {
            const auto row = parse(line);
            const auto event = row.value("event").toString().toULongLong();
            LOG_REQUIRE(event > previous);
            previous = event;
            const auto message = row.value("text").toString();
            if (message.startsWith(diagnostic + ':')) {
                LOG_REQUIRE(expected.isEmpty());
                expected = row;
            }
            if (message.contains("WORKBENCH_CLEANUP_COMPLETE")) {
                LOG_REQUIRE(!cleanup && row.value("source") == "platform");
                cleanup = event;
            }
        }
    }
    LOG_REQUIRE(expected.value("source") == source && expected.value("level").toInt(-1) == static_cast<int>(OutputLevel::Error));
    const auto eventId = expected.value("event").toString().toULongLong();
    LOG_REQUIRE(eventId > 0 && cleanup == previous && cleanup > eventId);

    const auto inventory = archiveInventory(logs);
    const auto saved = readSavedLogRun(workspace, run);
    LOG_REQUIRE(saved && saved->ended && saved->version == metadata.value("version").toString());
    const auto found = readSavedLogEvent(workspace, run, eventId);
    LOG_REQUIRE(found && found->eventId == eventId && found->sourceId == source);
    LOG_REQUIRE(QString::fromStdString(found->entry.text) == expected.value("text").toString());
    LOG_REQUIRE(found->entry.sequence == expected.value("sequence").toString().toULongLong());
    LOG_REQUIRE(found->entry.level == OutputLevel::Error && found->utcMs == expected.value("utcMs").toString().toLongLong());
    if (source == "platform") {
        LOG_REQUIRE(!expected.contains("instance") && !found->entry.owner);
    } else {
        LOG_REQUIRE(found->entry.owner && found->entry.owner->instance.value() == expected.value("instance").toString().toULongLong());
        LOG_REQUIRE(found->entry.owner->epoch == expected.value("epoch").toString().toULongLong());
    }
    const auto ended = readSavedLogEvent(workspace, run, cleanup);
    LOG_REQUIRE(ended && ended->sourceId == "platform" && ended->entry.text.find("WORKBENCH_CLEANUP_COMPLETE") != std::string::npos);
    LOG_REQUIRE(!readSavedLogEvent(workspace, run, cleanup + 1));
    LOG_REQUIRE(archiveInventory(logs) == inventory);
    return {{"run", run}, {"metadata", metadata}, {"rawEvent", expected}, {"eventId", QString::number(eventId)},
        {"cleanupEventId", QString::number(cleanup)}, {"unchangedArchive", inventory}};
}

void verify(const QStringList& args, const QDir& evidence)
{
    const bool conflict = args.size() == 5 && args[4] == "close_retry";
    const auto workspace = evidence.filePath(QString::fromUtf8(u8"测试工作区 𠮷"));
    const auto fixture = files::install(args[1], evidence.filePath("module"), "org.eve.test.logging",
                                       QJsonArray{"test.logging"}, "qt-ui");
    const auto pid = launch(evidence, QCoreApplication::applicationFilePath(),
                            {conflict ? "--cancel-close" : "--producer", fixture, workspace, evidence.path()},
                            "plugin", EXIT_SUCCESS);
    const auto producer = parse(files::read(evidence.filePath("plugin-emission.json")));
    const auto producerPid = producer.value("pid").toString().toLongLong();
    LOG_REQUIRE(producerPid > 0 && producerPid != QCoreApplication::applicationPid());
    LOG_REQUIRE(!pid || pid == producerPid);
    LOG_REQUIRE(producer.value("normalClose").toBool());
    const auto plugin = verifyRun(workspace, "TEST_OPERATION_REJECTED", "plugin/test.logging");
    const auto pluginEvent = plugin.value("rawEvent").toObject();
    LOG_REQUIRE(pluginEvent.value("text") == producer.value("eventText"));
    LOG_REQUIRE(pluginEvent.value("module") == "org.eve.test.logging" &&
                pluginEvent.value("moduleVersion") == "1.0.0" &&
                pluginEvent.value("generation").toString().toULongLong() > 0 &&
                pluginEvent.value("sourceSequence") == pluginEvent.value("sequence"));
    LOG_REQUIRE(pluginEvent.value("sequence") == producer.value("sequence") &&
                pluginEvent.value("instance") == producer.value("instance") && pluginEvent.value("epoch") == producer.value("epoch"));
    if (conflict) {
        int continued = 0;
        const auto run = plugin.value("run").toString();
        const auto final = plugin.value("cleanupEventId").toString().toULongLong();
        const auto warning = eve::ui::detail::scanSavedDescending(workspace, run, 0, [&](const auto& row) {
            if (row.entry.text == "TEST_AFTER_CANCELLED_CLOSE") {
                LOG_REQUIRE(row.sourceId == "platform" && final > row.eventId);
                ++continued;
            }
            return true;
        });
        LOG_REQUIRE(warning.isEmpty() && continued == 1);
        files::write(evidence.filePath("result.json"), QJsonDocument(QJsonObject{{"passed", true}, {"plugin", plugin}}).toJson());
        return;
    }

    const auto badModuleWorkspace = evidence.filePath("bad-module-workspace");
    const QByteArray manifestDirName("EVE_L1_PRIVATE_MODULE");
    const QByteArray invalidContents("EVE_L1_PRIVATE_MANIFEST_PAYLOAD");
    const auto malformedDir = evidence.filePath(QString::fromLatin1(manifestDirName));
    LOG_REQUIRE(QDir().mkpath(malformedDir));
    const auto invalidManifest = QDir(malformedDir).filePath("module.json");
    files::write(invalidManifest, invalidContents);
    launch(evidence, args[2], {"--module", invalidManifest, "--workspace", badModuleWorkspace},
           "manifest", EXIT_FAILURE);
    const auto manifest = verifyRun(badModuleWorkspace, "QT_MANIFEST_INVALID", "platform", {manifestDirName, invalidContents});
    LOG_REQUIRE(plugin.value("run") != manifest.value("run"));
    const QJsonObject result{{"queryPid", QString::number(QCoreApplication::applicationPid())},
        {"plugin", plugin}, {"manifest", manifest}, {"passed", true}};
    files::write(evidence.filePath("result.json"), QJsonDocument(result).toJson());
}
}

int main(int argc, char** argv)
{
    try {
        if (argc > 1 && (std::strcmp(argv[1], "--producer") == 0 || std::strcmp(argv[1], "--cancel-close") == 0)) {
            QApplication app(argc, argv);
            app.setQuitOnLastWindowClosed(false);
            const auto args = app.arguments();
            LOG_REQUIRE(args.size() == 5);
            produce(args[2], args[3], QDir(args[4]), args[1] == "--cancel-close");
            return EXIT_SUCCESS;
        }
        const QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        LOG_REQUIRE((args.size() == 4 || args.size() == 5) && QDir().mkpath(args[3]));
        QTemporaryDir directory(QDir(args[3]).filePath(args.size() == 5 ? "logging-l3-close-XXXXXX" : "logging-l1-XXXXXX"));
        LOG_REQUIRE(directory.isValid());
        directory.setAutoRemove(false);
        std::cout << "LOG_ARCHIVE_EVIDENCE=" << directory.path().toUtf8().constData() << '\n' << std::flush;
        verify(args, QDir(directory.path()));
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "LOG-1 acceptance: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
