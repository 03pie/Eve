/** @file replacement_scope.cpp
 * @brief 对真实 Core 入口验证排他、冻结、停止屏障及所有权重入。
 */
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
using namespace cordis;
using cordis::testing::FakeScheduler;
static_assert(!std::is_copy_constructible_v<ReplacementScope> && !std::is_copy_assignable_v<ReplacementScope>);
static_assert(std::is_nothrow_move_constructible_v<ReplacementScope> &&
              std::is_nothrow_move_assignable_v<ReplacementScope>);
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    void run() { scheduler->runUntilIdle(30000).value(); }
    Runtime provider(std::string name = "a", ServiceKey key = {"a"})
    {
        return kernel
            ->registerPlugin(std::move(name),
                             [key](Context context) {
                                 context.provide(key, std::make_shared<int>(7)).value();
                                 return completedTask();
                             },
                             {}, {ServiceOutput::of<int>(key)})
            .value();
    }
    Runtime consumer(std::string name = "c", ServiceKey key = {"a"})
    {
        return kernel
            ->registerPlugin(std::move(name), [](Context) { return completedTask(); },
                             {ServiceRequirement::of<int>(key)})
            .value();
    }
    void close()
    {
        auto task = kernel->close();
        run();
        CHECK(task.tryResult() && task.tryResult()->hasValue() && task.tryResult()->value().quiescent);
        CHECK(kernel->services().snapshot().value().empty() && kernel->replacements().value().empty());
    }
};
struct Scheduler : IScheduler {
    FakeScheduler queue;
    bool reject = false;
    std::function<void()> onPost;
    bool post(ScheduledJob job) override
    {
        auto hook = std::exchange(onPost, {});
        if (hook)
            hook();
        return !reject && queue.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    bool isOpen() const override { return queue.isOpen(); }
};
InstanceId id(const Instance& instance)
{
    return instance.snapshot().value().instance;
}
void admission()
{
    Scene s;
    auto a = s.provider(), c = s.consumer();
    auto ai = s.kernel->mount(a).value(), ci = s.kernel->mount(c).value();
    s.run();
    auto context = ci.whenActiveAfter().tryResult()->value();
    auto before = s.kernel->snapshot().value();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto snapshot = scope.snapshot().value();
    CHECK(snapshot.state == ReplacementScopeState::Reserved && snapshot.closure.members.size() == 2 &&
          snapshot.operation.valid());
    CHECK(!s.kernel->mount(a).hasValue() && !s.kernel->checkMount(c).hasValue());
    CHECK(ai.update("new", 1).error().code == ErrorCode::ConfigConflict && !ci.stop().tryResult()->hasValue());
    CHECK(!context.inject(c).hasValue());
    CHECK(context.consume<int>({"a"}).value().withService([](int& v) { CHECK(v == 7); }).hasValue());
    Runtime direct([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})});
    bool rejected = false;
    try {
        Instance::mount(direct, s.scheduler, {}, std::make_shared<ServiceRegistry>(s.kernel->services()));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected && !s.kernel->services().stage({"a"}, std::make_shared<int>(9)).hasValue());
    CHECK(ai.snapshot().value().epoch == 1 && ci.snapshot().value().revision == 1 &&
          before.instances.size() == s.kernel->snapshot().value().instances.size());
    auto released = s.kernel->whenReplacementReleased(snapshot.operation);
    CHECK(!released.isReady());
    scope.release().value();
    CHECK(released.tryResult()->hasValue());
    CHECK(scope.stop().tryResult()->error().code == ErrorCode::OperationCancelled);
    ci.update("next", 1).value();
    s.run();
    s.close();
}
void freeze()
{
    Scene s;
    auto a = s.provider();
    auto ai = s.kernel->mount(a).value();
    s.run();
    auto context = ai.whenActiveAfter().tryResult()->value();
    auto raw = s.kernel->services().find<int>({"a"}).value();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    scope.freeze().value();
    auto waiting = ai.whenActiveAfter();
    CHECK(!waiting.isReady());
    CHECK(!context.identity().hasValue() &&
          !context.effect([](EffectSetupContext) { return completedTask(); }).hasValue());
    CHECK(!raw.withService([](int&) {}).hasValue() && !s.kernel->services().find<int>({"a"}).hasValue());
    CHECK(s.kernel->services().snapshot().value()[0].state == BindingState::Published);
    CHECK(ai.snapshot().value().state == InstanceState::Active);
    scope.release().value();
    s.run();
    CHECK(waiting.tryResult()->hasValue() && waiting.tryResult()->value().identity().value().epoch == 1);
    CHECK(context.identity().hasValue() && raw.withService([](int&) {}).hasValue());
    s.close();
}
void ordered()
{
    Scene s;
    std::vector<char> cleaned;
    Promise<void> bGate, cGate;
    auto a = s.kernel
                 ->registerPlugin("a",
                                  [&](Context ctx) {
                                      ctx.provide({"a"}, std::make_shared<int>(7)).value();
                                      ctx.onCleanup([&](CleanupContext) {
                                             cleaned.push_back('a');
                                             return completedTask();
                                         })
                                          .value();
                                      return completedTask();
                                  },
                                  {}, {ServiceOutput::of<int>({"a"})})
                 .value();
    auto b = s.kernel
                 ->registerPlugin("b",
                                  [&](Context ctx) {
                                      ctx.provide({"b"}, std::make_shared<int>(8)).value();
                                      auto upstream = ctx.consume<int>({"a"}).value();
                                      ctx.onCleanup([&, upstream](CleanupContext old) {
                                             old.withService(upstream, [](int& v) { CHECK(v == 7); }).value();
                                             cleaned.push_back('b');
                                             return bGate.task();
                                         })
                                          .value();
                                      return completedTask();
                                  },
                                  {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})})
                 .value();
    auto c = s.kernel
                 ->registerPlugin("c",
                                  [&](Context ctx) {
                                      auto upstream = ctx.consume<int>({"b"}).value();
                                      ctx.onCleanup([&, upstream](CleanupContext old) {
                                             old.withService(upstream, [](int& v) { CHECK(v == 8); }).value();
                                             cleaned.push_back('c');
                                             return cGate.task();
                                         })
                                          .value();
                                      return completedTask();
                                  },
                                  {ServiceRequirement::of<int>({"b"})})
                 .value();
    auto ai = s.kernel->mount(a).value(), bi = s.kernel->mount(b).value(), ci = s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto stopped = scope.stop();
    s.run();
    CHECK(!stopped.isReady() && cleaned == std::vector<char>{'c'} &&
          ai.snapshot().value().state == InstanceState::Active && bi.snapshot().value().state == InstanceState::Active);
    CHECK(!scope.release().hasValue());
    cGate.resolve();
    s.run();
    CHECK(!stopped.isReady() && cleaned.size() == 2 && cleaned[1] == 'b' &&
          ci.snapshot().value().state == InstanceState::Disposed);
    bGate.resolve();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent && cleaned.size() == 3 && cleaned[2] == 'a');
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Stopped && !s.kernel->mount(a).hasValue());
    scope.release().value();
    CHECK(ai.snapshot().value().state == InstanceState::Disposed);
    s.close();
}
void inflight()
{
    Scene s;
    Promise<void> gate;
    auto a = s.provider();
    auto ai = s.kernel->mount(a).value();
    s.run();
    auto c = s.kernel
                 ->registerPlugin("c",
                                  [&](Context ctx) {
                                      ctx.consume<int>({"a"}).value().invoke([&](int&) { return gate.task(); });
                                      return completedTask();
                                  },
                                  {ServiceRequirement::of<int>({"a"})})
                 .value();
    auto ci = s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto stopped = scope.stop();
    s.run();
    CHECK(!stopped.isReady() && ai.snapshot().value().state == InstanceState::Active &&
          ci.snapshot().value().state == InstanceState::Unloading);
    CHECK(s.kernel->services().snapshot().value()[0].activeCalls == 1);
    gate.resolve();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    scope.release().value();
    s.close();
}
void pending()
{
    Scene s;
    int entered = 0;
    auto a = s.kernel
                 ->registerPlugin("a",
                                  [&](Context) {
                                      ++entered;
                                      return completedTask();
                                  })
                 .value();
    auto ai = s.kernel->mount(a).value();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    s.run();
    CHECK(entered == 0 && !ai.ready().isReady());
    scope.release().value();
    s.run();
    CHECK(entered == 1 && ai.ready().tryResult()->hasValue());
    s.close();
}
void parallel()
{
    Scene s;
    auto realm = s.kernel->services().createRealm().value();
    auto view = RealmMap{}.isolate("a", realm).value();
    auto a = s.provider(), b = s.provider("b"), c = s.consumer();
    s.kernel->mount(a).value();
    s.kernel->mount(b, {}, view).value();
    auto ci = s.kernel->mount(c).value(), di = s.kernel->mount(c, {}, view).value();
    s.run();
    auto first = s.kernel->reserveReplacement({a.id()}).value(),
         second = s.kernel->reserveReplacement({b.id()}).value();
    CHECK(s.kernel->replacements().value().size() == 2 &&
          first.snapshot().value().operation != second.snapshot().value().operation);
    first.freeze().value();
    CHECK(!ci.whenActiveAfter().isReady() && di.whenActiveAfter().tryResult()->hasValue());
    first.release().value();
    CHECK(!di.checkMutation().hasValue() && s.kernel->replacements().value().size() == 1);
    second.release().value();
    s.run();
    s.close();
}
void overlap()
{
    Scene s;
    auto a = s.provider(), b = s.provider("b", {"b"});
    auto c = s.kernel
                 ->registerPlugin("c", [](Context) { return completedTask(); },
                                  {ServiceRequirement::of<int>({"a"}), ServiceRequirement::of<int>({"b"})})
                 .value();
    s.kernel->mount(a).value();
    s.kernel->mount(b).value();
    s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto other = s.kernel->reserveReplacement({b.id()});
    CHECK(!other.hasValue() && other.error().operation == scope.snapshot().value().operation);
    scope.release().value();
    s.close();
}
void unbound()
{
    Scene s;
    auto make = [&](const char* name) {
        return s.kernel
            ->registerPlugin(name, [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"missing"})},
                             {ServiceOutput::of<int>({"a"}, false)})
            .value();
    };
    auto a = make("a"), b = make("b");
    s.kernel->mount(a).value();
    s.kernel->mount(b).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    CHECK(!s.kernel->reserveReplacement({b.id()}).hasValue() && scope.snapshot().value().reservedOutputs.size() == 1);
    scope.release().value();
    s.close();
}
void children()
{
    Scene s;
    auto child = s.provider("child", {"inner"});
    std::optional<Instance> nested;
    std::optional<Context> context;
    auto parent = s.kernel
                      ->registerPlugin("parent",
                                       [&](Context ctx) {
                                           context = ctx;
                                           nested.emplace(ctx.inject(child).value());
                                           return completedTask();
                                       })
                      .value();
    auto root = s.kernel->mount(parent).value();
    auto c = s.consumer("c", {"inner"});
    s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({child.id()}).value();
    CHECK(scope.snapshot().value().closure.members.size() == 3 && !root.checkMutation().hasValue());
    CHECK(!context->inject(child).hasValue());
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent && nested->snapshot().value().state == InstanceState::Disposed);
    scope.release().value();
    s.close();
}
void stale()
{
    Scene s;
    auto host = s.kernel->services().stage({"a"}, std::make_shared<int>(4)).value();
    host.publish().value();
    auto a = s.kernel
                 ->registerPlugin("optional", [](Context) { return completedTask(); }, {},
                                  {ServiceOutput::of<int>({"a"}, false)})
                 .value();
    auto c = s.consumer();
    s.kernel->mount(a).value();
    s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    CHECK(scope.snapshot().value().reservedOutputs.empty());
    host.retire().value();
    s.run();
    host.release().value();
    CHECK(scope.freeze().error().code == ErrorCode::ConfigConflict &&
          scope.snapshot().value().state == ReplacementScopeState::Reserved);
    scope.release().value();
    s.close();
}
void cleanupFailure()
{
    Scene s;
    auto a = s.provider();
    auto ai = s.kernel->mount(a).value();
    auto c = s.kernel
                 ->registerPlugin("c",
                                  [](Context ctx) {
                                      ctx.onCleanup([](CleanupContext) {
                                             return failedTask(makeError(ErrorCode::CleanupFailed, "retained"));
                                         })
                                          .value();
                                      return completedTask();
                                  },
                                  {ServiceRequirement::of<int>({"a"})})
                 .value();
    s.kernel->mount(c).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->hasValue() && !stopped.tryResult()->value().quiescent);
    CHECK(ai.snapshot().value().state == InstanceState::Active && !scope.release().hasValue() &&
          scope.snapshot().value().cleanup.has_value());
    auto closed = s.kernel->close();
    s.run();
    CHECK((!closed.isReady() || !closed.tryResult()->value().quiescent) && !s.kernel->replacements().value().empty());
}
void abandoned()
{
    Scene s;
    auto a = s.provider();
    s.kernel->mount(a).value();
    s.run();
    {
        auto scope = s.kernel->reserveReplacement({a.id()}).value();
        scope.freeze().value();
    }
    s.run();
    CHECK(s.kernel->replacements().value().empty());
    {
        auto scope = s.kernel->reserveReplacement({a.id()}).value();
        scope.stop();
    }
    s.run();
    CHECK(s.kernel->replacements().value().size() == 1 && s.kernel->replacements().value()[0].abandoned &&
          !s.kernel->mount(a).hasValue());
    s.close();
}
void ownerGone()
{
    Scene s;
    auto a = s.provider();
    auto actor = s.kernel->mount(a).value();
    s.run();
    auto services = s.kernel->services();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    s.kernel.reset();
    scope.freeze().value();
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    scope.release().value();
    CHECK(services.snapshot().value().empty() && actor.snapshot().value().state == InstanceState::Disposed);
}
void crossThread()
{
    Scene s;
    auto a = s.provider();
    auto actor = s.kernel->mount(a).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    bool denied = false;
    std::thread worker([guard = std::move(scope), &denied]() mutable {
        denied = guard.snapshot().error().code == ErrorCode::WrongThread &&
                 guard.freeze().error().code == ErrorCode::WrongThread &&
                 guard.release().error().code == ErrorCode::WrongThread &&
                 guard.stop().tryResult()->error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(denied && !scope.snapshot().hasValue() && s.kernel->replacements().value()[0].abandoned &&
          !actor.checkMutation().hasValue());
    s.run();
    CHECK(s.kernel->replacements().value().empty());
    s.close();
}
void guards()
{
    Scene s;
    auto a = s.provider();
    Runtime foreign([](Context) { return completedTask(); });
    ReplacementScope empty;
    CHECK(!empty.snapshot().hasValue() && !empty.freeze().hasValue() && !empty.release().hasValue() &&
          !empty.stop().tryResult()->hasValue());
    CHECK(!s.kernel->reserveReplacement({}).hasValue() && !s.kernel->reserveReplacement({foreign.id()}).hasValue());
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    CHECK(scope.snapshot().value().closure.members.empty());
    auto task = scope.stop();
    CHECK(task.tryResult()->value().quiescent);
    scope.release().value();
    scope.release().value();
    CHECK(!s.kernel->whenReplacementReleased(OperationId{}).tryResult()->hasValue());
    s.close();
    CHECK(!s.kernel->reserveReplacement({a.id()}).hasValue());
}
void shutdown()
{
    Scene s;
    Promise<void> gate;
    auto a = s.kernel
                 ->registerPlugin("a",
                                  [&](Context ctx) {
                                      ctx.onCleanup([&](CleanupContext) { return gate.task(); }).value();
                                      return completedTask();
                                  })
                 .value();
    s.kernel->mount(a).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto stopped = scope.stop();
    s.run();
    auto closed = s.kernel->close();
    CHECK(stopped.tryResult()->error().code == ErrorCode::OperationCancelled && !closed.isReady() &&
          scope.snapshot().value().cancelled);
    gate.resolve();
    s.run();
    CHECK(closed.tryResult()->value().quiescent && scope.snapshot().value().state == ReplacementScopeState::Released &&
          s.kernel->replacements().value().empty());
}
void mountClose()
{
    auto scheduler = std::make_shared<Scheduler>();
    auto kernel = std::make_shared<Kernel>(scheduler);
    int entered = 0;
    auto a = kernel
                 ->registerPlugin("a",
                                  [&](Context) {
                                      ++entered;
                                      return completedTask();
                                  })
                 .value();
    std::optional<Task<CleanupReport>> closed;
    scheduler->onPost = [&] { closed.emplace(kernel->close()); };
    auto actor = kernel->mount(a).value();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed->tryResult()->value().quiescent && actor.snapshot().value().state == InstanceState::Disposed &&
          entered == 0);
}
void injectClose()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto parent = kernel.registerPlugin("p", [](Context) { return completedTask(); }).value();
    auto actor = kernel.mount(parent).value();
    scheduler->queue.runUntilIdle().value();
    auto context = actor.whenActiveAfter().tryResult()->value();
    int entered = 0;
    Runtime child([&](Context) {
        ++entered;
        return completedTask();
    });
    std::optional<Task<CleanupReport>> closed;
    scheduler->onPost = [&] { closed.emplace(actor.stop()); };
    auto nested = context.inject(child).value();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed->tryResult()->value().quiescent && nested.snapshot().value().state == InstanceState::Disposed &&
          entered == 0);
    auto all = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(all.tryResult()->value().quiescent);
}
void mountClaim()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    int entered = 0;
    auto a = kernel
                 .registerPlugin("a",
                                 [&](Context) {
                                     ++entered;
                                     return completedTask();
                                 })
                 .value();
    std::optional<ReplacementScope> scope;
    scheduler->onPost = [&] { scope.emplace(kernel.reserveReplacement({a.id()}).value()); };
    auto actor = kernel.mount(a).value();
    scheduler->queue.runUntilIdle().value();
    CHECK(scope->snapshot().value().closure.members.size() == 1 && entered == 0 && !actor.ready().isReady());
    scope->release().value();
    scheduler->queue.runUntilIdle().value();
    CHECK(entered == 1);
    auto all = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(all.tryResult()->value().quiescent);
}
void facadeReentry()
{
    auto scheduler = std::make_shared<Scheduler>();
    auto kernel = std::make_unique<Kernel>(scheduler);
    auto a = kernel->registerPlugin("a", [](Context) { return completedTask(); }).value();
    std::weak_ptr<int> lifetime;
    {
        auto value = std::make_shared<int>(1);
        lifetime = value;
        kernel->registerPlugin("spare", [value](Context) { return completedTask(); }).value();
    }
    bool alive = false;
    scheduler->onPost = [&] {
        kernel.reset();
        alive = !lifetime.expired();
    };
    auto actor = kernel->mount(a).value();
    CHECK(!kernel && alive && lifetime.expired());
    scheduler->queue.runUntilIdle().value();
    auto stopped = actor.stop();
    scheduler->queue.runUntilIdle().value();
    CHECK(stopped.tryResult()->value().quiescent);
}
void releaseFailure()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto a = kernel.registerPlugin("a", [](Context) { return completedTask(); }).value();
    kernel.mount(a).value();
    scheduler->queue.runUntilIdle().value();
    auto scope = kernel.reserveReplacement({a.id()}).value();
    scope.freeze().value();
    scheduler->reject = true;
    CHECK(!scope.release().hasValue() && scope.snapshot().value().state == ReplacementScopeState::Released &&
          scope.snapshot().value().deliveryBlocked && !scope.release().hasValue());
    scheduler->reject = false;
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
}
void releaseReentry()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto a = kernel.registerPlugin("a", [](Context) { return completedTask(); }).value();
    kernel.mount(a).value();
    scheduler->queue.runUntilIdle().value();
    auto scope = kernel.reserveReplacement({a.id()}).value();
    std::optional<ReplacementScope> next;
    bool pending = false;
    scheduler->onPost = [&] {
        pending = !scope.release().hasValue();
        next.emplace(kernel.reserveReplacement({a.id()}).value());
    };
    scope.release().value();
    CHECK(pending && next && kernel.replacements().value().size() == 1);
    next->release().value();
    scheduler->queue.runUntilIdle().value();
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
}
void abandonRejected()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto runtime = kernel.registerPlugin("a", [](Context) { return completedTask(); }).value();
    kernel.mount(runtime).value();
    scheduler->queue.runUntilIdle().value();
    auto scope = kernel.reserveReplacement({runtime.id()}).value();
    scheduler->reject = true;
    std::thread worker([guard = std::move(scope)] {});
    worker.join();
    const auto retained = kernel.replacements().value();
    CHECK(retained.size() == 1 && retained[0].abandoned && retained[0].deliveryBlocked &&
          retained[0].state == ReplacementScopeState::Failed);
    CHECK(!kernel.mount(runtime).hasValue());
    scheduler->reject = false;
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent && kernel.replacements().value().empty());
}
void moveReentry()
{
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto a = kernel.registerPlugin("a", [](Context) { return completedTask(); }).value();
    auto b = kernel.registerPlugin("b", [](Context) { return completedTask(); }).value();
    auto c = kernel.registerPlugin("c", [](Context) { return completedTask(); }).value();
    kernel.mount(a).value();
    kernel.mount(b).value();
    kernel.mount(c).value();
    scheduler->queue.runUntilIdle().value();
    auto first = kernel.reserveReplacement({a.id()}).value();
    auto second = kernel.reserveReplacement({b.id()}).value();
    auto third = kernel.reserveReplacement({c.id()}).value();
    const auto expected = third.snapshot().value().operation;
    scheduler->onPost = [&] { first = std::move(third); };
    first = std::move(second);
    CHECK(first.snapshot().value().operation == expected && kernel.replacements().value().size() == 1);
    CHECK(!third.snapshot().hasValue() && !second.snapshot().hasValue());
    first.release().value();
    scheduler->queue.runUntilIdle().value();
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
}
void contractResume()
{
    Scene s;
    auto a = s.provider();
    s.kernel->mount(a).value();
    auto other = s.kernel
                     ->registerPlugin("other", [](Context) { return completedTask(); },
                                      {ServiceRequirement::of<int>({"x"})}, {ServiceOutput::of<int>({"a"}, false)})
                     .value();
    auto actor = s.kernel->mount(other).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    auto host = s.kernel->services().stage({"x"}, std::make_shared<int>(2)).value();
    host.publish().value();
    s.run();
    CHECK(actor.snapshot().value().state == InstanceState::Pending && !actor.update("next", 1).hasValue());
    scope.release().value();
    s.run();
    CHECK(actor.snapshot().value().state == InstanceState::Active);
    auto stopped = actor.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    host.retire().value();
    host.release().value();
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "admission")
            admission();
        else if (name == "freeze")
            freeze();
        else if (name == "ordered")
            ordered();
        else if (name == "inflight")
            inflight();
        else if (name == "pending")
            pending();
        else if (name == "parallel")
            parallel();
        else if (name == "overlap")
            overlap();
        else if (name == "unbound")
            unbound();
        else if (name == "children")
            children();
        else if (name == "stale")
            stale();
        else if (name == "cleanup_failure")
            cleanupFailure();
        else if (name == "abandoned")
            abandoned();
        else if (name == "owner_gone")
            ownerGone();
        else if (name == "cross_thread")
            crossThread();
        else if (name == "guards")
            guards();
        else if (name == "shutdown")
            shutdown();
        else if (name == "mount_close")
            mountClose();
        else if (name == "inject_close")
            injectClose();
        else if (name == "mount_claim")
            mountClaim();
        else if (name == "facade_reentry")
            facadeReentry();
        else if (name == "release_failure")
            releaseFailure();
        else if (name == "release_reentry")
            releaseReentry();
        else if (name == "contract_resume")
            contractResume();
        else if (name == "abandon_rejected")
            abandonRejected();
        else if (name == "move_reentry")
            moveReentry();
        else
            throw std::runtime_error("unknown replacement scope case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
