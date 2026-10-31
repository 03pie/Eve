/** @file replacement_closure.cpp
 * @brief 实际绑定、等待依赖和父子所有权上的闭包检查，不执行替换。
 */
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel{scheduler};
    void run() { scheduler->runUntilIdle().value(); }
    Runtime provider(const std::string& name = "a", ServiceKey key = {"a"})
    {
        return kernel
            .registerPlugin(name,
                            [key](Context context) {
                                context.provide(key, std::make_shared<int>(7)).value();
                                return completedTask();
                            },
                            {}, {ServiceOutput::of<int>(key)})
            .value();
    }
    Runtime consumer(const std::string& name, ServiceKey key)
    {
        return kernel.registerPlugin(name, [](Context) { return completedTask(); }, {ServiceRequirement::of<int>(key)})
            .value();
    }
    void close()
    {
        auto stopped = kernel.close();
        run();
        CHECK(stopped.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
    }
};
InstanceId id(const Instance& value)
{
    return value.snapshot().value().instance;
}
std::size_t position(const ReplacementClosure& closure, InstanceId value)
{
    auto at = std::find(closure.stopOrder.begin(), closure.stopOrder.end(), value);
    CHECK(at != closure.stopOrder.end());
    return static_cast<std::size_t>(at - closure.stopOrder.begin());
}
void chain()
{
    Scene s;
    int calls = 0;
    auto a = s.provider();
    auto b = s.kernel
                 .registerPlugin(
                     "b",
                     [&](Context context) {
                         context.consume<int>({"a"}).value().withService([](int& value) { CHECK(value == 7); }).value();
                         context.provide({"b"}, std::make_shared<int>(8)).value();
                         ++calls;
                         return completedTask();
                     },
                     {}, {ServiceOutput::of<int>({"b"})})
                 .value();
    auto ai = s.kernel.mount(a).value();
    s.run();
    auto bi = s.kernel.mount(b).value();
    auto c = s.kernel
                 .registerPlugin("c",
                                 [&](Context) {
                                     ++calls;
                                     return completedTask();
                                 },
                                 {ServiceRequirement::of<int>({"b"}), ServiceRequirement::of<int>({"missing"})},
                                 {ServiceOutput::of<int>({"c"})})
                 .value();
    auto d = s.consumer("d", {"c"});
    auto ci = s.kernel.mount(c).value(), di = s.kernel.mount(d).value();
    s.run();
    auto pending = s.scheduler->pending();
    const auto closure = s.kernel.replacementClosure({a.id()}).value();
    CHECK(closure.members.size() == 4 && calls == 1 && pending == s.scheduler->pending());
    CHECK(position(closure, id(di)) < position(closure, id(ci)) &&
          position(closure, id(ci)) < position(closure, id(bi)) &&
          position(closure, id(bi)) < position(closure, id(ai)));
    CHECK(closure.startOrder.front() == id(ai) && closure.startOrder.back() == id(di));
    auto graph = s.kernel.services().dependencyGraph().value();
    CHECK(graph.bindings.size() == 2 && graph.instances.size() == 4);
    const auto& first = graph.bindings[0];
    CHECK(first.provider == id(ai) && first.managedProvider && first.consumers.size() == 1);
    CHECK(first.consumers[0].instance == id(bi) && first.consumers[0].managed && first.consumers[0].epoch == 1);
    CHECK(ci.snapshot().value().state == InstanceState::Pending &&
          di.snapshot().value().state == InstanceState::Pending);
    s.close();
}
void pending()
{
    Scene s;
    int calls = 0;
    auto a = s.kernel
                 .registerPlugin("a",
                                 [&](Context) {
                                     ++calls;
                                     return completedTask();
                                 },
                                 {ServiceRequirement::of<int>({"absent"})}, {ServiceOutput::of<int>({"a"}, false)})
                 .value();
    auto b = s.consumer("b", {"a"});
    auto ai = s.kernel.mount(a).value(), bi = s.kernel.mount(b).value();
    s.run();
    auto closure = s.kernel.replacementClosure({a.id()}).value();
    CHECK(calls == 0 && closure.members.size() == 2 && closure.stopOrder.front() == id(bi));
    CHECK(closure.members[0].epoch == 0 && closure.members[1].epoch == 0 && closure.startOrder.front() == id(ai));
    s.close();
}
void children()
{
    Scene s;
    Runtime child(
        [](Context context) {
            context.provide({"inner"}, std::make_shared<int>(3)).value();
            return completedTask();
        },
        {}, {ServiceOutput::of<int>({"inner"})});
    std::optional<Instance> nested;
    auto parent = s.kernel
                      .registerPlugin("parent",
                                      [&](Context context) {
                                          nested.emplace(context.inject(child).value());
                                          return completedTask();
                                      })
                      .value();
    auto consumer = s.consumer("consumer", {"inner"});
    auto pi = s.kernel.mount(parent).value(), ci = s.kernel.mount(consumer).value();
    s.run();
    auto closure = s.kernel.replacementClosure({parent.id()}).value();
    CHECK(closure.members.size() == 3 && position(closure, id(ci)) < position(closure, id(*nested)) &&
          position(closure, id(*nested)) < position(closure, id(pi)));
    const auto& node = closure.members[position(closure, id(*nested))];
    CHECK(node.parent == id(pi) && node.runtime == child.id());
    s.close();
}
void realms()
{
    Scene s;
    auto realm = s.kernel.services().createRealm().value();
    auto view = RealmMap{}.isolate("a", realm).value();
    auto a = s.provider(), other = s.provider("other");
    auto consumer = s.consumer("consumer", {"a"}), major = s.consumer("major", {"a", 2});
    auto ai = s.kernel.mount(a).value(), oi = s.kernel.mount(other, {}, view).value();
    auto ci = s.kernel.mount(consumer).value(), privateConsumer = s.kernel.mount(consumer, {}, view).value();
    s.kernel.mount(major).value();
    s.run();
    auto closure = s.kernel.replacementClosure({a.id()}).value();
    CHECK(closure.members.size() == 2 && closure.stopOrder.front() == id(ci) && closure.stopOrder.back() == id(ai));
    auto both = s.kernel.replacementClosure({other.id(), a.id()}).value();
    CHECK(both.members.size() == 4 && position(both, id(privateConsumer)) < position(both, id(oi)));
    s.close();
}
void external(bool waiting)
{
    Scene s;
    auto a = waiting ? s.kernel
                           .registerPlugin("a", [](Context) { return completedTask(); },
                                           {ServiceRequirement::of<int>({"missing"})}, {ServiceOutput::of<int>({"a"})})
                           .value()
                     : s.provider();
    auto ai = s.kernel.mount(a).value();
    s.run();
    Runtime foreign(
        [](Context context) {
            context.consume<int>({"a"}).value();
            return completedTask();
        },
        {ServiceRequirement::of<int>({"a"})});
    auto outsider = Instance::mount(foreign, s.scheduler, {}, std::make_shared<ServiceRegistry>(s.kernel.services()));
    s.run();
    auto denied = s.kernel.replacementClosure({a.id()});
    CHECK(!denied.hasValue() && denied.error().code == ErrorCode::ConfigConflict &&
          denied.error().instance == id(outsider));
    CHECK(ai.snapshot().value().state == (waiting ? InstanceState::Pending : InstanceState::Active));
    outsider.stop();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.size() == 1);
    s.close();
}
void optionalExternal()
{
    Scene s;
    auto host = s.kernel.services().stage({"a"}, std::make_shared<int>(4)).value();
    host.publish().value();
    auto optional = s.kernel
                        .registerPlugin("optional", [](Context) { return completedTask(); }, {},
                                        {ServiceOutput::of<int>({"a"}, false)})
                        .value();
    auto consumer = s.consumer("consumer", {"a"});
    auto oi = s.kernel.mount(optional).value(), ci = s.kernel.mount(consumer).value();
    s.run();
    auto closure = s.kernel.replacementClosure({optional.id()}).value();
    CHECK(closure.members.size() == 1 && closure.stopOrder.front() == id(oi));
    CHECK(!s.kernel.services().dependencyGraph().value().bindings[0].managedProvider);
    ci.stop();
    s.run();
    host.retire().value();
    host.release().value();
    s.close();
}
void transitions()
{
    Scene s;
    Promise<void> activation, cleanup;
    auto a = s.kernel
                 .registerPlugin("a",
                                 [&](Context context) {
                                     context.provide({"a"}, std::make_shared<int>(1)).value();
                                     context.onCleanup([&](CleanupContext) { return cleanup.task(); }).value();
                                     return activation.task();
                                 },
                                 {}, {ServiceOutput::of<int>({"a"})})
                 .value();
    auto ai = s.kernel.mount(a).value();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::ContextClosing);
    activation.resolve();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.size() == 1);
    auto stopped = ai.stop();
    s.run();
    CHECK(!stopped.isReady() && s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::ContextClosing);
    cleanup.resolve();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent && s.kernel.replacementClosure({a.id()}).value().members.empty());
    s.close();
}
void publication()
{
    Scene s;
    auto group = s.kernel.holdPublication().value();
    auto a = s.provider();
    s.kernel.mountHeld(group, a).value();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::ConfigConflict);
    group.commit().value();
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.size() == 1);
    s.close();
}
void guards()
{
    Scene s;
    auto a = s.provider();
    Runtime foreign([](Context) { return completedTask(); });
    CHECK(!s.kernel.replacementClosure({}).hasValue() && !s.kernel.replacementClosure({RuntimeId{}}).hasValue());
    CHECK(!s.kernel.replacementClosure({foreign.id()}).hasValue() &&
          !s.kernel.replacementClosure({a.id(), a.id()}).hasValue());
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.empty());
    bool denied = false;
    std::thread worker([&] {
        denied = s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::WrongThread &&
                 s.kernel.services().dependencyGraph().error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(denied);
    s.close();
    CHECK(s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::ContextClosing);
    CHECK(s.kernel.services().dependencyGraph().value().instances.empty());
}
void inFlight()
{
    Scene s;
    Promise<void> gate;
    auto a = s.provider();
    s.kernel.mount(a).value();
    s.run();
    auto c = s.kernel
                 .registerPlugin("c",
                                 [&](Context context) {
                                     context.consume<int>({"a"}).value().invoke([&](int&) { return gate.task(); });
                                     return completedTask();
                                 })
                 .value();
    auto ci = s.kernel.mount(c).value();
    s.run();
    auto before = s.kernel.services().dependencyGraph().value();
    CHECK(before.bindings[0].binding.activeCalls == 1 && before.bindings[0].consumers[0].instance == id(ci));
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.size() == 2);
    auto stopped = ci.stop();
    s.run();
    CHECK(!stopped.isReady() && s.kernel.services().dependencyGraph().value().bindings[0].consumers.size() == 1);
    gate.resolve();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent &&
          s.kernel.services().dependencyGraph().value().bindings[0].consumers.empty());
    s.close();
}
void cycle()
{
    Scene s;
    auto a = s.kernel
                 .registerPlugin("a", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"b"})},
                                 {ServiceOutput::of<int>({"a"})})
                 .value();
    auto b = s.kernel
                 .registerPlugin("b", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})},
                                 {ServiceOutput::of<int>({"b"})})
                 .value();
    s.kernel.mount(a).value();
    s.kernel.mount(b).value();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).error().code == ErrorCode::DependencyCycle);
    s.close();
}
void snapshots()
{
    Scene s;
    auto a = s.provider();
    auto ai = s.kernel.mount(a).value();
    s.run();
    auto before = s.kernel.replacementClosure({a.id()}).value();
    ai.update("new", 1).value();
    s.run();
    auto after = s.kernel.replacementClosure({a.id()}).value();
    CHECK(before.members[0].epoch == 1 && before.members[0].revision == 1);
    CHECK(after.members[0].epoch == 2 && after.members[0].revision == 2);
    before.stopOrder.clear();
    CHECK(ai.snapshot().value().state == InstanceState::Active);
    s.close();
}
void instances()
{
    Scene s;
    auto realm = s.kernel.services().createRealm().value();
    auto view = RealmMap{}.isolate("a", realm).value();
    auto a = s.provider(), c = s.consumer("consumer", {"a"});
    auto root = s.kernel.mount(a).value(), isolated = s.kernel.mount(a, {}, view).value();
    auto consumer = s.kernel.mount(c, {}, view).value();
    s.run();
    auto closure = s.kernel.replacementClosure({a.id()}).value();
    CHECK(closure.members.size() == 3 && position(closure, id(consumer)) < position(closure, id(isolated)));
    CHECK(std::count_if(closure.members.begin(), closure.members.end(),
                        [&](const auto& member) { return member.runtime == a.id(); }) == 2);
    root.stop();
    s.run();
    CHECK(s.kernel.replacementClosure({a.id()}).value().members.size() == 2);
    s.close();
}
void delivery()
{
    struct Scheduler final : IScheduler {
        FakeScheduler queue;
        bool reject = false;
        bool post(ScheduledJob job) override { return !reject && queue.post(std::move(job)); }
        bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
        bool isOpen() const override { return queue.isOpen(); }
    };
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto a = kernel
                 .registerPlugin("a",
                                 [](Context context) {
                                     context.provide({"a"}, std::make_shared<int>(7)).value();
                                     return completedTask();
                                 },
                                 {}, {ServiceOutput::of<int>({"a"})})
                 .value();
    auto actor = kernel.mount(a).value();
    scheduler->queue.runUntilIdle().value();
    Promise<void> gate;
    auto call = kernel.services().find<int>({"a"}).value().invoke([&](int&) { return gate.task(); });
    scheduler->reject = true;
    gate.resolve();
    scheduler->reject = false;
    auto graph = kernel.services().dependencyGraph().value();
    CHECK(graph.bindings[0].binding.deliveryBlocked && graph.bindings[0].binding.activeCalls == 1 && !call.isReady());
    CHECK(actor.snapshot().value().state == InstanceState::Active && !actor.snapshot().value().deliveryBlocked);
    auto denied = kernel.replacementClosure({a.id()});
    CHECK(!denied.hasValue() && denied.error().code == ErrorCode::SchedulerClosed &&
          denied.error().instance == id(actor));
    // 故障原有调用屏障固定保留，不伪造静止或为测试强行清账。
    auto stopped = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(!stopped.isReady());
    CHECK(kernel.services().dependencyGraph().value().bindings[0].binding.deliveryBlocked);
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "chain")
            chain();
        else if (name == "pending")
            pending();
        else if (name == "children")
            children();
        else if (name == "realms")
            realms();
        else if (name == "external")
            external(false);
        else if (name == "external_pending")
            external(true);
        else if (name == "optional_external")
            optionalExternal();
        else if (name == "transitions")
            transitions();
        else if (name == "publication")
            publication();
        else if (name == "guards")
            guards();
        else if (name == "in_flight")
            inFlight();
        else if (name == "cycle")
            cycle();
        else if (name == "snapshots")
            snapshots();
        else if (name == "instances")
            instances();
        else if (name == "delivery")
            delivery();
        else
            throw std::runtime_error("unknown replacement closure case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
