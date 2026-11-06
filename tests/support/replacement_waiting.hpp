/** @file replacement_waiting.hpp
 * @brief 实际配置子项停止后保留逻辑记录；替换必须连同活祖先重建。
 */
#pragma once
#include "../../src/core/replacement_inspect_p.hpp"
#include <cordis/replacement_operation.hpp>
#include <utility>

template <class SceneType, class Wait>
void operationWaitingCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                          const std::shared_ptr<cordis::IScheduler>& scheduler,
                          const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    using namespace cordis::loader;
    const bool recovering = mode.endsWith("_recovery");
    const bool stopRoot = mode == "operation_waiting_root";
    const bool unstarted = mode.startsWith("operation_unstarted");
    const auto take = [](auto result, const char* step) {
        if (!result.hasValue())
            throw std::runtime_error(std::string(step) + ": " + result.error().message);
        return std::move(result).value();
    };
    const auto failParent = std::make_shared<bool>(unstarted);
    const auto parent = kernel
                            ->registerPlugin("waiting.parent",
                                             [failParent](Context) {
                                                 if (std::exchange(*failParent, false))
                                                     return failedTask(makeError(ErrorCode::ActivationFailed,
                                                                                 "initial parent failed once"));
                                                 return completedTask();
                                             })
                            .value();
    const auto id = scene.qt ? "org.cordis.qt.page" : "org.cordis.plugin";
    const auto schema = [](const std::string&) { return Result<void>::success(); };
    auto before = take(ConfigurationDefinitions::create({{{id, {}, schema, {"business", "old/module.json"}}, old},
                                                         {{"parent", {}, schema}, parent}}),
                       "old definitions");
    auto after =
        take(before->replacing({{{id, {}, schema, {"business", "next/module.json"}}, next}}), "new definitions");
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, before);
    const auto file = scene.root + "/waiting.yaml";
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(file), readConfigurationFile(file).value());
    const auto text = [&](const char* folder, bool failing = false) {
        return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + folder +
               "/module.json}}\nplugins: [{id: root, plugin: parent, children: [{id: child, plugin: " + id +
               ", module: business, config: " +
               (failing ? (scene.qt ? "start_failure" : "start-fail") : (scene.qt ? "normal" : "sync")) + "}]}]\n";
    };
    auto initial = take(session->apply(session->preview(text("old")).value()), "initial target");
    CHECK(waitFor(initial.settled)->value().outcome == (unstarted ? PlanOutcome::Failed : PlanOutcome::Applied));
    CHECK(waitFor(session->save(1).value())->hasValue());
    const auto oldRoot = driver->snapshot().value()[0].instance;
    std::optional<Instance> stopped;
    if (!unstarted) {
        // 测试布置从真实 Core 凭据取得实际子句柄，不按编号伪造子实例或开放 SDK 查找接口。
        auto probe = take(kernel->reserveReplacement({old.id()}), "setup reservation");
        probe.prepareDefinitions({{old.id(), next}}).value();
        const auto inputs =
            take(cordis::detail::CoreReplacementAccess::original(probe, *kernel), "actual setup members");
        const auto member = std::find_if(inputs.members.begin(), inputs.members.end(),
                                         [&](const auto& item) { return item.input.runtime == old.id(); });
        CHECK(member != inputs.members.end());
        stopped = member->instance;
        probe.release().value();
        CHECK(waitFor(stopped->stop())->value().quiescent);
        CHECK(stopped->snapshot().value().state == InstanceState::Disposed);
    }
    if (stopRoot)
        CHECK(waitFor(kernel->findInstance(oldRoot).value().stop())->value().quiescent);
    const auto rows = driver->snapshot().value();
    if (unstarted)
        CHECK(rows.size() == 1 && rows[0].state == InstanceState::Failed);
    else if (stopRoot)
        CHECK(rows.size() == 1 && !rows[0].instance.valid());
    else
        CHECK(rows.size() == 2 && rows[0].state == InstanceState::Active && !rows[1].instance.valid());
    auto target = std::make_shared<const ValidatedCandidate>(
        after->validate(parseConfiguration(text("next", recovering)).value()).value());
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.pluginRoot = scene.root.toStdString();
    request.definitions.push_back({old.id(), next});
    request.configurations.push_back({session, driver, after, target, 1});
    auto operation = take(ReplacementOperation::begin(std::move(request)), "replacement begin");
    auto result = waitFor(operation.settled());
    CHECK(result && result->hasValue());
    const auto outcome = recovering ? ReplacementOutcome::Restored : ReplacementOutcome::Applied;
    if (result->value().outcome != outcome)
        for (const auto& error : result->value().errors)
            std::cerr << error.message << "\n";
    CHECK(result->value().outcome == outcome && result->value().committed == !recovering &&
          result->value().recovered == recovering && !result->value().resourcesRetained);
    const auto current = driver->snapshot().value();
    CHECK(current.size() == 2 && current[0].state == InstanceState::Active &&
          current[1].state == InstanceState::Active);
    CHECK(current[0].instance != oldRoot);
    if (stopped)
        CHECK(current[1].instance != stopped->snapshot().value().instance);
    CHECK(driver->configurationDefinitions()->owns(*session->snapshot().value().coordinator.desired));
    CHECK(waitFor(session->save(recovering ? 1 : 2).value())->hasValue());
    CHECK(readConfigurationFile(file).value().text ==
          session->snapshot().value().coordinator.desired->source().normalizedYaml());
    CHECK(waitFor(session->close())->hasValue());
    CHECK(waitFor(kernel->close())->value().quiescent);
}
