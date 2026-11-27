/** @file module_state_operation_qt.cpp
 * @brief 真实双代 Qt 状态 DLL 的整体迁移、原值恢复和完整对象退役。
 */
#include "../../src/module/catalog_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <array>
#include <eve/module/qt_module.hpp>
#include <eve/qt/scheduler.hpp>
#include <eve/replacement_operation.hpp>
#include <eve_abi/state.h>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
using namespace eve;
using namespace eve::module;
namespace files = eve::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
std::array<std::function<void(int)>, 2> hooks;
std::exception_ptr hookFailure;
void invoke(std::size_t image, int phase)
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
void EVE_CALL oldHook(int phase)
{
    invoke(0, phase);
}
void EVE_CALL nextHook(int phase)
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
        CHECK(timer.elapsed() < 8000);
        events();
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        QThread::msleep(1);
    }
    if (hookFailure)
        std::rethrow_exception(hookFailure);
    return task.tryResult();
}
struct Observation {
    QByteArray value, activated;
    bool restored;
};
struct Probe {
    GenerationId generation;
    HMODULE image = nullptr;
    ModuleLease call() const
    {
        const auto actual = eve::module::detail::findGeneration(generation);
        CHECK(actual);
        return actual->ledger.acquire(ResourceKind::Call, "test.qt.state.operation").value();
    }
    template <class F> F entry(const char* name) const
    {
        const auto address = GetProcAddress(image, name);
        CHECK(address);
        F result;
        static_assert(sizeof(result) == sizeof(address), "Windows entry");
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
    int countHeld(int index) const { return entry<int(EVE_CALL*)(int)>("eve_qt_state_count")(index); }
    QObject* objectHeld(int index) const { return entry<QObject*(EVE_CALL*)(int)>("eve_qt_state_object")(index); }
    int count(int index) const
    {
        const auto held = call();
        return countHeld(index);
    }
    template <class F> auto object(int index, F action) const
    {
        const auto held = call();
        auto actual = objectHeld(index);
        CHECK(actual);
        return action(*actual);
    }
    void mode(std::string_view value) const
    {
        const auto held = call();
        entry<void(EVE_CALL*)(EveBytesV1)>("eve_qt_state_mode")(
            {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
    }
    void observe(std::size_t index, std::function<void(int)> callback) const
    {
        const auto held = call();
        hooks[index] = std::move(callback);
        entry<void(EVE_CALL*)(void(EVE_CALL*)(int))>("eve_qt_state_hook")(
            hooks[index] ? (index == 0 ? &oldHook : &nextHook) : nullptr);
    }
    std::map<InstanceId, Observation> values() const
    {
        const auto held = call();
        std::map<InstanceId, Observation> result;
        for (int index = 0; index != countHeld(7); ++index) {
            auto actual = objectHeld(index);
            if (!actual)
                continue;
            const auto identity = actual->property("coreInstance").toULongLong();
            if (!identity)
                continue; // restore 中失败的对象尚未 activate，不能冒充已激活成员。
            CHECK(result
                      .emplace(InstanceId(identity), Observation{actual->property("state").toByteArray(),
                                                                 actual->property("activatedState").toByteArray(),
                                                                 actual->property("activatedRestored").toBool()})
                      .second);
        }
        return result;
    }
    int alive() const
    {
        const auto held = call();
        int result = 0;
        for (int index = 0; index != countHeld(7); ++index)
            if (objectHeld(index))
                ++result;
        return result;
    }
    QPointer<QObject> active(int phase) const
    {
        const auto held = call();
        for (int index = 0; index != countHeld(7); ++index) {
            auto actual = objectHeld(index);
            if (actual && actual->property("stateCall").toInt() == phase)
                return actual;
        }
        return {};
    }
};
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<ModuleManager> manager = std::make_shared<ModuleManager>();
    Probe old, next;
    std::optional<Runtime> original, replacement;
    std::vector<Instance> instances;
    std::map<InstanceId, QByteArray> checkpoints;
    std::vector<QPointer<QObject>> retired;
    std::optional<ReplacementOperation> operation;
    std::optional<Task<CleanupReport>> closed;
    int destroyedSignals = 0;
    Scene(const QString& root, const QStringList& fixtures)
    {
        CHECK(fixtures.size() == 2);
        const auto first = files::install(fixtures[0], root + "/old", "org.eve.qt.example",
                                          QJsonArray{"org.eve.qt.page", "org.eve.qt.settings"}, "qt-ui");
        const auto second =
            files::install(fixtures[1], root + "/next", "org.eve.qt.example",
                           QJsonArray{"org.eve.qt.page", "org.eve.qt.settings"}, "qt-ui", "2.0.0");
        old.generation = loadQtModule(root.toStdString(), first.toStdString()).value().generation;
        next.generation = stageQtModule(root.toStdString(), second.toStdString(), old.generation).value().generation;
        old.image = GetModuleHandleW(QDir::toNativeSeparators(root + "/old/module.dll").toStdWString().c_str());
        next.image = GetModuleHandleW(QDir::toNativeSeparators(root + "/next/module.dll").toStdWString().c_str());
        CHECK(old.image && next.image && old.image != next.image);
        old.mode("");
        next.mode("");
        original.emplace(
            kernel
                ->registerRuntime("state", qtRuntime(old.generation, {"org.eve.qt.page", {}, {}}, scheduler).value())
                .value());
        replacement.emplace(qtRuntime(next.generation, {"org.eve.qt.page", {}, {}}, scheduler).value());
        CHECK(original->stateSchema()->version == 2 && replacement->stateSchema()->version == 3);
        for (const auto value : {QByteArray("11"), QByteArray("9007199254740993")}) {
            auto instance = kernel->mount(*original).value();
            CHECK(wait(instance.ready())->hasValue());
            const auto index = static_cast<int>(instances.size());
            const auto id = instance.snapshot().value().instance;
            old.object(index, [&](QObject& object) {
                CHECK(object.property("coreInstance").toULongLong() == id.value());
                object.setProperty("state", value);
                retired.push_back(&object);
                QObject::connect(
                    &object, &QObject::destroyed, QCoreApplication::instance(),
                    [this] {
                        try {
                            CHECK(records(old.generation, ResourceKind::Instance) > 0 &&
                                  records(old.generation, ResourceKind::CleanupTask) > 0);
                            CHECK(old.count(6) + 1 == old.count(5)); // destroyed 早于 Child，租约须仍在。
                            ++destroyedSignals;
                        } catch (...) {
                            if (!hookFailure)
                                hookFailure = std::current_exception();
                        }
                    },
                    Qt::DirectConnection);
            });
            checkpoints.emplace(id, value);
            instances.push_back(std::move(instance));
        }
        CHECK(old.count(7) == 2 && old.count(4) == 2 && next.count(7) == 0);
    }
    std::size_t records(GenerationId generation, ResourceKind kind) const
    {
        const auto data = eve::module::detail::findGeneration(generation)->ledger.snapshot();
        return static_cast<std::size_t>(
            std::count_if(data.records.begin(), data.records.end(),
                          [&](const ResourceRecord& record) { return record.kind == kind; }));
    }
    ModuleGenerationSnapshot state(GenerationId generation) const
    {
        const auto snapshot = manager->snapshot().value();
        const auto found = std::find_if(snapshot.begin(), snapshot.end(),
                                        [&](const auto& item) { return item.generation == generation; });
        CHECK(found != snapshot.end());
        return *found;
    }
    void begin()
    {
        ReplacementRequest request;
        request.scheduler = scheduler;
        request.kernel = kernel;
        request.modules = manager;
        request.previous = old.generation;
        request.candidate = next.generation;
        request.definitions.push_back({original->id(), *replacement});
        auto result = ReplacementOperation::begin(std::move(request));
        if (!result.hasValue())
            throw std::runtime_error(result.error().message + " [" + result.error().phase + "]");
        operation.emplace(std::move(result.value()));
    }
    ReplacementReport settled()
    {
        auto result = wait(operation->settled());
        CHECK(result->hasValue());
        return result->value();
    }
    void mapped(const ReplacementReport& report, bool restored)
    {
        CHECK(report.instances.size() == 2 && report.recovered == restored && report.committed == !restored &&
              !report.resourcesRetained);
        const auto actual = (restored ? old : next).values();
        CHECK(actual.size() == 2);
        std::set<InstanceId> seen;
        for (const auto& item : report.instances) {
            CHECK(seen.insert(item.previous).second && checkpoints.count(item.previous) && actual.count(item.current));
            const auto expected = checkpoints.at(item.previous) + (restored ? "" : "0");
            const auto& value = actual.at(item.current);
            CHECK(value.restored && value.value == expected && value.activated == expected &&
                  item.current != item.previous);
            CHECK(wait(kernel->findInstance(item.current).value().ready())->hasValue());
        }
        CHECK(!state(old.generation).replacement && !state(next.generation).replacement &&
              kernel->replacements().value().empty());
        CHECK(state(old.generation).role == (restored ? GenerationRole::Current : GenerationRole::Retired));
        CHECK(state(next.generation).role == (restored ? GenerationRole::Candidate : GenerationRole::Current));
        CHECK(old.count(5) >= 2 && destroyedSignals == 2 &&
              std::all_of(retired.begin(), retired.end(), [](const auto& item) { return item.isNull(); }));
        CHECK(records((restored ? next : old).generation, ResourceKind::Instance) == 0);
    }
    void unchanged(const ReplacementReport& report)
    {
        CHECK(!report.committed && !report.recovered && !report.resourcesRetained && report.instances.empty());
        const auto actual = old.values();
        CHECK(actual.size() == checkpoints.size());
        for (const auto& item : checkpoints)
            CHECK(actual.at(item.first).value == item.second);
        for (const auto& instance : instances)
            CHECK(instance.snapshot().value().state == InstanceState::Active &&
                  wait(instance.whenActiveAfter())->value().identity().hasValue());
        CHECK(old.count(5) == 0 && old.count(6) == 0 && old.count(7) == 2 && next.count(7) == 0);
        CHECK(!state(old.generation).replacement && !state(next.generation).replacement &&
              kernel->replacements().value().empty());
        CHECK(state(old.generation).role == GenerationRole::Current &&
              state(next.generation).role == GenerationRole::Candidate);
    }
    void faulted(const Probe& probe) const
    {
        const auto current = state(probe.generation);
        CHECK(current.nativeMapped && current.state == ModuleState::Failed && current.businessFailure &&
              current.businessFailure->code == ErrorCode::AbiProtocol);
        CHECK(std::find(current.resources.pinReasons.begin(), current.resources.pinReasons.end(),
                        "qt.businessUncertain") != current.resources.pinReasons.end());
        CHECK(records(probe.generation, ResourceKind::Call) == 0);
    }
    void close(bool quiet = true)
    {
        old.observe(0, {});
        next.observe(1, {});
        auto receipt = wait(kernel->close());
        CHECK(receipt->hasValue() && receipt->value().quiescent == quiet);
        if (quiet) {
            CHECK(old.count(5) == old.count(7) && old.count(6) == old.count(7));
            CHECK(next.count(5) == next.count(7) && next.count(6) == next.count(7));
            CHECK(old.alive() == 0 && next.alive() == 0);
            CHECK(records(old.generation, ResourceKind::Instance) == 0 &&
                  records(next.generation, ResourceKind::Instance) == 0);
            CHECK(records(old.generation, ResourceKind::CleanupTask) == 0 &&
                  records(next.generation, ResourceKind::CleanupTask) == 0);
        }
        CHECK(records(old.generation, ResourceKind::Call) == 0 && records(next.generation, ResourceKind::Call) == 0);
        CHECK(scheduler->close().hasValue());
    }
    ~Scene() { hooks = {}; }
};
bool hasError(const ReplacementReport& report, ErrorCode code)
{
    return std::any_of(report.errors.begin(), report.errors.end(),
                       [&](const auto& error) { return error.code == code; });
}
void early(Scene& scene, const QString& name)
{
    if (name == "capture_busy")
        scene.old.mode("capture_busy");
    else
        scene.next.mode("validate_reject");
    scene.begin();
    auto report = scene.settled();
    CHECK(report.outcome == ReplacementOutcome::Failed &&
          hasError(report, name == "capture_busy" ? ErrorCode::ConfigConflict : ErrorCode::ConfigInvalid));
    scene.unchanged(report);
    scene.close();
}
void recovery(Scene& scene, const QString& name)
{
    const bool failed = name.startsWith("recovery_");
    scene.next.mode(name == "restore_reject" ? "restore_reject" : "activate_fail");
    if (failed)
        scene.next.observe(1, [&](int phase) {
            if (phase == 4)
                scene.old.mode(name == "recovery_restore"    ? "restore_reject"
                               : name == "recovery_validate" ? "validate_reject"
                                                             : "activate_fail");
        });
    scene.begin();
    const auto report = scene.settled();
    if (!failed) {
        CHECK(report.outcome == ReplacementOutcome::Restored && !report.errors.empty());
        scene.mapped(report, true);
        CHECK(scene.old.count(2) == 2 && scene.next.count(5) == scene.next.count(7));
    } else {
        CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.committed &&
              !report.recovered && report.instances.empty());
        CHECK(hasError(report, ErrorCode::RecoveryFailed));
        CHECK(scene.state(scene.old.generation).replacement && scene.state(scene.next.generation).replacement);
        CHECK(scene.state(scene.old.generation).role == GenerationRole::Current &&
              scene.state(scene.next.generation).role == GenerationRole::Candidate);
        CHECK(scene.old.count(5) >= 2 && scene.old.alive() == 0 && scene.next.alive() == 0);
    }
    scene.close();
}
void cancel(Scene& scene, const QString& name)
{
    const bool beforeStop = name == "cancel_capture" || name == "cancel_validate";
    const int desired = name == "cancel_capture" ? 3 : name == "cancel_validate" ? 1 : name == "cancel_restore" ? 2 : 4;
    bool requested = false;
    const auto& probe = name == "cancel_capture" ? scene.old : scene.next;
    probe.observe(name == "cancel_capture" ? 0U : 1U, [&](int phase) {
        if (phase != desired || requested)
            return;
        requested = true;
        CHECK(scene.operation->snapshot().value().phase ==
              (beforeStop ? ReplacementPhase::Capturing : ReplacementPhase::Building));
        CHECK(scene.operation->cancel().hasValue());
        events();
    });
    scene.begin();
    const auto report = scene.settled();
    CHECK(requested && report.outcome == ReplacementOutcome::Cancelled);
    if (beforeStop)
        scene.unchanged(report);
    else {
        scene.mapped(report, true);
        CHECK(scene.old.count(2) == 2);
    }
    scene.close();
}
void closing(Scene& scene, bool restoring)
{
    bool requested = false;
    const auto& probe = restoring ? scene.next : scene.old;
    probe.observe(restoring ? 1U : 0U, [&](int phase) {
        if (phase != (restoring ? 2 : 3) || requested)
            return;
        requested = true;
        auto guarded = probe.active(phase);
        CHECK(guarded);
        const auto destroyed = probe.count(5);
        scene.closed = scene.kernel->close();
        events();
        CHECK(guarded && !scene.closed->isReady());
        if (restoring)
            CHECK(probe.count(5) == destroyed);
    });
    scene.begin();
    const auto report = scene.settled();
    CHECK(requested && report.outcome == ReplacementOutcome::Cancelled && !report.committed && !report.recovered &&
          !report.resourcesRetained);
    CHECK(scene.closed && wait(*scene.closed)->value().quiescent);
    CHECK(!scene.state(scene.old.generation).replacement && !scene.state(scene.next.generation).replacement);
    scene.close();
}
void protocol(Scene& scene, const QString& name)
{
    const bool capture = name.startsWith("capture"), validating = name.startsWith("validate");
    const auto& probe = capture ? scene.old : scene.next;
    const auto mode = name.endsWith("throw") ? (capture      ? "capture_throw"
                                                : validating ? "validate_throw"
                                                             : "restore_throw")
                      : capture              ? "capture_pointer"
                      : validating           ? "validate_pending"
                                             : "restore_pending";
    probe.mode(mode);
    scene.begin();
    const auto report = scene.settled();
    CHECK(report.outcome == ReplacementOutcome::Failed && hasError(report, ErrorCode::AbiProtocol));
    scene.faulted(probe);
    if (validating) {
        scene.unchanged(report);
        scene.close();
    } else {
        CHECK(report.resourcesRetained && !report.recovered && !report.committed && report.instances.empty());
        CHECK(probe.alive() > 0 && scene.records(probe.generation, ResourceKind::Instance) > 0);
        CHECK(scene.state(scene.old.generation).replacement && scene.state(scene.next.generation).replacement);
        if (!capture)
            CHECK(scene.old.alive() == 0 && scene.old.count(5) == 2);
        scene.close(false);
    }
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        Scene scene(temporary.path(), args[3].split(';'));
        const auto name = args[1];
        if (name == "roundtrip") {
            scene.begin();
            const auto report = scene.settled();
            CHECK(report.outcome == ReplacementOutcome::Applied);
            scene.mapped(report, false);
            CHECK(scene.old.count(3) == 2 && scene.next.count(2) == 2);
            const auto before = scene.state(scene.old.generation);
            CHECK(before.role == GenerationRole::Retired && before.loadPolicy == ModuleLoadPolicy::Resident &&
                  before.nativeMapped && before.nativePinned);
            const auto refused = scene.manager->tryUnload(scene.old.generation);
            CHECK(!refused.hasValue() && refused.error().code == ErrorCode::ModulePinned);
            const auto after = scene.state(scene.old.generation);
            CHECK(after.role == GenerationRole::Retired && after.state == before.state && after.nativeMapped &&
                  after.nativePinned && after.loadPolicy == before.loadPolicy && !after.unloadFailure);
            CHECK(scene.old.count(3) == 2 && scene.old.alive() == 0);
            const auto activations = scene.next.count(4);
            const auto created = scene.kernel->mount(*scene.replacement).value();
            CHECK(wait(created.ready())->hasValue());
            const auto identity = created.snapshot().value().instance;
            const auto values = scene.next.values();
            CHECK(scene.next.count(4) == activations + 1 && values.count(identity) &&
                  values.at(identity).value == "7" && values.at(identity).activated == "7");
            CHECK(wait(created.stop())->value().quiescent);
            scene.mapped(report, false);
            scene.close();
        } else if (name == "capture_busy" || name == "validate_reject")
            early(scene, name);
        else if (name == "restore_reject" || name == "activate_fail" || name.startsWith("recovery_"))
            recovery(scene, name);
        else if (name.startsWith("cancel_"))
            cancel(scene, name);
        else if (name == "close_capture" || name == "close_restore")
            closing(scene, name == "close_restore");
        else if (name.endsWith("protocol") || name.endsWith("throw"))
            protocol(scene, name);
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
