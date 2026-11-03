/** @file replacement_commit.cpp
 * @brief Core 规范定义绑定和隔离输出一起切换；不充当模块/配置事务验收。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Scheduler : IScheduler {
    testing::FakeScheduler queue;
    std::function<void()> onPost;
    int posts = 0;
    bool reject = false;
    bool post(ScheduledJob job) override
    {
        ++posts;
        auto hook = std::exchange(onPost, {});
        if (hook)
            hook();
        return !reject && queue.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    bool isOpen() const override { return queue.isOpen(); }
};
struct Scene {
    std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    void run() { scheduler->queue.runUntilIdle(30000).value(); }
    void close()
    {
        auto closed = kernel->close();
        run();
        CHECK(closed.tryResult()->value().quiescent && kernel->replacements().value().empty());
    }
    static Runtime provider(int value)
    {
        return Runtime(
            [value](Context context) {
                context.provide({"a"}, std::make_shared<int>(value)).value();
                return completedTask();
            },
            {}, {ServiceOutput::of<int>({"a"})});
    }
    Runtime old() { return kernel->registerRuntime("canonical", provider(7)).value(); }
    RuntimeId named(const std::string& name = "canonical")
    {
        for (const auto& entry : kernel->snapshot().value().names)
            if (entry.name == name)
                return entry.id;
        throw std::runtime_error("missing canonical definition");
    }
    void stop(ReplacementScope& scope)
    {
        auto task = scope.stop();
        run();
        CHECK(task.tryResult()->value().quiescent);
    }
};
void canonical()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    int seen = 0;
    auto consumer =
        s.kernel
            ->registerPlugin(
                "consumer",
                [&](Context context) {
                    context.consume<int>({"a"}).value().withService([&](int& value) { seen = value; }).value();
                    return completedTask();
                },
                {ServiceRequirement::of<int>({"a"})})
            .value();
    auto previous = s.kernel->mount(old).value();
    s.kernel->mount(consumer).value();
    s.run();
    CHECK(seen == 7);
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    CHECK(s.named() == old.id() && !s.kernel->mount(next).hasValue() &&
          s.kernel->registerRuntime("escape", next).error().code == ErrorCode::ConfigConflict);
    s.stop(scope);
    auto p = scope.stage(next).value(), c = scope.stage(consumer).value();
    s.run();
    CHECK(seen == 42 && c.ready().tryResult()->hasValue());
    auto context = p.whenActiveAfter().tryResult()->value();
    scope.prepareCommit().value();
    CHECK(!context.identity().hasValue() && s.named() == old.id());
    bool hostMetadata = false, notified = false;
    s.scheduler->onPost = [&] {
        notified = true;
        CHECK(hostMetadata && s.named() == next.id() && !scope.release().hasValue());
    };
    const auto posts = s.scheduler->posts;
    scope.commitPrepared().value();
    CHECK(!notified && s.scheduler->posts == posts && s.named() == next.id() &&
          s.kernel->snapshot().value().runtimes == 2);
    CHECK(!s.kernel->services().find<int>({"a"}).hasValue() && !p.checkMutation().hasValue());
    hostMetadata = true;
    scope.release().value();
    s.run();
    CHECK(notified && context.identity().hasValue() && p.checkMutation().hasValue());
    CHECK(previous.snapshot().value().instance != p.snapshot().value().instance && !s.kernel->mount(old).hasValue());
    s.kernel->services().find<int>({"a"}).value().withService([](int& value) { CHECK(value == 42); }).value();
    s.close();
}
void recovery()
{
    Scene s;
    auto old = s.old();
    s.kernel->mount(old).value();
    s.run();
    Runtime next([](Context) { return failedTask(makeError(ErrorCode::CallbackFailed, "bad candidate")); });
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    auto failed = scope.stage(next).value();
    s.run();
    CHECK(!failed.ready().tryResult()->hasValue());
    auto discarded = scope.discard();
    s.run();
    CHECK(discarded.tryResult()->value().quiescent);
    auto restored = scope.stage(old).value();
    s.run();
    CHECK(!scope.prepareCommit().hasValue() && s.named() == old.id());
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    scope.commitPrepared().value();
    scope.release().value();
    s.run();
    CHECK(restored.ready().tryResult()->hasValue() && s.named() == old.id() && !s.kernel->mount(next).hasValue());
    s.close();
}
void guards()
{
    Scene s;
    auto a = s.kernel->registerPlugin("a", [](Context) { return completedTask(); }).value(),
         b = s.kernel->registerPlugin("b", [](Context) { return completedTask(); }).value();
    Runtime x([](Context) { return completedTask(); }), y([](Context) { return completedTask(); });
    auto scope = s.kernel->reserveReplacement({a.id(), b.id()}).value();
    CHECK(!scope.prepareDefinitions({{a.id(), x}}).hasValue());
    CHECK(!scope.prepareDefinitions({{a.id(), x}, {a.id(), y}}).hasValue());
    CHECK(!scope.prepareDefinitions({{a.id(), x}, {b.id(), x}}).hasValue());
    CHECK(!scope.prepareDefinitions({{a.id(), a}, {b.id(), y}}).hasValue());
    scope.prepareDefinitions({{a.id(), x}, {b.id(), y}}).value();
    CHECK(!scope.prepareDefinitions({{a.id(), x}, {b.id(), y}}).hasValue());
    bool wrong = false;
    std::thread thread([&] { wrong = scope.prepareCommit().error().code == ErrorCode::WrongThread; });
    thread.join();
    CHECK(wrong);
    s.stop(scope);
    scope.prepareCommit().value();
    const auto posts = s.scheduler->posts;
    scope.commitPrepared().value();
    CHECK(s.scheduler->posts == posts);
    scope.release().value();
    CHECK(s.named("a") == x.id() && s.named("b") == y.id() && s.kernel->snapshot().value().runtimes == 2);
    s.close();
}
void unrelated()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    s.kernel->mount(old).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    scope.stage(next).value();
    s.run();
    scope.prepareCommit().value();
    auto extra = s.kernel->registerPlugin("unrelated", [](Context) { return completedTask(); }).value();
    scope.commitPrepared().value();
    CHECK(s.named() == next.id() && s.named("unrelated") == extra.id());
    scope.release().value();
    s.close();
}
void consumerFailure()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    bool fail = false;
    auto consumer =
        s.kernel
            ->registerPlugin("consumer",
                             [&](Context) {
                                 return fail ? failedTask(makeError(ErrorCode::CallbackFailed, "consumer failed"))
                                             : completedTask();
                             },
                             {ServiceRequirement::of<int>({"a"})})
            .value();
    s.kernel->mount(old).value();
    s.kernel->mount(consumer).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    fail = true;
    auto p = scope.stage(next).value(), c = scope.stage(consumer).value();
    s.run();
    CHECK(p.ready().tryResult()->hasValue() && !c.ready().tryResult()->hasValue());
    CHECK(!scope.prepareCommit().hasValue() && s.named() == old.id() &&
          !s.kernel->services().find<int>({"a"}).hasValue());
    auto discarded = scope.discard();
    s.run();
    CHECK(discarded.tryResult()->value().quiescent);
    scope.release().value();
    s.close();
}
void invalidated()
{
    Scene s;
    auto host = s.kernel->services().stage({"host"}, std::make_shared<int>(1)).value();
    host.publish().value();
    auto old = s.old();
    s.kernel->mount(old).value();
    s.run();
    Runtime next(
        [](Context context) {
            context.provide({"a"}, std::make_shared<int>(42)).value();
            return completedTask();
        },
        {ServiceRequirement::of<int>({"host"})}, {ServiceOutput::of<int>({"a"})});
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    scope.stage(next).value();
    s.run();
    scope.prepareCommit().value();
    host.retire().value();
    s.run();
    CHECK(!scope.commitPrepared().hasValue() && s.named() == old.id());
    auto discarded = scope.discard();
    s.run();
    CHECK(discarded.tryResult()->value().quiescent);
    scope.release().value();
    host.release().value();
    s.close();
}
void shutdown()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    s.kernel->mount(old).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    scope.stage(next).value();
    s.run();
    scope.prepareCommit().value();
    auto closed = s.kernel->close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent && !scope.commitPrepared().hasValue() && s.named() == old.id());
}
void metadataAbort()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    scope.prepareCommit().value();
    auto task = scope.discard();
    CHECK(task.tryResult()->value().quiescent && s.named() == old.id());
    scope.release().value();
    s.close();
}
void bounded()
{
    Scene s;
    auto current = s.old();
    s.kernel->mount(current).value();
    s.run();
    for (int value = 10; value < 20; ++value) {
        auto next = Scene::provider(value);
        auto scope = s.kernel->reserveReplacement({current.id()}).value();
        scope.prepareDefinitions({{current.id(), next}}).value();
        s.stop(scope);
        scope.stage(next).value();
        s.run();
        scope.prepareCommit().value();
        scope.commitPrepared().value();
        scope.release().value();
        s.run();
        auto state = s.kernel->snapshot().value();
        CHECK(state.runtimes == 1 && state.names.size() == 1 && state.instances.size() == 1 &&
              state.names[0].id == next.id());
        current = next;
    }
    s.close();
}
void notificationFailure()
{
    Scene s;
    auto old = s.old(), next = Scene::provider(42);
    s.kernel->mount(old).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    scope.stage(next).value();
    s.run();
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    s.scheduler->reject = true;
    CHECK(!scope.release().hasValue() && scope.snapshot().value().state == ReplacementScopeState::Released &&
          scope.snapshot().value().deliveryBlocked && s.named() == next.id());
    s.scheduler->reject = false;
    CHECK(!scope.release().hasValue());
    s.close();
}
void definitionLifetime()
{
    Scene s;
    std::weak_ptr<int> lifetime;
    RuntimeId previous;
    {
        auto token = std::make_shared<int>(7);
        lifetime = token;
        auto runtime = s.kernel->registerPlugin("canonical", [token](Context) { return completedTask(); }).value();
        previous = runtime.id();
    }
    auto next = Scene::provider(42);
    auto scope = s.kernel->reserveReplacement({previous}).value();
    scope.prepareDefinitions({{previous, next}}).value();
    CHECK(scope.commitPrepared().error().code == ErrorCode::ContextClosing);
    s.stop(scope);
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    CHECK(!lifetime.expired() && s.named() == next.id());
    scope.release().value();
    CHECK(!lifetime.expired());
    scope = ReplacementScope{};
    CHECK(lifetime.expired());
    s.close();
}
void resourceDeliveryFailure()
{
    Scene s;
    auto old = s.old();
    s.kernel->mount(old).value();
    s.run();
    Promise<void> setup;
    Runtime next(
        [&](Context context) {
            context.effect([&](EffectSetupContext) { return setup.task(); }, "late-resource").value();
            context.provide({"a"}, std::make_shared<int>(42)).value();
            return completedTask();
        },
        {}, {ServiceOutput::of<int>({"a"})});
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    s.stop(scope);
    auto candidate = scope.stage(next).value();
    s.run();
    scope.prepareCommit().value();
    s.scheduler->reject = true;
    setup.resolve();
    s.scheduler->reject = false;
    CHECK(candidate.snapshot().value().resources.deliveryBlocked);
    CHECK(!candidate.snapshot().value().deliveryBlocked);
    CHECK(!scope.commitPrepared().hasValue());
    CHECK(s.named() == old.id());
    CHECK(!scope.release().hasValue());
    auto closed = s.kernel->close();
    s.run();
    CHECK(!closed.isReady());
    CHECK(candidate.snapshot().value().hasRetainedResources());
}
void parallel()
{
    Scene s;
    const auto left = RealmMap{}.isolate("a", s.kernel->services().createRealm().value()).value();
    const auto right = RealmMap{}.isolate("a", s.kernel->services().createRealm().value()).value();
    auto a = s.kernel->registerRuntime("a", Scene::provider(1)).value();
    auto b = s.kernel->registerRuntime("b", Scene::provider(2)).value();
    auto consumer =
        s.kernel
            ->registerPlugin("consumer", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})})
            .value();
    s.kernel->mount(a, {}, left).value();
    s.kernel->mount(b, {}, right).value();
    s.kernel->mount(consumer, {}, left).value();
    s.kernel->mount(consumer, {}, right).value();
    s.run();
    auto first = s.kernel->reserveReplacement({a.id()}).value(),
         second = s.kernel->reserveReplacement({b.id()}).value();
    auto nextA = Scene::provider(10), nextB = Scene::provider(20);
    first.prepareDefinitions({{a.id(), nextA}}).value();
    second.prepareDefinitions({{b.id(), nextB}}).value();
    s.stop(first);
    s.stop(second);
    first.stage(nextA, {}, left).value();
    first.stage(consumer, {}, left).value();
    second.stage(nextB, {}, right).value();
    second.stage(consumer, {}, right).value();
    s.run();
    first.prepareCommit().value();
    second.prepareCommit().value();
    first.commitPrepared().value();
    first.release().value();
    second.commitPrepared().value();
    second.release().value();
    s.run();
    CHECK(s.named("a") == nextA.id() && s.named("b") == nextB.id() && s.named("consumer") == consumer.id());
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "canonical")
            canonical();
        else if (name == "recovery")
            recovery();
        else if (name == "guards")
            guards();
        else if (name == "unrelated")
            unrelated();
        else if (name == "consumer_failure")
            consumerFailure();
        else if (name == "invalidated")
            invalidated();
        else if (name == "shutdown")
            shutdown();
        else if (name == "metadata_abort")
            metadataAbort();
        else if (name == "bounded")
            bounded();
        else if (name == "notification_failure")
            notificationFailure();
        else if (name == "definition_lifetime")
            definitionLifetime();
        else if (name == "parallel")
            parallel();
        else if (name == "resource_delivery_failure")
            resourceDeliveryFailure();
        else
            throw std::runtime_error("unknown commit case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
