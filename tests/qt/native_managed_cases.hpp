/** @file native_managed_cases.hpp
 * @brief 在远端低层桥上验证 Core 托管输出、冻结输入和真实 DLL 退出。
 */
#pragma once
#include "native_host_cases.hpp"
#include "native_resource_cases.hpp"
#include "native_service_cases.hpp"
#include <cordis/kernel.hpp>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/module/runtime.hpp>

namespace native_managed_test {
using namespace native_service_test;
struct Managed : Services {
    CordisHandle providerEffect = 0;
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    Runtime provider =
        module.runtime({"org.cordis.plugin", {}, {{{"test.service", 1}, true, {2, {"read", u8"中"}}}}}).value();
    Runtime consumer = module.runtime({"org.cordis.plugin", {{{"test.service", 1}, 1, {"read"}}}, {}}).value();
    Managed()
    {
        CHECK(kernel->registerRuntime("provider", provider).value().id() == provider.id());
        CHECK(kernel->registerRuntime("consumer", consumer).hasValue());
        CHECK(!kernel->registerRuntime("alias", provider).hasValue());
    }
    Instance mountProvider(bool setup = true, CordisHandle cleanup = 11, CordisHandle token = 7)
    {
        auto value = kernel->mount(provider, "\"start-pending\"").value();
        pump();
        CHECK(!value.ready().isReady());
        providerEffect = add(cleanup, 0, setup);
        provide(providerEffect, token);
        CHECK(finishAll() == 1);
        pump();
        return value;
    }
    void finishManaged()
    {
        auto closed = kernel->close();
        pump();
        unsigned turns = 0;
        while (finishAll()) {
            CHECK(++turns < 8);
            pump();
        }
        CHECK(closed.isReady() && closed.tryResult()->value().quiescent);
        CHECK(kernel->services().snapshot().value().empty());
        finish();
    }
};
void flow()
{
    Managed s;
    auto provider = s.mountProvider();
    CHECK(provider.ready().tryResult()->hasValue());
    auto consumer = s.kernel->mount(s.consumer).value();
    s.pump();
    CHECK(consumer.ready().tryResult()->hasValue());
    CHECK(consumer.snapshot().value().committedLeases == 1);
    CHECK(s.kernel->services().snapshot().value()[0].committedLeases == 1);
    CHECK(s.module.close().tryResult()->error().code == ErrorCode::ContextClosing);
    CHECK(!s.module.snapshot().value().closing);
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(1, 0) == 0 && s.finishAll() == 1);
    s.pump();
    CHECK(s.result(1) == "{\"integer\":9007199254740993}");
    s.finishManaged();
}
void publication(bool cancel)
{
    Managed s;
    auto provider = s.mountProvider(false);
    CHECK(!provider.ready().isReady());
    CHECK(s.kernel->services().snapshot().value()[0].state == BindingState::Staged);
    auto consumer = s.kernel->mount(s.consumer).value();
    s.pump();
    CHECK(consumer.snapshot().value().state == InstanceState::Pending &&
          s.module.snapshot().value().instances.size() == 1);
    auto native = s.module.snapshot().value().instances[0];
    CHECK(native.resources.effects.size() == 1);
    if (cancel) {
        auto stopped = provider.stop();
        s.pump();
        CHECK(!stopped.isReady());
        // The actual start has settled; native stop must run even while setup is pending.
        CHECK(s.stat(0, 7) == 1);
        CHECK(s.ready(0, s.providerEffect, CORDIS_E_CANCELLED) == CORDIS_OK);
        s.pump();
        CHECK(stopped.tryResult()->value().quiescent);
    } else {
        CHECK(s.ready(0, s.providerEffect, CORDIS_OK) == CORDIS_OK);
        s.pump();
        CHECK(provider.ready().tryResult()->hasValue() && consumer.ready().tryResult()->hasValue());
    }
    s.finishManaged();
}
void retainedKey()
{
    Managed s;
    auto provider = s.mountProvider(true, 12);
    CHECK(provider.ready().tryResult()->hasValue());
    auto stopped = provider.stop();
    s.pump();
    CHECK(!stopped.isReady() && s.stat(0, 2) == 1);
    const auto bindings = s.kernel->services().snapshot().value();
    CHECK(bindings.size() == 1 && bindings[0].state == BindingState::Retiring);
    CHECK(!s.kernel->services().stage<int>({"test.service", 1}, std::make_shared<int>(7)).hasValue());
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent && s.kernel->services().snapshot().value().empty());
    s.finishManaged();
}
void cleanup()
{
    Managed s;
    auto provider = s.mountProvider();
    auto consumer = s.kernel->mount(s.consumer, "\"start-pending\"").value();
    s.pump();
    s.add(15, 1);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(consumer.ready().tryResult()->hasValue() && s.stat(1, 0) == 0);
    auto stopped = provider.stop();
    s.pump();
    CHECK(!stopped.isReady() && s.stat(1, 2) == 1 && s.stat(0, 1) == 1);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    s.finishManaged();
}
void guards()
{
    Managed s;
    NativePlugin invalid{"org.cordis.plugin", {}, {}};
    invalid.id = "missing";
    CHECK(!s.module.runtime(invalid).hasValue());
    invalid.id = "org.cordis.plugin";
    invalid.dependencies.push_back({{"test.service", 1}, 0, {"read"}});
    const auto repeated = invalid.dependencies[0];
    invalid.dependencies.assign(129, repeated);
    CHECK(!s.module.runtime(invalid).hasValue());
    auto bare = s.module.runtime({"org.cordis.plugin", {}, {}}).value();
    CHECK(s.kernel->registerRuntime("bare", bare).hasValue());
    auto instance = s.kernel->mount(bare).value();
    s.pump();
    CHECK(instance.ready().tryResult()->hasValue());
    auto effect = s.add();
    std::string name = "late", caps = "[]";
    CordisServiceKeyV1 key{span(name), 1, 0, 1};
    CordisServiceMetadataV1 metadata{sizeof(metadata), 0, span(caps)};
    CordisHandle binding = 99;
    CHECK(s.reg(0, effect, &key, &metadata, 8, &binding) == CORDIS_E_CLOSED && binding == 0);
    s.finishManaged();
}
void realms()
{
    Managed s;
    auto realm = s.kernel->services().createRealm().value();
    auto view = RealmMap{}.isolate("test.service", realm).value();
    auto provider = s.kernel->mount(s.provider, "\"start-pending\"", view).value();
    s.pump();
    s.provide(s.add(), 7);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(provider.ready().tryResult()->hasValue());
    auto consumer = s.kernel->mount(s.consumer, "null", view).value();
    s.pump();
    CHECK(consumer.ready().tryResult()->hasValue());
    CordisRequestId request = 0;
    CHECK(s.request(request) == CORDIS_PENDING); // The DLL uses its original realm; the Host applies the frozen view.
    s.pump();
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(s.stat(1, 3) == CORDIS_OK && s.kernel->services().snapshot().value()[0].key.realm == realm);
    s.finishManaged();
}
void staleDispatch()
{
    Managed s;
    auto provider = s.mountProvider(true, 12);
    auto request = s.send(8);
    auto stopped = provider.stop();
    s.pump();
    CHECK(!stopped.isReady() && s.stat(0, 1) == 0 && s.stat(0, 0) == 1 && s.stat(0, 3) == CORDIS_E_CANCELLED);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    (void)request; // The canceled ACK runs during shutdown, before the real DLL object is destroyed.
    s.finishManaged();
}
struct MemoryWriter final : loader::IConfigurationWriter {
    loader::ConfigurationFileSnapshot disk{{"memory/native.yaml", false, {}}, {}};
    Task<loader::ConfigurationFileCommit> commit(std::shared_ptr<const loader::ValidatedCandidate> target,
                                                 loader::ConfigurationFileVersion expected) override
    {
        Promise<loader::ConfigurationFileCommit> done;
        if (expected.exists != disk.version.exists || expected.sha256 != disk.version.sha256)
            done.resolve(
                {loader::FileCommitOutcome::Conflict, disk, makeError(ErrorCode::ConfigConflict, "memory conflict")});
        else {
            disk.text = target->source().normalizedYaml();
            disk.version.exists = true;
            disk.version.sha256 =
                QCryptographicHash::hash(QByteArray::fromStdString(disk.text), QCryptographicHash::Sha256)
                    .toHex()
                    .toStdString();
            done.resolve({loader::FileCommitOutcome::Committed, disk, {}});
        }
        return done.task();
    }
};
void session()
{
    using namespace cordis::loader;
    Managed s;
    auto optional = s.module.runtime({"org.cordis.plugin", {}, {}}).value();
    CHECK(s.kernel->registerRuntime("optional", optional).hasValue());
    const PluginModule owned{"native", "module/module.json"};
    bool available = true;
    auto gate = [&] {
        return available ? Result<void>::success()
                         : Result<void>::failure(makeError(ErrorCode::ModulePinned, "unavailable"));
    };
    auto driver = std::make_shared<KernelReconcileDriver>(
        s.scheduler, s.kernel,
        std::vector<KernelPlugin>{
            {"provider", s.provider, {}, owned, gate, 128},
            {"consumer", optional, {nativeRequirement({{"test.service", 1}, 1, {"read"}})}, owned, gate, 128}});
    auto writer = std::make_shared<MemoryWriter>();
    auto schema = [](const std::string&) { return Result<void>::success(); };
    ConfigurationSession session(
        s.scheduler, driver, {{"provider", {}, schema, owned}, {"consumer", {}, schema, owned}}, writer, writer->disk);
    const std::string text =
        "formatVersion: 1\nmodules: {native: {manifest: module/module.json}}\nplugins: [{id: p, plugin: provider, "
        "module: native, config: start-pending}, {id: c, plugin: consumer, module: native, config: start-pending, "
        "inject: [{name: test.service, minimumMinor: 1, capabilities: [read]}]}]";
    auto draft = session.preview(text).value();
    CHECK(s.module.snapshot().value().instances.empty());
    CHECK(!session.preview("formatVersion: 1\nmodules: {native: {manifest: changed.json}}\nplugins: []").hasValue());
    auto accepted = session.apply(draft).value();
    s.pump();
    s.provide(s.add(), 7);
    CHECK(s.finishAll() == 1);
    s.pump();
    s.add(15, 1);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(session.apply(draft).error().code == ErrorCode::ConfigConflict);
    auto saved = session.save(accepted.revision).value();
    s.pump();
    CHECK(saved.tryResult()->value().outcome == SaveOutcome::Written &&
          parseConfiguration(writer->disk.text).hasValue());
    available = false;
    // Disabling the entire unavailable module must remain possible.
    auto disabled =
        session.preview("formatVersion: 1\nmodules: {native: {manifest: module/module.json}}\nplugins: []").value();
    auto removed = session.apply(disabled).value();
    s.pump();
    CHECK(!removed.settled.isReady());
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(removed.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto closed = session.close();
    s.pump();
    CHECK(closed.tryResult()->hasValue());
    s.finishManaged();
}
void rejectedOutput(bool undeclared)
{
    Managed s;
    auto runtime = undeclared ? s.module.runtime({"org.cordis.plugin", {}, {}}).value() : s.provider;
    if (undeclared)
        CHECK(s.kernel->registerRuntime("bare", runtime).hasValue());
    auto instance = s.kernel->mount(runtime, "\"start-pending\"").value();
    s.pump();
    auto effect = s.add();
    std::string name = "test.service", caps = "[\"read\",\"\\u4e2d\"]";
    CordisServiceKeyV1 key{span(name), 1, 0, 1};
    CordisServiceMetadataV1 metadata{sizeof(metadata), undeclared ? 2u : 3u, span(caps)};
    CordisHandle binding = 99;
    CHECK(s.reg(0, effect, &key, &metadata, 7, &binding) != CORDIS_OK && binding == 0);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(instance.ready().isReady() && !instance.ready().tryResult()->hasValue());
    CHECK(!s.module.snapshot().value().faulted && s.module.snapshot().value().instances.empty());
    CHECK(s.kernel->services().snapshot().value().empty());
    s.finishManaged();
}
void setupFailure()
{
    Managed s;
    auto instance = s.mountProvider(false);
    CHECK(s.ready(0, s.providerEffect, CORDIS_E_FAILED) == CORDIS_OK);
    s.pump();
    CHECK(instance.ready().isReady() && !instance.ready().tryResult()->hasValue());
    CHECK(!s.module.snapshot().value().faulted && s.kernel->services().snapshot().value().empty());
    s.finishManaged();
}
void wrongScheduler()
{
    Managed s;
    auto other = std::make_shared<FakeScheduler>();
    Kernel kernel(other);
    CHECK(kernel.registerRuntime("foreign", s.provider).hasValue());
    auto instance = kernel.mount(s.provider).value();
    CHECK(other->runUntilIdle().hasValue());
    CHECK(instance.ready().tryResult()->error().code == ErrorCode::InvalidArgument &&
          s.module.snapshot().value().instances.empty());
    auto closed = kernel.close();
    CHECK(other->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
    s.finishManaged();
}
void mixedIndex()
{
    Managed s;
    s.active(1000);
    s.provide(s.add());
    s.pump();
    auto bare = s.module.runtime({"org.cordis.plugin", {}, {}}).value();
    CHECK(s.kernel->registerRuntime("bare", bare).hasValue());
    auto instance = s.kernel->mount(bare).value();
    s.pump();
    CHECK(instance.ready().tryResult()->error().message == "native index differs from owning Core registry");
    CHECK(s.module.snapshot().value().instances.size() == 1);
    s.finishManaged();
}
void generationGate()
{
    Managed s;
    s.mountProvider();
    auto offender = s.kernel->mount(s.consumer, "\"start-pending\"").value();
    s.pump();
    auto effect = s.add(11, 1);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(offender.ready().tryResult()->hasValue());
    Services peer(s.scheduler, "services_peer", "peer", s.kernel->services());
    auto runtime = peer.module.runtime({"org.cordis.plugin", {{{"test.service", 1}, 1, {"read"}}}, {}}).value();
    CHECK(s.kernel->registerRuntime("peer", runtime).hasValue());
    auto caller = s.kernel->mount(runtime).value();
    s.pump();
    CHECK(caller.ready().tryResult()->hasValue());
    CordisRequestId queued = 0, rejected = 99;
    CHECK(peer.request(queued, 0) == CORDIS_PENDING);
    CHECK(s.ready(1, effect, CORDIS_OK) == CORDIS_E_ABI_PROTOCOL);
    CHECK(s.module.snapshot().value().faulted);
    CHECK(peer.request(rejected, 0) == CORDIS_E_CLOSED && rejected == 0);
    s.pump();
    CHECK(s.stat(0, 1) == 0 && peer.stat(0, 0) == 1 && peer.stat(0, 3) == CORDIS_E_CLOSED);
    CHECK(!peer.module.snapshot().value().faulted);
    auto closed = s.kernel->close();
    s.pump();
    CHECK(!closed.isReady() && !s.manager.drain(s.generation).hasValue());
    CHECK(peer.module.close().tryResult()->value().quiescent);
}
void incomingBudget()
{
    Managed s;
    s.mountProvider(true, 11, 8);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(s.kernel->mount(s.consumer).hasValue());
    s.pump();
    for (unsigned caller = 1; caller <= 3; ++caller)
        for (unsigned i = 0; i < 128; ++i) {
            CordisRequestId request = 0;
            CHECK(s.request(request, caller) == CORDIS_PENDING);
        }
    s.pump();
    CHECK(!s.module.snapshot().value().faulted && s.stat(0, 1) == 256);
    CHECK(s.stat(1, 0) == 128 && s.stat(2, 0) == 128 && s.stat(3, 0) == 128);
    CHECK(static_cast<CordisStatus>(s.stat(3, 3)) == CORDIS_E_LIMIT);
    CordisRequestId recovered = 0;
    CHECK(s.request(recovered, 3) == CORDIS_PENDING);
    s.pump();
    CHECK(s.stat(0, 1) == 257 && s.stat(3, 3) == CORDIS_OK);
    s.finishManaged();
}
void createFailure()
{
    Managed s;
    s.mountProvider();
    std::vector<Instance> consumers;
    for (unsigned i = 0; i < 3; ++i)
        consumers.push_back(s.kernel->mount(s.consumer).value());
    s.pump();
    // The real DLL has four slots. A fifth create rejects synchronously without retaining anything.
    auto rejected = s.kernel->mount(s.consumer).value();
    s.pump();
    CHECK(rejected.ready().isReady() && !rejected.ready().tryResult()->hasValue());
    CHECK(rejected.ready().tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(!s.module.snapshot().value().faulted && s.module.snapshot().value().instances.size() == 4);
    CHECK(s.kernel->services().snapshot().value()[0].committedLeases == 3);
    auto stopped = consumers.back().stop();
    s.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    auto recovered = s.kernel->mount(s.consumer).value();
    s.pump();
    CHECK(recovered.ready().tryResult()->hasValue() && !s.module.snapshot().value().faulted);
    CHECK(s.kernel->services().snapshot().value()[0].committedLeases == 3);
    s.finishManaged();
}
void ackCapacity()
{
    Managed s;
    auto a = s.module.runtime({"org.cordis.plugin", {}, {{{"a", 1}, true, {2, {"read", u8"中"}}}}}).value();
    auto b =
        s.module.runtime({"org.cordis.plugin", {{{"a", 1}, 0, {}}}, {{{"b", 1}, true, {2, {"read", u8"中"}}}}}).value();
    auto c = s.module.runtime({"org.cordis.plugin", {{{"b", 1}, 0, {}}}, {}}).value();
    CHECK(s.kernel->registerRuntime("a", a).hasValue() && s.kernel->registerRuntime("b", b).hasValue() &&
          s.kernel->registerRuntime("c", c).hasValue());
    auto first = s.kernel->mount(a, "\"start-pending\"").value();
    s.pump();
    s.provide(s.add(), 8, 1, "a");
    CHECK(s.finishAll() == 1);
    s.pump();
    auto middle = s.kernel->mount(b, "\"start-pending\"").value();
    s.pump();
    s.provide(s.add(11, 1), 10, 1, "b", 1);
    CHECK(s.finishAll() == 1);
    s.pump();
    CHECK(s.kernel->mount(c).hasValue() && s.kernel->mount(c).hasValue());
    s.pump();
    for (unsigned caller = 2; caller <= 3; ++caller)
        for (unsigned i = 0; i < 128; ++i) {
            CordisRequestId request = 0;
            CHECK(s.request(request, caller, 1, 0, "[]", "{}", "b") == CORDIS_PENDING);
        }
    s.pump();
    bool full = false;
    const auto instances = s.module.snapshot().value().instances;
    const auto middleId = middle.snapshot().value().instance;
    for (const auto& value : instances)
        if (value.instance == middleId)
            full = value.pendingRequests == 256;
    CHECK(full && !s.module.snapshot().value().faulted && first.ready().tryResult()->hasValue());
    s.send(8, 1);
    s.pump();
    CHECK(!s.module.snapshot().value().faulted && s.stat(1, 0) == 1 &&
          static_cast<CordisStatus>(s.stat(1, 3)) == CORDIS_E_LIMIT);
    CordisRequestId request = 0;
    CHECK(s.request(request, 1, 1, 0, "[]", "{}", "a") == CORDIS_PENDING);
    s.pump();
    CHECK(!s.module.snapshot().value().faulted && s.stat(1, 0) == 2 && s.stat(1, 3) == CORDIS_OK);
    CHECK(s.finishAll() == 256);
    s.pump();
    CHECK(s.stat(2, 0) == 128 && s.stat(3, 0) == 128);
    s.finishManaged();
}
bool run(const QString& name)
{
    if (name == "managed_flow")
        flow();
    else if (name == "managed_publication")
        publication(false);
    else if (name == "managed_setup_cancel")
        publication(true);
    else if (name == "managed_key")
        retainedKey();
    else if (name == "managed_cleanup")
        cleanup();
    else if (name == "managed_guards")
        guards();
    else if (name == "managed_realms")
        realms();
    else if (name == "managed_stale_dispatch")
        staleDispatch();
    else if (name == "managed_session")
        session();
    else if (name == "managed_metadata")
        rejectedOutput(false);
    else if (name == "managed_undeclared")
        rejectedOutput(true);
    else if (name == "managed_setup_failure")
        setupFailure();
    else if (name == "managed_scheduler")
        wrongScheduler();
    else if (name == "managed_index")
        mixedIndex();
    else if (name == "managed_generation")
        generationGate();
    else if (name == "managed_incoming_budget")
        incomingBudget();
    else if (name == "managed_create_failure")
        createFailure();
    else if (name == "managed_ack_capacity")
        ackCapacity();
    else
        return false;
    return true;
}
} // namespace native_managed_test
