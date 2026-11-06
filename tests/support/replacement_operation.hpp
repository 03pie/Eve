/** @file replacement_operation.hpp
 * @brief 真实两后端的整体操作，不手动推进 Core 或模块提交凭据。
 */
#pragma once
#include "replacement_owner.hpp"
#include <cordis/replacement_operation.hpp>
template <class SceneType, class Wait>
bool operationCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                   const std::shared_ptr<cordis::IScheduler>& scheduler, const std::shared_ptr<cordis::Kernel>& kernel,
                   Wait waitFor, const QString& mode)
{
    using namespace cordis;
    using namespace cordis::loader;
    auto take = [](auto result, const char* phase) {
        if (!result.hasValue())
            throw std::runtime_error(std::string(phase) + ": " + result.error().message + " [" + result.error().phase +
                                     "]");
        return std::move(result).value();
    };
    bool failChild = false;
    auto builtin =
        kernel
            ->registerPlugin("builtin",
                             [&](Context) {
                                 if (failChild) {
                                     failChild = false;
                                     return failedTask(makeError(ErrorCode::ActivationFailed, "child failed once"));
                                 }
                                 return completedTask();
                             })
            .value();
    const auto id = scene.qt ? "org.cordis.qt.page" : "org.cordis.plugin";
    auto schema = [](const std::string&) { return Result<void>::success(); };
    auto first = take(ConfigurationDefinitions::create({{{id, {}, schema, {"business", "old/module.json"}}, old},
                                                        {{"builtin", {}, schema}, builtin}}),
                      "old definitions");
    std::function<void()> validationHook;
    auto nextSchema = [&](const std::string&) {
        if (validationHook) {
            auto hook = std::move(validationHook);
            validationHook = {};
            hook();
        }
        return Result<void>::success();
    };
    auto following =
        take(first->replacing({{{id, {}, nextSchema, {"business", "next/module.json"}}, next}}), "next definitions");
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, first);
    const auto file = scene.root + "/operation.yaml";
    auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(file), readConfigurationFile(file).value());
    auto text = [&](const char* folder, const std::string& config) {
        return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + folder +
               "/module.json}}\nplugins: [{id: item, plugin: " + id + ", module: business, config: " + config +
               ", children: [{id: kid, plugin: builtin}]}, {id: stay, plugin: builtin, config: keep}]\n";
    };
    const auto normal = scene.qt ? "normal" : "sync";
    auto initial = session->apply(session->preview(text("old", normal)).value()).value();
    CHECK(waitFor(initial.settled)->value().outcome == PlanOutcome::Applied);
    CHECK(waitFor(session->save(1).value())->hasValue());
    const auto originalFile = readConfigurationFile(file).value().text;
    const bool prepareFailure = mode == "operation_sessions_prepare_failure";
    const bool prepareCancel = mode == "operation_sessions_prepare_cancel";
    const bool multiple =
        mode == "operation_sessions" || mode == "operation_sessions_recovery" || prepareFailure || prepareCancel;
    std::shared_ptr<KernelReconcileDriver> otherDriver;
    std::shared_ptr<ConfigurationSession> otherSession;
    if (multiple) {
        otherDriver = KernelReconcileDriver::configured(scheduler, kernel, first);
        const auto otherFile = scene.root + "/other-operation.yaml";
        otherSession = std::make_shared<ConfigurationSession>(scheduler, otherDriver,
                                                              std::make_shared<QtConfigurationWriter>(otherFile),
                                                              readConfigurationFile(otherFile).value());
        auto accepted = otherSession->apply(otherSession->preview(text("old", normal)).value()).value();
        CHECK(waitFor(accepted.settled)->value().outcome == PlanOutcome::Applied);
        CHECK(waitFor(otherSession->save(1).value())->hasValue());
    }
    const auto before = driver->snapshot().value();
    const auto oldRoot =
        std::find_if(before.begin(), before.end(), [](const auto& item) { return item.path == "item"; })->instance;
    auto manual = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
    CHECK(waitFor(manual.ready())->hasValue());
    const auto config = mode == "operation_recovery" || mode == "operation_sessions_recovery"
                            ? (scene.qt ? "start_failure" : "start-fail")
                            : normal;
    auto target = std::make_shared<const ValidatedCandidate>(
        following->validate(parseConfiguration(text("next", config)).value()).value());
    auto request = [&] {
        ReplacementRequest value;
        value.scheduler = scheduler;
        value.kernel = kernel;
        value.modules = scene.managerOwner;
        value.previous = scene.current;
        value.candidate = scene.candidate;
        value.pluginRoot = scene.root.toStdString();
        value.definitions.push_back({old.id(), next});
        value.configurations.push_back({session, driver, following, target, 1});
        if (otherSession)
            value.configurations.push_back({otherSession, otherDriver, following, target, 1});
        return value;
    };
    if (mode == "operation_guards") {
        auto invalid = request();
        invalid.definitions[0].candidate = Runtime([](Context) { return completedTask(); });
        CHECK(!ReplacementOperation::begin(std::move(invalid)).hasValue());
        CHECK(driver->snapshot().value()[0].state == InstanceState::Active);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    } else {
        failChild = mode == "operation_child_failure";
        auto operation = take(ReplacementOperation::begin(request()), "operation begin");
        bool cancelAccepted = false;
        bool sawPrepared = false;
        if (prepareFailure || prepareCancel)
            validationHook = [&] {
                validationHook = [&] {
                    const auto sealed = session->snapshot();
                    CHECK(!sealed.hasValue() && sealed.error().code == ErrorCode::ConfigConflict);
                    CHECK(otherSession->snapshot().value().coordinator.desiredRevision == 1);
                    CHECK(driver->configurationDefinitions() == first &&
                          otherDriver->configurationDefinitions() == first);
                    CHECK(scene.state(scene.current).role == module::GenerationRole::Current &&
                          scene.state(scene.candidate).role == module::GenerationRole::Candidate);
                    CHECK(readConfigurationFile(file).value().text == originalFile &&
                          readConfigurationFile(scene.root + "/other-operation.yaml").value().text == originalFile);
                    sawPrepared = true;
                    if (prepareCancel)
                        cancelAccepted = operation.cancel().hasValue();
                    else
                        throw std::runtime_error("second participant schema refused");
                };
            };
        if (mode == "operation_cancel_commit")
            validationHook = [&] { cancelAccepted = operation.cancel().hasValue(); };
        if (mode == "operation_recovery_mapping")
            validationHook = [&] {
                failChild = true;
                cancelAccepted = operation.cancel().hasValue();
            };
        if (mode == "operation_cancel")
            operation.cancel().value();
        if (mode == "operation_merge") {
            auto source = setEntryConfiguration(session->snapshot().value().coordinator.desired->source(), "stay",
                                                "9007199254740993")
                              .value();
            auto edit = session->apply(session->preview(source.normalizedYaml()).value()).value();
            CHECK(edit.revision == 2);
        }
        auto completed = waitFor(operation.settled());
        CHECK(completed && completed->hasValue());
        const auto report = completed->value();
        if (mode == "operation_recovery_mapping") {
            CHECK(cancelAccepted);
            CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained);
            CHECK(!report.committed && !report.recovered);
            CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                              [](const auto& error) { return error.code == ErrorCode::RecoveryFailed; }));
            CHECK(report.instances.empty());
            CHECK(waitFor(kernel->close())->value().quiescent);
            return true;
        }
        const bool restored = mode == "operation_recovery" || mode == "operation_child_failure" ||
                              mode == "operation_cancel_commit" || mode == "operation_sessions_recovery" ||
                              prepareFailure || prepareCancel;
        const bool cancelled = mode == "operation_cancel" || mode == "operation_cancel_commit" || prepareCancel;
        const bool untouched = mode == "operation_cancel";
        const auto expectedOutcome = cancelled  ? ReplacementOutcome::Cancelled
                                     : restored ? ReplacementOutcome::Restored
                                                : ReplacementOutcome::Applied;
        if (report.outcome != expectedOutcome) {
            for (const auto& error : report.errors)
                std::cerr << error.message << " [" << error.phase << "]\n";
        }
        CHECK(report.outcome == expectedOutcome);
        if (prepareFailure || prepareCancel) {
            CHECK(sawPrepared && cancelAccepted == prepareCancel);
            CHECK(std::any_of(report.errors.begin(), report.errors.end(), [prepareFailure](const auto& error) {
                return prepareFailure ? error.code == ErrorCode::ConfigInvalid && error.phase == "loader.schema" &&
                                            error.entryPath == "item" &&
                                            error.message.find("second participant schema refused") != std::string::npos
                                      : error.code == ErrorCode::OperationCancelled;
            }));
            CHECK(readConfigurationFile(file).value().text == originalFile &&
                  readConfigurationFile(scene.root + "/other-operation.yaml").value().text == originalFile);
        }
        if (mode == "operation_cancel_commit")
            CHECK(cancelAccepted);
        CHECK(!report.resourcesRetained && report.committed == (!restored && !cancelled));
        CHECK(report.recovered == restored);
        const auto state = session->snapshot().value();
        CHECK(state.coordinator.desiredRevision == (restored || cancelled ? 1U : mode == "operation_merge" ? 3U : 2U));
        CHECK(driver->configurationDefinitions()->owns(*state.coordinator.desired));
        const auto rows = driver->snapshot().value();
        const auto root = std::find_if(rows.begin(), rows.end(), [](const auto& item) { return item.path == "item"; });
        CHECK(root != rows.end() && root->state == InstanceState::Active &&
              (untouched ? root->instance == oldRoot : root->instance != oldRoot));
        CHECK(report.instances.size() == (untouched ? 0U : multiple ? 5U : 3U));
        if (otherSession) {
            const auto other = otherSession->snapshot().value();
            CHECK(other.coordinator.desiredRevision == state.coordinator.desiredRevision);
            CHECK(otherDriver->configurationDefinitions()->owns(*other.coordinator.desired));
            CHECK(other.store.persistedRevision == 1U && other.store.dirty == !restored);
            CHECK(state.store.persistedRevision == 1U && state.store.dirty == !restored);
            const auto otherRows = otherDriver->snapshot().value();
            CHECK(std::all_of(otherRows.begin(), otherRows.end(),
                              [](const auto& row) { return row.state == InstanceState::Active; }));
            CHECK(waitFor(otherSession->save(other.coordinator.desiredRevision).value())->hasValue());
            CHECK(readConfigurationFile(scene.root + "/other-operation.yaml").value().text ==
                  other.coordinator.desired->source().normalizedYaml());
        }
        CHECK(scene.state(scene.current).role ==
              (restored || cancelled ? module::GenerationRole::Current : module::GenerationRole::Retired));
        CHECK(scene.state(scene.candidate).role ==
              (restored || cancelled ? module::GenerationRole::Candidate : module::GenerationRole::Current));
        if (mode == "operation_merge")
            CHECK(state.coordinator.desired->source().structure().entries().at("stay").config == "9007199254740993");
        if (!restored && !cancelled) {
            CHECK(waitFor(session->save(state.coordinator.desiredRevision).value())->hasValue());
            CHECK(readConfigurationFile(file).value().text == state.coordinator.desired->source().normalizedYaml());
        }
        if (prepareFailure || prepareCancel) {
            CHECK(waitFor(session->save(1).value())->hasValue());
            CHECK(readConfigurationFile(file).value().text == originalFile);
        }
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    }
    if (otherSession)
        CHECK(waitFor(otherSession->close())->hasValue());
    CHECK(waitFor(session->close())->hasValue());
    CHECK(waitFor(kernel->close())->value().quiescent);
    return false;
}
