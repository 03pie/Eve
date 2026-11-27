/** @file workspace_stress_reduce.cpp
 * @brief 对真实 Notebook 捕获边界做 fresh-child 删除最小证明，不伪造产品失败。
 */
#include "workspace_stress_child_p.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <eve_abi/state.h>
#include <set>
#include <stdexcept>

namespace workspace_stress_test {
namespace {
const QString captureSignature =
    QStringLiteral("notebook.capture.limit/InvalidArgument/module.state/unchanged-before-stop");

bool falseValue(const QJsonValue& value)
{
    return value.isBool() && !value.toBool();
}
bool originalLimit(const QJsonObject& failure)
{
    if (failure["code"] == decimal(static_cast<std::uint64_t>(eve::ErrorCode::InvalidArgument)) &&
        failure["phase"] == "module.state" &&
        failure["message"] == "state operation rejected, status " + QString::number(EVE_E_LIMIT))
        return true;
    for (const auto& cause : failure["causes"].toArray())
        if (cause.isObject() && originalLimit(cause.toObject()))
            return true;
    return false;
}
bool reproduces(const ChildResult& child)
{
    if (child.status == "Passed" || child.status == "InvalidTrace")
        return false;
    if (child.status == "HarnessError")
        throw std::runtime_error("candidate harness failure: " + child.harnessFailure.toStdString());
    if (child.status != "Failed" || child.signature != captureSignature)
        throw std::runtime_error("candidate produced an unexpected product failure");
    PLATFORM_CHECK(child.exitCode == 20 && !child.events.isEmpty());
    const auto terminal = child.events.last().toObject();
    PLATFORM_CHECK(terminal["failure_signature"] == captureSignature && terminal["action"].isString());
    QJsonObject begin, end;
    unsigned ends = 0;
    for (const auto& value : child.events) {
        const auto event = value.toObject();
        if (event["id"] != terminal["action"])
            continue;
        if (event["event"] == "action_begin")
            begin = event;
        if (event["event"] == "action_end") {
            end = event;
            ++ends;
        }
    }
    PLATFORM_CHECK(ends == 1 && !begin.isEmpty() && begin["kind"] == "notebookUpgrade" &&
                   end["kind"] == "notebookUpgrade");
    const auto result = end["result"].toObject();
    const auto report = result["report"].toObject();
    const auto replacement = report["replacement"].toObject();
    PLATFORM_CHECK(result["failure_signature"] == captureSignature && report["alias"] == "notebook" &&
                   report["failure"].isNull() && falseValue(report["retained"]) &&
                   falseValue(report["delivery_blocked"]) &&
                   replacement["outcome"] == static_cast<int>(eve::ReplacementOutcome::Failed) &&
                   falseValue(replacement["committed"]) && falseValue(replacement["recovered"]) &&
                   falseValue(replacement["resources_retained"]) && replacement["instances"].isArray() &&
                   replacement["instances"].toArray().isEmpty() && parseDecimal(replacement["operation"]) != 0 &&
                   parseDecimal(report["candidate"]) != 0 && report["candidate"] != report["previous"] &&
                   report["retired"] == report["candidate"]);
    bool rejectedByOriginalStateBoundary = false;
    for (const auto& failure : replacement["errors"].toArray())
        rejectedByOriginalStateBoundary = rejectedByOriginalStateBoundary || originalLimit(failure.toObject());
    PLATFORM_CHECK(rejectedByOriginalStateBoundary);
    const auto notes = result["notebooks_before"].toObject();
    PLATFORM_CHECK(!notes.isEmpty() && result["notebooks_after"].isObject() &&
                   notes == result["notebooks_after"].toObject() && notes["notes"].isObject());
    const auto note = notes["notes"].toObject();
    PLATFORM_CHECK(parseDecimal(note["instance"]) != 0 && parseDecimal(note["epoch"]) != 0 &&
                   note["draft"].isString() && note["draft"].toString() == QString(65537, QLatin1Char('x')));
    const auto before = begin["before"].toObject();
    const auto after = end["after"].toObject();
    // These are facts within one actual child, so its original instance/epoch/binding values must match.
    for (const auto* key : {"desired_revision", "desired", "module_locators", "module_paths", "driver",
                            "coordinator_running", "pages", "commands"})
        PLATFORM_CHECK(before.contains(key) && before[key] == after[key]);
    const auto beforeCore = before["kernel"].toObject(), afterCore = after["kernel"].toObject();
    PLATFORM_CHECK(beforeCore["instances"].isArray() && beforeCore["instances"] == afterCore["instances"] &&
                   beforeCore["names"] == afterCore["names"] && afterCore["claims"].isArray() &&
                   afterCore["claims"].toArray().isEmpty());
    const auto beforeServices = before["services"].toObject(), afterServices = after["services"].toObject();
    PLATFORM_CHECK(beforeServices["bindings"] == afterServices["bindings"] &&
                   beforeServices["declarations"] == afterServices["declarations"]);
    bool page = false;
    for (const auto& value : after["pages"].toArray()) {
        const auto row = value.toObject();
        page =
            page || (row["id"] == "notebook" && row["instance"] == note["instance"] && row["epoch"] == note["epoch"] &&
                     row["widget"] == QJsonValue(true) && falseValue(row["retiring"]));
    }
    PLATFORM_CHECK(page);
    return true;
}
QJsonArray actionValues(const Trace& trace)
{
    QJsonArray values;
    for (const auto& action : trace.actions)
        values.append(QJsonObject{{"id", action.id}, {"kind", action.kind}, {"args", action.args}});
    return values;
}
class Proof {
public:
    Proof(QString source, QString directory)
        : source_(std::move(source)), directory_(std::move(directory)),
          events_(QDir(directory_).filePath("proof.jsonl"))
    {
    }
    bool evaluate(const Trace& trace, const QString& purpose, bool requirePassed = false)
    {
        const auto trial = decimal(++trials_);
        const auto prefix = QDir(directory_).filePath("candidate-" + trial);
        const auto path = prefix + ".actions.json";
        PLATFORM_CHECK(!QFileInfo::exists(path));
        saveTrace(trace, path);
        events_.append({{"event", "candidate_begin"},
                        {"trial", trial},
                        {"purpose", purpose},
                        {"input_path", QFileInfo(path).absoluteFilePath()},
                        {"actions", actionValues(trace)}});
        const auto child = replayChild(source_, path, prefix);
        // Store the actual child result before interpreting it; an unexpected product/harness failure aborts.
        QJsonObject record{
            {"trial", trial}, {"purpose", purpose}, {"actions", actionValues(trace)}, {"child", childSummary(child)}};
        records_.append(record);
        events_.append({{"event", "candidate_child"}, {"trial", trial}, {"child", childSummary(child)}});
        if (!child.scratchPath.isEmpty())
            PLATFORM_CHECK(scratches_.insert(child.scratchPath).second);
        if (requirePassed)
            PLATFORM_CHECK(child.status == "Passed");
        const bool matched = reproduces(child);
        record.insert("retained_target_failure", matched);
        records_.replace(records_.size() - 1, record);
        events_.append({{"event", "candidate_result"}, {"trial", trial}, {"retained_target_failure", matched}});
        return matched;
    }
    void finish(const Trace& original, const Trace& minimal, const Trace& boundary)
    {
        const auto minimalPath = QDir(directory_).filePath("minimal.actions.json");
        const auto boundaryPath = QDir(directory_).filePath("boundary-65536.actions.json");
        PLATFORM_CHECK(!QFileInfo::exists(minimalPath) && !QFileInfo::exists(boundaryPath));
        saveTrace(minimal, minimalPath);
        saveTrace(boundary, boundaryPath);
        const QJsonObject result{{"schema", "eve.workspace.reduction-proof/1"},
                                 {"passed", true},
                                 {"minimality", "deletion-minimal"},
                                 {"target_failure_signature", captureSignature},
                                 {"original_actions", actionValues(original)},
                                 {"minimal_actions", actionValues(minimal)},
                                 {"minimal_input", QFileInfo(minimalPath).absoluteFilePath()},
                                 {"boundary_65536_input", QFileInfo(boundaryPath).absoluteFilePath()},
                                 {"candidate_count", decimal(trials_)},
                                 {"candidates", records_}};
        const auto path = QDir(directory_).filePath("proof.json");
        PLATFORM_CHECK(!QFileInfo::exists(path));
        QSaveFile file(path);
        const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
        PLATFORM_CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit());
        events_.append({{"event", "terminal"},
                        {"status", "Passed"},
                        {"minimality", "deletion-minimal"},
                        {"minimal_actions", actionValues(minimal)},
                        {"candidate_count", decimal(trials_)}});
    }

private:
    QString source_, directory_;
    Evidence events_;
    std::uint64_t trials_ = 0;
    QJsonArray records_;
    std::set<QString> scratches_;
};
} // namespace

void reductionProof(const QString& sourceBin, const QString& directory)
{
    PLATFORM_CHECK(QDir().mkpath(directory));
    PLATFORM_CHECK(!QFileInfo::exists(QDir(directory).filePath("proof.jsonl")));
    Proof proof(sourceBin, directory);
    Trace original{"1",
                   artifactInputs(sourceBin),
                   {{"1", "save", {}},
                    {"2", "configure", {{"path", "notes"}, {"configuration", "{title: Noise, text: before boundary}"}}},
                    {"3", "mount", {{"id", "noise"}, {"configuration", "{title: Noise sibling, text: other}"}}},
                    {"4", "withdraw", {{"path", "noise"}}},
                    {"5", "draft", {{"path", "notes"}, {"text", QString(65537, QLatin1Char('x'))}}},
                    {"6", "notebookUpgrade", {{"variant", "b"}}},
                    {"7", "save", {}}}};
    original.generator = "authored/1";
    PLATFORM_CHECK(proof.evaluate(original, "complete-original-reproduction-1"));
    PLATFORM_CHECK(proof.evaluate(original, "complete-original-reproduction-2"));
    Trace minimal = original;
    std::size_t granularity = 2;
    while (minimal.actions.size() >= 2) {
        const auto block = (minimal.actions.size() + granularity - 1) / granularity;
        bool reduced = false;
        for (std::size_t first = 0; first < minimal.actions.size(); first += block) {
            Trace candidate = minimal;
            const auto last = std::min(first + block, candidate.actions.size());
            candidate.actions.erase(candidate.actions.begin() + static_cast<std::ptrdiff_t>(first),
                                    candidate.actions.begin() + static_cast<std::ptrdiff_t>(last));
            if (candidate.actions.empty())
                continue; // Empty traces are outside the original input schema; never count them as reproduction.
            if (proof.evaluate(candidate, "delete-block")) {
                minimal = std::move(candidate);
                granularity = std::max<std::size_t>(2, granularity - 1);
                reduced = true;
                break;
            }
        }
        if (reduced)
            continue;
        if (granularity >= minimal.actions.size())
            break;
        granularity = std::min(minimal.actions.size(), granularity * 2);
    }
    PLATFORM_CHECK(minimal.actions.size() == 2 && minimal.actions[0].kind == "draft" &&
                   minimal.actions[1].kind == "notebookUpgrade");
    // Both final single-deletion cases must launch actual new children, including the one-action Draft trace.
    for (std::size_t index = 0; index < minimal.actions.size(); ++index) {
        Trace candidate = minimal;
        candidate.actions.erase(candidate.actions.begin() + static_cast<std::ptrdiff_t>(index));
        PLATFORM_CHECK(!proof.evaluate(candidate, "final-single-deletion-" + decimal(index), true));
    }
    Trace boundary = minimal;
    boundary.actions[0].args.insert("text", QString(65536, QLatin1Char('x')));
    PLATFORM_CHECK(!proof.evaluate(boundary, "exact-65536-byte-success-control", true));
    proof.finish(original, minimal, boundary);
}
} // namespace workspace_stress_test
