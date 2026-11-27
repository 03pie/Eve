/** @file replacement_session_close.hpp
 * @brief 会话关闭必须先结束替换，再关闭自身驱动；手动实例仍按旧代恢复。
 */
#pragma once
#include "replacement_owner.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <algorithm>
#include <eve/loader/qt_file.hpp>
#include <exception>
#include <functional>
#include <optional>

template <class SceneType, class Wait>
bool operationSessionCloseCase(SceneType& scene, const eve::Runtime& old, const eve::Runtime& next,
                               const std::shared_ptr<eve::IScheduler>& scheduler,
                               const std::shared_ptr<eve::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace eve;
    using namespace eve::loader;
    const auto id = scene.qt ? "org.eve.qt.page" : "org.eve.plugin";
    const bool prepared = mode.endsWith("_prepared"), stopping = mode.endsWith("_stopping");
    const bool atCommit = mode.endsWith("_commit"), failedRecovery = mode.endsWith("_recovery_failure");
    const bool recoveryCommit = mode.endsWith("_recovery_commit");
    const bool stateCapture = mode.endsWith("_state_capture"), stateRestore = mode.endsWith("_state_restore");
    const bool stateCase = stateCapture || stateRestore;
    struct StateValue {
        ContextIdentity identity;
        std::string json, observed;
        unsigned restored = 0;
        bool cleaned = false;
    };
    // 只观察实际 Context 的值；两 Session 的显式子成员沿默认 Builder 重建，不扩大旧拥有者协议。
    std::vector<std::shared_ptr<StateValue>> stateValues;
    std::function<void(const std::shared_ptr<StateValue>&, bool)> stateHook;
    std::exception_ptr stateFailure;
    const auto invokeState = [&](const std::shared_ptr<StateValue>& value, bool restoring) {
        try {
            const auto hook = stateHook;
            if (hook)
                hook(value, restoring);
        } catch (...) {
            if (!stateFailure)
                stateFailure = std::current_exception();
        }
    };
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
    const auto instanceReferences = [&](GenerationId generation) {
        const auto resources = scene.state(generation).resources.records;
        return std::count_if(resources.begin(), resources.end(),
                             [](const auto& record) { return record.kind == module::ResourceKind::Instance; });
    };
    std::vector<ConfiguredPlugin> definitions{{{id, {}, schema, {"business", "old/module.json"}}, old, {}, available}};
    if (stateCase) {
        auto builtin = Runtime::stateful(
            [&](Context context) {
                auto value = std::make_shared<StateValue>();
                value->identity = context.identity().value();
                value->json = context.config();
                stateValues.push_back(value);
                context
                    .afterCleanup([value] {
                        value->cleaned = true;
                        return completedTask();
                    })
                    .value();
                auto imported = context.state({[&, value] {
                                                   invokeState(value, false);
                                                   return Result<std::string>::success(value->json);
                                               },
                                               [&, value](std::string_view json) {
                                                   value->json = json;
                                                   ++value->restored;
                                                   invokeState(value, true);
                                                   return Result<void>::success();
                                               }});
                if (!imported.hasValue())
                    return failedTask(imported.error());
                value->observed = value->json;
                return completedTask();
            },
            {{"session.counter", 1}, [](const StateSnapshot& value) {
                 CHECK(value.schema.id == "session.counter" && value.schema.version == 1);
                 return Result<std::string>::success(value.json);
             }});
        kernel->registerRuntime("session.state", builtin).value();
        definitions.push_back({{"session.state", {}, schema}, builtin});
    }
    auto before = ConfigurationDefinitions::create(std::move(definitions)).value();
    auto after = before->replacing({{{id, {}, nextSchema, {"business", "next/module.json"}}, next}}).value();
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, before);
    const auto file = scene.root + "/session-close.yaml";
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(file), readConfigurationFile(file).value());
    const auto text = [&](const char* folder) {
        return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + folder +
               "/module.json}}\nplugins: [{id: item, plugin: " + id +
               ", module: business, config: " + (scene.qt ? "normal" : "sync") +
               (stateCase ? ", children: [{id: state, plugin: session.state, config: 42}]" : "") + "}]\n";
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
    CHECK(!stateCase || stateValues.size() == 2);
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
    if (!stateCase) {
        request.owners.emplace(configured, owner);
        request.owners.emplace(otherConfigured, owner);
        request.owners.emplace(manualId, owner);
    }
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
    bool stateInterrupted = false;
    if (stateCase) {
        stateHook = [&](const std::shared_ptr<StateValue>& value, bool restoring) {
            if (restoring != stateRestore || stateInterrupted)
                return;
            stateInterrupted = true;
            CHECK(operation.snapshot().value().phase ==
                  (stateRestore ? ReplacementPhase::Building : ReplacementPhase::Capturing));
            CHECK(!value->cleaned && value->json == "42");
            const auto oldReferences = instanceReferences(scene.current);
            const auto nextReferences = instanceReferences(scene.candidate);
            const auto cleaned =
                std::count_if(stateValues.begin(), stateValues.end(), [](const auto& item) { return item->cleaned; });
            CHECK(stateRestore ? oldReferences == 0 && nextReferences > 0 && cleaned == 2
                               : oldReferences == 3 && nextReferences == 0 && cleaned == 0);
            CHECK(value->restored == (stateRestore ? 1U : 0U));
            close();
            // FakeScheduler 与 QtScheduler 均禁止重入业务推进；Qt 删除事件仍可到达实际资源屏障。
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            CHECK(!closed->isReady() && !value->cleaned && !operation.settled().isReady());
            CHECK(instanceReferences(scene.current) == oldReferences &&
                  instanceReferences(scene.candidate) == nextReferences);
            CHECK(std::count_if(stateValues.begin(), stateValues.end(),
                                [](const auto& item) { return item->cleaned; }) == cleaned);
        };
    } else if (prepared)
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
    stateHook = {};
    if (stateFailure)
        std::rethrow_exception(stateFailure);
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
        const bool untouched = prepared || stateCapture;
        CHECK(report.outcome == ReplacementOutcome::Cancelled && report.recovered == !untouched && !report.committed &&
              !report.resourcesRetained &&
              report.instances.size() == (untouched      ? 0U
                                          : stateRestore ? 5U
                                                         : 3U));
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        const auto restored = std::find_if(report.instances.begin(), report.instances.end(),
                                           [&](const auto& mapping) { return mapping.previous == manualId; });
        const auto manualCurrent =
            untouched ? manualId : (restored == report.instances.end() ? InstanceId{} : restored->current);
        CHECK(kernel->findInstance(manualCurrent).value().snapshot().value().state == InstanceState::Active);
        const auto otherRows = otherDriver->snapshot().value();
        CHECK(otherRows.size() == (stateCase ? 2U : 1U) &&
              std::all_of(otherRows.begin(), otherRows.end(),
                          [](const auto& row) { return row.state == InstanceState::Active; }));
        CHECK(otherSession->save(1).hasValue());
        if (stateCase) {
            CHECK(stateInterrupted && driver->configurationDefinitions() == before &&
                  otherDriver->configurationDefinitions() == before);
            for (const auto& active : {session, otherSession}) {
                const auto snapshot = active->snapshot().value();
                CHECK(snapshot.coordinator.desiredRevision == 1 && snapshot.store.persistedRevision == 1 &&
                      !snapshot.store.dirty && before->owns(*snapshot.coordinator.desired));
            }
            CHECK(scene.state(scene.current).role == module::GenerationRole::Current &&
                  scene.state(scene.candidate).role == module::GenerationRole::Candidate);
            CHECK(instanceReferences(scene.current) == 2 && instanceReferences(scene.candidate) == 0);
            CHECK(stateValues[0]->cleaned && stateValues[0]->json == "42");
            if (stateCapture) {
                CHECK(stateValues.size() == 2 && !stateValues[1]->cleaned && otherRows[0].instance == otherConfigured &&
                      manualCurrent == manualId);
                CHECK(stateValues[1]->json == "42" && stateValues[1]->observed == "42" &&
                      stateValues[1]->restored == 0);
            } else {
                CHECK(stateValues[1]->cleaned && stateValues.size() > 4);
                for (unsigned i = 0; i != 2; ++i) {
                    const auto mapping =
                        std::find_if(report.instances.begin(), report.instances.end(), [&](const auto& item) {
                            return item.previous == stateValues[i]->identity.instance;
                        });
                    CHECK(mapping != report.instances.end() && mapping->current != mapping->previous);
                    const auto value = std::find_if(stateValues.begin(), stateValues.end(), [&](const auto& item) {
                        return item->identity.instance == mapping->current;
                    });
                    CHECK(value != stateValues.end() && (*value)->json == "42" && (*value)->observed == "42" &&
                          (*value)->restored == 1 && (*value)->cleaned == (i == 0));
                }
                CHECK(otherRows[0].instance != otherConfigured && manualCurrent != manualId);
            }
            const auto otherChild = std::find_if(otherRows.begin(), otherRows.end(),
                                                 [](const auto& row) { return row.path == "item/state"; });
            CHECK(otherChild != otherRows.end());
            for (const auto& value : stateValues)
                CHECK(value->cleaned || value->identity.instance == otherChild->instance);
        }
    }
    if (owner->observedBuilder)
        CHECK(!owner->observedBuilder->stage(*owner->lastFactory).hasValue());
    if (!stateCase)
        owner->pending.reject(makeError(ErrorCode::ActivationFailed, "late candidate after session close"));
    CHECK(waitFor(operation.settled())->value().outcome == report.outcome);
    CHECK(waitFor(kernel->close())->value().quiescent);
    if (stateCase) {
        CHECK(std::all_of(stateValues.begin(), stateValues.end(), [](const auto& value) { return value->cleaned; }));
        CHECK(instanceReferences(scene.current) == 0 && instanceReferences(scene.candidate) == 0);
    }
    CHECK(waitFor(otherSession->close())->hasValue());
    return failedRecovery;
}
