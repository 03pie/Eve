/** @file native_service_cases.hpp
 * @brief 同一真实 C DLL 的独立 Core 实例对服务桥、旧绑定与清理顺序的联合检查。
 */
#pragma once

namespace native_service_test {
using namespace native_resource_test;
struct Services : Resources {
    using Invoke = CordisStatus(CORDIS_CALL*)(unsigned, uint32_t, uint64_t, uint32_t, CordisBytesV1, CordisBytesV1,
                                              CordisBytesV1, CordisRequestId*);
    using Withdraw = CordisStatus(CORDIS_CALL*)(unsigned, CordisHandle, CordisRequestId*);
    using Handle = CordisHandle(CORDIS_CALL*)(unsigned, unsigned);
    Invoke invoke;
    Withdraw withdraw;
    Handle handle;
    std::shared_ptr<ServiceRegistry> registry;
    std::vector<Instance> instances;
    Services()
        : Resources(std::make_shared<FakeScheduler>(), "services"),
          invoke(entry<Invoke>(image, "cordis_service_invoke")),
          withdraw(entry<Withdraw>(image, "cordis_service_withdraw")),
          handle(entry<Handle>(image, "cordis_service_handle")), registry(std::make_shared<ServiceRegistry>(scheduler))
    {
    }
    NativePlugin definition(bool provider)
    {
        NativePlugin result{"org.cordis.plugin", {}, {}};
        if (provider)
            result.outputs.push_back({{"native.echo", 1}, true, {2, {"echo", "utf8"}}});
        else
            result.dependencies.push_back({{"native.echo", 1}, 2, {"echo"}});
        return result;
    }
    Instance mount(bool provider, const std::string& config = {}, RealmMap realms = {})
    {
        auto runtime = module.runtime(definition(provider)).value();
        auto instance =
            Instance::mount(runtime, scheduler, config.empty() ? (provider ? "\"provider\"" : "\"consumer\"") : config,
                            registry, realms);
        instances.push_back(instance);
        return instance;
    }
    CordisStatus send(CordisRequestId& request, const std::string& method = "echo",
                      const std::string& json = "{\"v\":1}", const std::string& caps = "[]", uint32_t minor = 2,
                      uint32_t major = 1, uint64_t realm = 1, unsigned index = 1)
    {
        auto bytes = [](const std::string& value) {
            return CordisBytesV1{reinterpret_cast<const uint8_t*>(value.data()), value.size()};
        };
        return invoke(index, major, realm, minor, bytes(caps), bytes(method), bytes(json), &request);
    }
    void finishManaged()
    {
        std::vector<Task<CleanupReport>> reports;
        for (const auto& instance : instances)
            reports.push_back(instance.stop());
        pump();
        unsigned turns = 0;
        while (finishAll()) {
            CHECK(++turns < 8);
            pump();
        }
        for (const auto& report : reports)
            CHECK(report.isReady() && report.tryResult()->value().quiescent);
        CHECK(registry->snapshot().value().empty());
        Scene::finish();
    }
};
void flow(const std::string& method)
{
    Services s;
    auto provider = s.mount(true);
    s.pump();
    CHECK(provider.ready().tryResult()->hasValue());
    auto consumer = s.mount(false);
    s.pump();
    CHECK(consumer.ready().tryResult()->hasValue());
    auto rejected = s.module.close();
    CHECK(rejected.tryResult()->error().code == ErrorCode::ContextClosing && !s.module.snapshot().value().closing);
    std::string payload = "{\"integer\":9007199254740993}";
    CordisRequestId request = 0;
    CHECK(s.send(request, method, payload) == CORDIS_PENDING && request);
    payload.assign(payload.size(), 'x');
    CHECK(s.stat(0, 1) == 0 && s.stat(1, 0) == 0);
    s.pump();
    if (method == "echo") {
        CHECK(s.stat(0, 4) && s.stat(0, 4) != request && s.stat(1, 0) == 0);
        unsigned count = 0;
        std::thread worker([&] { count = s.finishAll(); });
        worker.join();
        CHECK(count == 1);
        s.pump();
    }
    CHECK(s.stat(1, 0) == 1 && s.stat(1, 5) == request);
    CHECK(static_cast<CordisStatus>(s.stat(1, 3)) == (method == "missing" ? CORDIS_E_NOT_FOUND : CORDIS_OK));
    CHECK(method == "missing" || s.result(1) == "{\"integer\":9007199254740993}");
    CHECK(!s.module.snapshot().value().faulted);
    s.finishManaged();
}
void cancellation(bool closing)
{
    Services s;
    s.mount(true);
    s.pump();
    auto consumer = s.mount(false);
    s.pump();
    CordisRequestId request = 0;
    CHECK(s.send(request) == CORDIS_PENDING);
    s.pump();
    std::optional<Task<CleanupReport>> stopped;
    if (closing)
        stopped = consumer.stop();
    else
        CHECK(s.cancel(1, request) == CORDIS_OK);
    s.pump();
    CHECK(s.stat(1, 0) == 0 && (!stopped || !stopped->isReady()));
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(static_cast<CordisStatus>(s.stat(1, 3)) == CORDIS_E_CANCELLED && s.stat(1, 0) == 1);
    if (stopped)
        CHECK(stopped->tryResult()->value().quiescent);
    CHECK(!s.module.snapshot().value().faulted);
    s.finishManaged();
}
void cleanupOldBinding()
{
    Services s;
    auto provider = s.mount(true);
    s.pump();
    s.mount(false, "\"consumer-cleanup\"");
    s.pump();
    auto stopped = provider.stop();
    s.pump();
    CHECK(!stopped.isReady() && s.stat(0, 4) != 0 && s.stat(1, 2) == 1);
    CHECK(s.registry->snapshot().value()[0].state == BindingState::Retiring);
    CHECK(!s.registry->find<NativeService>({"native.echo", 1}).hasValue());
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent && s.stat(1, 0) == 1);
    CHECK(s.result(1) == "{\"cleanup\":true}" && !s.module.snapshot().value().faulted);
    s.finishManaged();
}
void withdrawal()
{
    Services s;
    auto provider = s.mount(true);
    s.pump();
    s.mount(false);
    s.pump();
    CordisRequestId request = 99;
    const auto binding = s.handle(0, 0);
    CHECK(s.withdraw(1, binding, &request) == CORDIS_E_NOT_FOUND && request == 0);
    CHECK(s.withdraw(0, binding, &request) == CORDIS_PENDING && request);
    s.pump();
    CHECK(s.stat(0, 0) == 1 && s.stat(0, 5) == request);
    CHECK(s.registry->snapshot().value()[0].state == BindingState::Retiring);
    CHECK(provider.snapshot().value().state == InstanceState::Active);
    CHECK(s.close(0, s.handle(0, 1), &request) == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(0, 2) == 1 && s.stat(0, 0) == 2);
    s.finishManaged();
}
void guards()
{
    Services s;
    s.mount(true);
    s.pump();
    s.mount(false);
    s.pump();
    CordisRequestId request = 99;
    CHECK(s.send(request, "early", "{}", "[]", 3) == CORDIS_E_UNSUPPORTED && !request);
    CHECK(s.send(request, "early", "{}", "[\"missing\"]") == CORDIS_E_UNSUPPORTED && !request);
    CHECK(s.send(request, "early", "{}", "[\"echo\",\"\\u0065cho\"]") == CORDIS_E_INVALID_ARGUMENT && !request);
    CHECK(s.send(request, "early", "{}", "[]", 2, 0) == CORDIS_E_INVALID_ARGUMENT && !request);
    CHECK(s.send(request, "early", "{}", "[]", 2, 1, 0) == CORDIS_E_INVALID_ARGUMENT && !request);
    CHECK(s.send(request, "early", "{}", "[]", 2, 2) == CORDIS_E_NOT_FOUND && !request);
    CordisStatus wrong = CORDIS_OK;
    std::thread worker([&] { wrong = s.send(request); });
    worker.join();
    CHECK(wrong == CORDIS_E_WRONG_THREAD && !request);
    CHECK(s.send(request, "early", "{}", "[\"\\u0065cho\"]") == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(1, 0) == 1 && s.stat(1, 3) == CORDIS_OK);
    s.finishManaged();
}
void realms()
{
    Services s;
    const auto realm = s.registry->createRealm().value();
    auto view = RealmMap{}.isolate("native.echo", realm).value();
    auto provider = s.mount(true, {}, view);
    s.pump();
    CHECK(provider.ready().tryResult()->hasValue() && s.registry->snapshot().value()[0].key.realm == realm);
    auto missing = s.mount(false);
    s.pump();
    CHECK(missing.snapshot().value().state == InstanceState::Pending);
    auto consumer = s.mount(false, {}, view);
    s.pump();
    CHECK(consumer.ready().tryResult()->hasValue());
    CordisRequestId request = 0;
    CHECK(s.send(request, "early") == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(1, 0) == 1 && s.stat(1, 3) == CORDIS_OK);
    s.finishManaged();
}
void readiness(bool cancel)
{
    Services s;
    auto provider = s.mount(true, "\"setup-provider\"");
    s.pump();
    CHECK(!provider.ready().isReady() && s.registry->snapshot().value()[0].state == BindingState::Staged);
    if (cancel) {
        auto stopped = provider.stop();
        s.pump();
        CHECK(stopped.isReady() && stopped.tryResult()->value().quiescent && !s.module.snapshot().value().faulted);
    } else {
        CHECK(s.ready(0, s.handle(0, 1), CORDIS_OK) == CORDIS_OK);
        s.pump();
        CHECK(provider.ready().tryResult()->hasValue() &&
              s.registry->snapshot().value()[0].state == BindingState::Published);
    }
    s.finishManaged();
}
void rollback()
{
    Services s;
    auto provider = s.mount(true, "\"failed-provider\"");
    s.pump();
    CHECK(provider.ready().tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(s.registry->snapshot().value().empty() && s.module.snapshot().value().instances.empty());
    CHECK(!s.module.snapshot().value().faulted);
    auto mismatch = s.definition(true);
    mismatch.outputs[0].metadata.minor = 3;
    auto runtime = s.module.runtime(mismatch).value();
    auto rejected = Instance::mount(runtime, s.scheduler, "\"provider\"", s.registry);
    s.instances.push_back(rejected);
    s.pump();
    CHECK(rejected.ready().tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(s.registry->snapshot().value().empty() && !s.module.snapshot().value().faulted);
    s.finishManaged();
}
void schedulerGuard()
{
    Services s;
    auto other = std::make_shared<FakeScheduler>();
    auto runtime = s.module.runtime(s.definition(true)).value();
    auto registry = std::make_shared<ServiceRegistry>(other);
    auto instance = Instance::mount(runtime, other, "\"provider\"", registry);
    CHECK(other->runUntilIdle().hasValue() && !instance.ready().tryResult()->hasValue());
    CHECK(s.module.snapshot().value().instances.empty());
    auto stopped = instance.stop();
    CHECK(other->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    s.finishManaged();
}
void rejectedVersionLease()
{
    Services s;
    s.mount(true);
    s.pump();
    auto definition = s.definition(false);
    definition.dependencies.clear();
    auto runtime = s.module.runtime(definition).value();
    auto consumer = Instance::mount(runtime, s.scheduler, "\"consumer\"", s.registry);
    s.instances.push_back(consumer);
    s.pump();
    CHECK(consumer.snapshot().value().committedLeases == 0);
    CordisRequestId request = 99;
    CHECK(s.send(request, "early", "{}", "[]", 3) == CORDIS_E_UNSUPPORTED && request == 0);
    const auto retained = consumer.snapshot().value().committedLeases;
    s.finishManaged();
    CHECK(retained == 0);
}
void consumerBudget()
{
    Services s;
    auto invalid = s.definition(false);
    invalid.dependencies[0].key.name.assign(129, 'x');
    CHECK(s.module.runtime(invalid).error().code == ErrorCode::InvalidArgument);
    invalid = s.definition(false);
    const auto repeated = invalid.dependencies[0];
    invalid.dependencies.assign(129, repeated);
    CHECK(s.module.runtime(invalid).error().code == ErrorCode::InvalidArgument);
    std::vector<ProviderHandle> providers;
    for (unsigned i = 0; i < 129; ++i) {
        auto object = std::make_shared<NativeService>([](std::string, std::string) {
            Promise<NativeServiceResult> result;
            result.resolve({CORDIS_OK, "{}"});
            return NativeServiceCall{result.task(), [] { return Result<void>::success(); }};
        });
        auto provider =
            s.registry->stage<NativeService>({"budget." + std::to_string(i), 1}, std::move(object), {2, {}}).value();
        CHECK(provider.publish().hasValue());
        providers.push_back(provider);
    }
    auto definition = s.definition(false);
    definition.dependencies.clear();
    auto runtime = s.module.runtime(definition).value();
    auto consumer = Instance::mount(runtime, s.scheduler, "\"consumer\"", s.registry);
    s.instances.push_back(consumer);
    s.pump();
    using Named = CordisStatus(CORDIS_CALL*)(unsigned, CordisBytesV1, CordisRequestId*);
    auto named = entry<Named>(s.image, "cordis_service_invoke_named");
    for (unsigned i = 0; i < 128; ++i) {
        auto name = "budget." + std::to_string(i);
        CordisRequestId request = 0;
        CHECK(named(0, {reinterpret_cast<const uint8_t*>(name.data()), name.size()}, &request) == CORDIS_PENDING &&
              request);
        s.pump();
    }
    const std::string overflow = "budget.128";
    CordisRequestId request = 99;
    const auto status = named(0, {reinterpret_cast<const uint8_t*>(overflow.data()), overflow.size()}, &request);
    const auto count = consumer.snapshot().value().committedLeases;
    auto stopped = consumer.stop();
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    for (const auto& provider : providers)
        CHECK(provider.retire().hasValue() && provider.whenUnused().isReady() && provider.release().hasValue());
    s.finishManaged();
    CHECK(status == CORDIS_E_LIMIT && request == 0 && count == 128);
}
void incomingBudget(bool ack = false)
{
    Services s;
    s.mount(true);
    s.pump();
    for (unsigned i = 0; i < 3; ++i) {
        s.mount(false);
        s.pump();
    }
    for (unsigned client = 1; client <= 2; ++client)
        for (unsigned i = 0; i < 128; ++i) {
            CordisRequestId request = 0;
            CHECK(s.send(request, "hold", "{}", "[]", 2, 1, 1, client) == CORDIS_PENDING);
        }
    CordisRequestId request = 0;
    CHECK(s.send(request, "hold", "{}", "[]", 2, 1, 1, 3) == CORDIS_PENDING);
    s.pump();
    CHECK(!s.module.snapshot().value().faulted);
    CHECK(s.stat(0, 1) == 256 && s.registry->snapshot().value()[0].activeCalls == 256);
    CHECK(s.stat(3, 0) == 1 && static_cast<CordisStatus>(s.stat(3, 3)) == CORDIS_E_LIMIT);
    if (ack) {
        s.Resources::send(8, 0, "{}");
        s.pump();
        CHECK(!s.module.snapshot().value().faulted);
        CHECK(s.stat(0, 0) == 1 && static_cast<CordisStatus>(s.stat(0, 3)) == CORDIS_E_LIMIT);
    }
    CHECK(s.finishAll() == 256);
    s.pump();
    CHECK(s.stat(1, 0) == 128 && s.stat(2, 0) == 128);
    CHECK(s.send(request, "early", "{}", "[]", 2, 1, 1, 3) == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(3, 0) == 2 && s.stat(3, 3) == CORDIS_OK);
    s.finishManaged();
}
void staleDispatch()
{
    Services s;
    s.mount(true);
    s.pump();
    auto consumer = s.mount(false);
    s.pump();
    s.Resources::send(8, 1, "{}");
    auto stopped = consumer.stop();
    s.pump();
    const auto calls = s.stat(1, 1);
    const auto status = static_cast<CordisStatus>(s.stat(1, 3));
    CHECK(stopped.tryResult()->value().quiescent);
    s.finishManaged();
    CHECK(calls == 0 && status == CORDIS_E_CANCELLED);
}
void generationFault()
{
    Services s;
    auto realm = s.registry->createRealm().value();
    s.mount(true);
    s.pump();
    s.mount(true, {}, RealmMap{}.isolate("native.echo", realm).value());
    s.pump();
    auto call = [&](const ServiceKey& key, const std::string& method) {
        auto ref = s.registry->find<NativeService>(key).value();
        Promise<void> result;
        return ref.invoke([method, result](NativeService& service) {
            auto invoked = service.invoke(method, "{}");
            cordis::detail::TaskAccess::observe(invoked.result, [result](Task<NativeServiceResult>::Snapshot value) {
                if (value->hasValue())
                    result.resolve();
                else
                    result.reject(value->error());
            });
            return result.task();
        });
    };
    auto failed = call({"native.echo", 1}, "fatal");
    s.pump();
    CHECK(!failed.tryResult()->hasValue() && s.module.snapshot().value().faulted);
    auto refused = call({"native.echo", 1, realm}, "early");
    s.pump();
    CHECK(!refused.tryResult()->hasValue() && s.stat(1, 1) == 0);
    CHECK(!s.module.snapshot().value().instances[0].failure->message.empty());
}
bool run(const QString& name)
{
    if (name == "service_flow")
        flow("echo");
    else if (name == "service_early")
        flow("early");
    else if (name == "service_failure")
        flow("missing");
    else if (name == "service_cancel")
        cancellation(false);
    else if (name == "service_closing")
        cancellation(true);
    else if (name == "service_cleanup")
        cleanupOldBinding();
    else if (name == "service_withdraw")
        withdrawal();
    else if (name == "service_guards")
        guards();
    else if (name == "service_realms")
        realms();
    else if (name == "service_ready")
        readiness(false);
    else if (name == "service_ready_cancel")
        readiness(true);
    else if (name == "service_rollback")
        rollback();
    else if (name == "service_scheduler")
        schedulerGuard();
    else if (name == "service_version_lease")
        rejectedVersionLease();
    else if (name == "service_consumers_budget")
        consumerBudget();
    else if (name == "service_incoming_budget")
        incomingBudget();
    else if (name == "service_ack_capacity")
        incomingBudget(true);
    else if (name == "service_stale_dispatch")
        staleDispatch();
    else if (name == "service_generation_fault")
        generationFault();
    else
        return false;
    return true;
}
} // namespace native_service_test
