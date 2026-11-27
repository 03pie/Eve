#include "runtime_log_archive_p.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#include <sddl.h>
#endif

#define REQUIRE(check) do { if (!(check)) throw std::runtime_error( \
    std::string(#check) + " at " + std::to_string(__LINE__)); } while (false)

using namespace eve::ui;
using namespace eve::ui::detail;

namespace {
quint64 append(RuntimeLogArchive& archive, quint64 id, OutputLevel level = OutputLevel::Info)
{
    const auto text = "log-" + std::to_string(id) + " " + std::string(500, 'a');
    return archive.append({id, {}, "platform", "Platform", text, level}, "platform", "Platform",
                          QDateTime::currentMSecsSinceEpoch());
}

template<class Predicate>
void waitUntil(Predicate predicate)
{
    QElapsedTimer limit;
    limit.start();
    while (!predicate()) {
        REQUIRE(limit.elapsed() < 10000);
        QThread::msleep(5);
    }
}

QJsonObject readMetadata(const RuntimeLogArchive& archive)
{
    QFile metadata(QDir(archive.directory()).filePath(archive.currentRun() + "/run.json"));
    REQUIRE(metadata.open(QIODevice::ReadOnly));
    const auto doc = QJsonDocument::fromJson(metadata.readAll());
    REQUIRE(doc.isObject());
    return doc.object();
}

void verifyTerminal(const RuntimeLogArchive& archive, bool ended)
{
    const auto state = archive.persistence();
    REQUIRE(state.queued == 0 && state.queuedBytes == 0);
    REQUIRE(state.accepted == state.confirmed + state.discarded + state.unconfirmed);
    const auto metadata = readMetadata(archive);
    REQUIRE(metadata.contains("ended") == ended);
    REQUIRE(!QFile::exists(QDir(archive.directory()).filePath(archive.currentRun() + "/active.lock")));
    if (ended) {
        const auto disk = metadata.value("persistence").toObject();
        REQUIRE(disk.value("accepted").toString().toULongLong() == state.accepted);
        REQUIRE(disk.value("confirmed").toString().toULongLong() == state.confirmed);
        REQUIRE(disk.value("discarded").toString().toULongLong() == state.discarded);
        REQUIRE(disk.value("unconfirmed").toString().toULongLong() == state.unconfirmed);
    }
}

void shortWrite(const QString& workspace, unsigned faultAt)
{
    LogArchiveOptions options;
    options.flushMs = 80;
    options.retryMs = 250;
    unsigned writes = 0;
    options.write = [&](QFile& file, const QByteArray& row) {
        if (++writes == faultAt)
            return file.write(row.constData(), row.size() / 2);
        return file.write(row);
    };
    RuntimeLogArchive archive(workspace, "writer-test", options);
    for (quint64 i = 1; i <= 3; ++i)
        REQUIRE(append(archive, i) == i);
    waitUntil([&] { return !archive.persistence().problem.isEmpty(); });
    waitUntil([&] { return archive.persistence().confirmed == 3; });
    REQUIRE(archive.error().isEmpty());
    archive.finish(true);
    verifyTerminal(archive, true);
    REQUIRE(archive.persistence().confirmed == 3);
    std::vector<quint64> disk;
    const auto warning = scanSavedDescending(workspace, archive.currentRun(), 0, [&](const LogRecord& row) {
        disk.push_back(row.eventId);
        return true;
    });
    REQUIRE((disk == std::vector<quint64>{3, 2, 1}));
    const QDir run(QDir(archive.directory()).filePath(archive.currentRun()));
    if (faultAt == 1) {
        REQUIRE(run.entryList({"fault-*.jsonl"}, QDir::Files).size() == 1);
    } else {
        REQUIRE(!warning.isEmpty());
        REQUIRE(run.entryList({"events-*.jsonl"}, QDir::Files).size() == 2);
    }
}

void pressure(const QString& workspace)
{
    std::mutex mutex;
    std::condition_variable ready;
    bool held = false, release = false;
    LogArchiveOptions options;
    options.queueRecords = 64;
    options.queueBytes = 48 * 1024;
    options.flushMs = 0;
    options.write = [&](QFile& file, const QByteArray& row) {
        std::unique_lock<std::mutex> guard(mutex);
        held = true;
        ready.notify_all();
        if (!ready.wait_for(guard, std::chrono::seconds(15), [&] { return release; }))
            return qint64{-1};
        return file.write(row);
    };
    RuntimeLogArchive archive(workspace, "queue-test", options);
    REQUIRE(append(archive, 1) == 1);
    {
        std::unique_lock<std::mutex> guard(mutex);
        REQUIRE(ready.wait_for(guard, std::chrono::seconds(5), [&] { return held; }));
    }
    QElapsedTimer production;
    production.start();
    for (quint64 i = 2; i < 100000; ++i)
        REQUIRE(append(archive, i) == i);
    const auto before = archive.persistence();
    REQUIRE(append(archive, 100000, OutputLevel::Error) == 100000);
    const auto congested = archive.persistence();
    {
        std::lock_guard<std::mutex> guard(mutex);
        release = true;
    }
    ready.notify_all();
    REQUIRE(before.queued == 56 && congested.queued == 57 && congested.discarded == 99943);
    REQUIRE(congested.peakQueued == 57 && congested.peakQueuedBytes <= options.queueBytes);
    REQUIRE(congested.losses.size() == 1 && congested.losses.front().first == 57 &&
            congested.losses.front().last == 99999 && !congested.lossRangesIncomplete);
    archive.finish(true);
    verifyTerminal(archive, true);
    REQUIRE(archive.persistence().confirmed == 57 && archive.persistence().unconfirmed == 0);
    REQUIRE(readSavedLogEvent(workspace, archive.currentRun(), 100000));
    REQUIRE(!readSavedLogEvent(workspace, archive.currentRun(), 57));
    std::cout << "accepted=100000 peakRecords=" << congested.peakQueued
              << " peakQueuedBytes=" << congested.peakQueuedBytes << " producerMs=" << production.elapsed() << '\n';
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS usage{};
    usage.cb = sizeof(usage);
    REQUIRE(K32GetProcessMemoryInfo(GetCurrentProcess(), &usage, sizeof(usage)));
    std::cout << "peakWorkingSetBytes=" << usage.PeakWorkingSetSize << '\n';
#endif
}

void unconfirmed(const QString& workspace)
{
    LogArchiveOptions options;
    options.flushMs = 0;
    options.write = [](QFile&, const QByteArray&) { return qint64{-1}; };
    RuntimeLogArchive archive(workspace, "failure-test", options);
    REQUIRE(append(archive, 1) == 1);
    waitUntil([&] { return !archive.persistence().problem.isEmpty(); });
    archive.finish(true);
    verifyTerminal(archive, true);
    REQUIRE(archive.persistence().unconfirmed == 1 && archive.persistence().confirmed == 0 &&
            archive.persistence().discarded == 0 && !archive.error().isEmpty());
    REQUIRE(!readSavedLogEvent(workspace, archive.currentRun(), 1));
    REQUIRE(append(archive, 2) == 0);
}
void concurrentClose(const QString& workspace)
{
    LogArchiveOptions options;
    options.batchRecords = 16;
    RuntimeLogArchive archive(workspace, "concurrent-close-test", options);
    std::atomic<quint64> produced{0};
    std::thread producer([&] {
        quint64 sequence = 1;
        while (append(archive, sequence) != 0)
            produced.store(sequence++, std::memory_order_release);
    });
    QElapsedTimer timeout;
    timeout.start();
    while (produced.load(std::memory_order_acquire) < 128 && timeout.elapsed() < 5000)
        QThread::msleep(1);
    archive.finish(true);
    producer.join();
    const auto count = produced.load(std::memory_order_acquire);
    REQUIRE(count >= 128 && archive.persistence().accepted == count);
    verifyTerminal(archive, true);
    REQUIRE(archive.persistence().unconfirmed == 0);
}
#ifdef Q_OS_WIN
class DenyFileCreation {
public:
    explicit DenyFileCreation(const QString& path) : path_(QDir::toNativeSeparators(path))
    {
        HANDLE token = nullptr;
        REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
        const std::unique_ptr<void, decltype(&CloseHandle)> handle(token, CloseHandle);
        DWORD required = 0;
        (void)GetTokenInformation(handle.get(), TokenUser, nullptr, 0, &required);
        REQUIRE(required > 0);
        std::vector<unsigned char> buffer(required);
        REQUIRE(GetTokenInformation(handle.get(), TokenUser, buffer.data(), required, &required));
        wchar_t* identifier = nullptr;
        REQUIRE(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &identifier));
        principal_ = '*' + QString::fromWCharArray(identifier);
        LocalFree(identifier);
        REQUIRE(change("/deny", principal_ + ":(WD,AD)"));
        denied_ = true;
    }
    ~DenyFileCreation() { if (denied_) (void)change("/remove:d", principal_); }
    void restore()
    {
        REQUIRE(change("/remove:d", principal_));
        denied_ = false;
    }
private:
    bool change(const QString& action, const QString& rules) const
    {
        QProcess process;
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/icacls.exe"), {path_, action, rules});
        return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    QString path_, principal_;
    bool denied_ = false;
};

void inaccessible(const QString& workspace)
{
    LogArchiveOptions options;
    options.flushMs = 0;
    RuntimeLogArchive archive(workspace, "directory-permission-test", options);
    const QDir run(QDir(archive.directory()).filePath(archive.currentRun()));
    DenyFileCreation denial(run.absolutePath());
    QFile probe(run.filePath("probe"));
    REQUIRE(!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(append(archive, 1) == 1);
    waitUntil([&] { return !archive.persistence().problem.isEmpty(); });
    REQUIRE(archive.persistence().queued == 1 && archive.persistence().confirmed == 0);
    denial.restore();
    waitUntil([&] { return archive.persistence().confirmed == 1 && archive.persistence().problem.isEmpty(); });
    archive.finish(true);
    verifyTerminal(archive, true);
    REQUIRE(readSavedLogEvent(workspace, archive.currentRun(), 1));
}
#endif
}

int main(int argc, char** argv)
{
    const QCoreApplication application(argc, argv);
    try {
        const QTemporaryDir workspace;
        REQUIRE(workspace.isValid() && application.arguments().size() == 2);
        const auto test = application.arguments().back();
        if (test == "short") shortWrite(workspace.path(), 2);
        else if (test == "first_short") shortWrite(workspace.path(), 1);
        else if (test == "pressure") pressure(workspace.path());
        else if (test == "unconfirmed") unconfirmed(workspace.path());
        else if (test == "concurrent_close") concurrentClose(workspace.path());
#ifdef Q_OS_WIN
        else if (test == "inaccessible") inaccessible(workspace.path());
#endif
        else REQUIRE(false);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
