/** @file native_resource_cases.hpp
 * @brief 真实 C DLL 对生产 dispatch/effect 桥、退出及故障账目的检查。
 */
#pragma once

namespace native_resource_test {
using namespace native_host_test;
struct Resources : Scene {
    using Dispatch = EveStatus(EVE_CALL*)(unsigned, EveHandle, EveBytesV1, EveRequestId*);
    using Cancel = EveStatus(EVE_CALL*)(unsigned, EveRequestId);
    using Effect = EveStatus(EVE_CALL*)(unsigned, EveHandle, EveHandle*);
    using Ready = EveStatus(EVE_CALL*)(unsigned, EveHandle, EveStatus);
    using Close = EveStatus(EVE_CALL*)(unsigned, EveHandle, EveRequestId*);
    using Stat = std::uint64_t(EVE_CALL*)(unsigned, unsigned);
    using Ack = void(EVE_CALL*)(unsigned, EveStatus);
    using Data = EveBytesV1(EVE_CALL*)(unsigned);
    Dispatch dispatch;
    Cancel cancel;
    Effect effect;
    Ready ready;
    Close close;
    Stat stat;
    Ack ack;
    Data data;
    explicit Resources(std::shared_ptr<IScheduler> scheduler = std::make_shared<FakeScheduler>(),
                       const QString& fixture = "resources", const QString& folder = "module",
                       std::optional<ServiceRegistry> services = {})
        : Scene(std::move(scheduler), fixture, folder, std::move(services)),
          dispatch(entry<Dispatch>(image, "eve_resource_dispatch")),
          cancel(entry<Cancel>(image, "eve_resource_cancel")),
          effect(entry<Effect>(image, "eve_resource_effect")), ready(entry<Ready>(image, "eve_resource_ready")),
          close(entry<Close>(image, "eve_resource_close")), stat(entry<Stat>(image, "eve_resource_stat")),
          ack(entry<Ack>(image, "eve_resource_ack_status")), data(entry<Data>(image, "eve_resource_result"))
    {
    }
    NativeInstance active(std::uint64_t id = 1)
    {
        auto instance = create("{}", id);
        auto started = instance.start();
        pump();
        CHECK(started.tryResult()->hasValue());
        return instance;
    }
    EveRequestId send(EveHandle callback = 7, unsigned index = 0, const std::string& text = "{\"v\":1}")
    {
        EveRequestId request = 0;
        CHECK(dispatch(index, callback, {reinterpret_cast<const uint8_t*>(text.data()), text.size()}, &request) ==
              EVE_PENDING);
        CHECK(request);
        return request;
    }
    EveHandle add(EveHandle callback = 11, unsigned index = 0, bool setup = true)
    {
        EveHandle handle = 0;
        CHECK(effect(index, callback, &handle) == EVE_OK && handle);
        if (setup)
            CHECK(ready(index, handle, EVE_OK) == EVE_OK);
        return handle;
    }
    std::string result(unsigned index = 0)
    {
        auto bytes = data(index);
        return std::string(reinterpret_cast<const char*>(bytes.data), static_cast<std::size_t>(bytes.size));
    }
};
void dispatch(bool early = false, bool sync = false)
{
    Resources s;
    auto instance = s.active();
    std::string payload = "{\"integer\":9007199254740993}";
    auto request = s.send(sync ? 8 : (early ? 9 : 7), 0, payload);
    payload.assign(payload.size(), 'x');
    CHECK(s.stat(0, 1) == 0 && s.stat(0, 0) == 0 && instance.snapshot().value().pendingHostOperations == 1);
    s.pump();
    if (!early && !sync) {
        CHECK(s.stat(0, 0) == 0 && s.stat(0, 4) != request);
        CHECK(s.finishAll() == 1);
        s.pump();
    }
    CHECK(s.stat(0, 0) == 1 && s.stat(0, 5) == request && s.stat(0, 3) == EVE_OK);
    CHECK(instance.snapshot().value().pendingHostOperations == 0);
    CHECK(s.result() == (sync ? "" : "{\"integer\":9007199254740993}"));
    s.finish();
}
void cancel(bool entered, bool closing)
{
    Resources s;
    auto instance = s.active();
    auto request = s.send();
    if (entered)
        s.pump();
    auto stopped = closing ? std::optional<Task<CleanupReport>>(instance.close()) : std::nullopt;
    if (!closing)
        CHECK(s.cancel(0, request) == EVE_OK);
    s.pump();
    if (entered) {
        CHECK(s.stat(0, 0) == 0 && (!stopped || !stopped->isReady()));
        CHECK(s.finishAll() == 1);
        s.pump();
    }
    CHECK(s.stat(0, 0) == 1 && static_cast<EveStatus>(s.stat(0, 3)) == EVE_E_CANCELLED);
    if (stopped)
        CHECK(stopped->tryResult()->value().quiescent);
    s.finish();
}
void effects(bool pending = false)
{
    Resources s;
    auto instance = s.active();
    s.add(11);
    s.add(pending ? 12 : 14);
    s.pump();
    CHECK(instance.snapshot().value().resources.effects.size() == 2 && s.stat(0, 2) == 0);
    auto closed = s.module.close();
    s.pump();
    if (pending) {
        CHECK(!closed.isReady() && s.stat(0, 2) == 1 && s.stat(0, 10) == 12);
        CHECK(instance.snapshot().value().nativeCreated && !s.manager.drain(s.generation).hasValue());
        CHECK(s.finishAll() == 1);
        s.pump();
    }
    CHECK(closed.tryResult()->value().quiescent && s.stat(0, 2) == 2);
    CHECK(s.stat(0, 10) == (pending ? 12U : 14U) && s.stat(0, 11) == 11);
    CHECK(s.manager.snapshot().value()[0].resources.records.empty());
}
void setupPending()
{
    Resources s;
    auto instance = s.active();
    auto effect = s.add(11, 0, false);
    auto closed = instance.close();
    s.pump();
    CHECK(!closed.isReady() && s.stat(0, 2) == 0);
    CHECK(s.ready(0, effect, EVE_E_CANCELLED) == EVE_OK);
    s.pump();
    CHECK(closed.tryResult()->value().quiescent && s.stat(0, 2) == 1);
    s.finish();
}
void explicitClose(bool fail = false)
{
    Resources s;
    auto instance = s.active();
    auto effect = s.add(fail ? 13 : 12);
    EveRequestId request = 0;
    CHECK(s.close(0, effect, &request) == EVE_PENDING && request && s.stat(0, 0) == 0);
    s.pump();
    if (!fail) {
        CHECK(s.stat(0, 0) == 0 && s.finishAll() == 1);
        s.pump();
    }
    CHECK(s.stat(0, 0) == 1 && s.stat(0, 5) == request);
    if (fail) {
        CHECK(static_cast<EveStatus>(s.stat(0, 3)) == EVE_E_FAILED);
        CHECK(instance.closed().tryResult()->value().quiescent == false);
        CHECK(instance.snapshot().value().uncertain && !s.manager.snapshot().value()[0].resources.records.empty());
    } else {
        CHECK(s.stat(0, 3) == EVE_OK && s.stat(0, 2) == 1);
        s.finish();
        CHECK(s.stat(0, 2) == 1);
    }
}
void failedCleanup()
{
    Resources s;
    auto instance = s.active();
    s.add(13);
    auto closed = s.module.close();
    s.pump();
    CHECK(closed.tryResult()->value().quiescent == false && instance.snapshot().value().uncertain);
    CHECK(instance.snapshot().value().resources.effects.size() == 1);
    CHECK(!s.manager.snapshot().value()[0].resources.records.empty());
}
void closeDuringCleanup()
{
    Resources s;
    auto instance = s.active();
    auto effect = s.add(12);
    EveRequestId request = 0;
    CHECK(s.close(0, effect, &request) == EVE_PENDING);
    s.pump();
    CHECK(s.stat(0, 4) != 0);
    auto closed = instance.close();
    s.pump();
    CHECK(!closed.isReady() && !instance.snapshot().value().uncertain);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(closed.tryResult()->value().quiescent && s.stat(0, 0) == 1);
    s.finish();
}
void guards()
{
    Resources s;
    auto a = s.active(), b = s.active(2);
    auto effect = s.add();
    EveRequestId request = 99;
    CHECK(s.close(1, effect, &request) == EVE_E_NOT_FOUND && request == 0);
    CHECK(s.ready(1, effect, EVE_OK) == EVE_E_NOT_FOUND);
    EveStatus wrongThread = EVE_OK;
    std::thread worker([&] {
        request = 99;
        wrongThread = s.dispatch(0, 8, {nullptr, 0}, &request);
    });
    worker.join();
    CHECK(wrongThread == EVE_E_WRONG_THREAD && request == 0);
    CHECK(s.dispatch(0, 8, {nullptr, 1}, &request) == EVE_E_INVALID_ARGUMENT && request == 0);
    CHECK(s.dispatch(0, 0, {reinterpret_cast<const uint8_t*>("{}"), 2}, &request) == EVE_E_INVALID_ARGUMENT);
    CHECK(s.cancel(0, 123456) == EVE_E_NOT_FOUND);
    for (unsigned i = 0; i < 128; ++i)
        s.send(8);
    CHECK(s.dispatch(0, 8, {reinterpret_cast<const uint8_t*>("{}"), 2}, &request) == EVE_E_LIMIT && request == 0);
    s.pump();
    CHECK(s.stat(0, 0) == 128);
    s.send(9);
    s.pump();
    CHECK(s.stat(0, 0) == 129);
    s.finish();
}
void duplicateReady()
{
    Resources s;
    auto instance = s.active();
    auto effect = s.add();
    CHECK(s.ready(0, effect, EVE_OK) == EVE_E_ABI_PROTOCOL);
    CHECK(instance.closed().tryResult()->value().quiescent == false);
    CHECK(instance.snapshot().value().uncertain);
}
void effectReuse()
{
    Resources s;
    auto instance = s.active();
    EveHandle previous = 0;
    for (unsigned i = 0; i < 128; ++i) {
        auto effect = s.add();
        CHECK(effect > previous);
        previous = effect;
        EveRequestId request = 0;
        CHECK(s.close(0, effect, &request) == EVE_PENDING);
        s.pump();
    }
    CHECK(s.stat(0, 2) == 128 && instance.snapshot().value().resources.effects.empty());
    EveHandle next = 0;
    CHECK(s.effect(0, 11, &next) == EVE_OK && next > previous);
    CHECK(s.ready(0, next, EVE_OK) == EVE_OK);
    s.finish();
    CHECK(s.stat(0, 2) == 129);
}
void rejectedAck()
{
    Resources s;
    auto instance = s.active();
    s.ack(0, EVE_PENDING);
    s.send(8);
    s.pump();
    CHECK(instance.closed().tryResult()->value().quiescent == false);
    CHECK(instance.snapshot().value().pendingHostOperations == 1 && instance.snapshot().value().uncertain);
}
void discard()
{
    Resources s;
    auto instance = s.active();
    s.add();
    auto scheduler = std::dynamic_pointer_cast<FakeScheduler>(s.scheduler);
    scheduler->close();
    CHECK(instance.closed().isReady() && !instance.closed().tryResult()->value().quiescent);
    CHECK(instance.snapshot().value().uncertain && !s.manager.snapshot().value()[0].resources.records.empty());
}
struct CaptureScheduler : IScheduler {
    FakeScheduler inner;
    bool capture = false;
    std::optional<ScheduledJob> retained;
    bool post(ScheduledJob job) override
    {
        if (capture) {
            capture = false;
            retained = job;
        }
        return inner.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override { return inner.isCurrentThread(); }
    bool isOpen() const override { return inner.isOpen(); }
};
void captureRetirement()
{
    auto scheduler = std::make_shared<CaptureScheduler>();
    Resources s(scheduler);
    auto instance = s.create("{}");
    auto started = instance.start();
    CHECK(scheduler->inner.runUntilIdle().hasValue() && started.tryResult()->hasValue());
    scheduler->capture = true;
    s.send(8);
    CHECK(scheduler->inner.runUntilIdle().hasValue() && s.stat(0, 0) == 1);
    auto closed = s.module.close();
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    const auto premature = closed.isReady();
    std::thread worker([&] { scheduler->retained.reset(); });
    worker.join();
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    CHECK(!premature && closed.tryResult()->value().quiescent);
    CHECK(s.manager.snapshot().value()[0].resources.records.empty());
}
void expiredScheduler()
{
    Resources s;
    auto instance = s.active();
    s.scheduler.reset();
    auto before = instance.snapshot();
    CHECK(before.hasValue() && s.module.snapshot().hasValue());
    auto closed = s.module.close();
    CHECK(closed.tryResult()->value().quiescent == false);
    CHECK(instance.snapshot().value().deliveryBlocked && instance.snapshot().value().uncertain);
    CHECK(s.module.snapshot().value().faulted);
}
bool run(const QString& name)
{
    if (name == "resource_dispatch")
        dispatch();
    else if (name == "resource_early")
        dispatch(true);
    else if (name == "resource_sync")
        dispatch(false, true);
    else if (name == "resource_cancel_queued")
        cancel(false, false);
    else if (name == "resource_cancel_pending")
        cancel(true, false);
    else if (name == "resource_close_queued")
        cancel(false, true);
    else if (name == "resource_close_pending")
        cancel(true, true);
    else if (name == "resource_effects")
        effects();
    else if (name == "resource_cleanup_pending")
        effects(true);
    else if (name == "resource_setup_pending")
        setupPending();
    else if (name == "resource_explicit_cleanup")
        explicitClose();
    else if (name == "resource_explicit_failure")
        explicitClose(true);
    else if (name == "resource_cleanup_failure")
        failedCleanup();
    else if (name == "resource_close_during_cleanup")
        closeDuringCleanup();
    else if (name == "resource_guards")
        guards();
    else if (name == "resource_duplicate_ready")
        duplicateReady();
    else if (name == "resource_effect_reuse")
        effectReuse();
    else if (name == "resource_expired_scheduler")
        expiredScheduler();
    else if (name == "resource_ack_protocol")
        rejectedAck();
    else if (name == "resource_discard")
        discard();
    else if (name == "resource_retire")
        captureRetirement();
    else
        return false;
    return true;
}
} // namespace native_resource_test
