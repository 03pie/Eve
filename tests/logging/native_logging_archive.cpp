/** @brief Exercise the archived path with a real Native ABI producer and Ela workbench. */
#include "../support/module_artifacts.hpp"
#include "runtime_log_archive_p.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>
#include <eve/module/manager.hpp>
#include <eve/module/native.hpp>
#include <eve/ui/ela_shell.hpp>
#include <eve_abi/plugin.h>
#include <windows.h>
#include <cstring>
#include <iostream>
#include <map>

#define REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace {
using namespace eve;
using namespace eve::module;
using namespace eve::ui;
using eve::ui::detail::LogSeverity;
using eve::ui::detail::readSavedLogEvent;
using eve::ui::detail::readSavedLogRun;
namespace fixture = eve::testing::modules;

template<class T> T take(Result<T> value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().message);
    return std::move(value).value();
}
void take(const Result<void>& value)
{
    if (!value.hasValue())
        throw std::runtime_error(value.error().message);
}
template<class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        REQUIRE(timer.elapsed() < 8000);
        QTest::qWait(5);
    }
    const auto result = task.tryResult();
    REQUIRE(result && result->hasValue());
    return result;
}
using EmitLog = EveStatus(EVE_CALL*)(EveHandle, std::uint32_t, EveBytesV1);
EmitLog resolveEndpoint(const QString& manifest)
{
    const auto imagePath = QDir::toNativeSeparators(QFileInfo(manifest).absolutePath() + "/module.dll").toStdWString();
    const auto image = GetModuleHandleW(imagePath.c_str());
    REQUIRE(image);
    const auto address = GetProcAddress(image, "eve_logging_fixture_emit");
    REQUIRE(address);
    EmitLog emitLog = nullptr;
    static_assert(sizeof(address) == sizeof(emitLog));
    std::memcpy(&emitLog, &address, sizeof(emitLog));
    return emitLog;
}
void exercise(const QString& modulePath, const QDir& workspace)
{
    const auto scheduler = std::make_shared<qt::QtScheduler>();
    auto shell = std::make_shared<ElaShell>(scheduler);
    take(shell->startOutputArchive(workspace.path().toStdString(), "native-log-acceptance"));
    const std::weak_ptr<ElaShell> view = shell;
    std::size_t delivered = 0;
    bool failedDelivery = false;
    std::optional<NativeLogEvent> lastAccepted;
    const auto sink = std::make_shared<NativeLogSink>([&](NativeLogEvent event) {
        if (const auto surface = view.lock()) {
            const auto accepted = surface->writeNativeOutput(event);
            if (accepted.hasValue()) {
                ++delivered;
                lastAccepted = std::move(event);
            } else {
                failedDelivery = true;
            }
        }
    });
    ModuleManager modules;
    const auto generation = take(modules.load(QFileInfo(modulePath).absolutePath().toStdString(), "module.json")).generation;
    const auto module = take(NativeModule::open(modules, generation, scheduler, {}, sink));
    const auto alpha = take(module.create("test.native.alpha", "null", InstanceId{901}, 5));
    const auto bravo = take(module.create("test.native.bravo", "null", InstanceId{902}, 8));
    wait(alpha.start());
    wait(bravo.start());
    const auto emitLog = resolveEndpoint(modulePath);
    const std::string full(EVE_MAX_LOG_BYTES, '"');
    const std::string shortText = u8"shared native text 日志";
    for (auto level = EVE_LOG_TRACE; level <= EVE_LOG_ERROR; ++level) {
        const auto& value = level == EVE_LOG_ERROR ? full : shortText;
        REQUIRE(emitLog(1, level, {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()}) == EVE_OK);
    }
    (void)take(shell->writeOutput("platform test marker"));
    REQUIRE(wait(module.close())->value().quiescent);
    wait(shell->close());
    REQUIRE(delivered == 9 && !failedDelivery && lastAccepted.has_value());
    REQUIRE(emitLog(1, EVE_LOG_ERROR, {nullptr, 0}) == EVE_E_CLOSED);
    const auto late = shell->writeNativeOutput(*lastAccepted);
    REQUIRE(!late.hasValue() && late.error().code == ErrorCode::ContextClosing);
    take(scheduler->close());

    const QDir root(workspace.filePath(".eve/logs"));
    const auto runs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    REQUIRE(runs.size() == 1);
    const auto run = runs.front();
    const auto saved = readSavedLogRun(workspace.path(), run);
    REQUIRE(saved && saved->ended && saved->acceptedThrough == 11);
    const QDir directory(root.filePath(run));
    REQUIRE(!directory.exists("active.lock"));
    const auto segments = directory.entryList({"events-*.jsonl"}, QDir::Files, QDir::Name);
    REQUIRE(!segments.isEmpty());
    std::map<int, quint64> levels;
    quint64 previous = 0;
    std::size_t closedSources = 0, platformEntries = 0, platformMarkers = 0;
    QJsonArray evidence;
    for (const auto& segment : segments) {
        const auto bytes = fixture::read(directory.filePath(segment));
        REQUIRE(bytes.endsWith('\n'));
        for (const auto& line : bytes.chopped(1).split('\n')) {
            const auto document = QJsonDocument::fromJson(line);
            REQUIRE(document.isObject());
            const auto row = document.object();
            const auto id = row.value("event").toString().toULongLong();
            REQUIRE(id > previous);
            previous = id;
            if (row.value("source") == "platform") {
                REQUIRE(!row.contains("instance") && !row.contains("module"));
                if (row.value("text") == "platform test marker")
                    ++platformMarkers;
                ++platformEntries;
                continue;
            }
            REQUIRE(row.value("module") == "org.eve.test.native.logging" && row.value("moduleVersion") == "1.0.0");
            REQUIRE(row.value("generation").toString().toULongLong() == generation.value());
            REQUIRE(row.value("utcMs").toString().toLongLong() > 0);
            const auto isAlpha = row.value("source") == "native/org.eve.test.native.logging/test.native.alpha";
            REQUIRE(isAlpha || row.value("source") == "native/org.eve.test.native.logging/test.native.bravo");
            REQUIRE(row.value("instance") == (isAlpha ? "901" : "902") && row.value("epoch") == (isAlpha ? "5" : "8"));
            const auto message = row.value("text").toString();
            if (message == "native fixture destroyed") {
                ++closedSources;
                continue;
            }
            if (message == "native fixture started")
                continue;
            const auto level = row.value("level").toInt(100);
            REQUIRE(level >= -2 && level <= 2 && isAlpha && levels.emplace(level, id).second);
            REQUIRE(row.value("sourceSequence").toString().toULongLong() == static_cast<quint64>(level + 4));
            const auto fromHistory = readSavedLogEvent(workspace.path(), run, id);
            REQUIRE(fromHistory && logSeverity(*fromHistory) == static_cast<LogSeverity>(level));
            REQUIRE(fromHistory->entry.text == (level == 2 ? full : shortText));
            REQUIRE(fromHistory->entry.text == message.toStdString());
            evidence.append(QJsonObject{{"event", row.value("event")}, {"level", level}, {"bytes", message.toUtf8().size()},
                                        {"sourceSequence", row.value("sourceSequence")}});
        }
    }
    REQUIRE(previous == 11 && levels.size() == 5 && closedSources == 2 && platformEntries == 2 &&
            platformMarkers == 1);
    REQUIRE(!readSavedLogEvent(workspace.path(), run, previous + 1));
    fixture::write(workspace.filePath("result.json"), QJsonDocument(QJsonObject{
        {"run", run}, {"pid", QString::number(QCoreApplication::applicationPid())},
        {"events", evidence}, {"passed", true}}).toJson());
}
} // namespace

int main(int argc, char** argv)
{
    const QApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        REQUIRE(args.size() == 3 && QDir().mkpath(args[2]));
        QTemporaryDir workspace(QDir(args[2]).filePath("logging-native-XXXXXX"));
        REQUIRE(workspace.isValid());
        workspace.setAutoRemove(false);
        std::cout << "LOG_NATIVE_EVIDENCE=" << workspace.path().toUtf8().constData() << '\n' << std::flush;
        const QDir root(workspace.path());
        const auto manifest = fixture::install(args[1], root.filePath("fixture"), "org.eve.test.native.logging",
                                                {"test.native.alpha", "test.native.bravo"});
        exercise(manifest, root);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
