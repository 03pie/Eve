/** @file module_state_operation_native.cpp
 * @brief 双版本真实 C DLL 的整体状态迁移、原值恢复及故障/关闭保留。
 */
#include "../../src/module/native_source_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <algorithm>
#include <array>
#include <cordis/module/native.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/replacement_operation.hpp>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
namespace native = cordis::module::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
using Hook = void(CORDIS_CALL*)(std::uint32_t);
std::array<std::function<void(std::uint32_t)>, 2> hooks;
std::exception_ptr hookFailure;
void invoke(unsigned image, std::uint32_t phase)
{
    try {
        const auto callback = hooks[image];
        if (callback)
            callback(phase);
    } catch (...) {
        if (!hookFailure)
            hookFailure = std::current_exception();
    }
}
void CORDIS_CALL oldHook(std::uint32_t phase)
{
    invoke(0, phase);
}
void CORDIS_CALL nextHook(std::uint32_t phase)
{
    invoke(1, phase);
}
void events()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        CHECK(timer.elapsed() < 5000);
        events();
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        QThread::msleep(1);
    }
    if (hookFailure)
        std::rethrow_exception(hookFailure);
    return task.tryResult();
}
struct Probe {
    native::NativeSource source;
    HMODULE image;
    Probe(GenerationId id, const QString& path)
        : source(native::acquireNativeSource(id).value()),
          image(GetModuleHandleW(QDir::toNativeSeparators(path).toStdWString().c_str()))
    {
        CHECK(image);
    }
    template <class F, class... Args> auto call(const char* name, Args... args) const
    {
        auto lease = source.ledger.acquire(ResourceKind::Call, "test.native.state.operation").value();
        const auto address = GetProcAddress(image, name);
        CHECK(address);
        F function;
        static_assert(sizeof(function) == sizeof(address), "Windows function pointer");
        std::memcpy(&function, &address, sizeof(function));
        return function(args...);
    }
    void mode(std::uint32_t value) const { call<void(CORDIS_CALL*)(std::uint32_t)>("cordis_state_set_mode", value); }
    std::uint32_t count(std::uint32_t index) const
    {
        return call<std::uint32_t(CORDIS_CALL*)(std::uint32_t)>("cordis_state_count", index);
    }
    unsigned creates() const { return call<unsigned(CORDIS_CALL*)()>("cordis_business_fixture_creates"); }
    unsigned live() const { return call<std::uint32_t(CORDIS_CALL*)()>("cordis_state_live"); }
    void observe(unsigned which, std::function<void(std::uint32_t)> action) const
    {
        hooks[which] = std::move(action);
        call<void(CORDIS_CALL*)(Hook)>("cordis_state_set_hook",
                                       hooks[which] ? (which ? &nextHook : &oldHook) : nullptr);
    }
    void set(unsigned slot, const std::string& text) const
    {
        const CordisBytesV1 input{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
        CHECK(call<CordisStatus(CORDIS_CALL*)(std::uint32_t, CordisBytesV1)>("cordis_state_set_value", slot, input) ==
              CORDIS_OK);
    }
    std::string value(unsigned slot) const
    {
        std::array<std::uint8_t, 128> storage{};
        CordisStateBufferV1 buffer{sizeof(CordisStateBufferV1), 0, storage.data(), storage.size(), 0};
        CHECK(call<CordisStatus(CORDIS_CALL*)(std::uint32_t, CordisStateBufferV1*)>("cordis_state_value", slot,
                                                                                    &buffer) == CORDIS_OK);
        CHECK(buffer.size <= storage.size());
        return {reinterpret_cast<const char*>(storage.data()), static_cast<std::size_t>(buffer.size)};
    }
};
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<ModuleManager> manager = std::make_shared<ModuleManager>();
    std::array<GenerationId, 2> generations;
    std::array<std::optional<Probe>, 2> probes;
    std::array<std::optional<NativeModule>, 2> modules;
    std::optional<Runtime> old, next;
    std::vector<Instance> originals;
    std::vector<InstanceId> identities;
    const std::array<std::string, 2> values{{"11", "9007199254740993"}};
    explicit Scene(const QString& root, const QStringList& dlls)
    {
        CHECK(dlls.size() == 2);
        const auto first = files::install(dlls[0], root + "/old", "org.cordis.state", QJsonArray{"org.cordis.plugin"});
        const auto second = files::install(dlls[1], root + "/next", "org.cordis.state", QJsonArray{"org.cordis.plugin"},
                                           "c-abi", "2.0.0");
        generations[0] = manager->load(root.toStdString(), first.toStdString()).value().generation;
        generations[1] = manager->stage(root.toStdString(), second.toStdString(), generations[0]).value().generation;
        for (unsigned i = 0; i != 2; ++i) {
            probes[i].emplace(generations[i], root + (i ? "/next/module.dll" : "/old/module.dll"));
            modules[i].emplace(NativeModule::open(*manager, generations[i], scheduler, kernel->services()).value());
        }
        old.emplace(
            kernel->registerRuntime("counter", modules[0]->runtime({"org.cordis.plugin", {}, {}}).value()).value());
        next.emplace(modules[1]->runtime({"org.cordis.plugin", {}, {}}).value());
        CHECK(old->stateSchema()->version == 2 && next->stateSchema()->version == 3);
        for (unsigned i = 0; i != 2; ++i) {
            originals.push_back(kernel->mount(*old, "\"sync\"").value());
            CHECK(wait(originals.back().ready())->hasValue());
            identities.push_back(originals.back().snapshot().value().instance);
            probes[0]->set(i, values[i]);
        }
        CHECK(probes[0]->live() == 2 && probes[1]->live() == 0);
    }
    Probe& before() { return *probes[0]; }
    Probe& after() { return *probes[1]; }
    ReplacementRequest request() const
    {
        ReplacementRequest result;
        result.scheduler = scheduler;
        result.kernel = kernel;
        result.modules = manager;
        result.previous = generations[0];
        result.candidate = generations[1];
        result.definitions.push_back({old->id(), *next});
        return result;
    }
    ModuleGenerationSnapshot state(unsigned index) const
    {
        const auto all = manager->snapshot().value();
        const auto found = std::find_if(all.begin(), all.end(),
                                        [&](const auto& value) { return value.generation == generations[index]; });
        CHECK(found != all.end());
        return *found;
    }
    void roles(bool applied, bool retained) const
    {
        CHECK(state(0).role == (applied ? GenerationRole::Retired : GenerationRole::Current));
        CHECK(state(1).role == (applied ? GenerationRole::Current : GenerationRole::Candidate));
        CHECK(state(0).replacement.has_value() == retained && state(1).replacement.has_value() == retained);
        CHECK(kernel->replacements().value().empty() == !retained);
        for (unsigned i = 0; i != 2; ++i) {
            const auto generation = state(i);
            for (const auto& record : generation.resources.records)
                CHECK(record.kind != ResourceKind::Call);
        }
    }
    void untouched() const
    {
        for (unsigned i = 0; i != 2; ++i) {
            const auto current = originals[i].snapshot().value();
            CHECK(current.instance == identities[i] && current.epoch == 1 && current.state == InstanceState::Active);
            CHECK(wait(originals[i].whenActiveAfter())->value().identity().hasValue());
            CHECK(probes[0]->value(i) == values[i]);
        }
        CHECK(probes[0]->count(5) == 0 && probes[0]->count(6) == 0 && probes[0]->live() == 2);
        CHECK(probes[1]->creates() == 0 && probes[1]->live() == 0);
    }
    void mapped(const ReplacementReport& report, bool candidate)
    {
        CHECK(report.instances.size() == 2);
        const auto actual = modules[candidate ? 1 : 0]->snapshot().value().instances;
        CHECK(actual.size() == 2);
        for (unsigned i = 0; i != 2; ++i) {
            const auto mapping = std::find_if(report.instances.begin(), report.instances.end(),
                                              [&](const auto& row) { return row.previous == identities[i]; });
            CHECK(mapping != report.instances.end() && mapping->current != identities[i]);
            const auto instance = std::find_if(actual.begin(), actual.end(),
                                               [&](const auto& row) { return row.instance == mapping->current; });
            CHECK(instance != actual.end() && instance->state == NativeInstanceState::Started &&
                  !instance->logs.empty());
            CHECK(instance->logs.back().text == values[i] + (candidate ? "0" : ""));
            CHECK(kernel->findInstance(mapping->current).value().snapshot().value().state == InstanceState::Active);
        }
    }
    void fault(unsigned index, const char* reason, ErrorCode code = ErrorCode::AbiProtocol) const
    {
        const auto current = state(index);
        CHECK(current.nativeMapped && current.state == ModuleState::Failed && current.businessFailure &&
              current.businessFailure->code == code);
        CHECK(std::find(current.resources.pinReasons.begin(), current.resources.pinReasons.end(), reason) !=
              current.resources.pinReasons.end());
    }
    void retainedInstance(unsigned index, ErrorCode code = ErrorCode::AbiProtocol) const
    {
        fault(index, "nativeInstanceUncertain", code);
        const auto generation = state(index);
        CHECK(std::any_of(generation.resources.records.begin(), generation.resources.records.end(),
                          [](const auto& record) { return record.kind == ResourceKind::Instance; }));
        const auto actual = modules[index]->snapshot().value().instances;
        CHECK(std::any_of(actual.begin(), actual.end(), [&](const auto& instance) {
            return instance.nativeCreated && instance.uncertain && instance.state == NativeInstanceState::Failed &&
                   instance.failure && instance.failure->code == code;
        }));
        CHECK(probes[index]->live() > 0);
    }
    void close(bool candidateFault = false)
    {
        before().observe(0, {});
        after().observe(1, {});
        CHECK(wait(kernel->close())->value().quiescent);
        for (unsigned i = 0; i != 2; ++i) {
            const auto result = wait(modules[i]->close());
            CHECK(result->hasValue() && result->value().quiescent == !(i == 1 && candidateFault));
            CHECK(probes[i]->live() == 0 && modules[i]->snapshot().value().instances.empty());
            const auto generation = state(i);
            for (const auto& record : generation.resources.records)
                CHECK(record.kind != ResourceKind::Instance && record.kind != ResourceKind::Call);
        }
        CHECK(scheduler->close().hasValue());
    }
    ~Scene() { hooks = {}; }
};
bool hasError(const ReplacementReport& report, ErrorCode code)
{
    return std::any_of(report.errors.begin(), report.errors.end(), [&](const auto& item) { return item.code == code; });
}
void rejected(Scene& scene, const std::string& name)
{
    if (name == "busy")
        scene.before().mode(7);
    if (name == "validate_reject")
        scene.after().mode(4);
    if (name == "old_validate_reject")
        scene.before().mode(4);
    if (name == "validate_pending")
        scene.after().mode(5);
    auto operation = ReplacementOperation::begin(scene.request()).value();
    const auto report = wait(operation.settled())->value();
    CHECK(report.outcome == ReplacementOutcome::Failed && !report.committed && !report.recovered &&
          !report.resourcesRetained && report.instances.empty());
    CHECK(hasError(report, name == "busy"               ? ErrorCode::ConfigConflict
                           : name == "validate_pending" ? ErrorCode::AbiProtocol
                                                        : ErrorCode::ConfigInvalid));
    scene.roles(false, false);
    scene.untouched();
    if (name == "validate_pending")
        scene.fault(1, "nativeStateUncertain");
    else
        CHECK(!scene.modules[0]->snapshot().value().faulted && !scene.modules[1]->snapshot().value().faulted);
    scene.before().mode(0);
    scene.after().mode(0);
    scene.before().set(0, "23");
    CHECK(scene.before().value(0) == "23"); // 解除冻结后实际旧对象继续可用。
    scene.close(name == "validate_pending");
}
void rollback(Scene& scene, const std::string& name)
{
    if (name == "restore_reject")
        scene.after().mode(11);
    else
        scene.after().mode(name == "partial_start" ? 19 : 18);
    if (name == "late_validate_reject") {
        scene.after().mode(0);
        scene.after().observe(1, [&](std::uint32_t phase) {
            if (phase == 1 && scene.after().count(1) == 3)
                scene.after().mode(4);
        });
    }
    if (name == "recovery_restore")
        scene.before().mode(11);
    if (name == "recovery_start")
        scene.before().mode(18);
    if (name == "recovery_validate")
        scene.after().observe(1, [&](std::uint32_t phase) {
            if (phase == 4)
                scene.before().mode(4);
        });
    auto operation = ReplacementOperation::begin(scene.request()).value();
    const auto report = wait(operation.settled())->value();
    scene.before().observe(0, {});
    scene.after().observe(1, {});
    const bool failedRecovery = name.compare(0, 9, "recovery_") == 0;
    CHECK(!report.committed && report.recovered == !failedRecovery && report.resourcesRetained == failedRecovery);
    CHECK(report.outcome == (failedRecovery ? ReplacementOutcome::Failed : ReplacementOutcome::Restored));
    CHECK(hasError(report, failedRecovery                                       ? ErrorCode::RecoveryFailed
                           : name == "start_failure" || name == "partial_start" ? ErrorCode::ActivationFailed
                                                                                : ErrorCode::ConfigInvalid));
    scene.roles(false, failedRecovery);
    CHECK(scene.before().count(5) >= 2 && scene.before().count(6) >= 2);
    CHECK(scene.after().live() == 0 && scene.after().count(6) == scene.after().creates());
    CHECK(!scene.modules[0]->snapshot().value().faulted && !scene.modules[1]->snapshot().value().faulted);
    if (failedRecovery) {
        CHECK(report.instances.empty() && scene.before().live() == 0);
        for (unsigned i = 0; i != 2; ++i) {
            CHECK(scene.modules[i]->snapshot().value().instances.empty());
            const auto generation = scene.state(i);
            for (const auto& record : generation.resources.records)
                CHECK(record.kind != ResourceKind::Instance);
        }
        CHECK(wait(scene.kernel->close())->value().quiescent);
        return;
    }
    scene.mapped(report, false);
    CHECK(scene.before().count(2) == 2 && scene.before().count(4) == 4);
    if (name == "restore_reject" || name == "late_validate_reject")
        CHECK(scene.after().count(4) == 0);
    if (name == "partial_start")
        CHECK(scene.after().count(4) == 2 && scene.after().creates() == 2);
    scene.close();
}
void interrupted(Scene& scene, const std::string& name)
{
    std::optional<ReplacementOperation> operation;
    std::optional<Task<CleanupReport>> closing;
    const bool kernel = name == "kernel_capture", scheduler = name == "scheduler_capture";
    const bool oldCallback = name == "cancel_capture" || kernel || scheduler;
    const std::uint32_t wanted = oldCallback ? 3U : name == "cancel_validate" ? 1U : name == "cancel_restore" ? 2U : 4U;
    bool invoked = false;
    auto action = [&](std::uint32_t phase) {
        if (phase != wanted || invoked)
            return;
        invoked = true;
        CHECK(operation);
        CHECK(operation->snapshot().value().phase ==
              (oldCallback || name == "cancel_validate" ? ReplacementPhase::Capturing : ReplacementPhase::Building));
        if (kernel)
            closing = scene.kernel->close();
        else if (scheduler)
            CHECK(scene.scheduler->close().hasValue());
        else
            CHECK(operation->cancel().hasValue());
        events();
        CHECK((oldCallback ? scene.before() : scene.after()).count(6) == 0);
        if (kernel)
            CHECK(closing && !closing->isReady());
    };
    (oldCallback ? scene.before() : scene.after()).observe(oldCallback ? 0U : 1U, action);
    operation.emplace(ReplacementOperation::begin(scene.request()).value());
    const auto result = wait(operation->settled());
    scene.before().observe(0, {});
    scene.after().observe(1, {});
    CHECK(invoked);
    if (scheduler) {
        CHECK(!result->hasValue() && result->error().code == ErrorCode::SchedulerClosed);
        CHECK(operation->snapshot().value().deliveryBlocked && operation->snapshot().value().report.resourcesRetained);
        scene.roles(false, true);
        scene.retainedInstance(0, ErrorCode::SchedulerClosed);
        return;
    }
    CHECK(result->hasValue());
    const auto& report = result->value();
    const bool restored = !oldCallback && name != "cancel_validate";
    CHECK(report.outcome == ReplacementOutcome::Cancelled && report.recovered == restored &&
          !report.resourcesRetained && !report.committed);
    scene.roles(false, false);
    if (kernel) {
        CHECK(closing && wait(*closing)->value().quiescent && scene.before().live() == 0 &&
              scene.after().creates() == 0);
    } else if (restored)
        scene.mapped(report, false);
    else
        scene.untouched();
    scene.close();
}
void protocol(Scene& scene, const std::string& name)
{
    const bool old = name == "capture_pending";
    (old ? scene.before() : scene.after()).mode(old ? 10U : 12U);
    auto operation = ReplacementOperation::begin(scene.request()).value();
    const auto report = wait(operation.settled())->value();
    CHECK(report.outcome == ReplacementOutcome::Failed && !report.recovered && !report.committed &&
          report.resourcesRetained);
    CHECK(report.instances.empty() && hasError(report, ErrorCode::AbiProtocol));
    scene.roles(false, true);
    scene.retainedInstance(old ? 0U : 1U);
    if (!old)
        CHECK(scene.before().live() == 0);
    CHECK(!wait(scene.kernel->close())->value().quiescent);
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto name = args[1].toStdString();
        Scene scene(args[2], args[3].split(';'));
        if (name == "roundtrip") {
            auto operation = ReplacementOperation::begin(scene.request()).value();
            const auto report = wait(operation.settled())->value();
            CHECK(report.outcome == ReplacementOutcome::Applied && report.committed && !report.recovered &&
                  !report.resourcesRetained);
            scene.roles(true, false);
            scene.mapped(report, true);
            CHECK(scene.before().count(1) == 2 && scene.after().count(1) == 4);
            CHECK(scene.before().live() == 0 && scene.before().count(6) == 2 && scene.after().count(2) == 2);
            scene.close();
        } else if (name == "busy" || name == "validate_reject" || name == "old_validate_reject" ||
                   name == "validate_pending")
            rejected(scene, name);
        else if (name == "capture_pending" || name == "restore_pending")
            protocol(scene, name);
        else if (name == "cancel_capture" || name == "cancel_validate" || name == "cancel_restore" ||
                 name == "cancel_start" || name == "kernel_capture" || name == "scheduler_capture")
            interrupted(scene, name);
        else if (name == "late_validate_reject" || name == "restore_reject" || name == "start_failure" ||
                 name == "partial_start" || name == "recovery_restore" || name == "recovery_start" ||
                 name == "recovery_validate")
            rollback(scene, name);
        else
            CHECK(false);
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
