/** @file native_lifecycle_cases.hpp
 * @brief 真实 Qt、生产请求组件/托管宿主与 DLL 内捕获的 S06/S07 验证。
 */
#pragma once
#include "../../src/module/native_source_p.hpp"
#include "../../src/module/request_p.hpp"
#include "native_host_cases.hpp"
#include <QEventLoop>
#include <QTimer>
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/qt/scheduler.hpp>
#include <exception>
#include <functional>

namespace native_lifecycle_test {
using namespace native_host_test;
using namespace cordis::module::detail;
CordisBytesV1 wire(const std::string& text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
template <class F> void until(F condition)
{
    if (condition())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    bool timedOut = false;
    std::exception_ptr failure;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        try {
            if (condition())
                loop.quit();
        } catch (...) {
            failure = std::current_exception();
            loop.quit();
        }
    });
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    poll.start(0);
    deadline.start(5000);
    loop.exec();
    if (failure)
        std::rethrow_exception(failure);
    CHECK(!timedOut);
}
struct QtGate final : IScheduler {
    std::shared_ptr<cordis::qt::QtScheduler> qt = std::make_shared<cordis::qt::QtScheduler>();
    bool capture = false;
    std::optional<ScheduledJob> retained;
    bool post(ScheduledJob job) override
    {
        if (capture) {
            capture = false;
            retained = job;
        }
        return qt->post(std::move(job));
    }
    bool isOpen() const override { return qt->isOpen(); }
    bool isCurrentThread() const noexcept override { return qt->isCurrentThread(); }
    void finish()
    {
        until([&] { return qt->snapshot().value().queued == 0; });
        CHECK(!retained && qt->snapshot().value().discarded == 0 && qt->snapshot().value().callbackErrors == 0);
        CHECK(qt->close().hasValue());
    }
};
template <class F> auto foreign(const GenerationLedger& ledger, F action)
{
    auto call = ledger.acquire(ResourceKind::Call, "test.lifecycle.entry");
    CHECK(call.hasValue());
    return action();
}
struct Life : Scene {
    using Queue = CordisStatus(CORDIS_CALL*)();
    using Stat = std::uint64_t(CORDIS_CALL*)(unsigned);
    Queue queue;
    Stat stat;
    std::shared_ptr<QtGate> gate;
    GenerationLedger ledger;
    explicit Life(std::shared_ptr<QtGate> target)
        : Scene(target, "lifecycle"), queue(entry<Queue>(image, "cordis_lifecycle_queue")),
          stat(entry<Stat>(image, "cordis_lifecycle_stat")), gate(std::move(target)),
          ledger(acquireNativeSource(generation).value().ledger)
    {
    }
    std::uint64_t read(unsigned field)
    {
        return foreign(ledger, [&] { return stat(field); });
    }
    void end()
    {
        auto closed = module.close();
        until([&] { return closed.isReady(); });
        CHECK(closed.tryResult()->value().quiescent && state().resources.records.empty());
        CHECK(manager.drain(generation).hasValue());
        CHECK(state().nativePinned && state().resources.drained() && state().state == ModuleState::Resident);
        gate->finish();
    }
};
void pendingStop()
{
    Life s(std::make_shared<QtGate>());
    auto instance = s.create("\"stop\"");
    auto ready = instance.start();
    until([&] { return ready.isReady(); });
    CHECK(ready.tryResult()->hasValue());
    auto closed = instance.close();
    CHECK(!closed.isReady() && s.read(5) == 1 && s.read(7) == 0);
    CHECK(!s.manager.drain(s.generation).hasValue());
    CHECK(!instance.snapshot().value().uncertain && instance.snapshot().value().nativeCreated);
    CHECK(foreign(s.ledger, [&] { return s.finishAll(); }) == 1);
    CHECK(!closed.isReady() && s.read(7) == 0); // Completion is copied, not an inline destroy.
    until([&] { return closed.isReady(); });
    CHECK(closed.tryResult()->value().quiescent && s.read(7) == 1);
    s.end();
}
void captured(bool stale, bool failed)
{
    Life s(std::make_shared<QtGate>());
    Kernel kernel(s.gate);
    auto runtime = s.module.runtime({"org.cordis.plugin", {}, {}}).value();
    CHECK(kernel.registerRuntime("capture", runtime).hasValue());
    auto instance = kernel.mount(runtime, failed ? "\"capture-fail\"" : "\"capture\"").value();
    auto ready = instance.ready();
    until([&] { return ready.isReady(); });
    CHECK(ready.tryResult()->hasValue());
    s.gate->capture = true;
    CHECK(foreign(s.ledger, [&] { return s.queue(); }) == CORDIS_PENDING && s.gate->retained);
    CHECK(s.read(0) == 0 && s.read(1) == 0 && s.read(9) == 1);
    if (!stale) {
        until([&] { return s.read(2) == 1; });
        CHECK(s.read(0) == 1 && static_cast<CordisStatus>(s.read(3)) == CORDIS_OK);
    }
    auto stopped = instance.stop();
    until([&] { return s.read(8) == 1; });
    CHECK(s.read(0) == (stale ? 0u : 1u) && s.read(2) == 1);
    CHECK(static_cast<CordisStatus>(s.read(3)) == (stale ? CORDIS_E_CANCELLED : CORDIS_OK));
    CHECK(s.read(1) == (failed ? 0u : 1u) && s.read(9) == (failed ? 1u : 0u));
    CHECK(s.read(7) == 0 && !s.manager.drain(s.generation).hasValue());
    const auto records = s.state().resources.records;
    CHECK(std::any_of(records.begin(), records.end(),
                      [](const auto& item) { return item.kind == ResourceKind::Callback; }));
    CHECK(std::any_of(records.begin(), records.end(),
                      [](const auto& item) { return item.kind == ResourceKind::QueuePayload; }));
    const auto logs = s.module.snapshot().value().instances.at(0).logs;
    CHECK(std::count_if(logs.begin(), logs.end(),
                        [](const auto& item) { return item.text == "DLL capture destroyed"; }) == (failed ? 0 : 1));
    if (!failed)
        CHECK(!stopped.isReady());
    // The retained host task copy is released on another thread; its final retirement must return to Qt owner.
    std::thread releaser([&] { s.gate->retained.reset(); });
    releaser.join();
    until([&] { return stopped.isReady(); });
    CHECK(stopped.tryResult()->value().quiescent == !failed && s.read(7) == (failed ? 0u : 1u));
    auto kernelClosed = kernel.close();
    until([&] { return kernelClosed.isReady(); });
    CHECK(kernelClosed.tryResult()->value().quiescent == !failed);
    if (failed) {
        auto closed = s.module.close();
        until([&] { return closed.isReady(); });
        CHECK(!closed.tryResult()->value().quiescent && s.module.snapshot().value().faulted);
        CHECK(s.read(1) == 0 && s.read(9) == 1 && !s.manager.drain(s.generation).hasValue());
        CHECK(s.state().nativePinned && !s.state().resources.records.empty());
        s.gate->finish();
    } else {
        CHECK(s.read(1) == 1 && kernel.services().snapshot().value().empty());
        s.end();
    }
}
// S06 explicitly exercises the internal production request boundary. NativeModule exposes no public stop-cancel API.
inline RequestChannel* stopChannel = nullptr;
CordisStatus CORDIS_CALL complete(CordisHandle context, CordisRequestId request, CordisStatus status,
                                  CordisBytesV1 json) noexcept
{
    return stopChannel ? stopChannel->complete(context, request, status, json) : CORDIS_E_CLOSED;
}
CordisStatus CORDIS_CALL log(CordisHandle, std::uint32_t, CordisBytesV1) noexcept
{
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL cancelHost(CordisHandle, CordisRequestId) noexcept
{
    return CORDIS_E_NOT_FOUND;
}
const CordisHostV1 stopHost{{sizeof(CordisHostV1), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0},
                            0,
                            0,
                            complete,
                            log,
                            cancelHost,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr};
void cancelledStop()
{
    ModuleManager manager;
    auto generation = manager.load(root.toStdString(), install("lifecycle").toStdString()).value().generation;
    auto source = acquireNativeSource(generation).value();
    auto scheduler = std::make_shared<QtGate>();
    RequestChannel channel(scheduler, source.ledger, InstanceId{1}, 1);
    stopChannel = &channel;
    struct Reset {
        ~Reset() { stopChannel = nullptr; }
    } reset;
    auto api = foreign(source.ledger, [&] { return queryBusiness(source.query, stopHost, source.manifest); }).value();
    auto lease = source.ledger.acquire(ResourceKind::Instance, "test.cancel.stop").value();
    CordisHandle native = 0;
    const std::string config = "\"stop\"", plugin = "org.cordis.plugin";
    CHECK(foreign(source.ledger, [&] {
              return api.table.create(channel.context(), wire(plugin), wire(config), &native);
          }) == CORDIS_OK);
    auto start = channel.begin(NativeRequestKind::Start, "test.start").value();
    auto started = foreign(source.ledger, [&] { return api.table.start(native, start.id); });
    CHECK(channel.returned(start.id, started).hasValue());
    until([&] { return start.result.isReady(); });
    CHECK(start.result.tryResult()->value().status == CORDIS_OK);
    CHECK(channel.close().hasValue());
    auto stop = channel.begin(NativeRequestKind::Stop, "test.stop").value();
    auto returned = foreign(source.ledger, [&] { return api.table.request_stop(native, stop.id); });
    CHECK(returned == CORDIS_PENDING && channel.returned(stop.id, returned).hasValue());
    CHECK(channel.cancel(stop.id).value());
    CHECK(foreign(source.ledger, [&] { return api.table.cancel_request(native, stop.id); }) == CORDIS_OK);
    CHECK(!channel.cancel(stop.id).value());
    CHECK(channel.seal().hasValue() && !channel.drained().isReady() && !stop.result.isReady());
    CHECK(!manager.drain(generation).hasValue());
    const auto records = source.ledger.snapshot().records;
    CHECK(std::any_of(records.begin(), records.end(),
                      [](const auto& item) { return item.kind == ResourceKind::CleanupTask; }));
    const auto path = manager.snapshot().value()[0].libraryPath;
    auto image = GetModuleHandleW(QString::fromUtf8(path.c_str()).toStdWString().c_str());
    CHECK(image);
    auto finish = entry<Scene::Count>(image, "cordis_business_fixture_finish_all");
    unsigned finished = 0;
    std::exception_ptr workerError;
    std::thread worker([&] {
        try {
            finished = foreign(source.ledger, [&] { return finish(); });
        } catch (...) {
            workerError = std::current_exception();
        }
    });
    worker.join();
    if (workerError)
        std::rethrow_exception(workerError);
    CHECK(finished == 1 && !stop.result.isReady() && !channel.drained().isReady());
    until([&] { return channel.drained().isReady(); });
    CHECK(stop.result.isReady() && stop.result.tryResult()->hasValue());
    const auto outcome = stop.result.tryResult()->value();
    CHECK(outcome.status == CORDIS_OK && outcome.cancelRequested && outcome.json == "{\"cancelled\":true}");
    CHECK(!channel.snapshot().value().faulted && channel.snapshot().value().requests.empty());
    CHECK(foreign(source.ledger, [&] { return api.table.destroy(native); }) == CORDIS_OK);
    lease.reset();
    source.session.reset();
    CHECK(source.ledger.snapshot().records.empty() && manager.drain(generation).hasValue());
    CHECK(manager.snapshot().value()[0].resources.drained() && manager.snapshot().value()[0].nativePinned);
    scheduler->finish();
}
bool run(const QString& name)
{
    if (name == "lifecycle_cancel_stop")
        cancelledStop();
    else if (name == "lifecycle_pending_stop")
        pendingStop();
    else if (name == "lifecycle_stale_capture")
        captured(true, false);
    else if (name == "lifecycle_live_capture")
        captured(false, false);
    else if (name == "lifecycle_failed_capture")
        captured(true, true);
    else
        return false;
    return true;
}
} // namespace native_lifecycle_test
