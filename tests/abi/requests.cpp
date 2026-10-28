/** @file requests.cpp
 * @brief 生产请求仲裁、投递退役、复制与失败保留；显式 gate，不依赖 sleep。
 */
#include "../../src/module/json_p.hpp"
#include "../../src/module/request_p.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cordis/testing/fake_scheduler.hpp>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cordis;
using namespace cordis::module;
using namespace cordis::module::detail;
using cordis::testing::FakeScheduler;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
CordisBytesV1 bytes(const std::string& value)
{
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    GenerationLedger ledger;
    RequestChannel channel{scheduler, ledger, InstanceId{1}, 1};
    NativeTicket begin(NativeRequestKind kind = NativeRequestKind::Start)
    {
        return channel.begin(kind, "test").value();
    }
    void pump() { CHECK(scheduler->runUntilIdle().hasValue()); }
    CordisStatus complete(const NativeTicket& ticket, const std::string& json = "{}", CordisStatus status = CORDIS_OK)
    {
        return channel.complete(channel.context(), ticket.id, status, bytes(json));
    }
};
void sync()
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.ledger.snapshot().records.size() == 2);
    CHECK(s.channel.returned(ticket.id, CORDIS_OK).hasValue());
    CHECK(!ticket.result.isReady());
    CHECK(s.channel.seal().hasValue() && !s.channel.drained().isReady());
    s.pump();
    CHECK(ticket.result.tryResult()->value().status == CORDIS_OK && s.channel.drained().isReady());
    CHECK(s.ledger.snapshot().records.empty());
}
void fence()
{
    Scene s;
    CHECK(s.channel.close().hasValue());
    auto stop = s.begin(NativeRequestKind::Stop);
    CHECK(!s.channel.drained().isReady());
    CHECK(s.channel.returned(stop.id, CORDIS_OK).hasValue());
    s.pump();
    CHECK(!s.channel.drained().isReady());
    auto cleanupTicket = s.begin(NativeRequestKind::Cleanup);
    CHECK(s.channel.seal().hasValue());
    CHECK(!s.channel.begin(NativeRequestKind::Stop, "after-seal").hasValue());
    CHECK(!s.channel.drained().isReady());
    CHECK(s.channel.returned(cleanupTicket.id, CORDIS_OK).hasValue());
    s.pump();
    CHECK(s.channel.drained().isReady() && s.channel.snapshot().value().sealed);
    CHECK(s.channel.close().hasValue() && s.channel.seal().hasValue());
    CHECK(!s.channel.begin(NativeRequestKind::Cleanup, "retired").hasValue());
}
void pending()
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    std::string input = "{\"large\":9007199254740993}";
    const auto original = input;
    CordisStatus status = CORDIS_E_FAILED;
    std::thread worker([&] { status = s.complete(ticket, input); });
    worker.join();
    input.assign(input.size(), 'x');
    CHECK(status == CORDIS_OK && !ticket.result.isReady());
    CHECK(s.ledger.snapshot().records.size() == 2);
    s.pump();
    CHECK(ticket.result.tryResult()->value().json == original);
    CHECK(s.ledger.snapshot().records.empty() && s.channel.snapshot().value().bufferedBytes == 0);
}
void early()
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.complete(ticket, "[1]") == CORDIS_OK);
    CHECK(!ticket.result.isReady() && s.scheduler->pending() == 0 && s.ledger.snapshot().records.size() == 3);
    CHECK(s.channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    s.pump();
    CHECK(ticket.result.tryResult()->value().json == "[1]");
    CHECK(s.ledger.snapshot().records.empty());
}
void race()
{
    Scene s;
    auto ticket = s.begin();
    std::promise<void> go;
    auto gate = go.get_future().share();
    std::array<CordisStatus, 2> results{};
    std::array<std::thread, 2> workers;
    for (std::size_t i = 0; i < workers.size(); ++i)
        workers[i] = std::thread([&, i] {
            gate.wait();
            results[i] = s.complete(ticket, std::to_string(i));
        });
    go.set_value();
    const bool recorded = s.channel.returned(ticket.id, CORDIS_PENDING).hasValue();
    for (auto& worker : workers)
        worker.join();
    CHECK(recorded);
    CHECK((results[0] == CORDIS_OK) != (results[1] == CORDIS_OK));
    CHECK(results[0] == CORDIS_E_ABI_PROTOCOL || results[1] == CORDIS_E_ABI_PROTOCOL);
    s.pump();
    CHECK(ticket.result.tryResult()->hasValue());
    CHECK(s.channel.snapshot().value().protocolErrors == 1 && s.ledger.snapshot().records.empty());
}
void cancel()
{
    Scene s;
    for (unsigned i = 0; i < 32; ++i) {
        auto ticket = s.begin();
        std::promise<void> go;
        auto gate = go.get_future();
        CordisStatus accepted = CORDIS_E_FAILED;
        std::thread worker([&] {
            gate.wait();
            accepted = s.complete(ticket, "true");
        });
        go.set_value();
        const bool recorded = s.channel.returned(ticket.id, CORDIS_PENDING).hasValue();
        const auto cancelled = s.channel.cancel(ticket.id);
        worker.join();
        CHECK(recorded && cancelled.hasValue() && cancelled.value() && accepted == CORDIS_OK);
        CHECK(!ticket.result.isReady());
        s.pump();
        CHECK(ticket.result.tryResult()->value().cancelRequested);
        CHECK(ticket.result.tryResult()->value().status == CORDIS_OK);
    }
    CHECK(s.channel.seal().hasValue() && s.channel.drained().isReady());
}
void protocol()
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.complete(ticket) == CORDIS_OK);
    CHECK(s.channel.returned(ticket.id, CORDIS_OK).hasValue());
    s.pump();
    CHECK(!ticket.result.tryResult()->hasValue() && ticket.result.tryResult()->error().code == ErrorCode::AbiProtocol);
    CHECK(s.channel.snapshot().value().requests[0].unsafe);
    CHECK(s.channel.seal().hasValue() && !s.channel.drained().isReady());
    CHECK(!s.ledger.snapshot().records.empty() && !s.ledger.snapshot().pinReasons.empty());
    Scene unknown;
    auto second = unknown.begin();
    CHECK(unknown.channel.returned(second.id, 23).hasValue());
    unknown.pump();
    CHECK(second.result.tryResult()->error().code == ErrorCode::AbiProtocol);
    CHECK(!unknown.channel.begin(NativeRequestKind::Invoke, "bad").hasValue());
}
void input()
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    CHECK(s.complete(ticket, "{}", CORDIS_PENDING) == CORDIS_E_ABI_PROTOCOL);
    CHECK(s.complete(ticket, "{bad}") == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.channel.complete(s.channel.context(), ticket.id, CORDIS_OK, {nullptr, 1}) == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.channel.complete(s.channel.context(), ticket.id, CORDIS_OK, {nullptr, UINT64_MAX}) ==
          CORDIS_E_INVALID_ARGUMENT);
    std::string tooLarge(1024 * 1024 + 1, ' ');
    CHECK(s.complete(ticket, tooLarge) == CORDIS_E_LIMIT);
    CHECK(!ticket.result.isReady() && s.channel.snapshot().value().bufferedBytes == 0);
    CHECK(s.complete(ticket, "{\"error\":true}", CORDIS_E_FAILED) == CORDIS_OK);
    s.pump();
    CHECK(ticket.result.tryResult()->value().status == CORDIS_E_FAILED);
    CHECK(s.complete(ticket) == CORDIS_E_ABI_PROTOCOL);
    CHECK(s.channel.snapshot().value().requests.empty());
}
void quota()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    GenerationLedger ledger;
    RequestChannel channel(scheduler, ledger, InstanceId{1}, 1, {2, 8, 8});
    auto a = channel.begin(NativeRequestKind::Invoke, "one").value();
    auto b = channel.begin(NativeRequestKind::Invoke, "two").value();
    CHECK(!channel.begin(NativeRequestKind::Invoke, "over").hasValue());
    CHECK(channel.returned(a.id, CORDIS_PENDING).hasValue() && channel.returned(b.id, CORDIS_PENDING).hasValue());
    CHECK(channel.complete(channel.context(), a.id, CORDIS_OK, bytes("\"abcd\"")) == CORDIS_OK);
    CHECK(channel.complete(channel.context(), b.id, CORDIS_OK, bytes("\"efgh\"")) == CORDIS_E_LIMIT);
    CHECK(channel.snapshot().value().bufferedBytes == 6);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(channel.complete(channel.context(), b.id, CORDIS_OK, bytes("\"efgh\"")) == CORDIS_OK);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto c = channel.begin(NativeRequestKind::Start, "new").value();
    CHECK(c.id != a.id && c.id != b.id);
    CHECK(channel.returned(c.id, CORDIS_OK).hasValue() && channel.seal().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue() && channel.drained().isReady());
}
void closing()
{
    Scene s;
    auto start = s.begin();
    CHECK(s.channel.returned(start.id, CORDIS_PENDING).hasValue() && s.channel.close().hasValue());
    CHECK(!s.channel.begin(NativeRequestKind::Invoke, "closed").hasValue());
    auto stop = s.begin(NativeRequestKind::Stop);
    CHECK(s.channel.returned(stop.id, CORDIS_OK).hasValue());
    s.pump();
    CHECK(!s.channel.drained().isReady() && !start.result.isReady());
    CHECK(s.channel.seal().hasValue());
    CHECK(s.complete(start, "", CORDIS_E_CANCELLED) == CORDIS_OK);
    s.pump();
    CHECK(s.channel.drained().isReady() && s.ledger.snapshot().records.empty());
}
void deliveryFailure(bool accepted)
{
    Scene s;
    auto ticket = s.begin();
    CHECK(s.channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    if (accepted)
        CHECK(s.complete(ticket) == CORDIS_OK);
    std::thread worker([&] { s.scheduler->close(); });
    worker.join();
    if (!accepted)
        CHECK(s.complete(ticket) == CORDIS_OK);
    CHECK(ticket.result.isReady() && ticket.result.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(s.channel.snapshot().value().requests[0].deliveryBlocked);
    CHECK(s.channel.seal().hasValue() && !s.channel.drained().isReady());
    CHECK(!s.ledger.snapshot().records.empty());
}
struct ThrowScheduler : IScheduler {
    FakeScheduler inner;
    bool post(ScheduledJob) override { throw std::runtime_error("post failed"); }
    bool isCurrentThread() const noexcept override { return inner.isCurrentThread(); }
    bool isOpen() const override { return true; }
};
void throwing()
{
    auto scheduler = std::make_shared<ThrowScheduler>();
    GenerationLedger ledger;
    RequestChannel channel(scheduler, ledger, InstanceId{1}, 1);
    auto ticket = channel.begin(NativeRequestKind::Invoke, "throw").value();
    CHECK(channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    CHECK(channel.complete(channel.context(), ticket.id, CORDIS_OK, {}) == CORDIS_OK);
    CHECK(ticket.result.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(!ledger.snapshot().records.empty());
}
struct CopyScheduler : IScheduler {
    FakeScheduler inner;
    std::promise<void> copied, release;
    std::shared_future<void> gate = release.get_future().share();
    std::atomic_bool block{true};
    bool post(ScheduledJob job) override
    {
        const bool accepted = inner.post(job); // 刻意保留生产者侧 std::function 副本。
        if (block.exchange(false)) {
            copied.set_value();
            gate.wait();
        }
        return accepted;
    }
    bool isCurrentThread() const noexcept override { return inner.isCurrentThread(); }
    bool isOpen() const override { return inner.isOpen(); }
};
void retirement()
{
    auto scheduler = std::make_shared<CopyScheduler>();
    GenerationLedger ledger;
    RequestChannel channel(scheduler, ledger, InstanceId{1}, 1);
    auto ticket = channel.begin(NativeRequestKind::Invoke, "copy").value();
    CHECK(channel.returned(ticket.id, CORDIS_PENDING).hasValue());
    auto copied = scheduler->copied.get_future();
    CordisStatus result = CORDIS_E_FAILED;
    std::thread worker([&] { result = channel.complete(channel.context(), ticket.id, CORDIS_OK, bytes("null")); });
    try {
        CHECK(copied.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        CHECK(scheduler->inner.runOne().hasValue());
        CHECK(ticket.result.isReady() && !ledger.snapshot().records.empty());
        const auto lateCancel = channel.cancel(ticket.id);
        CHECK(lateCancel.hasValue() && !lateCancel.value());
        CHECK(channel.seal().hasValue() && !channel.drained().isReady());
    } catch (...) {
        scheduler->release.set_value();
        worker.join();
        throw;
    }
    scheduler->release.set_value();
    worker.join();
    CHECK(result == CORDIS_OK && !channel.drained().isReady());
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    CHECK(channel.drained().isReady() && ledger.snapshot().records.empty());
}
void abandoned()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    GenerationLedger ledger;
    Promise<NativeOutcome> placeholder;
    auto task = placeholder.task();
    {
        RequestChannel channel(scheduler, ledger, InstanceId{1}, 1);
        task = channel.begin(NativeRequestKind::Invoke, "orphan").value().result;
    }
    CHECK(!task.isReady() && ledger.snapshot().records.size() == 2);
    CHECK(!ledger.snapshot().pinReasons.empty());
}
void threads()
{
    Scene s;
    auto ticket = s.begin();
    bool rejected = false;
    std::thread worker([&] {
        rejected = !s.channel.begin(NativeRequestKind::Invoke, "wrong").hasValue() &&
                   !s.channel.returned(ticket.id, CORDIS_OK).hasValue() && !s.channel.cancel(ticket.id).hasValue() &&
                   !s.channel.close().hasValue() && !s.channel.seal().hasValue() && !s.channel.snapshot().hasValue();
    });
    worker.join();
    CHECK(rejected && s.channel.snapshot().value().requests.size() == 1);
    Scene newer;
    CHECK(newer.channel.context() != s.channel.context());
    CHECK(newer.channel.complete(s.channel.context(), ticket.id, CORDIS_OK, {}) == CORDIS_E_ABI_PROTOCOL);
    CHECK(s.channel.returned(ticket.id, CORDIS_OK).hasValue());
    s.pump();
}
void json(bool valid)
{
    const std::vector<std::string> accepted{"null",
                                            "true",
                                            "false",
                                            "0",
                                            "-0",
                                            "1e9999",
                                            "9007199254740993",
                                            "-1.25E+3",
                                            "[]",
                                            "{}",
                                            "{\"x\":[1,{},false]}",
                                            "\"\\u0000\\uD83D\\uDE00\"",
                                            "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"",
                                            "\"\xe4\xb8\xad\xf0\x9f\x98\x80\"",
                                            " \r\n\t42 \r\n",
                                            std::string(64, '[') + "0" + std::string(64, ']')};
    const std::vector<std::string> rejected{"",
                                            " ",
                                            "01",
                                            "+1",
                                            "--1",
                                            "1.",
                                            ".1",
                                            "1e",
                                            "1e+",
                                            "NaN",
                                            "Infinity",
                                            "true false",
                                            "[1,]",
                                            "{\"x\":}",
                                            "{\"x\" 1}",
                                            "{1:2}",
                                            "{\"x\":1,}",
                                            "[}",
                                            "\"\\uD800\"",
                                            "\"\\uDC00\"",
                                            "\"\\uD800\\u0041\"",
                                            "\"\\x20\"",
                                            "\"\\uZZZZ\"",
                                            "\"\xc0\xaf\"",
                                            "\"\xed\xa0\x80\"",
                                            "\"\xf4\x90\x80\x80\"",
                                            "\"\xe4\xb8\"",
                                            "\xef\xbb\xbf{}",
                                            std::string("\"\0\"", 3),
                                            std::string(65, '[') + "0" + std::string(65, ']')};
    for (const auto& value : valid ? accepted : rejected)
        CHECK(validAbiJson(value) == valid);
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "fence")
            fence();
        else if (name == "sync")
            sync();
        else if (name == "pending")
            pending();
        else if (name == "early")
            early();
        else if (name == "race")
            race();
        else if (name == "cancel")
            cancel();
        else if (name == "protocol")
            protocol();
        else if (name == "input")
            input();
        else if (name == "quota")
            quota();
        else if (name == "closing")
            closing();
        else if (name == "reject")
            deliveryFailure(false);
        else if (name == "discard")
            deliveryFailure(true);
        else if (name == "throw")
            throwing();
        else if (name == "retire")
            retirement();
        else if (name == "abandoned")
            abandoned();
        else if (name == "threads")
            threads();
        else if (name == "json_valid")
            json(true);
        else if (name == "json_invalid")
            json(false);
        else
            throw std::runtime_error("unknown request case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
