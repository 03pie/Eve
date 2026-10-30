/** @file module_qt_runtime.cpp
 * @brief 用真实 Qt 工厂 DLL 验证多实例、Core 换代及资源/完整析构先后。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstring>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
void pump(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        CHECK(timer.elapsed() < 5000);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
}
template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    pump([&] { return task.isReady(); });
    return task.tryResult();
}
struct Probe {
    explicit Probe(const QString& path) : image(GetModuleHandleW(path.toStdWString().c_str())) { CHECK(image); }
    HMODULE image;
    template <class F> F function(const char* name) const
    {
        const auto address = GetProcAddress(image, name);
        CHECK(address);
        F result;
        static_assert(sizeof(result) == sizeof(address), "Windows entry");
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
    int count(int index) const { return function<int(__cdecl*)(int)>("cordis_qt_business_count")(index); }
    void release(int index, bool cleanup) const
    {
        function<void(__cdecl*)(int, int)>("cordis_qt_business_release")(index, cleanup);
    }
    QObject* object(int index) const { return function<QObject*(__cdecl*)(int)>("cordis_qt_business_object")(index); }
    bool context(int index) const { return function<int(__cdecl*)(int)>("cordis_qt_business_context")(index) != 0; }
    std::uint64_t childId(int index) const
    {
        return function<std::uint64_t(__cdecl*)(int)>("cordis_qt_business_child_id")(index);
    }
    std::uint64_t runtimeId() const { return function<std::uint64_t(__cdecl*)()>("cordis_qt_business_runtime_id")(); }
};
std::size_t records(ModuleManager& manager, ResourceKind kind)
{
    const auto data = manager.snapshot().value()[0].resources.records;
    return static_cast<std::size_t>(
        std::count_if(data.begin(), data.end(), [&](const ResourceRecord& value) { return value.kind == kind; }));
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto name = args[1];
        app.setProperty("cordis.qt.factoryMode", name);
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        const auto root = temporary.path();
        const auto path = files::install(args[3], root + "/plugin", "org.cordis.qt.example",
                                         QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}, "qt-ui");
        auto loaded = loadQtModule(root.toStdString(), path.toStdString());
        CHECK(loaded.hasValue());
        auto scheduler = std::make_shared<qt::QtScheduler>();
        ModuleManager manager;
        CHECK(!manager.snapshot().value()[0].descriptorAccepted);
        if (name == "missing_factory") {
            auto rejected = qtRuntime(loaded.value().generation, {"org.cordis.qt.page", {}, {}}, scheduler);
            CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::AbiMismatch);
            CHECK(manager.snapshot().value()[0].state == ModuleState::Failed &&
                  records(manager, ResourceKind::QObject) == 1);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        Probe probe(root + "/plugin/module.dll");
        if (name == "guards") {
            CHECK(!qtRuntime({}, {"org.cordis.qt.page", {}, {}}, scheduler).hasValue());
            CHECK(!qtRuntime(loaded.value().generation, {"unknown", {}, {}}, scheduler).hasValue());
            CHECK(!qtRuntime(loaded.value().generation, {"org.cordis.qt.page", {}, {}}, {}).hasValue());
            bool rejected = false;
            std::thread worker([&] {
                auto value = qtRuntime(loaded.value().generation, {"org.cordis.qt.page", {}, {}}, scheduler);
                rejected = !value.hasValue() && value.error().code == ErrorCode::WrongThread;
            });
            worker.join();
            CHECK(rejected && probe.count(1) == 0 && probe.count(2) == 0);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        QtPlugin contract{"org.cordis.qt.page", {}, {}};
        std::shared_ptr<ServiceRegistry> services;
        if (name == "output") {
            contract.outputs.push_back(ServiceOutput::of<int>({"qt.value"}));
            services = std::make_shared<ServiceRegistry>(scheduler);
        }
        auto created = qtRuntime(loaded.value().generation, std::move(contract), scheduler);
        // Invoked only by the negative static-SDK build control; not a normal positive fixture.
        if (name == "static_core" || name == "wrong_core") {
            CHECK(!created.hasValue() && created.error().code == ErrorCode::AbiMismatch);
            CHECK(created.error().message.find("another Core runtime") != std::string::npos);
            CHECK(probe.count(1) == 0 && probe.count(2) == 0 &&
                  manager.snapshot().value()[0].state == ModuleState::Failed);
            CHECK(records(manager, ResourceKind::QObject) == 1 && records(manager, ResourceKind::ExternalHandle) == 0);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        if (name == "bad_catalog" || name == "query_throw") {
            CHECK(!created.hasValue() && probe.count(1) == 1 && probe.count(2) == 0);
            CHECK(manager.snapshot().value()[0].state == ModuleState::Failed);
            CHECK(records(manager, ResourceKind::QObject) == 1 && records(manager, ResourceKind::ExternalHandle) == 0);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        CHECK(created.hasValue());
        std::optional<Runtime> runtime = std::move(created).value();
        const auto runtimeId = runtime->id();
        CHECK(manager.snapshot().value()[0].descriptorAccepted && manager.snapshot().value()[0].businessAccepted);
        CHECK(records(manager, ResourceKind::ExternalHandle) == 1 && probe.count(1) == 1);
        if (name == "shared_core") {
            Runtime before([](Context) { return completedTask(); });
            const auto across = probe.runtimeId();
            Runtime after([](Context) { return completedTask(); });
            CHECK(before.id().value() < across && across < after.id().value() &&
                  runtimeId.value() < before.id().value());
        }
        if (name == "scheduler") {
            auto wrong = std::make_shared<testing::FakeScheduler>();
            auto instance = Instance::mount(*runtime, wrong);
            CHECK(wrong->runUntilIdle().hasValue());
            CHECK(!instance.ready().tryResult()->hasValue() && probe.count(2) == 0);
            auto stopped = instance.stop();
            CHECK(wrong->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
            runtime.reset();
            CHECK(records(manager, ResourceKind::ExternalHandle) == 0);
            CHECK(scheduler->close().hasValue());
            return 0;
        }
        auto instance = Instance::mount(*runtime, scheduler, name.toStdString(), services);
        if (name == "start_pending") {
            pump([&] { return probe.count(3) == 1; });
            auto stopped = instance.stop();
            QCoreApplication::processEvents();
            CHECK(!stopped.isReady() && probe.count(5) == 0 && records(manager, ResourceKind::Instance) == 1);
            CHECK(!probe.context(0));
            probe.release(0, false);
            CHECK(wait(stopped)->value().quiescent);
        } else if (name == "cleanup_pending") {
            CHECK(wait(instance.ready())->hasValue());
            auto stopped = instance.stop();
            pump([&] { return probe.count(4) == 1; });
            CHECK(!stopped.isReady() && probe.count(5) == 0 && records(manager, ResourceKind::Instance) == 1);
            CHECK(instance.snapshot().value().finalizers && !instance.snapshot().value().finalizers->closing);
            probe.release(0, true);
            CHECK(wait(stopped)->value().quiescent);
        } else if (name == "tail_pending") {
            CHECK(wait(instance.ready())->hasValue());
            auto stopped = instance.stop();
            pump([&] {
                auto state = instance.snapshot().value();
                return state.finalizing && state.finalizers && state.finalizers->effects.back().cleanupPending;
            });
            CHECK(probe.count(5) == 0 && records(manager, ResourceKind::Instance) == 1 && !stopped.isReady());
            probe.release(0, true);
            CHECK(wait(stopped)->value().quiescent);
        } else if (name == "tail_failure") {
            CHECK(wait(instance.ready())->hasValue());
            CHECK(!wait(instance.stop())->value().quiescent);
            CHECK(instance.snapshot().value().finalizing && probe.count(5) == 0 &&
                  records(manager, ResourceKind::Instance) == 1);
            CHECK(scheduler->close().hasValue());
            return 0;
        } else if (name == "cleanup_failure") {
            CHECK(wait(instance.ready())->hasValue());
            auto result = wait(instance.stop());
            CHECK(!result->value().quiescent && probe.count(5) == 0 && records(manager, ResourceKind::Instance) == 1);
            CHECK(instance.snapshot().value().finalizers && !instance.snapshot().value().finalizers->closing);
            CHECK(scheduler->close().hasValue());
            return 0;
        } else if (name == "null" || name == "create_throw" || name == "root" || name == "parent" ||
                   name == "wrong_instance") {
            CHECK(!wait(instance.ready())->hasValue());
            const auto report = wait(instance.stop());
            const bool clean = name == "null" || name == "wrong_instance";
            CHECK(report->value().quiescent == clean);
            CHECK(records(manager, ResourceKind::Instance) == (clean ? 0u : 1u));
            CHECK(probe.count(2) == 0 && probe.count(5) == 0);
            if (name != "null")
                CHECK(manager.snapshot().value()[0].state == ModuleState::Failed);
            runtime.reset();
            CHECK(scheduler->close().hasValue());
            return 0;
        } else if (name == "start_failure" || name == "activation_throw") {
            CHECK(!wait(instance.ready())->hasValue());
            CHECK(wait(instance.stop())->value().quiescent);
        } else {
            CHECK(wait(instance.ready())->hasValue() && probe.count(2) == 1 && probe.context(0));
            if (name == "inject")
                CHECK(instance.snapshot().value().children == 1 &&
                      probe.childId(0) > instance.snapshot().value().instance.value());
            if (name == "output")
                CHECK(services->find<int>({"qt.value"})
                          .value()
                          .withService([](int& value) { CHECK(value == 7); })
                          .hasValue());
            bool destructionObserved = false;
            QObject::connect(
                probe.object(0), &QObject::destroyed, &app,
                [&] {
                    CHECK(records(manager, ResourceKind::Instance) > 0 &&
                          records(manager, ResourceKind::CleanupTask) > 0);
                    CHECK(probe.count(6) + 1 == probe.count(5));
                    destructionObserved = true;
                },
                Qt::DirectConnection);
            if (name == "multi" || name == "duplicate") {
                auto secondDefinition = name == "multi" ? qtRuntime(loaded.value().generation,
                                                                    {"org.cordis.qt.settings", {}, {}}, scheduler)
                                                        : Result<Runtime>::success(*runtime);
                CHECK(secondDefinition.hasValue());
                auto second = Instance::mount(secondDefinition.value(), scheduler);
                if (name == "duplicate") {
                    CHECK(!wait(second.ready())->hasValue());
                    CHECK(!wait(second.stop())->value().quiescent);
                    CHECK(probe.count(2) == 1);
                } else {
                    CHECK(wait(second.ready())->hasValue());
                    CHECK(probe.object(0)->property("pluginId").toString() == "org.cordis.qt.page" &&
                          probe.object(1)->property("pluginId").toString() == "org.cordis.qt.settings");
                    CHECK(probe.object(0) != probe.object(1) &&
                          second.snapshot().value().instance != instance.snapshot().value().instance);
                    CHECK(probe.count(1) == 1 && records(manager, ResourceKind::Instance) == 2);
                    CHECK(wait(second.stop())->value().quiescent && probe.count(5) == 1);
                }
            }
            if (name == "update") {
                auto change = instance.update("next", 1);
                CHECK(change.hasValue());
                CHECK(wait(change.value().target)->value().outcome == StableOutcome::Active);
                CHECK(probe.count(2) == 2 && probe.count(5) == 1 && !probe.context(0) && probe.context(1));
            }
            CHECK(wait(instance.stop())->value().quiescent);
            CHECK(destructionObserved);
        }
        CHECK(probe.count(5) == probe.count(2) && probe.count(6) == probe.count(2) && probe.count(7) == 0);
        CHECK(records(manager, ResourceKind::Instance) == (name == "duplicate" ? 1u : 0u));
        CHECK(instance.snapshot().value().state == InstanceState::Disposed && !instance.snapshot().value().finalizers);
        runtime.reset();
        CHECK(records(manager, ResourceKind::ExternalHandle) == (name == "duplicate" ? 1u : 0u));
        CHECK(scheduler->close().hasValue());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
