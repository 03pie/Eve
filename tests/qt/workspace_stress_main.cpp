/** @file workspace_stress_main.cpp
 * @brief 一个真实平台执行已落盘有限动作；失败、无效输入及harness故障分开。
 */
#include "workspace_stress_child_p.hpp"
#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <exception>
#include <iostream>
#include <memory>

namespace workspace_stress_test {
namespace {
void expectedResult(const QJsonObject& result)
{
    if (!result.contains("plan"))
        return;
    // 当前状态由verify独立核验；历史Pending不由后来的实际状态改写。
    const auto plan = result["plan"].toObject();
    PLATFORM_CHECK(plan["entries"].isArray() && plan["errors"].isArray() && plan["errors"].toArray().isEmpty());
    bool pending = false;
    for (const auto& entry : plan["entries"].toArray()) {
        const auto row = entry.toObject();
        const auto status = row["status"].toInt(-1);
        PLATFORM_CHECK((status == static_cast<int>(eve::loader::EntryStatus::Applied) ||
                        status == static_cast<int>(eve::loader::EntryStatus::Pending)) &&
                       row["error"].isNull());
        pending = pending || status == static_cast<int>(eve::loader::EntryStatus::Pending);
    }
    const auto expected = pending ? eve::loader::PlanOutcome::Pending : eve::loader::PlanOutcome::Applied;
    PLATFORM_CHECK(plan["outcome"].toInt(-1) == static_cast<int>(expected));
}
int run(const Trace& trace, const QString& sourceBin, Evidence& evidence)
{
    Scene scene;
    const Action* current = nullptr;
    try {
        scene.open(sourceBin);
        scene.apply(scene.state->initial);
        settle(scene);
        verifySettled(scene);
        evidence.append({{"event", "bootstrap"},
                         {"seed", trace.seed},
                         {"generator", trace.generator},
                         {"pid", decimal(static_cast<std::uint64_t>(QCoreApplication::applicationPid()))},
                         {"scratch", scene.scratch.path()},
                         {"qpa", QGuiApplication::platformName()},
                         {"artifacts", trace.artifacts},
                         {"observation", observe(scene)}});
        for (const auto& action : trace.actions) {
            current = &action;
            evidence.append({{"event", "action_begin"},
                             {"id", action.id},
                             {"kind", action.kind},
                             {"args", action.args},
                             {"before", observe(scene)}});
            const auto result = execute(scene, sourceBin, action);
            settle(scene);
            verifySettled(scene);
            expectedResult(result);
            evidence.append({{"event", "action_end"},
                             {"id", action.id},
                             {"kind", action.kind},
                             {"result", result},
                             {"after", observe(scene)}});
            if (!result["failure_signature"].toString().isEmpty()) {
                // 真实capture失败不伪装成通过；固定原close仍须检查，不清理PIN。
                platform_upgrade_test::finish(scene);
                evidence.append({{"event", "terminal"},
                                 {"status", "Failed"},
                                 {"failure_signature", result["failure_signature"]},
                                 {"action", action.id}});
                return 20;
            }
        }
        current = nullptr;
        platform_upgrade_test::finish(scene);
        evidence.append({{"event", "terminal"}, {"status", "Passed"}, {"actions", decimal(trace.actions.size())}});
        return 0;
    } catch (...) {
        const auto original = std::current_exception();
        QJsonObject failure{{"event", "run_exception"},
                            {"action", current ? QJsonValue(current->id) : QJsonValue()},
                            {"kind", current ? QJsonValue(current->kind) : QJsonValue()}};
        try {
            std::rethrow_exception(original);
        } catch (const ProductError& reason) {
            failure.insert("error", error(reason.reason));
        } catch (const std::exception& reason) {
            failure.insert("message", QString::fromUtf8(reason.what()));
        }
        // 原异常仍是出口；观察失败仅作次级证据，不能覆盖原 ErrorInfo。
        try {
            failure.insert("after", observe(scene));
        } catch (const ProductError& reason) {
            failure.insert("observation_error", error(reason.reason));
        } catch (const std::exception& reason) {
            failure.insert("observation_message", QString::fromUtf8(reason.what()));
        }
        evidence.append(std::move(failure));
        std::rethrow_exception(original);
    }
}
} // namespace
} // namespace workspace_stress_test

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    using namespace workspace_stress_test;
    std::unique_ptr<Evidence> evidence;
    try {
        const auto args = application.arguments();
        PLATFORM_CHECK(QGuiApplication::platformName() == "windows");
        if (args.size() == 4) {
            const auto sourceBin = QDir(args[2]).absolutePath();
            const auto root = QDir(args[3]).absolutePath();
            PLATFORM_CHECK(QDir().mkpath(root));
            QTemporaryDir output(QDir(root).filePath("run-XXXXXX"));
            PLATFORM_CHECK(output.isValid());
            output.setAutoRemove(false);
            const auto directory = output.path();
            std::cout << "Proof evidence: " << directory.toStdString() << std::endl;
            if (args[1] == "replay-proof")
                replayProof(sourceBin, directory);
            else if (args[1] == "invalid-proof")
                invalidTraceProof(sourceBin, directory);
            else if (args[1] == "reduce-proof")
                reductionProof(sourceBin, directory);
            else
                throw InvalidTrace("unknown proof command");
            return 0;
        }
        if (args.size() != 5 || (args[1] != "generate" && args[1] != "replay"))
            throw InvalidTrace("usage: generate|replay <source-bin> <seed|input-file> <output-prefix>");
        const auto sourceBin = QDir(args[2]).absolutePath();
        const auto prefix = QDir::cleanPath(args[4]);
        evidence = std::make_unique<Evidence>(prefix + ".jsonl");
        if (args[1] == "generate")
            saveTrace(generateTrace(sourceBin, args[3]), prefix + ".actions.json");
        // 即使生成模式也从实际落盘文件解释；执行器不接收PRNG或原生成器状态。
        const auto trace = loadTrace(args[1] == "generate" ? prefix + ".actions.json" : args[3], sourceBin);
        const auto result = run(trace, sourceBin, *evidence);
        std::cout << "Workspace action trace status " << result << ", seed=" << trace.seed.toStdString() << '\n';
        return result;
    } catch (const InvalidTrace& reason) {
        if (evidence)
            evidence->append({{"event", "terminal"}, {"status", "InvalidTrace"}, {"message", reason.what()}});
        std::cerr << "InvalidTrace: " << reason.what() << '\n';
        return 21;
    } catch (const ProductError& reason) {
        if (evidence)
            evidence->append({{"event", "terminal"}, {"status", "Failed"}, {"error", error(reason.reason)}});
        std::cerr << "ProductError: " << reason.what() << '\n';
        return 20;
    } catch (const std::exception& reason) {
        if (evidence)
            evidence->append({{"event", "terminal"}, {"status", "HarnessError"}, {"message", reason.what()}});
        std::cerr << reason.what() << '\n';
        return 1;
    }
}
