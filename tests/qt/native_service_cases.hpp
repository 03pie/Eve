/** @file native_service_cases.hpp
 * @brief 真实 C DLL 验证生产服务桥、Core 元数据/域、旧代清理与跨模块排空。
 */
#pragma once
namespace native_service_test {
using namespace native_resource_test;
CordisBytesV1 span(const std::string& text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
struct Services : Resources {
    using Register = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, const CordisServiceKeyV1*,
                                                const CordisServiceMetadataV1*, CordisHandle, CordisHandle*);
    using Withdraw = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisRequestId*);
    using Invoke = CordisStatus(CORDIS_CALL*)(unsigned, const CordisServiceKeyV1*, uint32_t, CordisBytesV1,
                                              CordisBytesV1, CordisBytesV1, CordisRequestId*);
    Register reg;
    Withdraw withdraw;
    Invoke invoke;
    explicit Services(std::shared_ptr<IScheduler> scheduler = std::make_shared<FakeScheduler>(),
                      const QString& fixture = "services", const QString& folder = "module",
                      std::optional<ServiceRegistry> services = {})
        : Resources(std::move(scheduler), fixture, folder, std::move(services)),
          reg(entry<Register>(image, "cordis_service_register")),
          withdraw(entry<Withdraw>(image, "cordis_service_withdraw")),
          invoke(entry<Invoke>(image, "cordis_service_invoke"))
    {
    }
    CordisHandle provide(CordisHandle ownedEffect, CordisHandle token = 8, std::uint64_t realm = 1,
                         const std::string& name = "test.service", unsigned index = 0)
    {
        std::string capabilities = "[\"read\",\"\\u4e2d\"]";
        CordisServiceKeyV1 key{span(name), 1, 0, realm};
        CordisServiceMetadataV1 metadata{sizeof(CordisServiceMetadataV1), 2, span(capabilities)};
        CordisHandle binding = 0;
        CHECK(reg(index, ownedEffect, &key, &metadata, token, &binding) == CORDIS_OK && binding);
        return binding;
    }
    CordisStatus request(CordisRequestId& output, unsigned index = 1, std::uint64_t realm = 1, std::uint32_t minor = 0,
                         const std::string& caps = "[]", const std::string& payload = "{\"integer\":9007199254740993}",
                         const std::string& name = "test.service", const std::string& method = "echo")
    {
        CordisServiceKeyV1 key{span(name), 1, 0, realm};
        return invoke(index, &key, minor, span(caps), span(method), span(payload), &output);
    }
};
void call(CordisHandle token)
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    auto binding = s.provide(s.add(), token);
    (void)binding;
    s.pump();
    auto services = provider.snapshot().value().services;
    CHECK(services.size() == 1 && services[0].state == BindingState::Published && services[0].metadata.minor == 2);
    CHECK(services[0].metadata.capabilities.size() == 2 && services[0].metadata.capabilities[1] == "中");
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING && request && s.stat(0, 1) == 0 && s.stat(1, 0) == 0);
    s.pump();
    if (token == 7) {
        CHECK(s.stat(1, 0) == 0 && s.finishAll() == 1);
        s.pump();
    }
    CHECK(s.stat(0, 1) == 1 && s.stat(0, 6) && s.stat(0, 6) != request);
    CHECK(s.stat(1, 0) == 1 && s.stat(1, 5) == request && s.stat(1, 3) == CORDIS_OK);
    CHECK(s.result(1) == (token == 8 ? "" : "{\"integer\":9007199254740993}"));
    CHECK(consumer.snapshot().value().committedServices == 1);
    s.finish();
}
void publication(bool fail)
{
    Services s;
    auto provider = s.create(fail ? "\"start-fail\"" : "\"start-pending\"");
    auto started = provider.start();
    auto effect = s.add();
    s.provide(effect);
    auto consumer = s.active(2);
    CordisRequestId request = 44;
    CHECK(s.request(request) == CORDIS_E_NOT_FOUND && request == 0);
    CHECK(provider.snapshot().value().services[0].state == BindingState::Staged);
    if (!fail) {
        CHECK(s.finishAll() == 1);
        s.pump();
        CHECK(started.tryResult()->hasValue());
        CHECK(s.request(request) == CORDIS_PENDING);
        s.pump();
        CHECK(s.stat(0, 1) == 1);
    } else
        CHECK(!started.tryResult()->hasValue());
    s.finish();
}
void guards()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    auto effect = s.add();
    auto binding = s.provide(effect);
    s.pump();
    CordisRequestId request = 99;
    CHECK(s.request(request, 1, 1, 3) == CORDIS_E_UNSUPPORTED && request == 0);
    CHECK(s.request(request, 1, 1, 0, "[\"missing\"]") == CORDIS_E_UNSUPPORTED && request == 0);
    CHECK(s.request(request, 1, 1, 0, "[\"a\",\"\\u0061\"]") == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.request(request, 1, 1, 0, "{}") == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.request(request, 1, 1, 0, "[]", "broken") == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.request(request, 1, 1, 0, "[]", "{}", "test.service", "") == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.request(request, 1, 999999) == CORDIS_E_INVALID_ARGUMENT);
    CHECK(s.withdraw(1, binding, &request) == CORDIS_E_NOT_FOUND && request == 0);
    CordisHandle foreign = 99;
    std::string name = "test.service", caps = "[]";
    CordisServiceKeyV1 key{span(name), 1, 0, 1};
    CordisServiceMetadataV1 metadata{sizeof(CordisServiceMetadataV1), 0, span(caps)};
    CHECK(s.reg(1, effect, &key, &metadata, 8, &foreign) == CORDIS_E_INVALID_ARGUMENT && foreign == 0);
    CHECK(s.reg(0, effect, &key, &metadata, 8, &foreign) == CORDIS_E_BUSY && foreign == 0);
    key.reserved = 1;
    CHECK(s.reg(0, effect, &key, &metadata, 8, &foreign) == CORDIS_E_INVALID_ARGUMENT);
    key.reserved = 0;
    metadata.struct_size = 0;
    CHECK(s.reg(0, effect, &key, &metadata, 8, &foreign) == CORDIS_E_INVALID_ARGUMENT);
    CordisStatus wrong = CORDIS_OK;
    std::thread worker([&] { wrong = s.request(request); });
    worker.join();
    CHECK(wrong == CORDIS_E_WRONG_THREAD && request == 0);
    CHECK(s.request(request, 0) == CORDIS_E_BUSY); // 不能提交等待自身退出的服务环。
    s.finish();
}
void realms()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto realm = registry.createRealm().value();
    Services s(scheduler, "services", "module", registry);
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add(), 8, realm.value());
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_E_NOT_FOUND);
    CHECK(s.request(request, 1, realm.value(), 2, "[\"read\"]") == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(0, 1) == 1);
    ServiceRegistry other(scheduler);
    CHECK(!NativeModule::open(s.manager, s.generation, scheduler, other).hasValue());
    auto foreign = other.createRealm().value();
    CHECK(s.request(request, 1, foreign.value()) == CORDIS_E_INVALID_ARGUMENT);
    auto another = std::make_shared<FakeScheduler>();
    ServiceRegistry wrong(another);
    CHECK(!NativeModule::open(s.manager, s.generation, scheduler, wrong).hasValue());
    s.finish();
}
void withdraw()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2), future = s.active(3);
    auto binding = s.provide(s.add());
    s.add(11, 1);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    CordisRequestId withdrawn = 0;
    CHECK(s.withdraw(0, binding, &withdrawn) == CORDIS_PENDING && withdrawn && s.stat(0, 0) == 0);
    CHECK(s.request(request) == CORDIS_E_CLOSED);
    CHECK(s.request(request, 2) == CORDIS_E_NOT_FOUND);
    s.pump();
    CHECK(consumer.closed().tryResult()->value().quiescent);
    CHECK(s.stat(0, 0) == 1 && s.stat(0, 5) == withdrawn && s.stat(0, 7) == 0);
    CHECK(provider.snapshot().value().services.empty());
    s.finish();
}
void cleanup()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add(), 9);
    s.add(15, 1);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    auto closed = provider.close();
    s.pump();
    CHECK(!closed.isReady() && s.stat(0, 7) == 0 && s.stat(1, 2) == 1 && s.stat(0, 1) == 2);
    CHECK(provider.snapshot().value().services[0].state == BindingState::Retiring);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(closed.tryResult()->value().quiescent && consumer.closed().tryResult()->value().quiescent &&
          s.stat(0, 7) == 1);
    s.finish();
}
void cancel(bool entered)
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add(), 7);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    if (entered)
        s.pump();
    CHECK(s.cancel(1, request) == CORDIS_OK);
    s.pump();
    if (entered) {
        CHECK(s.stat(1, 0) == 0 && s.finishAll() == 1);
        s.pump();
    }
    CHECK(s.stat(0, 1) == (entered ? 1U : 0U) && static_cast<CordisStatus>(s.stat(1, 3)) == CORDIS_E_CANCELLED);
    s.finish();
}
void limits()
{
    Services s;
    auto provider = s.active();
    auto effect = s.add();
    for (int i = 0; i < 128; ++i)
        s.provide(effect, 8, 1, "service." + std::to_string(i));
    std::string name = "extra", caps = "[]";
    CordisServiceKeyV1 key{span(name), 1, 0, 1};
    CordisServiceMetadataV1 metadata{sizeof(CordisServiceMetadataV1), 0, span(caps)};
    CordisHandle binding = 88;
    CHECK(s.reg(0, effect, &key, &metadata, 8, &binding) == CORDIS_E_LIMIT && binding == 0);
    s.pump();
    s.finish();
}
void shared()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    Services first(scheduler, "services", "module", registry), second(scheduler, "services_peer", "peer", registry);
    auto provider = first.active(), consumer = second.active(2);
    first.provide(first.add(), 9);
    first.pump();
    CordisRequestId request = 0;
    CHECK(second.request(request, 0) == CORDIS_PENDING);
    first.pump();
    CHECK(first.stat(0, 1) == 1 && second.stat(0, 0) == 1 && second.stat(0, 5) == request);
    auto closed = first.module.close();
    first.pump();
    CHECK(closed.tryResult()->value().quiescent);
    CHECK(consumer.closed().tryResult()->value().quiescent);
    second.finish();
    CHECK(first.manager.snapshot().value()[0].resources.records.empty());
}
/** @brief 失败的宿主受理不应偷偷保留消费者绑定。 */
void rejectedBudget()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add());
    s.pump();
    for (int i = 0; i < 128; ++i)
        s.send(8, 1);
    CordisRequestId request = 77;
    CHECK(s.request(request) == CORDIS_E_LIMIT && request == 0);
    CHECK(consumer.snapshot().value().committedServices == 0);
    s.pump();
    CHECK(s.stat(0, 1) == 0 && s.stat(1, 0) == 128);
    s.finish();
}
/** @brief 提供者取消协议损坏必须隔离提供者和消费者并保留两端账目。 */
void cancelProtocol()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add(), 7);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    s.ack(0, CORDIS_PENDING);
    CHECK(s.cancel(1, request) == CORDIS_E_ABI_PROTOCOL);
    CHECK(provider.snapshot().value().uncertain && consumer.snapshot().value().uncertain);
    CHECK(!provider.closed().tryResult()->value().quiescent && !consumer.closed().tryResult()->value().quiescent);
    CHECK(!s.state().resources.records.empty());
}
/** @brief 服务必须同时等到真实 effect ready 和 start 成功，失败 setup 永不发布。 */
void effectBarrier()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    auto effect = s.add(11, 0, false);
    s.provide(effect);
    CordisRequestId request = 55;
    CHECK(s.request(request) == CORDIS_E_NOT_FOUND && !request);
    s.pump();
    CHECK(provider.snapshot().value().services[0].state == BindingState::Staged);
    CHECK(s.ready(0, effect, CORDIS_OK) == CORDIS_OK);
    CHECK(s.request(request) == CORDIS_E_NOT_FOUND); // Core ready 尚未收敛。
    s.pump();
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    auto failed = s.add(11, 0, false);
    s.provide(failed, 8, 1, "test.failed");
    CHECK(s.ready(0, failed, CORDIS_E_FAILED) == CORDIS_OK);
    s.pump();
    CHECK(s.request(request, 1, 1, 0, "[]", "{}", "test.failed") == CORDIS_E_NOT_FOUND);
    s.finish();
}
/** @brief Core 的真实 committed 依赖图拒绝两实例互等；清理不新增绑定。 */
void cycle()
{
    Services s;
    auto first = s.active(), second = s.active(2);
    s.provide(s.add(), 8, 1, "first", 0);
    s.provide(s.add(16, 1), 8, 1, "second", 1);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request, 1, 1, 0, "[]", "{}", "first") == CORDIS_PENDING);
    s.pump();
    CHECK(s.request(request, 0, 1, 0, "[]", "{}", "second") == CORDIS_E_BUSY && !request);
    s.finish();
}
/** @brief ACK 之后调度器仍保留的消费者投递副本阻止两端提前静止。 */
void retirement()
{
    auto scheduler = std::make_shared<CaptureScheduler>();
    Services s(scheduler);
    auto provider = s.create("{}"), consumer = s.create("{}", 2);
    auto first = provider.start(), second = consumer.start();
    CHECK(scheduler->inner.runUntilIdle().hasValue() && first.tryResult()->hasValue() &&
          second.tryResult()->hasValue());
    s.provide(s.add(), 9);
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    scheduler->capture = true;
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    CHECK(scheduler->inner.runUntilIdle().hasValue() && s.stat(1, 0) == 1);
    auto stopped = s.module.close();
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    CHECK(!stopped.isReady() && s.stat(0, 7) == 0);
    std::thread worker([&] { scheduler->retained.reset(); });
    worker.join();
    CHECK(scheduler->inner.runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && s.state().resources.records.empty());
}
/** @brief 消费者清理失败不得释放旧绑定或允许提供者停止。 */
void failedCleanup()
{
    Services s;
    auto provider = s.active(), consumer = s.active(2);
    s.provide(s.add());
    s.add(13, 1);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    auto closed = provider.close();
    s.pump();
    CHECK(!closed.isReady() && s.stat(0, 7) == 0 && !consumer.closed().tryResult()->value().quiescent);
    CHECK(provider.snapshot().value().services[0].committedLeases == 1 && !s.state().resources.records.empty());
}
bool run(const QString& name)
{
    if (name == "service_sync")
        call(8);
    else if (name == "service_early")
        call(9);
    else if (name == "service_async")
        call(7);
    else if (name == "service_publication")
        publication(false);
    else if (name == "service_start_failure")
        publication(true);
    else if (name == "service_guards")
        guards();
    else if (name == "service_realms")
        realms();
    else if (name == "service_withdraw")
        withdraw();
    else if (name == "service_cleanup")
        cleanup();
    else if (name == "service_cancel_queued")
        cancel(false);
    else if (name == "service_cancel_pending")
        cancel(true);
    else if (name == "service_limits")
        limits();
    else if (name == "service_shared")
        shared();
    else if (name == "service_budget")
        rejectedBudget();
    else if (name == "service_cancel_protocol")
        cancelProtocol();
    else if (name == "service_effect_barrier")
        effectBarrier();
    else if (name == "service_cycle")
        cycle();
    else if (name == "service_retire")
        retirement();
    else if (name == "service_cleanup_failure")
        failedCleanup();
    else
        return false;
    return true;
}
} // namespace native_service_test
