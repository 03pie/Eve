/** @file replacement_session_close.hpp
 * @brief 会话关闭必须先结束替换，再关闭自身驱动；手动实例仍按旧代恢复。
 */
#pragma once
#include "replacement_owner.hpp"
#include <algorithm>
#include <cordis/loader/qt_file.hpp>
#include <functional>
#include <optional>

template <class SceneType, class Wait>
bool operationSessionCloseCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                               const std::shared_ptr<cordis::IScheduler>& scheduler,
                               const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    using namespace cordis::loader;
    const auto id = scene.qt ? "org.cordis.qt.page" : "org.cordis.plugin";
    const bool prepared = mode.endsWith("_prepared"), stopping = mode.endsWith("_stopping");
    const bool atCommit = mode.endsWith("_commit"), failedRecovery = mode.endsWith("_recovery_failure");
    const bool recoveryCommit = mode.endsWith("_recovery_commit");
    std::function<void()> validationHook, recoveryHook;
    const auto available = [&] {
        if (recoveryHook) {
            auto hook = std::move(recoveryHook);
            recoveryHook = {};
            hook();
        }
        return Result<void>::success();
    };
    const auto nextSchema = [&](const std::string&) {
        if (validationHook) {
            auto hook = std::move(validationHook);
            validationHook = {};
            hook();
        }
        return Result<void>::success();
    };
    const auto schema = [](const std::string&) { return Result<void>::success(); };
    auto before =
        ConfigurationDefinitions::create({{{id, {}, schema, {"business", "old/module.json"}}, old, {}, available}})
            .value();
    auto after = before->replacing({{{id, {}, nextSchema, {"business", "next/module.json"}}, next}}).value();
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, before);
    const auto file = scene.root + "/session-close.yaml";
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(file), readConfigurationFile(file).value());
    const auto text = [&](const char* folder) {
        return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + folder +
               "/module.json}}\nplugins: [{id: item, plugin: " + id +
               ", module: business, config: " + (scene.qt ? "normal" : "sync") + "}]\n";
    };
    const auto initial = session->apply(session->preview(text("old")).value()).value();
    CHECK(waitFor(initial.settled)->value().outcome == PlanOutcome::Applied);
    CHECK(waitFor(session->save(1).value())->hasValue());
    const auto originalFile = readConfigurationFile(file).value().text;
    const auto configured = driver->snapshot().value()[0].instance;
    auto otherDriver = KernelReconcileDriver::configured(scheduler, kernel, before);
    const auto otherFile = scene.root + "/other-session-close.yaml";
    auto otherSession = std::make_shared<ConfigurationSession>(scheduler, otherDriver,
                                                               std::make_shared<QtConfigurationWriter>(otherFile),
                                                               readConfigurationFile(otherFile).value());
    const auto otherInitial = otherSession->apply(otherSession->preview(text("old")).value()).value();
    CHECK(waitFor(otherInitial.settled)->value().outcome == PlanOutcome::Applied);
    CHECK(waitFor(otherSession->save(1).value())->hasValue());
    const auto otherConfigured = otherDriver->snapshot().value()[0].instance;
    const auto manual = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
    CHECK(waitFor(manual.ready())->hasValue());
    const auto manualId = manual.snapshot().value().instance;
    auto owner = std::make_shared<OperationOwnerProbe>();
    owner->scheduler = scheduler;
    owner->suspendCandidate = !atCommit;
    owner->rejectRecovery = failedRecovery;
    owner->rejectCandidate = recoveryCommit;
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.pluginRoot = scene.root.toStdString();
    request.definitions.push_back({old.id(), next});
    auto target =
        std::make_shared<const ValidatedCandidate>(after->validate(parseConfiguration(text("next")).value()).value());
    request.configurations.push_back({session, driver, after, target, 1});
    request.configurations.push_back({otherSession, otherDriver, after, target, 1});
    request.owners.emplace(configured, owner);
    request.owners.emplace(otherConfigured, owner);
    request.owners.emplace(manualId, owner);
    if (mode.endsWith("_preflight_kernel")) {
        std::optional<Task<CleanupReport>> closing;
        owner->preflightHook = [&] {
            closing = kernel->close();
            CHECK(!closing->isReady());
        };
        auto started = ReplacementOperation::begin(std::move(request));
        CHECK(!started.hasValue() && closing && !started.error().causes.empty());
        CHECK(owner->rebuilt == 0);
        CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
        CHECK(waitFor(*closing)->value().quiescent);
        Promise<void> observed;
        CHECK(scheduler->post({[observed] { observed.resolve(); }, {}}));
        CHECK(waitFor(observed.task())->hasValue());
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(waitFor(session->close())->hasValue() && waitFor(otherSession->close())->hasValue());
        CHECK(readConfigurationFile(file).value().text == originalFile);
        return false;
    }
    if (mode.contains("_preflight")) {
        std::optional<Task<void>> closed;
        const auto interrupt = [&] {
            closed = session->close();
            // 拥有者可进入原生嵌套事件循环；关闭不得在预检栈内越过替换权限。
            for (int i = 0; i != 16; ++i) {
                Promise<void> advanced;
                CHECK(scheduler->post({[advanced] { advanced.resolve(); }, {}}));
                CHECK(waitFor(advanced.task())->hasValue());
            }
            CHECK(!closed->isReady());
            if (mode.endsWith("_throw"))
                throw std::runtime_error("preflight owner rejected after close");
        };
        if (mode.endsWith("_available"))
            recoveryHook = interrupt;
        else
            owner->preflightHook = interrupt;
        auto started = ReplacementOperation::begin(std::move(request));
        CHECK(!started.hasValue() && closed && owner->rebuilt == 0);
        CHECK(waitFor(*closed)->hasValue());
        CHECK(driver->snapshot().value().empty());
        CHECK(readConfigurationFile(file).value().text == originalFile);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(manual.snapshot().value().state == InstanceState::Active);
        CHECK(otherDriver->snapshot().value()[0].instance == otherConfigured);
        CHECK(otherSession->snapshot().value().coordinator.desiredRevision == 1);
        auto retry = kernel->reserveReplacement({old.id()});
        CHECK(retry.hasValue());
        retry.value().release().value();
        CHECK(waitFor(otherSession->close())->hasValue());
        CHECK(waitFor(kernel->close())->value().quiescent);
        return false;
    }
    auto started = ReplacementOperation::begin(std::move(request));
    CHECK(started.hasValue());
    auto operation = std::move(started).value();
    std::optional<Task<void>> closed;
    const auto close = [&] {
        closed = session->close();
        CHECK(!closed->isReady());
        CHECK(!session->preview(text("old")).hasValue());
    };
    if (prepared)
        close();
    else if (stopping)
        CHECK(scheduler->post({[&] {
                                   CHECK(operation.snapshot().value().phase == ReplacementPhase::Stopping);
                                   close();
                               },
                               {}}));
    else if (recoveryCommit)
        recoveryHook = close;
    else if (atCommit)
        validationHook = close;
    else {
        CHECK(waitFor(owner->entered.task())->hasValue());
        CHECK(!operation.settled().isReady());
        close();
    }
    auto result = waitFor(operation.settled());
    CHECK(result && result->hasValue());
    const auto& report = result->value();
    CHECK(closed && waitFor(*closed)->hasValue());
    CHECK(driver->snapshot().value().empty());
    CHECK(readConfigurationFile(file).value().text == originalFile);
    CHECK(readConfigurationFile(otherFile).value().text == originalFile);
    if (failedRecovery) {
        CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.recovered &&
              !report.committed);
        CHECK(report.instances.empty());
        CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                          [](const auto& error) { return error.code == ErrorCode::RecoveryFailed; }));
        CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
    } else {
        CHECK(report.outcome == ReplacementOutcome::Cancelled && report.recovered == !prepared && !report.committed &&
              !report.resourcesRetained && report.instances.size() == (prepared ? 0 : 3));
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        const auto restored = std::find_if(report.instances.begin(), report.instances.end(),
                                           [&](const auto& mapping) { return mapping.previous == manualId; });
        const auto manualCurrent =
            prepared ? manualId : (restored == report.instances.end() ? InstanceId{} : restored->current);
        CHECK(kernel->findInstance(manualCurrent).value().snapshot().value().state == InstanceState::Active);
        CHECK(otherDriver->snapshot().value().size() == 1 &&
              otherDriver->snapshot().value()[0].state == InstanceState::Active);
        CHECK(otherSession->save(1).hasValue());
    }
    if (owner->observedBuilder)
        CHECK(!owner->observedBuilder->stage(*owner->lastFactory).hasValue());
    owner->pending.reject(makeError(ErrorCode::ActivationFailed, "late candidate after session close"));
    CHECK(waitFor(operation.settled())->value().outcome == report.outcome);
    CHECK(waitFor(kernel->close())->value().quiescent);
    CHECK(waitFor(otherSession->close())->hasValue());
    return failedRecovery;
}
