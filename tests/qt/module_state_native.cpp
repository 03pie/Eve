/** @file module_state_native.cpp
 * @brief 真实 DLL 的 Core 状态顺序、原生直接关闭及不确定资源保留。
 */
#include "../../src/core/instance_state_p.hpp"
#include "../../src/module/native_source_p.hpp"
#include "../../src/module/native_state_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <algorithm>
#include <eve/kernel.hpp>
#include <eve/module/manager.hpp>
#include <eve/module/native.hpp>
#include <eve/module/runtime.hpp>
#include <cstring>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace eve;
using namespace eve::module;
namespace files = eve::testing::modules;
namespace native = eve::module::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
struct Scheduler : IScheduler {
    const std::thread::id owner = std::this_thread::get_id();
    std::deque<ScheduledJob> jobs;
    bool post(ScheduledJob job) override
    {
        jobs.push_back(std::move(job));
        return true;
    }
    bool isCurrentThread() const noexcept override { return owner == std::this_thread::get_id(); }
    bool isOpen() const override { return true; }
    void run()
    {
        std::size_t count = 0;
        while (!jobs.empty()) {
            CHECK(++count < 30000);
            auto job = std::move(jobs.front());
            jobs.pop_front();
            job.run();
        }
    }
};
std::function<void(std::uint32_t)> hook;
std::exception_ptr hookFailure;
void EVE_CALL callback(std::uint32_t phase)
{
    try {
        if (hook)
            hook(phase);
    } catch (...) {
        hookFailure = std::current_exception();
    }
}
using Hook = void(EVE_CALL*)(std::uint32_t);
struct Scene {
    std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ModuleManager manager;
    GenerationId generation;
    std::optional<native::NativeSource> source;
    std::optional<NativeModule> module;
    HMODULE image = nullptr;
    Scene(const QString& root, const QString& fixture, bool legacy)
    {
        const auto directory = root + "/plugin";
        const auto manifest = files::install(fixture, directory, legacy ? "org.eve.business" : "org.eve.state",
                                             QJsonArray{"org.eve.plugin"});
        generation = manager.load(root.toStdString(), manifest.toStdString()).value().generation;
        source.emplace(native::acquireNativeSource(generation).value());
        const auto path = QDir::toNativeSeparators(directory + "/module.dll").toStdWString();
        image = GetModuleHandleW(path.c_str());
        CHECK(image);
    }
    template <class F> F entry(const char* name)
    {
        const auto raw = GetProcAddress(image, name);
        CHECK(raw);
        F pointer;
        static_assert(sizeof(pointer) == sizeof(raw), "fixture entry");
        std::memcpy(&pointer, &raw, sizeof(pointer));
        return pointer;
    }
    void mode(std::uint32_t value)
    {
        auto call = source->ledger.acquire(ResourceKind::Call, "test.state.mode").value();
        entry<void(EVE_CALL*)(std::uint32_t)>("eve_state_set_mode")(value);
    }
    std::uint32_t count(std::uint32_t index)
    {
        auto call = source->ledger.acquire(ResourceKind::Call, "test.state.count").value();
        return entry<std::uint32_t(EVE_CALL*)(std::uint32_t)>("eve_state_count")(index);
    }
    unsigned creates()
    {
        auto call = source->ledger.acquire(ResourceKind::Call, "test.state.creates").value();
        return entry<unsigned(EVE_CALL*)()>("eve_business_fixture_creates")();
    }
    void observe(std::function<void(std::uint32_t)> action)
    {
        hook = std::move(action);
        auto call = source->ledger.acquire(ResourceKind::Call, "test.state.hook").value();
        entry<void(EVE_CALL*)(Hook)>("eve_state_set_hook")(callback);
    }
    void open() { module.emplace(NativeModule::open(manager, generation, scheduler, kernel->services()).value()); }
    Runtime runtime() { return module->runtime({"org.eve.plugin", {}, {}}).value(); }
    template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
    {
        scheduler->run();
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        CHECK(task.isReady());
        return task.tryResult();
    }
    void close()
    {
        CHECK(wait(kernel->close())->value().quiescent);
        CHECK(wait(module->close())->value().quiescent);
        CHECK(module->snapshot().value().instances.empty());
        const auto resources = source->ledger.snapshot();
        for (const auto& record : resources.records)
            CHECK(record.kind != ResourceKind::Instance && record.kind != ResourceKind::Call);
    }
    void retained(const char* reason)
    {
        const auto values = manager.snapshot().value();
        CHECK(values.size() == 1 && values.front().generation == generation);
        const auto& value = values.front();
        CHECK(value.nativeMapped && value.state == ModuleState::Failed && value.businessFailure &&
              value.businessFailure->code == ErrorCode::AbiProtocol);
        CHECK(std::find(value.resources.pinReasons.begin(), value.resources.pinReasons.end(), reason) !=
              value.resources.pinReasons.end());
        for (const auto& record : value.resources.records)
            CHECK(record.kind != ResourceKind::Call);
    }
    ~Scene() { hook = {}; }
};
void metadata(Scene& s)
{
    s.open();
    auto one = s.runtime(), two = s.runtime();
    CHECK(one.stateSchema()->id == "counter" && two.stateSchema()->version == 2 && one.id() != two.id());
    CHECK(s.count(7) == 1 && s.count(0) == 1); // 不在每个 Runtime/激活中重复读取描述。
    s.close();
}
void originLifetime(Scene& s)
{
    s.open();
    std::optional<Runtime> definition{s.runtime()};
    const auto id = definition->id();
    std::optional<Runtime> copy{*definition};
    definition.reset();
    auto instance =
        Instance::mount(*copy, s.scheduler, "\"need-restore\"", std::make_shared<ServiceRegistry>(s.kernel->services()),
                        {}, {}, StateSnapshot{{"counter", 1}, "57"});
    copy.reset();
    CHECK(s.wait(instance.ready())->hasValue());
    CHECK(s.manager.runtimeOrigin(id).value()->generation == s.generation);
    CHECK(s.wait(instance.stop())->value().quiescent);
    CHECK(!s.manager.runtimeOrigin(id).value()); // 激活与校验两个闭包都必须已释放。
    CHECK(s.count(6) == 1);
    s.close();
}
void roundtrip(Scene& s)
{
    s.open();
    auto runtime = s.runtime();
    auto old = s.kernel->registerRuntime("state", runtime).value();
    // 初始输入走实际 Core 入口，旧版本由实际 DLL 校验；随后以同一工厂挂两个独立实例。
    auto first = Instance::mount(runtime, s.scheduler, "\"need-restore\"",
                                 std::make_shared<ServiceRegistry>(s.kernel->services()), {}, {},
                                 StateSnapshot{{"counter", 1}, "9007199254740993"});
    CHECK(s.wait(first.ready())->hasValue());
    const auto entered = s.module->snapshot().value().instances;
    CHECK(entered.size() == 1 && entered.front().logs.back().text == "9007199254740993");
    CHECK(s.count(1) == 1 && s.count(2) == 1 && s.count(4) == 1);
    CHECK(s.wait(first.stop())->value().quiescent);
    auto one = s.kernel->mount(old).value(), two = s.kernel->mount(old).value();
    CHECK(s.wait(one.ready())->hasValue() && s.wait(two.ready())->hasValue());
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.freeze().value();
    auto a = eve::detail::InstanceStateAccess::capture(scope, one).value();
    auto b = eve::detail::InstanceStateAccess::capture(scope, two).value();
    CHECK(a && b && a->json == "7" && b->json == "7" && a->schema.version == 2);
    s.mode(7);
    CHECK(eve::detail::InstanceStateAccess::capture(scope, one).error().code == ErrorCode::ConfigConflict);
    CHECK(!s.module->snapshot().value().faulted);
    s.mode(0);
    CHECK(eve::detail::InstanceStateAccess::capture(scope, one).value()->json == "7");
    scope.release().value();
    s.close();
    CHECK(s.count(6) == 3);
}
void directClose(Scene& s, const std::string& name)
{
    s.open();
    auto instance = s.module->create("org.eve.plugin", "\"sync\"", InstanceId(700), 1).value();
    auto participant = native::NativeStateAccess::participant(instance, {"counter", 2}).value();
    const bool restoring = name == "restore_close";
    if (!restoring)
        CHECK(s.wait(instance.start())->hasValue());
    if (name == "capture_close_busy")
        s.mode(7);
    const auto stops = s.count(5), destroyed = s.count(6);
    std::optional<Task<CleanupReport>> closed;
    s.observe([&](std::uint32_t phase) {
        CHECK(phase == (restoring ? 2U : 3U));
        closed = name == "module_close" ? s.module->close() : instance.close();
        s.scheduler->run(); // 故意模拟违规泵事件，检查真实适配层仍保护对象。
        CHECK(!closed->isReady() && s.count(5) == stops && s.count(6) == destroyed);
        CHECK(instance.snapshot().value().nativeCreated);
    });
    if (restoring)
        CHECK(participant.restore("57").error().code == ErrorCode::ContextClosing);
    else
        CHECK(participant.capture().error().code == ErrorCode::ContextClosing);
    CHECK(closed && s.wait(*closed)->value().quiescent);
    CHECK(s.count(5) == stops + 1 && s.count(6) == destroyed + 1);
    CHECK(!instance.snapshot().value().nativeCreated && !instance.snapshot().value().uncertain);
    s.observe({});
    CHECK(!participant.capture().hasValue());
    s.close();
}
void coreFailure(Scene& s, const std::string& name)
{
    s.open();
    auto runtime = s.kernel->registerRuntime("state", s.runtime()).value();
    if (name == "validate_reject")
        s.mode(4);
    if (name == "validate_pending")
        s.mode(5);
    if (name == "validate_throw")
        s.mode(6);
    if (name == "restore_reject")
        s.mode(11);
    if (name == "restore_pending")
        s.mode(12);
    if (name == "restore_throw")
        s.mode(13);
    std::optional<Task<CleanupReport>> closed;
    if (name == "factory_close")
        s.observe([&](std::uint32_t phase) {
            CHECK(phase == 1);
            closed = s.module->close();
            s.scheduler->run();
            CHECK(!closed->isReady() && s.creates() == 0);
        });
    auto instance = Instance::mount(
        runtime, s.scheduler, "\"need-restore\"", std::make_shared<ServiceRegistry>(s.kernel->services()), {}, {},
        StateSnapshot{{name == "schema_id" ? "other" : "counter", name == "schema_version" ? 3U : 1U}, "41"});
    auto ready = s.wait(instance.ready());
    CHECK(!ready->hasValue() && s.count(4) == 0);
    const bool protocol = name.find("pending") != std::string::npos || name.find("throw") != std::string::npos;
    CHECK(ready->error().code == (protocol                  ? ErrorCode::AbiProtocol
                                  : name == "factory_close" ? ErrorCode::ContextClosing
                                                            : ErrorCode::ConfigInvalid));
    CHECK(s.module->snapshot().value().faulted == protocol);
    auto stopped = s.wait(instance.stop());
    const bool retained = protocol && name.find("restore") == 0;
    CHECK(stopped->value().quiescent == !retained);
    CHECK(s.wait(s.module->close())->value().quiescent == !protocol);
    if (closed)
        CHECK(closed->tryResult()->value().quiescent);
    CHECK(s.wait(s.kernel->close())->value().quiescent);
    if (protocol)
        s.retained(retained ? "nativeInstanceUncertain" : "nativeStateUncertain");
}
void captureFailure(Scene& s, std::uint32_t mode)
{
    s.open();
    auto instance = s.module->create("org.eve.plugin", "\"sync\"", InstanceId(701), 1).value();
    CHECK(s.wait(instance.start())->hasValue());
    auto participant = native::NativeStateAccess::participant(instance, {"counter", 2}).value();
    s.mode(mode);
    CHECK(participant.capture().error().code == ErrorCode::AbiProtocol);
    CHECK(instance.snapshot().value().uncertain && instance.snapshot().value().nativeCreated);
    CHECK(!s.wait(instance.close())->value().quiescent && !s.wait(s.module->close())->value().quiescent);
    CHECK(s.count(6) == 0);
    s.retained("nativeInstanceUncertain");
    CHECK(s.wait(s.kernel->close())->value().quiescent);
}
void rawGuards(Scene& s, const std::string& name)
{
    s.open();
    auto instance = s.module->create("org.eve.plugin", "\"sync\"", InstanceId(702), 1).value();
    CHECK(!native::NativeStateAccess::participant(instance, {"counter", 1}).hasValue());
    auto participant = native::NativeStateAccess::participant(instance, {"counter", 2}).value();
    CHECK(participant.restore("{").error().code == ErrorCode::InvalidArgument && s.count(2) == 0);
    CHECK(s.wait(instance.start())->hasValue());
    if (name == "wrong_thread") {
        std::optional<ErrorCode> failed;
        std::thread worker([&] { failed = participant.capture().error().code; });
        worker.join();
        CHECK(failed == ErrorCode::WrongThread && s.count(3) == 0);
    } else if (name == "restore_started") {
        CHECK(participant.restore("57").error().code == ErrorCode::ContextClosing && s.count(2) == 0);
    } else {
        s.observe([&](std::uint32_t phase) {
            CHECK(phase == 3 && participant.capture().error().code == ErrorCode::ConfigConflict);
            CHECK(s.count(3) == 1);
        });
        CHECK(participant.capture().value() == "7");
        if (hookFailure)
            std::rethrow_exception(hookFailure);
        s.observe({});
    }
    CHECK(!s.module->snapshot().value().faulted);
    CHECK(s.wait(instance.close())->value().quiescent);
    s.close();
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto fixtures = args[3].split(';');
        CHECK(fixtures.size() == 2);
        const auto name = args[1].toStdString();
        Scene scene(args[2], fixtures[name == "legacy" ? 1 : 0], name == "legacy");
        if (name == "query_pending" || name == "query_table" || name == "describe_bad" || name == "query_throw" ||
            name == "describe_throw" || name == "query_reject") {
            scene.mode(name == "query_pending"    ? 1
                       : name == "query_table"    ? 2
                       : name == "describe_bad"   ? 3
                       : name == "query_throw"    ? 15
                       : name == "describe_throw" ? 16
                                                  : 17);
            const auto expected = name == "query_table"    ? ErrorCode::AbiMismatch
                                  : name == "query_reject" ? ErrorCode::ConfigInvalid
                                                           : ErrorCode::AbiProtocol;
            CHECK(NativeModule::open(scene.manager, scene.generation, scene.scheduler).error().code == expected);
            scene.mode(0);
            CHECK(!NativeModule::open(scene.manager, scene.generation, scene.scheduler).hasValue());
            CHECK(scene.count(7) == 1 && scene.creates() == 0);
            CHECK(scene.wait(scene.kernel->close())->value().quiescent);
        } else if (name == "legacy" || name == "stateless") {
            if (name == "stateless")
                scene.mode(14);
            scene.open();
            auto runtime = scene.kernel->registerRuntime("old", scene.runtime()).value();
            CHECK(!runtime.stateSchema());
            auto instance = scene.kernel->mount(runtime, "\"sync\"").value();
            CHECK(scene.wait(instance.ready())->hasValue());
            scene.close();
        } else if (name == "metadata")
            metadata(scene);
        else if (name == "origin_lifetime")
            originLifetime(scene);
        else if (name == "roundtrip")
            roundtrip(scene);
        else if (name == "capture_close" || name == "capture_close_busy" || name == "restore_close" ||
                 name == "module_close")
            directClose(scene, name);
        else if (name == "capture_json")
            captureFailure(scene, 8);
        else if (name == "capture_pointer")
            captureFailure(scene, 9);
        else if (name == "capture_pending")
            captureFailure(scene, 10);
        else if (name == "wrong_thread" || name == "restore_started" || name == "reentry")
            rawGuards(scene, name);
        else
            coreFailure(scene, name);
        CHECK(!hookFailure);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
