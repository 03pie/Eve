/** @file workspace_stress_child.cpp
 * @brief 三个具体proof共用实际子进程与落盘回执；不装配平台或持有模块能力。
 */
#include "workspace_stress_child_p.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>
#include <algorithm>
#include <stdexcept>

namespace workspace_stress_test {
namespace {
constexpr qint64 maximumEvidenceBytes = 64 * 1024 * 1024;
constexpr qint64 childBudgetMs = 60000;

QString digest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("cannot read child evidence input: " + path.toStdString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        throw std::runtime_error("cannot hash child evidence input");
    return QString::fromLatin1(hash.result().toHex());
}
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void readEvents(ChildResult& result)
{
    QFile file(result.evidencePath);
    require(file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= maximumEvidenceBytes,
            "child has no bounded evidence file");
    const auto bytes = file.readAll();
    require(bytes.endsWith('\n'), "child evidence has an incomplete final line");
    const auto lines = bytes.split('\n');
    std::uint64_t sequence = 0;
    int terminals = 0, bootstraps = 0;
    QString pendingAction, pendingKind;
    bool runException = false;
    for (int index = 0; index + 1 < lines.size(); ++index) {
        const auto& line = lines[index];
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(line, &parse);
        require(parse.error == QJsonParseError::NoError && document.isObject() &&
                    document.toJson(QJsonDocument::Compact) == line,
                "child evidence is not canonical complete JSONL");
        const auto event = document.object();
        require(parseDecimal(event["sequence"]) == ++sequence, "child evidence sequence is discontinuous");
        require(event["event"].isString(), "child evidence has no event kind");
        if (event["event"] == "bootstrap") {
            ++bootstraps;
            require(bootstraps == 1 && index == 0 && parseDecimal(event["pid"]) == result.pid &&
                        event["qpa"] == "windows" && event["scratch"].isString(),
                    "child bootstrap does not identify the actual process");
            result.scratchPath = QDir::cleanPath(event["scratch"].toString());
            require(!result.scratchPath.isEmpty() && QFileInfo(result.scratchPath).isAbsolute(),
                    "child bootstrap has no absolute independent scratch path");
        } else if (event["event"] == "action_begin") {
            require(bootstraps == 1 && pendingAction.isEmpty() && !runException && event["id"].isString() &&
                        event["kind"].isString() && event["args"].isObject() && event["before"].isObject(),
                    "child action begin is out of sequence or incomplete");
            pendingAction = event["id"].toString();
            pendingKind = event["kind"].toString();
            parseDecimal(event["id"]);
            require(!pendingKind.isEmpty(), "child action identity is invalid");
        } else if (event["event"] == "action_end") {
            require(!pendingAction.isEmpty() && !runException && event["id"] == pendingAction &&
                        event["kind"] == pendingKind && event["result"].isObject() && event["after"].isObject(),
                    "child action end does not complete its original begin");
            pendingAction.clear();
            pendingKind.clear();
        } else if (event["event"] == "run_exception") {
            require(!runException, "child reported more than one run exception");
            runException = true;
        } else if (event["event"] == "terminal") {
            ++terminals;
            require(terminals == 1 && index + 2 == lines.size() && event["status"].isString(),
                    "child terminal must occur exactly once at the end");
            result.status = event["status"].toString();
            if (event.contains("failure_signature")) {
                require(event["failure_signature"].isString(), "child failure signature is not a string");
                result.signature = event["failure_signature"].toString();
            }
        } else
            throw std::runtime_error("child evidence contains an unknown event kind");
        result.events.append(event);
    }
    require(terminals == 1, "child evidence has no terminal result");
    if (result.status == "Passed")
        require(result.exitCode == 0 && bootstraps == 1 && pendingAction.isEmpty() && !runException,
                "Passed child has inconsistent exit/bootstrap/action completion");
    else if (result.status == "Failed") {
        require(result.exitCode == 20 && bootstraps == 1, "Failed child has inconsistent exit/bootstrap");
        require(result.signature.isEmpty() || (pendingAction.isEmpty() && !runException),
                "signed product failure has no completed original action report");
    } else if (result.status == "InvalidTrace")
        require(result.exitCode == 21 && result.signature.isEmpty(),
                "InvalidTrace child has inconsistent exit/signature");
    else
        throw std::runtime_error("child reported HarnessError or an unknown terminal status");
}
void saveReceipt(const ChildResult& result, const QString& path, const QStringList& arguments)
{
    require(!QFileInfo::exists(path), "refusing to replace an existing child receipt");
    auto value = childSummary(result);
    value.insert("arguments", QJsonArray::fromStringList(arguments));
    value.insert("executable", QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath());
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    PLATFORM_CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit());
}
ChildResult child(const QString& sourceBin, const QString& input, const QString& prefix, bool generate)
{
    ChildResult result;
    const auto absolutePrefix = QFileInfo(prefix).absoluteFilePath();
    result.inputPath = generate ? absolutePrefix + ".actions.json" : QFileInfo(input).absoluteFilePath();
    result.evidencePath = absolutePrefix + ".jsonl";
    result.processLog = absolutePrefix + ".process.log";
    const auto receiptPath = absolutePrefix + ".child.json";
    const auto executable = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    const QStringList arguments{generate ? QStringLiteral("generate") : QStringLiteral("replay"),
                                QFileInfo(sourceBin).absoluteFilePath(), generate ? input : result.inputPath,
                                absolutePrefix};
    // A reused prefix is a caller/harness error; never replace any earlier child's evidence.
    require(!QFileInfo::exists(result.evidencePath) && !QFileInfo::exists(result.processLog) &&
                !QFileInfo::exists(receiptPath) && (!generate || !QFileInfo::exists(result.inputPath)),
            "child output prefix already contains evidence");
    QProcess process;
    try {
        require(QFileInfo(executable).isAbsolute() && QFileInfo(executable).isFile(),
                "actual absolute child executable is missing");
        require(QDir().mkpath(QFileInfo(absolutePrefix).absolutePath()), "cannot create child evidence directory");
        result.executableSha256 = digest(executable);
        if (!generate)
            result.inputSha256 = digest(result.inputPath);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setStandardOutputFile(result.processLog, QIODevice::Truncate);
        process.setWorkingDirectory(QFileInfo(executable).absolutePath());
        QElapsedTimer elapsed;
        elapsed.start();
        process.start(executable, arguments, QIODevice::ReadOnly);
        require(process.waitForStarted(10000), "child process did not start");
        result.pid = static_cast<std::uint64_t>(process.processId());
        require(result.pid != 0, "started child has no actual PID");
        const auto remaining = std::max<qint64>(1, childBudgetMs - elapsed.elapsed());
        if (!process.waitForFinished(static_cast<int>(remaining))) {
            process.kill();
            require(process.waitForFinished(10000), "timed-out child could not be reaped after kill");
            throw std::runtime_error("child exceeded the 60-second process budget; killed and reaped");
        }
        result.exitCode = process.exitCode();
        require(process.exitStatus() == QProcess::NormalExit, "child process crashed");
        require(digest(executable) == result.executableSha256, "actual executable changed during child execution");
        const auto inputHash = digest(result.inputPath);
        require(generate || inputHash == result.inputSha256, "replay child input changed during execution");
        result.inputSha256 = inputHash;
        result.evidenceSha256 = digest(result.evidencePath);
        readEvents(result);
    } catch (const std::exception& failure) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            if (!process.waitForFinished(10000))
                result.harnessFailure = "child cleanup could not reap the process; ";
        }
        result.status = "HarnessError";
        result.signature.clear();
        result.harnessFailure += QString::fromUtf8(failure.what());
        if (process.state() == QProcess::NotRunning && result.pid != 0)
            result.exitCode = process.exitCode();
    }
    try {
        if (result.inputSha256.isEmpty() && QFileInfo::exists(result.inputPath))
            result.inputSha256 = digest(result.inputPath);
        if (result.evidenceSha256.isEmpty() && QFileInfo::exists(result.evidencePath))
            result.evidenceSha256 = digest(result.evidencePath);
    } catch (const std::exception& failure) {
        result.status = "HarnessError";
        result.signature.clear();
        result.harnessFailure += "; final evidence hash: " + QString::fromUtf8(failure.what());
    }
    // Actual events remain untouched on a harness failure; the receipt carries its distinct reason.
    saveReceipt(result, receiptPath, arguments);
    return result;
}
} // namespace

ChildResult generateChild(const QString& sourceBin, const QString& seed, const QString& prefix)
{
    parseDecimal(seed);
    return child(sourceBin, seed, prefix, true);
}
ChildResult replayChild(const QString& sourceBin, const QString& inputPath, const QString& prefix)
{
    return child(sourceBin, inputPath, prefix, false);
}
QJsonObject childSummary(const ChildResult& value)
{
    return {{"status", value.status},
            {"failure_signature", value.signature},
            {"harness_failure", value.harnessFailure},
            {"actual_pid", decimal(value.pid)},
            {"exit_code", value.exitCode},
            {"scratch", value.scratchPath},
            {"input_path", value.inputPath},
            {"input_sha256", value.inputSha256},
            {"evidence_path", value.evidencePath},
            {"evidence_sha256", value.evidenceSha256},
            {"process_log", value.processLog},
            {"executable_sha256", value.executableSha256},
            {"event_count", decimal(static_cast<std::uint64_t>(value.events.size()))}};
}
} // namespace workspace_stress_test
