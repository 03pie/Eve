/** @file workspace_stress_main.cpp
 * @brief 一个真实平台执行已落盘有限动作；失败、无效输入及harness故障分开。
 */
#include "workspace_stress_trace_p.hpp"
#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <exception>
#include <iostream>
#include <memory>

namespace workspace_stress_test {
namespace {
void expectedResult(const Action& action, const QJsonObject& result)
{
    if (!result.contains("plan"))
        return;
    // 此有限轨迹的唯一缺服务目标是停用默认Counter；不把任何原Failed都当作压力通过。
    const bool pending =
        action.kind == "enable" && action.args["path"] == "counter" && action.args["enabled"] == QJsonValue(false);
    const auto expected = pending ? cordis::loader::PlanOutcome::Pending : cordis::loader::PlanOutcome::Applied;
    PLATFORM_CHECK(result["plan"].toObject()["outcome"].toInt(-1) == static_cast<int>(expected));
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
                         {"pid", decimal(static_cast<std::uint64_t>(QCoreApplication::applicationPid()))},
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
            expectedResult(action, result);
            settle(scene);
            verifySettled(scene);
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
        if (args.size() != 5 || args[1] != "generate")
            throw InvalidTrace("usage: generate <source-bin> <seed> <output-prefix>");
        PLATFORM_CHECK(QGuiApplication::platformName() == "windows");
        const auto sourceBin = QDir(args[2]).absolutePath();
        const auto prefix = QDir::cleanPath(args[4]);
        evidence = std::make_unique<Evidence>(prefix + ".jsonl");
        saveTrace(generateTrace(sourceBin, args[3]), prefix + ".actions.json");
        // 即使生成模式也从实际落盘文件解释；执行器不接收PRNG或原生成器状态。
        const auto trace = loadTrace(prefix + ".actions.json", sourceBin);
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
