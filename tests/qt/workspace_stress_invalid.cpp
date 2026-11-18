/** @file workspace_stress_invalid.cpp
 * @brief 原子进程拒绝损坏输入及失效取消窗口；不把 harness 故障当作 InvalidTrace。
 */
#include "workspace_stress_child_p.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <cordis_abi/state.h>
#include <set>

namespace workspace_stress_test {
namespace {
void write(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    PLATFORM_CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit());
}
QByteArray canonical(const QJsonObject& value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Indented);
}
QJsonObject action(const QString& id, const QString& kind, const QJsonObject& args)
{
    return {{"id", id}, {"kind", kind}, {"args", args}};
}
bool captureLimit(const QJsonValue& value)
{
    if (value.isArray()) {
        for (const auto& item : value.toArray())
            if (captureLimit(item))
                return true;
    } else if (value.isObject()) {
        const auto object = value.toObject();
        if (object["code"] == decimal(static_cast<std::uint64_t>(cordis::ErrorCode::InvalidArgument)) &&
            object["phase"] == "module.state" &&
            object["message"] == "state operation rejected, status " + QString::number(CORDIS_E_LIMIT))
            return true;
        for (auto item = object.begin(); item != object.end(); ++item)
            if (captureLimit(item.value()))
                return true;
    }
    return false;
}
void save(const QString& directory, const QJsonArray& cases, bool passed)
{
    write(QDir(directory).filePath("proof.json"),
          canonical({{"proof", "invalid-trace"}, {"passed", passed}, {"cases", cases}}));
}
} // namespace

void invalidTraceProof(const QString& sourceBin, const QString& outputDirectory)
{
    const auto directory = QDir(outputDirectory).absolutePath();
    Evidence evidence(QDir(directory).filePath("proof.jsonl"));
    const auto artifacts = artifactInputs(sourceBin);
    const auto baselinePath = QDir(directory).filePath("baseline.actions.json");
    auto authored = generateTrace(sourceBin, "1");
    authored.generator = "authored/1";
    saveTrace(authored, baselinePath);
    const auto base = [&] {
        QFile baseline(baselinePath);
        PLATFORM_CHECK(baseline.open(QIODevice::ReadOnly));
        return QJsonDocument::fromJson(baseline.readAll()).object();
    }();
    QJsonArray cases;
    std::set<std::uint64_t> processes;
    auto check = [&](const QString& name, const QByteArray& bytes, bool executes, bool building = false) {
        const auto input = QDir(directory).filePath(name + ".actions.json");
        write(input, bytes);
        const auto child = replayChild(sourceBin, input, QDir(directory).filePath(name));
        auto record = childSummary(child);
        record.insert("case", name);
        record.insert("file", input);
        record.insert("expected",
                      building ? "InvalidTrace/exit21, real capture limit before Held" : "InvalidTrace/exit21");
        cases.append(record);
        evidence.append(record);
        save(directory, cases, false);
        PLATFORM_CHECK(child.status != "HarnessError" && child.status == "InvalidTrace" && child.exitCode == 21 &&
                       child.signature.isEmpty() && child.pid != 0 &&
                       child.pid != static_cast<std::uint64_t>(QCoreApplication::applicationPid()) &&
                       processes.insert(child.pid).second);
        bool bootstrap = false, failedTerminal = false, actualCapture = false, draft = false;
        for (const auto& value : child.events) {
            const auto event = value.toObject();
            bootstrap = bootstrap || event["event"] == "bootstrap";
            failedTerminal = failedTerminal || (event["event"] == "terminal" && event["status"] == "Failed");
            if (event["event"] == "run_exception")
                actualCapture = captureLimit(event["after"].toObject()["upgrade"]);
            if (event["event"] == "action_end" && event["kind"] == "draft")
                draft = event["result"].toObject()["utf8_bytes"] == "65537";
        }
        PLATFORM_CHECK(bootstrap == executes && !failedTerminal && (!executes || !child.scratchPath.isEmpty()));
        if (building)
            PLATFORM_CHECK(draft && actualCapture);
        PLATFORM_CHECK(artifactInputs(sourceBin) == artifacts);
    };
    auto mutate = [&](const QString& name, const QString& field, const QJsonValue& value) {
        auto changed = base;
        changed.insert(field, value);
        check(name, canonical(changed), false);
    };
    mutate("unknown-schema", "schema", "cordis.workspace.actions/999");
    mutate("unknown-generator", "generator", "splitmix64/999");
    mutate("extra-root-field", "unknown", true);
    {
        auto changed = base;
        changed.remove("seed");
        check("missing-root-field", canonical(changed), false);
    }
    mutate("wrong-seed-type", "seed", 1);
    mutate("seed-overflow", "seed", "18446744073709551616");
    mutate("noncanonical-seed", "seed", "01");
    auto actionCase = [&](const QString& name, const QJsonObject& value) {
        auto changed = base;
        changed.insert("actions", QJsonArray{value});
        check(name, canonical(changed), false);
    };
    actionCase("unknown-action", action("1", "unknown", {}));
    {
        auto value = action("1", "save", {});
        value.insert("unknown", false);
        actionCase("extra-action-field", value);
        value.remove("unknown");
        value.remove("args");
        actionCase("missing-action-field", value);
    }
    {
        auto value = action("1", "save", {});
        value.insert("id", 1);
        actionCase("wrong-action-id-type", value);
    }
    actionCase("noncanonical-action-id", action("01", "save", {}));
    actionCase("action-id-overflow", action("18446744073709551616", "save", {}));
    actionCase("wrong-argument-type", action("1", "enable", {{"path", "counter"}, {"enabled", "true"}}));
    actionCase("extra-argument", action("1", "save", {{"unknown", true}}));
    actionCase("missing-argument", action("1", "enable", {{"path", "counter"}}));
    {
        auto changed = base;
        auto inputs = changed["artifacts"].toArray();
        auto input = inputs[0].toObject();
        input.insert("sha256", QString(64, QLatin1Char('0')));
        inputs[0] = input;
        changed.insert("artifacts", inputs);
        check("changed-artifact-hash", canonical(changed), false);
    }
    {
        auto bytes = canonical(base);
        bytes.insert(1, "\n    \"seed\": \"1\",");
        check("duplicate-raw-key", bytes, false);
    }
    {
        auto changed = base;
        changed.insert("actions", QJsonArray{action("1", "withdraw", {{"path", "missing"}})});
        check("missing-withdraw-path", canonical(changed), true);
        changed.insert("actions",
                       QJsonArray{action("1", "draft", {{"path", "notes"}, {"text", QString(65537, QLatin1Char('A'))}}),
                                  action("2", "buildingCancelNotebook", {{"variant", "b"}})});
        check("building-window-before-capture", canonical(changed), true, true);
    }
    PLATFORM_CHECK(cases.size() == 20 && processes.size() == 20 && artifactInputs(sourceBin) == artifacts);
    save(directory, cases, true);
    evidence.append({{"event", "proof_terminal"}, {"status", "Passed"}, {"cases", "20"}});
}
} // namespace workspace_stress_test
