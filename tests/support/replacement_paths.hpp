/** @file replacement_paths.hpp
 * @brief 两个真实模块共享未创建配置祖先时排他，独立根可并行提交目录。
 */
#pragma once
#include "replacement_peer.hpp"
#include <algorithm>
#include <cordis/loader/qt_store.hpp>
#include <cordis/module/native.hpp>
#include <cordis/replacement_operation.hpp>
#include <utility>

template <class SceneType, class Wait>
void operationPathsCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                        const std::shared_ptr<cordis::IScheduler>& scheduler,
                        const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, bool parallel)
{
    using namespace cordis;
    using namespace cordis::loader;
    using namespace cordis::module;
    const auto take = [](auto result, const char* step) {
        if (!result.hasValue())
            throw std::runtime_error(std::string(step) + ": " + result.error().message);
        return std::move(result).value();
    };
    auto peer = openReplacementPeer(scene, scheduler, kernel);
    const auto peerOld = peer.previous, peerNext = peer.candidate;
    auto& peerOldModule = peer.previousModule;
    auto& peerNextModule = peer.candidateModule;
    const auto peerFactory = peer.previousRuntime, peerCandidate = peer.candidateRuntime;
    kernel->registerRuntime("paths.peer", peerFactory).value();
    const auto firstActivation = std::make_shared<bool>(true);
    const auto parent = kernel
                            ->registerPlugin("paths.parent",
                                             [firstActivation](Context) {
                                                 if (std::exchange(*firstActivation, false))
                                                     return failedTask(makeError(ErrorCode::ActivationFailed,
                                                                                 "initial parent unavailable"));
                                                 return completedTask();
                                             })
                            .value();
    const auto id = scene.qt ? "org.cordis.qt.page" : "org.cordis.plugin";
    const auto schema = [](const std::string&) { return Result<void>::success(); };
    auto definitions =
        take(ConfigurationDefinitions::create(
                 {{{id, {}, schema, {"business", "old/module.json"}}, old},
                  {{"org.cordis.peer.plugin", {}, schema, {"peer", "peer-old/module.json"}}, peerFactory},
                  {{"parent", {}, schema}, parent}}),
             "path definitions");
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, definitions);
    const auto file = scene.root + "/paths.yaml";
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(file), readConfigurationFile(file).value());
    const auto text = [&](bool primaryNew, bool peerNew) {
        auto source = std::string("formatVersion: 1\nmodules: {business: {manifest: ") + (primaryNew ? "next" : "old") +
                      "/module.json}, peer: {manifest: " + (peerNew ? "peer-next" : "peer-old") +
                      "/module.json}}\nplugins: [{id: root, plugin: parent, children: [{id: a, plugin: " + id +
                      ", module: business, config: " + (scene.qt ? "normal" : "sync") + "}";
        source += parallel ? "]}, {id: root-peer, plugin: parent, children: [" : ", ";
        return source + "{id: b, plugin: org.cordis.peer.plugin, module: peer, config: sync}]}]\n";
    };
    auto initial = take(session->apply(session->preview(text(false, false)).value()), "initial paths");
    CHECK(waitFor(initial.settled)->value().outcome == PlanOutcome::Failed);
    for (const auto& row : driver->snapshot().value())
        if (row.instance.valid())
            CHECK(waitFor(kernel->findInstance(row.instance).value().stop())->value().quiescent);
    CHECK(kernel->snapshot().value().instances.empty());
    CHECK(waitFor(session->save(1).value())->hasValue());
    const auto request = [&](bool primary, bool primaryAlreadyNew) {
        auto current = driver->configurationDefinitions();
        auto following =
            take(current->replacing(
                     primary ? std::vector<ConfiguredPlugin>{{{id, {}, schema, {"business", "next/module.json"}}, next}}
                             : std::vector<ConfiguredPlugin>{{{"org.cordis.peer.plugin",
                                                               {},
                                                               schema,
                                                               {"peer", "peer-next/module.json"}},
                                                              peerCandidate}}),
                 "path candidate definitions");
        auto target = std::make_shared<const ValidatedCandidate>(
            following->validate(parseConfiguration(text(primary || primaryAlreadyNew, !primary)).value()).value());
        ReplacementRequest value;
        value.scheduler = scheduler;
        value.kernel = kernel;
        value.modules = scene.managerOwner;
        value.previous = primary ? scene.current : peerOld;
        value.candidate = primary ? scene.candidate : peerNext;
        value.pluginRoot = scene.root.toStdString();
        value.definitions.push_back({primary ? old.id() : peerFactory.id(), primary ? next : peerCandidate});
        value.configurations.push_back(
            {session, driver, following, target, session->snapshot().value().coordinator.desiredRevision});
        return value;
    };
    auto first = take(ReplacementOperation::begin(request(true, false)), "first logical operation");
    if (!parallel) {
        auto competing = scene.manager.reserveReplacement(peerOld, peerNext);
        CHECK(!competing.hasValue() && competing.error().code == ErrorCode::ConfigConflict);
        const auto closing = peerOldModule.close().tryResult();
        CHECK(closing && !closing->hasValue() && closing->error().code == ErrorCode::ConfigConflict);
        const auto draining = scene.manager.drain(peerOld);
        CHECK(!draining.hasValue() && draining.error().code == ErrorCode::ConfigConflict);
    }
    const auto unchanged = session->snapshot().value().coordinator.desired;
    const auto direct = driver->apply({"root", ChangeKind::Add}, unchanged, 1).tryResult();
    CHECK(direct && !direct->hasValue() && direct->error().code == ErrorCode::ConfigConflict);
    CHECK(!session->setEnabled("root", false, 1).hasValue());
    CHECK(session->snapshot().value().coordinator.desiredRevision == 1 && kernel->snapshot().value().instances.empty());
    auto second = ReplacementOperation::begin(request(false, false));
    const auto applied = [&](const auto& task) {
        const auto result = waitFor(task);
        CHECK(result && result->hasValue());
        if (result->value().outcome != ReplacementOutcome::Applied)
            for (const auto& error : result->value().errors)
                std::cerr << error.message << " [" << error.phase << "]\n";
        CHECK(result->value().outcome == ReplacementOutcome::Applied);
    };
    if (parallel) {
        CHECK(second.hasValue());
        applied(first.settled());
        applied(second.value().settled());
    } else {
        CHECK(!second.hasValue() && second.error().code == ErrorCode::ConfigConflict);
        CHECK(second.error().operation == first.snapshot().value().report.operation);
        CHECK(!scene.state(peerOld).replacement && !scene.state(peerNext).replacement);
        CHECK(kernel->snapshot().value().instances.empty());
        CHECK(session->snapshot().value().coordinator.desiredRevision == 1);
        applied(first.settled());
        auto retry = take(ReplacementOperation::begin(request(false, true)), "retry second operation");
        applied(retry.settled());
    }
    const auto rows = driver->snapshot().value();
    CHECK(rows.size() == (parallel ? 4 : 3));
    CHECK(std::all_of(rows.begin(), rows.end(), [](const auto& row) { return row.state == InstanceState::Active; }));
    CHECK(scene.state(scene.candidate).role == GenerationRole::Current &&
          scene.state(peerNext).role == GenerationRole::Current);
    const auto state = session->snapshot().value();
    CHECK(state.coordinator.desiredRevision == 3 &&
          state.coordinator.desired->source().structure().modules().at("business") == "next/module.json");
    CHECK(state.coordinator.desired->source().structure().modules().at("peer") == "peer-next/module.json");
    CHECK(waitFor(session->save(3).value())->hasValue());
    CHECK(readConfigurationFile(file).value().text == state.coordinator.desired->source().normalizedYaml());
    CHECK(waitFor(session->close())->hasValue() && waitFor(kernel->close())->value().quiescent);
    CHECK(waitFor(peerOldModule.close())->value().quiescent && waitFor(peerNextModule.close())->value().quiescent);
}
