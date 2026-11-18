/** @file workspace_stress_replay.cpp
 * @brief 两个实际 seed 的落盘动作在独立进程重放，不比较跨进程能力标识。
 */
#include "workspace_stress_child_p.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QSaveFile>
#include <set>

namespace workspace_stress_test {
namespace {
QJsonObject fields(const QJsonObject& source, const QStringList& names)
{
    QJsonObject result;
    for (const auto& name : names) {
        PLATFORM_CHECK(source.contains(name));
        result.insert(name, source[name]);
    }
    return result;
}
QJsonValue reportValue(const QJsonValue& source, const QString& scratch)
{
    // 仅投影本工具原报告：保留结果、错误树、草稿及映射数量，不比较进程内 ID/revision。
    if (source.isArray()) {
        QJsonArray result;
        for (const auto& value : source.toArray())
            result.append(reportValue(value, scratch));
        return result;
    }
    if (!source.isObject()) {
        if (source.isString()) {
            auto text = source.toString();
            if (!scratch.isEmpty())
                text.replace(scratch, QStringLiteral("<scratch>"));
            return text;
        }
        return source;
    }
    QJsonObject result;
    const auto object = source.toObject();
    for (auto item = object.begin(); item != object.end(); ++item) {
        const auto& name = item.key();
        if (name == "operation" || name == "instance" || name == "epoch" || name == "previous" || name == "candidate" ||
            name == "retired" || name == "current" || name == "revision" || name == "accepted_revision" ||
            name == "base_revision" || name == "first_accepted_revision" || name == "second_accepted_revision")
            continue;
        result.insert(name, reportValue(item.value(), scratch));
    }
    return result;
}
QJsonArray running(const QJsonArray& source, const QString& scratch)
{
    QJsonArray result;
    for (const auto& value : source)
        result.append(reportValue(
            fields(value.toObject(), {"path", "configuration", "state", "waiting_for_parent", "failure"}), scratch));
    return result;
}
QJsonObject observation(const QJsonObject& source, const QString& scratch)
{
    QJsonObject result = fields(source, {"desired", "module_locators", "module_errors"});
    result.insert("driver", running(source["driver"].toArray(), scratch));
    result.insert("coordinator_running", running(source["coordinator_running"].toArray(), scratch));
    result.insert("latest_plan", reportValue(source["latest_plan"], scratch));
    result.insert("session", reportValue(source["session"], scratch));
    const auto store = source["store"].toObject();
    auto storeValue = fields(store, {"dirty", "writing", "queued_writes", "closing", "closed", "delivery_blocked",
                                     "last_error", "baseline", "external"});
    storeValue.insert("persisted", !store["persisted_revision"].isNull());
    result.insert("store", reportValue(storeValue, scratch));
    const auto kernel = source["kernel"].toObject();
    auto kernelValue = fields(kernel, {"closing", "delivery_blocked", "runtimes"});
    kernelValue.insert("instances", kernel["instances"].toArray().size());
    kernelValue.insert("claims", kernel["claims"].toArray().size());
    result.insert("kernel", kernelValue);
    QJsonArray generations;
    const auto directory = source["directory"].toObject();
    for (const auto& value : directory["generations"].toArray()) {
        const auto row = value.toObject();
        auto semantic = fields(row, {"module",
                                     "version",
                                     "sha256",
                                     "loader",
                                     "policy",
                                     "state",
                                     "role",
                                     "native_mapped",
                                     "native_pinned",
                                     "loading",
                                     "admissions",
                                     "image_reference",
                                     "observation_image_reference",
                                     "file_guard",
                                     "backend",
                                     "accepting",
                                     "pin_reasons",
                                     "failure",
                                     "business_failure",
                                     "unload_failure"});
        QJsonObject resources;
        for (const auto& record : row["records"].toArray()) {
            const auto resource = record.toObject();
            auto label = resource["label"].toString();
            if (label.startsWith("qt.instance."))
                label = "qt.instance";
            const auto key = QString::number(resource["kind"].toInt()) + ':' + label;
            resources.insert(key, resources[key].toInt() + 1);
        }
        semantic.insert("resources", resources);
        semantic.insert("runtime_origins", row["runtime_origins"].toArray().size());
        semantic.insert("instance_origins", row["instance_origins"].toArray().size());
        generations.append(reportValue(semantic, scratch));
    }
    result.insert("generations", generations);
    result.insert("reader_operations", directory["reader_operations"]);
    result.insert("readers", directory["readers"].toArray().size());
    result.insert("reservations", directory["reservations"].toArray().size());
    QJsonArray paths, pages, commands;
    for (const auto& value : source["module_paths"].toArray())
        paths.append(fields(value.toObject(), {"alias", "plugin", "paths"}));
    for (const auto& value : source["pages"].toArray())
        pages.append(fields(value.toObject(), {"id", "navigation", "widget", "current", "retiring"}));
    for (const auto& value : source["commands"].toArray())
        commands.append(fields(value.toObject(), {"id", "title", "calls", "retiring", "delivery_blocked"}));
    result.insert("module_paths", paths);
    result.insert("pages", pages);
    result.insert("commands", commands);
    QJsonArray bindings, natives;
    QJsonObject realms;
    for (const auto& value : source["services"].toObject()["bindings"].toArray()) {
        const auto row = value.toObject();
        const auto key = row["key"].toObject();
        const auto realm = key["realm"].toString();
        if (!realms.contains(realm))
            realms.insert(realm, realms.size());
        auto service =
            fields(row, {"minor", "capabilities", "state", "leases", "calls", "delivery_blocked", "managed_provider"});
        auto serviceKey = fields(key, {"name", "major"});
        serviceKey.insert("realm_partition", realms[realm]);
        service.insert("key", serviceKey);
        service.insert("consumers", row["consumers"].toArray().size());
        bindings.append(service);
    }
    for (const auto& value : source["native"].toArray()) {
        const auto row = value.toObject();
        auto module = fields(row, {"plugin", "closed", "closing", "faulted"});
        QJsonArray instances;
        for (const auto& item : row["instances"].toArray())
            instances.append(fields(item.toObject(), {"plugin", "state", "closing", "delivery_blocked", "uncertain",
                                                      "native_created", "host_calls", "host_operations", "requests",
                                                      "committed_services", "logs", "failure"}));
        module.insert("instances", instances);
        natives.append(reportValue(module, scratch));
    }
    result.insert("service_bindings", bindings);
    result.insert("native", natives);
    result.insert("queue",
                  fields(source["queue"].toObject(), {"open", "running", "queued", "discarded", "callback_errors"}));
    result.insert("upgrade", reportValue(source["upgrade"], scratch));
    return result;
}
QJsonArray semantics(const ChildResult& child, const Trace& trace)
{
    PLATFORM_CHECK(child.events.size() == 70 && trace.actions.size() == 34);
    QJsonArray result;
    PLATFORM_CHECK(child.events[0].toObject()["event"] == "bootstrap");
    result.append(observation(child.events[0].toObject()["observation"].toObject(), child.scratchPath));
    for (std::size_t index = 0; index < trace.actions.size(); ++index) {
        const auto before = child.events[static_cast<int>(index * 2 + 1)].toObject();
        const auto after = child.events[static_cast<int>(index * 2 + 2)].toObject();
        const auto& action = trace.actions[index];
        PLATFORM_CHECK(before["event"] == "action_begin" && after["event"] == "action_end" &&
                       before["id"] == action.id && after["id"] == action.id && before["kind"] == action.kind &&
                       after["kind"] == action.kind && before["args"].toObject() == action.args);
        result.append(QJsonObject{{"id", action.id},
                                  {"kind", action.kind},
                                  {"args", action.args},
                                  {"before", observation(before["before"].toObject(), child.scratchPath)},
                                  {"result", reportValue(after["result"], child.scratchPath)},
                                  {"after", observation(after["after"].toObject(), child.scratchPath)}});
    }
    PLATFORM_CHECK(child.events.last().toObject()["event"] == "terminal" &&
                   child.events.last().toObject()["status"] == "Passed");
    return result;
}
void save(const QString& directory, const QJsonArray& children, const QJsonArray& comparisons, bool passed)
{
    QSaveFile file(QDir(directory).filePath("proof.json"));
    const auto bytes =
        QJsonDocument(QJsonObject{{"proof", "normal-replay"},
                                  {"passed", passed},
                                  {"children", children},
                                  {"comparisons", comparisons},
                                  {"semantic_comparison", "pure states/reports, no process capability IDs"}})
            .toJson(QJsonDocument::Indented);
    PLATFORM_CHECK(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit());
}
} // namespace

void replayProof(const QString& sourceBin, const QString& outputDirectory)
{
    const auto directory = QDir(outputDirectory).absolutePath();
    Evidence evidence(QDir(directory).filePath("proof.jsonl"));
    const auto artifacts = artifactInputs(sourceBin);
    QJsonArray children, comparisons;
    std::set<std::uint64_t> processes;
    std::set<QString> scratches;
    auto record = [&](const QString& name, const ChildResult& child) {
        auto value = childSummary(child);
        value.insert("case", name);
        value.insert("expected", "Passed/exit0, original 34 saved actions");
        children.append(value);
        evidence.append(value);
        save(directory, children, comparisons, false);
        PLATFORM_CHECK(child.status != "HarnessError" && child.status == "Passed" && child.exitCode == 0 &&
                       child.signature.isEmpty() && child.pid != 0 &&
                       child.pid != static_cast<std::uint64_t>(QCoreApplication::applicationPid()) &&
                       processes.insert(child.pid).second && !child.scratchPath.isEmpty() &&
                       scratches.insert(QDir::cleanPath(child.scratchPath)).second);
    };
    for (const auto& seed : {QStringLiteral("1"), QStringLiteral("18446744073709551615")}) {
        const auto base = QDir(directory).filePath("seed-" + seed);
        const auto generated = generateChild(sourceBin, seed, QDir(base).filePath("generate"));
        record(seed + "/generate", generated);
        const auto trace = loadTrace(generated.inputPath, sourceBin);
        PLATFORM_CHECK(trace.seed == seed && trace.artifacts == artifacts);
        const auto original = semantics(generated, trace);
        const auto semanticHash = QString::fromLatin1(
            QCryptographicHash::hash(QJsonDocument(original).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
                .toHex());
        for (int repetition = 1; repetition <= 2; ++repetition) {
            const auto replayed = replayChild(sourceBin, generated.inputPath,
                                              QDir(base).filePath("replay-" + QString::number(repetition)));
            record(seed + "/replay-" + QString::number(repetition), replayed);
            PLATFORM_CHECK(replayed.inputPath == generated.inputPath && replayed.inputSha256 == generated.inputSha256 &&
                           semantics(replayed, trace) == original);
            const QJsonObject compared{{"event", "semantic_comparison"},
                                       {"seed", seed},
                                       {"repetition", QString::number(repetition)},
                                       {"original_evidence", generated.evidencePath},
                                       {"replayed_evidence", replayed.evidencePath},
                                       {"semantic_sha256", semanticHash},
                                       {"matched", true}};
            comparisons.append(compared);
            evidence.append(compared);
            save(directory, children, comparisons, false);
        }
    }
    PLATFORM_CHECK(children.size() == 6 && processes.size() == 6 && scratches.size() == 6 &&
                   artifactInputs(sourceBin) == artifacts);
    save(directory, children, comparisons, true);
    evidence.append({{"event", "proof_terminal"}, {"status", "Passed"}, {"children", "6"}});
}
} // namespace workspace_stress_test
