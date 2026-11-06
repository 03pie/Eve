/** @file replacement_sources.hpp
 * @brief 真实外部服务的模块祖先不在替换闭包内，仍须持有其代次读意图。
 */
#pragma once
#include "../../src/module/host_services_p.hpp"
#include "replacement_owner.hpp"
#include "replacement_peer.hpp"
#include "replacement_subtree.hpp"
#include <cordis_abi/plugin.h>

template <class T> T replacementSourceEntry(HMODULE module, const char* name)
{
    const auto symbol = GetProcAddress(module, name);
    CHECK(symbol);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
}
template <class SceneType, class Wait>
bool operationRawSourceCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                            const std::shared_ptr<cordis::IScheduler>& scheduler,
                            const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor)
{
    using namespace cordis;
    using namespace cordis::module;
    const auto artifact =
        testing::modules::install(qEnvironmentVariable("CORDIS_TEST_RAW_SOURCE_DLL"), scene.root + "/raw-source",
                                  "org.cordis.services", {"org.cordis.plugin"});
    const auto generation = scene.manager.load(scene.root.toStdString(), artifact.toStdString()).value().generation;
    auto module = NativeModule::open(scene.manager, generation, scheduler, kernel->services()).value();
    const auto provider = module.create("org.cordis.plugin", "{}", InstanceId{100001}, 1).value();
    CHECK(waitFor(provider.start())->hasValue());
    const auto path =
        QDir::toNativeSeparators(QString::fromStdString(scene.state(generation).libraryPath)).toStdWString();
    const auto image = GetModuleHandleW(path.c_str());
    CHECK(image);
    using Effect = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisHandle*);
    using Ready = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisStatus);
    using Register = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, const CordisServiceKeyV1*,
                                                const CordisServiceMetadataV1*, CordisHandle, CordisHandle*);
    using Ack = void(CORDIS_CALL*)(unsigned, CordisStatus);
    using Dispatch = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisBytesV1, CordisRequestId*);
    const auto effect = replacementSourceEntry<Effect>(image, "cordis_resource_effect");
    const auto ready = replacementSourceEntry<Ready>(image, "cordis_resource_ready");
    const auto reg = replacementSourceEntry<Register>(image, "cordis_service_register");
    const auto ack = replacementSourceEntry<Ack>(image, "cordis_resource_ack_status");
    const auto dispatch = replacementSourceEntry<Dispatch>(image, "cordis_resource_dispatch");
    CordisHandle ownedEffect = 0, binding = 0;
    CHECK(effect(0, 11, &ownedEffect) == CORDIS_OK && ownedEffect);
    CHECK(ready(0, ownedEffect, CORDIS_OK) == CORDIS_OK);
    CordisServiceKeyV1 key{{reinterpret_cast<const uint8_t*>("raw.input"), 9}, 1, 0, 1};
    CordisServiceMetadataV1 metadata{sizeof(CordisServiceMetadataV1), 0, {reinterpret_cast<const uint8_t*>("[]"), 2}};
    CHECK(reg(0, ownedEffect, &key, &metadata, 8, &binding) == CORDIS_OK && binding);
    const auto requirement = ServiceRequirement::of<cordis::module::detail::NativeServiceEndpoint>({"raw.input"});
    const auto root = kernel->mount(old, scene.qt ? "normal" : "\"sync\"", {}, {requirement}).value();
    CHECK(waitFor(root.ready())->hasValue());
    CHECK(provider.snapshot().value().services.size() == 1 &&
          provider.snapshot().value().services[0].state == BindingState::Published);
    auto owner = std::make_shared<OperationOwnerProbe>();
    owner->scheduler = scheduler;
    owner->suspendCandidate = true;
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.definitions.push_back({old.id(), next});
    request.owners.emplace(root.snapshot().value().instance, owner);
    auto operation = ReplacementOperation::begin(std::move(request)).value();
    CHECK(waitFor(owner->entered.task())->hasValue() && !operation.settled().isReady());
    const auto drained = scene.manager.drain(generation);
    CHECK(!drained.hasValue() && drained.error().code == ErrorCode::ConfigConflict);
    ack(0, CORDIS_PENDING);
    CordisRequestId call = 0;
    CHECK(dispatch(0, 8, {reinterpret_cast<const uint8_t*>("{}"), 2}, &call) == CORDIS_PENDING && call);
    for (int i = 0; i != 16; ++i) {
        Promise<void> advanced;
        CHECK(scheduler->post({[advanced] { advanced.resolve(); }, {}}));
        CHECK(waitFor(advanced.task())->hasValue());
    }
    CHECK(provider.snapshot().value().uncertain && scene.state(generation).businessFailure);
    CHECK(operation.settled().isReady());
    const auto completed = waitFor(operation.settled());
    CHECK(completed && completed->hasValue());
    const auto& report = completed->value();
    CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.committed &&
          !report.recovered && report.instances.empty());
    CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                      [](const auto& error) { return error.code == ErrorCode::RecoveryFailed; }));
    CHECK(owner->rebuilt == 1 && !owner->observedBuilder->stage(*owner->lastFactory).hasValue());
    owner->pending.reject(makeError(ErrorCode::OperationCancelled, "late raw source owner"));
    CHECK(operation.snapshot().value().report.outcome == ReplacementOutcome::Failed);
    CHECK(waitFor(kernel->close())->value().quiescent);
    return true;
}
template <class SceneType, class Wait>
bool operationSourcesCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                          const std::shared_ptr<cordis::IScheduler>& scheduler,
                          const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    if (mode == "operation_sources_raw_loss")
        return operationRawSourceCase(scene, old, next, scheduler, kernel, waitFor);
    auto peer = openReplacementPeer(scene, scheduler, kernel);
    const bool sharedLoss = mode == "operation_sources_loss_shared";
    const bool lostSource = mode == "operation_sources_loss" || sharedLoss;
    const bool independent = mode == "operation_sources_independent" || (lostSource && !sharedLoss);
    const auto parentFactory =
        independent ? kernel->registerPlugin("source.parent", [](Context) { return completedTask(); }).value()
                    : kernel->registerRuntime("source.parent", peer.previousRuntime).value();
    const auto parent = kernel->mount(parentFactory, "\"sync\"").value();
    CHECK(waitFor(parent.ready())->hasValue());
    Runtime provider(
        [](Context context) {
            context.provide<int>({"owner.child"}, std::make_shared<int>(7)).value();
            return completedTask();
        },
        {}, {ServiceOutput::of<int>({"owner.child"})});
    const auto source = parent.whenActiveAfter().tryResult()->value().inject(provider).value();
    CHECK(waitFor(source.ready())->hasValue());
    CHECK(!scene.manager.runtimeOrigin(provider.id()).value()); // 提供者自身内置，实际祖先来自 peer DLL。
    CHECK(scene.manager.runtimeOrigin(peer.previousRuntime.id()).value()->generation == peer.previous);
    Runtime transient([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"owner.child"})});
    const auto root = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
    CHECK(waitFor(root.ready())->hasValue());
    const auto first = root.whenActiveAfter().tryResult()->value().inject(transient, "first").value();
    CHECK(waitFor(first.ready())->hasValue());
    const auto second = first.whenActiveAfter().tryResult()->value().inject(transient, "second").value();
    CHECK(waitFor(second.ready())->hasValue());
    auto owner = std::make_shared<SubtreeReplacementOwner>(scheduler, transient);
    owner->suspendCandidate = lostSource;
    using Count = unsigned(CORDIS_CALL*)();
    const auto counter = [&](GenerationId generation) {
        const auto path =
            QDir::toNativeSeparators(QString::fromStdString(scene.state(generation).libraryPath)).toStdWString();
        return replacementSourceEntry<Count>(GetModuleHandleW(path.c_str()), "cordis_business_fixture_creates");
    };
    const auto creates = counter(peer.previous), nextCreates = counter(peer.candidate);
    const auto before = creates(), nextBefore = nextCreates();
    const bool dynamic = mode != "operation_sources_ancestor";
    if (dynamic) {
        const auto factory = mode == "operation_sources_declared"
                                 ? peer.previousModule.runtime({"org.cordis.peer.plugin", {}, {}}).value()
                                 : peer.previousRuntime;
        if (mode == "operation_sources_declared")
            CHECK(factory.id() != peer.previousRuntime.id());
        owner->candidateFactories.emplace("second", factory);
        owner->candidateFactories.emplace("\"sync\"", factory);
        owner->candidateConfigs.emplace("second", "\"sync\"");
        const module::ModuleFactorySource declared{peer.previous, "org.cordis.peer.plugin"};
        if (mode == "operation_sources_declared" || independent || mode == "operation_sources_mismatch" || sharedLoss)
            owner->candidateSources.emplace("second", declared);
        else if (mode == "operation_sources_misattributed")
            owner->candidateSources.emplace("first", declared);
        else if (mode == "operation_sources_plugin")
            owner->candidateSources.emplace("second", module::ModuleFactorySource{peer.previous, "unknown.plugin"});
        else if (mode == "operation_sources_role")
            owner->candidateSources.emplace("second", module::ModuleFactorySource{peer.candidate, declared.pluginId});
        else if (mode == "operation_sources_recovery_source")
            owner->recoverySources.emplace("second", declared);
        if (mode == "operation_sources_mismatch") {
            owner->candidateFactories.erase("second");
            owner->candidateFactories.erase("\"sync\"");
            owner->candidateFactories.emplace("second", peer.candidateRuntime);
            owner->candidateFactories.emplace("\"sync\"", peer.candidateRuntime);
        }
    }
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.definitions.push_back({old.id(), next});
    request.owners.emplace(root.snapshot().value().instance, owner);
    auto started = ReplacementOperation::begin(std::move(request));
    const bool rejected = mode == "operation_sources_plugin" || mode == "operation_sources_role" ||
                          mode == "operation_sources_recovery_source";
    if (rejected) {
        CHECK(!started.hasValue() && started.error().code == ErrorCode::ConfigConflict && owner->rebuilt == 0);
        CHECK(creates() == before && nextCreates() == nextBefore &&
              root.snapshot().value().state == InstanceState::Active &&
              first.snapshot().value().state == InstanceState::Active &&
              second.snapshot().value().state == InstanceState::Active);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        auto released = scene.manager.reserveReplacement(peer.previous, peer.candidate);
        CHECK(released.hasValue());
        released.value().reset();
        CHECK(waitFor(kernel->close())->value().quiescent);
        CHECK(waitFor(peer.previousModule.close())->value().quiescent);
        CHECK(waitFor(peer.candidateModule.close())->value().quiescent);
        return false;
    }
    CHECK(started.hasValue());
    auto operation = std::move(started).value();
    const auto id = operation.snapshot().value().report.operation;
    auto denied = scene.manager.reserveReplacement(peer.previous, peer.candidate);
    CHECK(!denied.hasValue() && denied.error().code == ErrorCode::ConfigConflict && denied.error().operation == id);
    const auto closed = peer.previousModule.close().tryResult();
    CHECK(closed && !closed->hasValue() && closed->error().code == ErrorCode::ConfigConflict);
    CHECK(!scene.manager.drain(peer.previous).hasValue());
    CHECK(owner->rebuilt == 0 && source.snapshot().value().state == InstanceState::Active &&
          parent.snapshot().value().state == InstanceState::Active);
    if (dynamic) {
        if (lostSource) {
            CHECK(waitFor(owner->entered.task())->hasValue() && !operation.settled().isReady());
            auto fault =
                peer.previousModule.create("org.cordis.peer.plugin", "\"create-live-error\"", InstanceId{100001}, 1);
            CHECK(!fault.hasValue() && fault.error().code == ErrorCode::AbiProtocol);
            CHECK(scene.state(peer.previous).businessFailure);
            for (int i = 0; i != 16; ++i) {
                Promise<void> advanced;
                CHECK(scheduler->post({[advanced] { advanced.resolve(); }, {}}));
                CHECK(waitFor(advanced.task())->hasValue());
            }
            CHECK(operation.settled().isReady());
        }
        const auto completed = waitFor(operation.settled());
        CHECK(completed && completed->hasValue());
        if (sharedLoss) {
            const auto& report = completed->value();
            CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.committed &&
                  !report.recovered && report.instances.empty());
            CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                              [](const auto& error) { return error.code == ErrorCode::RecoveryFailed; }));
            CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                              [](const auto& error) { return error.code == ErrorCode::AbiProtocol; }));
            CHECK(owner->rebuilt == 1 && !owner->observed && creates() == before + 1 && nextCreates() == nextBefore);
            CHECK(!owner->previousBuilder->stage(*owner->previousFactory).hasValue());
            CHECK(root.snapshot().value().state == InstanceState::Disposed &&
                  scene.state(scene.current).role == module::GenerationRole::Current &&
                  scene.state(scene.candidate).role == module::GenerationRole::Candidate);
            CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
            owner->pending.reject(makeError(ErrorCode::OperationCancelled, "late shared source owner"));
            CHECK(operation.snapshot().value().report.outcome == ReplacementOutcome::Failed && owner->rebuilt == 1);
            CHECK(waitFor(kernel->close())->value().quiescent);
            return true;
        }
        const bool applied = !lostSource && (mode == "operation_sources_declared" || independent);
        const bool misplaced = mode == "operation_sources_misattributed";
        CHECK(creates() == before + (applied || misplaced || lostSource ? 1U : 0U) && nextCreates() == nextBefore);
        const auto& report = completed->value();
        CHECK(report.outcome == (applied ? ReplacementOutcome::Applied : ReplacementOutcome::Restored) &&
              report.recovered == !applied && report.committed == applied && !report.resourcesRetained &&
              report.instances.size() == 3);
        if (!applied)
            CHECK(std::any_of(report.errors.begin(), report.errors.end(), [misplaced, lostSource](const auto& error) {
                if (lostSource)
                    return error.code == ErrorCode::AbiProtocol;
                return error.code == ErrorCode::ConfigConflict &&
                       (misplaced ? error.message == "actual factory source differs from preflight"
                                  : error.message == "factory source is outside sealed plan");
            }));
        CHECK(root.snapshot().value().state == InstanceState::Disposed && owner->rebuilt == (applied ? 1U : 2U));
        CHECK(std::all_of(owner->observed->members.begin(), owner->observed->members.end(), [](const auto& member) {
            return member.instance.snapshot().value().state == InstanceState::Active;
        }));
        if (lostSource) {
            CHECK(!owner->previousBuilder->stage(*owner->previousFactory).hasValue());
            owner->pending.reject(makeError(ErrorCode::OperationCancelled, "late source owner"));
            CHECK(operation.snapshot().value().report.outcome == ReplacementOutcome::Restored && owner->rebuilt == 2);
        }
    } else {
        CHECK(operation.cancel().hasValue());
        const auto cancelled = waitFor(operation.settled());
        CHECK(cancelled && cancelled->hasValue() && cancelled->value().outcome == ReplacementOutcome::Cancelled &&
              !cancelled->value().resourcesRetained);
    }
    if (!lostSource) {
        auto released = scene.manager.reserveReplacement(peer.previous, peer.candidate);
        CHECK(released.hasValue());
        released.value().reset();
    }
    CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    CHECK(waitFor(kernel->close())->value().quiescent);
    CHECK(waitFor(peer.previousModule.close())->value().quiescent == !lostSource);
    CHECK(waitFor(peer.candidateModule.close())->value().quiescent);
    return false;
}
