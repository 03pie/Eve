/** @file native_host_cases.hpp
 * @brief 真实 DLL 与生产 HostTable/请求组件的联合生命周期检查。
 */
#pragma once
#include <atomic>
#include <chrono>
#include <cordis/module/native.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <future>

namespace native_host_test {
using cordis::testing::FakeScheduler;
template <class T> T entry(HMODULE image, const char* name)
{
    auto address = GetProcAddress(image, name);
    CHECK(address);
    T value = nullptr;
    static_assert(sizeof(value) == sizeof(address), "native fixture entry");
    std::memcpy(&value, &address, sizeof(value));
    return value;
}
struct Scene {
    ModuleManager manager;
    std::shared_ptr<IScheduler> scheduler;
    GenerationId generation;
    NativeModule module;
    HMODULE image;
    using Count = unsigned(CORDIS_CALL*)();
    Count finishAll;
    Count queries;
    Scene(std::shared_ptr<IScheduler> target = std::make_shared<FakeScheduler>(), const QString& name = "business",
          const QString& folder = "module", std::optional<ServiceRegistry> services = {})
        : scheduler(std::move(target)),
          generation(manager.load(root.toStdString(), install(name, folder).toStdString()).value().generation),
          module(NativeModule::open(manager, generation, scheduler, std::move(services)).value())
    {
        const auto value = state();
        const auto path = QDir::toNativeSeparators(QString::fromUtf8(value.libraryPath.c_str())).toStdWString();
        image = GetModuleHandleW(path.c_str());
        CHECK(image);
        finishAll = entry<Count>(image, "cordis_business_fixture_finish_all");
        queries = entry<Count>(image, "cordis_business_fixture_queries");
    }
    NativeInstance create(const std::string& config = "\"sync\"", std::uint64_t id = 1, Epoch epoch = 1)
    {
        return module.create("org.cordis.plugin", config, InstanceId{id}, epoch).value();
    }
    void pump()
    {
        auto fake = std::dynamic_pointer_cast<FakeScheduler>(scheduler);
        CHECK(fake && fake->runUntilIdle().hasValue());
    }
    ModuleGenerationSnapshot state()
    {
        const auto snapshot = manager.snapshot().value();
        for (const auto& value : snapshot)
            if (value.generation == generation)
                return value;
        throw std::runtime_error("fixture generation missing");
    }
    void finish()
    {
        auto report = module.close();
        pump();
        CHECK(report.isReady() && report.tryResult()->hasValue() && report.tryResult()->value().quiescent);
        CHECK(state().resources.records.empty());
        CHECK(manager.drain(generation).hasValue());
    }
};
void synchronous()
{
    Scene s;
    CHECK(s.manager.snapshot().value()[0].businessAccepted);
    CHECK(s.manager.snapshot().value()[0].state == ModuleState::Serving);
    CHECK(NativeModule::open(s.manager, s.generation, s.scheduler).hasValue() && s.queries() == 1);
    auto instance = s.create();
    auto started = instance.start();
    CHECK(!started.isReady());
    s.pump();
    CHECK(started.tryResult()->hasValue() && instance.snapshot().value().state == NativeInstanceState::Started);
    CHECK(cordis::detail::TaskAccess::sameTask(started, instance.start()));
    CHECK(!s.manager.drain(s.generation).hasValue());
    s.finish();
    CHECK(instance.snapshot().value().state == NativeInstanceState::Destroyed);
    CHECK(!instance.start().tryResult()->hasValue());
    CHECK(!s.module.create("org.cordis.plugin", "{}", InstanceId{2}, 1).hasValue());
    CHECK(!NativeModule::open(s.manager, s.generation, s.scheduler).hasValue());
}
void asynchronous()
{
    Scene s;
    auto instance = s.create("{\"value\":1}");
    auto started = instance.start();
    auto closed = s.module.close();
    s.pump();
    CHECK(!started.isReady() && !closed.isReady());
    CHECK(s.manager.snapshot().value()[0].state == ModuleState::Draining);
    unsigned completed = 0;
    std::thread worker([&] { completed = s.finishAll(); });
    worker.join();
    CHECK(completed == 1);
    s.pump();
    CHECK(started.tryResult()->error().code == ErrorCode::OperationCancelled);
    CHECK(closed.tryResult()->value().quiescent && instance.snapshot().value().state == NativeInstanceState::Destroyed);
    CHECK(s.manager.snapshot().value()[0].resources.records.empty());
}
void multiple()
{
    Scene s;
    std::string config = "{\"a\":1}";
    auto a = s.create(config, 1), b = s.create("{\"b\":2}", 2);
    config.assign(config.size(), 'x');
    auto startA = a.start(), startB = b.start();
    CHECK(s.finishAll() == 2);
    s.pump();
    CHECK(startA.tryResult()->hasValue() && startB.tryResult()->hasValue());
    CHECK(a.snapshot().value().logs[0].text == "{\"a\":1}" && b.snapshot().value().logs[0].text == "{\"b\":2}");
    auto stopA = a.close();
    s.pump();
    CHECK(stopA.tryResult()->value().quiescent && b.snapshot().value().state == NativeInstanceState::Started);
    CHECK(!s.manager.drain(s.generation).hasValue());
    auto c = s.create("\"sync\"", 1, 2);
    auto startC = c.start();
    s.pump();
    CHECK(startC.tryResult()->hasValue());
    s.finish();
    CHECK(s.queries() == 1);
}
void early()
{
    Scene s;
    auto instance = s.create("\"early\"");
    auto started = instance.start();
    CHECK(!started.isReady() && instance.snapshot().value().state == NativeInstanceState::Starting);
    s.pump();
    CHECK(started.tryResult()->hasValue());
    s.finish();
}
void closeQueued()
{
    Scene s;
    auto instance = s.create("{}");
    auto started = instance.start();
    CHECK(s.finishAll() == 1);
    auto scheduler = std::dynamic_pointer_cast<FakeScheduler>(s.scheduler);
    CHECK(scheduler->runOne().hasValue() && !started.isReady());
    auto closed = s.module.close();
    s.pump();
    CHECK(closed.isReady() && closed.tryResult()->value().quiescent);
    CHECK(started.tryResult()->hasValue() && !s.module.snapshot().value().faulted);
}
void startFailure()
{
    Scene s;
    CHECK(!s.module.create("org.cordis.plugin", "\"create-fail\"", InstanceId{1}, 1).hasValue());
    CHECK(s.module.snapshot().value().instances.empty());
    auto instance = s.create("\"start-fail\"");
    auto started = instance.start();
    s.pump();
    CHECK(started.tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(!instance.snapshot().value().uncertain);
    auto firstClosed = instance.close();
    s.pump();
    CHECK(firstClosed.tryResult()->value().quiescent);
    auto asynchronous = s.create("\"async-fail\"", 2);
    auto failed = asynchronous.start();
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(failed.tryResult()->error().causes[0].message == "\"async-fail\"");
    s.finish();
    CHECK(asynchronous.snapshot().value().failure->causes[0].message == "\"async-fail\"");
}
void failedClose(const char* mode)
{
    Scene s;
    auto instance = s.create(std::string("\"") + mode + "\"");
    auto started = instance.start();
    s.pump();
    CHECK(started.tryResult()->hasValue());
    auto closed = s.module.close();
    s.pump();
    CHECK(closed.isReady() && !closed.tryResult()->value().quiescent);
    const auto snapshot = instance.snapshot().value();
    CHECK(snapshot.uncertain && snapshot.nativeCreated && snapshot.state == NativeInstanceState::Failed);
    CHECK(!s.manager.drain(s.generation).hasValue() && !s.manager.snapshot().value()[0].resources.records.empty());
    CHECK(s.module.snapshot().value().faulted);
    CHECK(s.manager.snapshot().value()[0].state == ModuleState::Failed);
}
void createProtocol(const char* mode)
{
    Scene s;
    const auto result = s.module.create("org.cordis.plugin", std::string("\"") + mode + "\"", InstanceId{1}, 1);
    CHECK(!result.hasValue() && result.error().code == ErrorCode::AbiProtocol);
    CHECK(s.module.snapshot().value().faulted && s.module.snapshot().value().instances.size() == 1);
    auto closed = s.module.close();
    s.pump();
    CHECK(closed.isReady() && !closed.tryResult()->value().quiescent);
    CHECK(!s.manager.snapshot().value()[0].resources.records.empty());
}
void startProtocol(const char* mode, bool throwing = false)
{
    Scene s(std::make_shared<FakeScheduler>(), throwing ? "business_throw" : "business");
    auto instance = s.create(std::string("\"") + mode + "\"");
    auto started = instance.start();
    s.pump();
    CHECK(started.isReady() && started.tryResult()->error().code == ErrorCode::AbiProtocol);
    CHECK(instance.snapshot().value().uncertain && instance.snapshot().value().nativeCreated);
    auto closed = s.module.close();
    s.pump();
    CHECK(!closed.tryResult()->value().quiescent && !s.manager.drain(s.generation).hasValue());
}
void missing()
{
    ModuleManager manager;
    auto file = install("good");
    const auto loaded = manager.load(root.toStdString(), file.toStdString()).value();
    auto scheduler = std::make_shared<FakeScheduler>();
    CHECK(!NativeModule::open(manager, loaded.generation, scheduler).hasValue());
    const auto value = manager.snapshot().value()[0];
    CHECK(value.businessFailure && !value.businessAccepted && value.nativePinned && value.resources.records.empty());
    CHECK(manager.load(root.toStdString(), file.toStdString()).value().reused);
    CHECK(!NativeModule::open(manager, loaded.generation, scheduler).hasValue());
    CHECK(manager.drain(loaded.generation).hasValue());
}
void guards()
{
    Scene s;
    CHECK(!NativeModule::open(s.manager, s.generation, std::make_shared<FakeScheduler>()).hasValue());
    CHECK(NativeModule::open(s.manager, s.generation, {}).error().code == ErrorCode::InvalidArgument);
    auto dead = std::make_shared<FakeScheduler>();
    dead->close();
    CHECK(NativeModule::open(s.manager, s.generation, dead).error().code == ErrorCode::SchedulerClosed);
    CHECK(!s.module.create("unknown", "{}", InstanceId{1}, 1).hasValue());
    CHECK(!s.module.create("org.cordis.plugin", "{bad}", InstanceId{1}, 1).hasValue());
    CHECK(!s.module.create("org.cordis.plugin", "{}", {}, 1).hasValue());
    CHECK(!s.module.create("org.cordis.plugin", "{}", InstanceId{1}, 0).hasValue());
    auto instance = s.create();
    CHECK(!s.module.create("org.cordis.plugin", "{}", InstanceId{1}, 1).hasValue());
    bool rejected = false;
    std::thread worker([&] {
        rejected = !NativeModule::open(s.manager, s.generation, s.scheduler).hasValue() &&
                   !s.module.create("org.cordis.plugin", "{}", InstanceId{2}, 1).hasValue() &&
                   instance.start().tryResult()->error().code == ErrorCode::WrongThread &&
                   instance.close().tryResult()->error().code == ErrorCode::WrongThread &&
                   !instance.snapshot().hasValue();
    });
    worker.join();
    CHECK(rejected && instance.snapshot().value().state == NativeInstanceState::Created);
    s.finish();
}
void closedScheduler()
{
    Scene s;
    auto instance = s.create("{}");
    auto started = instance.start();
    std::static_pointer_cast<FakeScheduler>(s.scheduler)->close();
    CHECK(s.finishAll() == 1);
    CHECK(started.isReady() && started.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(instance.snapshot().value().uncertain && instance.snapshot().value().deliveryBlocked);
    CHECK(!s.module.close().tryResult()->value().quiescent);
    CHECK(!s.manager.drain(s.generation).hasValue());
}
void reuse()
{
    Scene s;
    auto closed = s.module.close();
    CHECK(closed.isReady() && closed.tryResult()->value().quiescent);
    CHECK(s.manager.snapshot().value()[0].state == ModuleState::Loaded);
    auto again = NativeModule::open(s.manager, s.generation, s.scheduler).value();
    CHECK(s.queries() == 1);
    auto instance = again.create("org.cordis.plugin", "\"sync\"", InstanceId{1}, 2).value();
    auto started = instance.start();
    s.pump();
    CHECK(started.tryResult()->hasValue());
    auto stopped = again.close();
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    CHECK(s.manager.drain(s.generation).hasValue());
}
struct GateScheduler : IScheduler {
    explicit GateScheduler(bool copies) : copy(copies) {}
    FakeScheduler inner;
    bool copy;
    std::promise<void> posted, release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic_bool first{true};
    bool post(ScheduledJob job) override
    {
        const auto accepted = copy ? inner.post(job) : inner.post(std::move(job));
        if (first.exchange(false)) {
            posted.set_value();
            gate.wait();
        }
        return accepted;
    }
    bool isCurrentThread() const noexcept override { return inner.isCurrentThread(); }
    bool isOpen() const override { return inner.isOpen(); }
};
void callbacks(bool copy, bool lateLog = false)
{
    auto scheduler = std::make_shared<GateScheduler>(copy);
    Scene s(scheduler);
    auto instance = s.create("{}");
    auto started = instance.start();
    auto posted = scheduler->posted.get_future();
    unsigned finished = 0;
    std::thread worker([&] { finished = s.finishAll(); });
    std::optional<Task<CleanupReport>> close;
    try {
        CHECK(posted.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        CHECK(scheduler->inner.runUntilIdle().hasValue() && started.isReady());
        close = instance.close();
        CHECK(scheduler->inner.runUntilIdle().hasValue() && !close->isReady());
        const auto snapshot = instance.snapshot().value();
        CHECK(snapshot.nativeCreated && snapshot.activeHostCalls == 1);
        if (!copy)
            CHECK(snapshot.pendingRequests == 0);
        CHECK(!s.manager.drain(s.generation).hasValue());
        if (lateLog) {
            auto logger = entry<CordisStatus(CORDIS_CALL*)()>(s.image, "cordis_business_fixture_log_one");
            CordisStatus status = CORDIS_OK;
            std::thread another([&] { status = logger(); });
            another.join();
            CHECK(status == CORDIS_E_CLOSED);
            CHECK(instance.closed().isReady() && !instance.closed().tryResult()->value().quiescent);
        }
    } catch (...) {
        scheduler->release.set_value();
        worker.join();
        throw;
    }
    scheduler->release.set_value();
    worker.join();
    CHECK(finished == 1 && scheduler->inner.runUntilIdle().hasValue());
    CHECK(close->isReady() && close->tryResult()->value().quiescent == !lateLog);
    auto closed = s.module.close();
    CHECK(closed.tryResult()->value().quiescent == !lateLog);
    CHECK(s.manager.snapshot().value()[0].resources.records.empty() == !lateLog);
}
bool run(const QString& name)
{
    if (name == "host_sync")
        synchronous();
    else if (name == "host_async")
        asynchronous();
    else if (name == "host_multi")
        multiple();
    else if (name == "host_close_queued")
        closeQueued();
    else if (name == "host_early")
        early();
    else if (name == "host_start_failure")
        startFailure();
    else if (name == "host_stop_failure")
        failedClose("stop-fail");
    else if (name == "host_destroy_failure")
        failedClose("destroy-fail");
    else if (name == "host_create_null")
        createProtocol("create-null");
    else if (name == "host_create_pending")
        createProtocol("create-pending");
    else if (name == "host_create_live_error")
        createProtocol("create-live-error");
    else if (name == "host_duplicate")
        startProtocol("duplicate");
    else if (name == "host_sync_protocol")
        startProtocol("start-protocol");
    else if (name == "host_exception")
        startProtocol("throw", true);
    else if (name == "host_missing")
        missing();
    else if (name == "host_guards")
        guards();
    else if (name == "host_closed_scheduler")
        closedScheduler();
    else if (name == "host_reuse")
        reuse();
    else if (name == "host_callback")
        callbacks(false);
    else if (name == "host_callback_copy")
        callbacks(true);
    else if (name == "host_late_log")
        callbacks(false, true);
    else
        return false;
    return true;
}
} // namespace native_host_test
