/** @file module_origin.cpp
 * @brief 两后端真实 DLL 的工厂来源与执行引用寿命，不以配置文本充当代次证明。
 */
#include "../../src/module/replacement_commit_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <cordis/kernel.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/native.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#ifdef CORDIS_TEST_QT_ORIGIN
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/scheduler.hpp>
#endif
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
ModuleGenerationSnapshot state(ModuleManager& manager, GenerationId generation)
{
    const auto values = manager.snapshot().value();
    const auto at = std::find_if(values.begin(), values.end(),
                                 [generation](const auto& item) { return item.generation == generation; });
    CHECK(at != values.end());
    return *at;
}
template <class Factory, class Wait>
void origins(ModuleManager& manager, GenerationId current, GenerationId candidate,
             const std::shared_ptr<IScheduler>& scheduler, const std::shared_ptr<ServiceRegistry>& services,
             Factory make, Wait wait, const char* plugin, bool qt)
{
    CHECK(manager.runtimeOrigin({}).error().code == ErrorCode::InvalidArgument);
    Runtime builtin([](Context) { return completedTask(); });
    CHECK(!manager.runtimeOrigin(builtin.id()).value());
    const auto baseline = state(manager, current).resources.records.size();
    CHECK(!make(current, true).hasValue());
    CHECK(state(manager, current).resources.records.size() == baseline);
    std::optional<Runtime> definition{make(current, false).value()};
    std::optional<Runtime> other{make(candidate, false).value()};
    const auto id = definition->id(), nextId = other->id();
    auto origin = manager.runtimeOrigin(id).value();
    CHECK(origin && origin->runtime == id && origin->generation == current && origin->pluginId == plugin &&
          origin->moduleId == state(manager, current).manifest.moduleId);
    CHECK(manager.runtimeOrigin(nextId).value()->generation == candidate && id != nextId);
    ModuleManager another;
    CHECK(another.runtimeOrigin(id).value()->generation == current);
    std::optional<ErrorCode> error;
    std::thread worker([&] { error = manager.runtimeOrigin(id).error().code; });
    worker.join();
    CHECK(error == ErrorCode::WrongThread);
    std::optional<Runtime> copy{*definition};
    definition.reset();
    CHECK(manager.runtimeOrigin(id).value());
    auto instance = Instance::mount(*copy, scheduler, qt ? "null" : "\"sync\"", services);
    copy.reset();
    const auto ready = wait(instance.ready());
    if (!ready->hasValue())
        throw std::runtime_error("origin activation: " + ready->error().message);
    CHECK(manager.runtimeOrigin(id).value());
    CHECK(state(manager, current).resources.records.size() > baseline);
    auto stopped = instance.stop();
    CHECK(wait(stopped)->value().quiescent);
    CHECK(!manager.runtimeOrigin(id).value() && origin->generation == current);
    CHECK(manager.runtimeOrigin(nextId).value());
    other.reset();
    CHECK(!manager.runtimeOrigin(nextId).value());
    CHECK(state(manager, current).resources.records.size() == baseline);
    for (int i = 0; i != 20; ++i) {
        RuntimeId released;
        {
            auto temporary = make(current, false).value();
            released = temporary.id();
            CHECK(manager.runtimeOrigin(released).value());
        }
        CHECK(!manager.runtimeOrigin(released).value());
    }
    CHECK(state(manager, current).resources.records.size() == baseline);
    // 活动实例已停止，Kernel 的不可变定义仍持有来源；Kernel 销毁后才释放。
    RuntimeId registered;
    {
        Kernel kernel(scheduler);
        auto factory = make(current, false).value();
        registered = factory.id();
        kernel.registerRuntime("registered", factory).value();
        CHECK(wait(kernel.close())->value().quiescent);
        CHECK(manager.runtimeOrigin(registered).value());
    }
    CHECK(!manager.runtimeOrigin(registered).value());
}
template <class Factory, class Wait, class CheckClose, class CheckUnmanaged>
void controlled(ModuleManager& manager, GenerationId current, GenerationId candidate,
                const std::shared_ptr<IScheduler>& scheduler, Kernel& kernel, Factory make, Wait wait,
                CheckClose checkClose, CheckUnmanaged checkUnmanaged, bool qt)
{
    const auto config = qt ? "null" : "\"sync\"";
    auto old = make(current, false).value(), next = make(candidate, false).value();
    kernel.registerRuntime("target", old).value();
    auto before = kernel.mount(old, config).value();
    CHECK(wait(before.ready())->hasValue());
    auto outside = Instance::mount(old, scheduler, config, std::make_shared<ServiceRegistry>(kernel.services()));
    CHECK(wait(outside.ready())->hasValue());
    Kernel foreign(scheduler);
    auto builtin = foreign.registerPlugin("foreign", [](Context) { return completedTask(); }).value();
    auto foreignScope = foreign.reserveReplacement({builtin.id()}).value();
    CHECK(!manager.reserveReplacement(current, candidate, foreignScope).hasValue());
    auto scope = kernel.reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    const auto incomplete = manager.reserveReplacement(current, candidate, scope);
    CHECK(!incomplete.hasValue() && incomplete.error().code == ErrorCode::ConfigConflict);
    CHECK(before.snapshot().value().state == InstanceState::Active &&
          outside.snapshot().value().state == InstanceState::Active);
    CHECK(wait(outside.stop())->value().quiescent);
    if (qt) {
        foreign.registerRuntime("same-module", old).value();
        auto another = foreign.mount(old, config).value();
        CHECK(wait(another.ready())->hasValue());
        auto crossKernel = manager.reserveReplacement(current, candidate, scope);
        CHECK(!crossKernel.hasValue() && crossKernel.error().code == ErrorCode::ConfigConflict);
        CHECK(wait(another.stop())->value().quiescent);
    }
    checkUnmanaged(scope);
    auto reservation = manager.reserveReplacement(current, candidate, scope).value();
    CHECK(!reservation.authorizeRecovery(scope).hasValue() && !reservation.authorizeRecovery(foreignScope).hasValue());
    CHECK(make(candidate, false).error().code == ErrorCode::ConfigConflict);
    auto unowned = std::make_shared<ServiceRegistry>(scheduler);
    for (const auto& factory : {old, next}) {
        auto denied = Instance::mount(factory, scheduler, config, unowned);
        CHECK(wait(denied.ready())->error().code == ErrorCode::ConfigConflict);
        CHECK(wait(denied.stop())->value().quiescent);
    }
    CHECK(wait(scope.stop())->value().quiescent);
    checkClose();
    auto deniedOld = scope.stage(old, config).value();
    CHECK(wait(deniedOld.ready())->error().code == ErrorCode::ConfigConflict);
    CHECK(wait(scope.discard())->value().quiescent);
    auto staged = scope.stage(next, config).value();
    CHECK(wait(staged.ready())->hasValue());
    CHECK(!reservation.authorizeRecovery(scope).hasValue());
    CHECK(wait(scope.discard())->value().quiescent);
    reservation.authorizeRecovery(scope).value();
    auto deniedNext = scope.stage(next, config).value();
    CHECK(wait(deniedNext.ready())->error().code == ErrorCode::ConfigConflict);
    CHECK(wait(scope.discard())->value().quiescent);
    auto recovered = scope.stage(old, config).value();
    CHECK(wait(recovered.ready())->hasValue());
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    scope.commitPrepared().value();
    reservation.reset();
    scope.release().value();
    CHECK(recovered.snapshot().value().state == InstanceState::Active &&
          state(manager, current).role == GenerationRole::Current &&
          state(manager, candidate).role == GenerationRole::Candidate);
    // Core 可准备宿主 builtin 工厂，但模块整体提交必须核对真实候选来源。
    auto invalid = kernel.reserveReplacement({old.id()}).value();
    Runtime fake([](Context) { return completedTask(); });
    invalid.prepareDefinitions({{old.id(), fake}}).value();
    auto invalidReservation = manager.reserveReplacement(current, candidate, invalid).value();
    CHECK(wait(invalid.stop())->value().quiescent);
    auto fakeInstance = invalid.stage(fake).value();
    CHECK(wait(fakeInstance.ready())->hasValue());
    invalid.prepareCommit().value();
    CHECK(!module::detail::ReplacementAccess::prepare(invalidReservation, invalid).hasValue());
    CHECK(wait(invalid.discard())->value().quiescent);
    invalidReservation.authorizeRecovery(invalid).value();
    auto restored = invalid.stage(old, config).value();
    CHECK(wait(restored.ready())->hasValue());
    invalid.prepareCommit(ReplacementCommitMode::Recovery).value();
    CHECK(invalid.preparation().value().mode == ReplacementCommitMode::Recovery);
    {
        auto prepared = module::detail::ReplacementAccess::prepare(invalidReservation, invalid).value();
        CHECK(!prepared.committed());
        prepared.publish().value();
        CHECK(prepared.committed());
    }
    invalidReservation.reset();
    invalid.release().value();
    auto gate = kernel.services().stage({"commit-gate"}, std::make_shared<int>(1)).value();
    gate.publish().value();
    auto interrupted = kernel.reserveReplacement({old.id()}).value();
    interrupted.prepareDefinitions({{old.id(), next}}).value();
    auto interruptedReservation = manager.reserveReplacement(current, candidate, interrupted).value();
    CHECK(wait(interrupted.stop())->value().quiescent);
    auto transient = interrupted.stage(next, config, {}, {ServiceRequirement::of<int>({"commit-gate"})}).value();
    CHECK(wait(transient.ready())->hasValue());
    interrupted.prepareCommit().value();
    gate.retire().value();
    {
        auto prepared = module::detail::ReplacementAccess::prepare(interruptedReservation, interrupted).value();
        CHECK(!prepared.publish().hasValue() && !prepared.committed());
    }
    CHECK(state(manager, current).role == GenerationRole::Current &&
          state(manager, candidate).role == GenerationRole::Candidate);
    const auto unchanged = kernel.snapshot().value();
    CHECK(std::any_of(unchanged.names.begin(), unchanged.names.end(),
                      [&](const auto& name) { return name.name == "target" && name.id == old.id(); }));
    CHECK(wait(interrupted.discard())->value().quiescent);
    gate.release().value();
    interruptedReservation.authorizeRecovery(interrupted).value();
    auto retry = interrupted.stage(old, config).value();
    CHECK(wait(retry.ready())->hasValue());
    interrupted.prepareCommit(ReplacementCommitMode::Recovery).value();
    {
        auto prepared = module::detail::ReplacementAccess::prepare(interruptedReservation, interrupted).value();
        prepared.publish().value();
    }
    interruptedReservation.reset();
    interrupted.release().value();
    auto replacement = kernel.reserveReplacement({old.id()}).value();
    replacement.prepareDefinitions({{old.id(), next}}).value();
    auto replacementReservation = manager.reserveReplacement(current, candidate, replacement).value();
    CHECK(wait(replacement.stop())->value().quiescent);
    auto final = replacement.stage(next, config).value();
    CHECK(wait(final.ready())->hasValue());
    replacement.prepareCommit().value();
    const auto proof = replacement.preparation().value();
    CHECK(proof.mode == ReplacementCommitMode::Candidate && proof.definitions.size() == 1 &&
          proof.definitions[0].previous == old.id() && proof.definitions[0].candidate == next.id());
    {
        auto prepared = module::detail::ReplacementAccess::prepare(replacementReservation, replacement).value();
        std::optional<ErrorCode> wrong;
        std::thread worker([&] { wrong = prepared.publish().error().code; });
        worker.join();
        CHECK(wrong == ErrorCode::WrongThread && !prepared.committed());
        prepared.publish().value();
        prepared.publish().value();
        CHECK(prepared.committed());
    }
    replacementReservation.reset();
    replacement.release().value();
    CHECK(state(manager, current).role == GenerationRole::Retired &&
          state(manager, candidate).role == GenerationRole::Current);
    CHECK(make(current, false).error().code == ErrorCode::ModulePinned);
    CHECK(!replacement.preparation().hasValue());
    auto following = kernel.mount(next, config).value();
    CHECK(wait(following.ready())->hasValue());
    CHECK(wait(kernel.close())->value().quiescent);
    foreignScope.release().value();
    CHECK(wait(foreign.close())->value().quiescent);
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const bool qt = args[1] == "qt";
        const auto dlls = args[3].split(';');
        CHECK(dlls.size() == 2);
        ModuleManager manager;
        auto install = [&](int index, const char* directory, const char* version) {
            return files::install(dlls[index], args[2] + '/' + directory,
                                  qt ? "org.cordis.qt.example" : "org.cordis.business",
                                  qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}
                                     : QJsonArray{"org.cordis.plugin"},
                                  qt ? "qt-ui" : "c-abi", version)
                .toStdString();
        };
        auto first = install(0, "old", "1.0.0"), second = install(1, "next", "2.0.0");
#ifdef CORDIS_TEST_QT_ORIGIN
        if (qt) {
            const auto current = loadQtModule(args[2].toStdString(), first).value().generation;
            const auto candidate = stageQtModule(args[2].toStdString(), second, current).value().generation;
            auto scheduler = std::make_shared<qt::QtScheduler>();
            Kernel kernel(scheduler);
            auto services = std::make_shared<ServiceRegistry>(kernel.services());
            auto make = [&](GenerationId id, bool invalid) {
                QtPlugin plugin{"org.cordis.qt.page", {}, {}};
                if (invalid)
                    plugin.outputs.push_back(ServiceOutput::of<int>({""}));
                return qtRuntime(id, std::move(plugin), scheduler);
            };
            auto wait = [&](const auto& task) {
                QElapsedTimer timer;
                timer.start();
                while (!task.isReady()) {
                    CHECK(timer.elapsed() < 5000);
                    QCoreApplication::processEvents();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    QThread::msleep(1);
                }
                return task.tryResult();
            };
            origins(manager, current, candidate, scheduler, services, make, wait, "org.cordis.qt.page", true);
            controlled(
                manager, current, candidate, scheduler, kernel, make, wait, [] {}, [](const ReplacementScope&) {},
                true);
            scheduler->close().value();
            return 0;
        }
#endif
        CHECK(!qt);
        const auto current = manager.load(args[2].toStdString(), first).value().generation;
        const auto candidate = manager.stage(args[2].toStdString(), second, current).value().generation;
        auto scheduler = std::make_shared<testing::FakeScheduler>();
        Kernel kernel(scheduler);
        auto services = std::make_shared<ServiceRegistry>(kernel.services());
        auto old = NativeModule::open(manager, current, scheduler, *services).value();
        auto next = NativeModule::open(manager, candidate, scheduler, *services).value();
        auto make = [&](GenerationId id, bool invalid) {
            NativePlugin plugin{"org.cordis.plugin", {}, {}};
            if (invalid)
                plugin.outputs.push_back({{""}});
            return (id == current ? old : next).runtime(std::move(plugin));
        };
        auto wait = [&](const auto& task) {
            scheduler->runUntilIdle(30000).value();
            CHECK(task.isReady());
            return task.tryResult();
        };
        origins(manager, current, candidate, scheduler, services, make, wait, "org.cordis.plugin", false);
        controlled(
            manager, current, candidate, scheduler, kernel, make, wait,
            [&] {
                for (const auto& module : {old, next}) {
                    const auto closed = module.close().tryResult();
                    CHECK(closed && !closed->hasValue() && closed->error().code == ErrorCode::ConfigConflict);
                    CHECK(!module.snapshot().value().closing);
                }
            },
            [&](const ReplacementScope& scope) {
                auto external = old.create("org.cordis.plugin", "\"sync\"", InstanceId{900001}, 1).value();
                auto refused = manager.reserveReplacement(current, candidate, scope);
                CHECK(!refused.hasValue() && refused.error().code == ErrorCode::ConfigConflict);
                CHECK(wait(external.close())->value().quiescent);
            },
            false);
        auto a = old.close(), b = next.close();
        scheduler->runUntilIdle(30000).value();
        CHECK(a.tryResult()->value().quiescent && b.tryResult()->value().quiescent);
        scheduler->close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
