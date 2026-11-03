/** @file replacement_stage.cpp
 * @brief 真实 Core 实例在旧闭包静止后隔离创建；尚非模块/配置整体提交验收。
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
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    void run() { scheduler->runUntilIdle(30000).value(); }
    Runtime provider(const char* name, int value = 7)
    {
        return kernel
            ->registerPlugin(name,
                             [value](Context context) {
                                 context.provide({"a"}, std::make_shared<int>(value)).value();
                                 return completedTask();
                             },
                             {}, {ServiceOutput::of<int>({"a"})})
            .value();
    }
    ReplacementScope stopped(const Runtime& runtime)
    {
        auto scope = kernel->reserveReplacement({runtime.id()}).value();
        auto task = scope.stop();
        run();
        CHECK(task.tryResult()->value().quiescent);
        return scope;
    }
    void close()
    {
        auto task = kernel->close();
        run();
        CHECK(task.tryResult()->value().quiescent && kernel->replacements().value().empty());
    }
};
void isolation()
{
    Scene s;
    auto old = s.provider("old"), next = s.provider("next", 42);
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
    auto before = s.kernel->mount(old).value();
    auto previous = s.kernel->mount(consumer).value();
    s.run();
    CHECK(seen == 7);
    auto scope = s.stopped(old);
    auto c = scope.stage(consumer).value();
    s.run();
    CHECK(c.snapshot().value().state == InstanceState::Pending);
    auto p = scope.stage(next).value();
    s.run();
    CHECK(seen == 42 && p.ready().tryResult()->hasValue() && c.ready().tryResult()->hasValue());
    CHECK(before.snapshot().value().state == InstanceState::Disposed &&
          previous.snapshot().value().state == InstanceState::Disposed);
    CHECK(!s.kernel->services().find<int>({"a"}).hasValue() &&
          s.kernel->services().snapshot().value()[0].state == BindingState::Held);
    CHECK(!s.kernel->mount(next).hasValue() && !s.kernel->mount(old).hasValue() && !p.stop().tryResult()->hasValue() &&
          !c.update("x", 1).hasValue());
    CHECK(!scope.release().hasValue());
    auto clean = scope.discard();
    s.run();
    CHECK(clean.tryResult()->value().quiescent);
    CHECK(p.snapshot().value().state == InstanceState::Disposed &&
          c.snapshot().value().state == InstanceState::Disposed);
    scope.release().value();
    CHECK(s.kernel->services().snapshot().value().empty());
    s.close();
}
void cleanup(bool fail)
{
    Scene s;
    auto old = s.provider("old");
    s.kernel->mount(old).value();
    s.run();
    Promise<void> gate;
    auto next = s.kernel
                    ->registerPlugin("next",
                                     [&](Context context) {
                                         context.provide({"a"}, std::make_shared<int>(42)).value();
                                         context.onCleanup([&](CleanupContext) { return gate.task(); }).value();
                                         return completedTask();
                                     },
                                     {}, {ServiceOutput::of<int>({"a"})})
                    .value();
    auto scope = s.stopped(old);
    auto p = scope.stage(next).value();
    s.run();
    auto clean = scope.discard();
    s.run();
    CHECK(!clean.isReady() && !scope.release().hasValue() && !s.kernel->mount(next).hasValue());
    if (fail)
        gate.reject(makeError(ErrorCode::CleanupFailed, "retained"));
    else
        gate.resolve();
    s.run();
    CHECK(clean.tryResult()->value().quiescent == !fail);
    if (fail) {
        const auto state = scope.snapshot().value();
        CHECK(state.state == ReplacementScopeState::Failed && state.failure &&
              state.failure->code == ErrorCode::CleanupFailed && state.cleanup->quiescent &&
              !scope.release().hasValue());
        auto closed = s.kernel->close();
        s.run();
        CHECK(!closed.tryResult()->value().quiescent);
        return;
    }
    CHECK(p.snapshot().value().state == InstanceState::Disposed);
    scope.release().value();
    s.close();
}
void children()
{
    Scene s;
    auto old = s.provider("old");
    s.kernel->mount(old).value();
    s.run();
    std::optional<Instance> child;
    std::optional<Context> parentContext;
    Runtime nested(
        [](Context context) {
            context.provide({"inner"}, std::make_shared<int>(9)).value();
            return completedTask();
        },
        {}, {ServiceOutput::of<int>({"inner"})});
    auto parent = s.kernel
                      ->registerPlugin("parent",
                                       [&](Context context) {
                                           parentContext = context;
                                           child.emplace(context.inject(nested).value());
                                           return completedTask();
                                       })
                      .value();
    auto scope = s.stopped(old);
    auto root = scope.stage(parent).value();
    s.run();
    CHECK(root.ready().tryResult()->hasValue() && child && child->ready().tryResult()->hasValue());
    CHECK(!s.kernel->services().find<int>({"inner"}).hasValue() && !child->checkMutation().hasValue() &&
          scope.snapshot().value().reservedOutputs.size() == 2);
    bool blocked = false;
    try {
        Instance::mount(nested, s.scheduler, {}, std::make_shared<ServiceRegistry>(s.kernel->services()));
    } catch (const std::invalid_argument&) {
        blocked = true;
    }
    CHECK(blocked);
    auto clean = scope.discard();
    s.run();
    CHECK(clean.tryResult()->value().quiescent && child->snapshot().value().state == InstanceState::Disposed &&
          !parentContext->identity().hasValue());
    scope.release().value();
    s.close();
}
void guards()
{
    Scene s;
    auto old = s.provider("old"), next = s.provider("next");
    s.kernel->mount(old).value();
    s.run();
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    CHECK(!scope.stage(next).hasValue());
    bool wrong = false;
    std::thread worker([&] { wrong = scope.stage(next).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    auto task = scope.stop();
    s.run();
    CHECK(task.tryResult()->value().quiescent);
    Runtime foreign([](Context) { return completedTask(); });
    CHECK(!scope.stage(foreign).hasValue() && scope.snapshot().value().state == ReplacementScopeState::Stopped);
    auto invalid = RealmMap{}.isolate("a", RealmId{999999}).value();
    CHECK(!scope.stage(next, {}, invalid).hasValue());
    auto clean = scope.discard();
    s.run();
    CHECK(clean.tryResult()->value().quiescent);
    scope.release().value();
    s.close();
}
void failure()
{
    Scene s;
    auto old = s.provider("old");
    s.kernel->mount(old).value();
    s.run();
    auto next =
        s.kernel
            ->registerPlugin("next",
                             [](Context) { return failedTask(makeError(ErrorCode::CallbackFailed, "setup failed")); })
            .value();
    auto scope = s.stopped(old);
    auto p = scope.stage(next).value();
    s.run();
    CHECK(!p.ready().tryResult()->hasValue());
    auto clean = scope.discard();
    s.run();
    CHECK(clean.tryResult()->value().quiescent);
    scope.release().value();
    s.close();
}
void reuse()
{
    Scene s;
    auto old = s.provider("old"), next = s.provider("next");
    s.kernel->mount(old).value();
    s.run();
    auto scope = s.stopped(old);
    auto first = scope.stage(next).value();
    s.run();
    auto context = first.whenActiveAfter().tryResult()->value();
    auto clean = scope.discard();
    s.run();
    CHECK(clean.tryResult()->value().quiescent);
    auto second = scope.stage(old).value();
    s.run();
    CHECK(second.ready().tryResult()->hasValue() && !context.identity().hasValue());
    auto again = scope.discard();
    s.run();
    CHECK(again.tryResult()->value().quiescent);
    scope.release().value();
    CHECK(s.kernel->mount(next).hasValue());
    s.run();
    s.close();
}
void shutdown()
{
    Scene s;
    auto old = s.provider("old");
    s.kernel->mount(old).value();
    s.run();
    Promise<void> gate;
    std::optional<Task<CleanupReport>> closed;
    auto next = s.kernel
                    ->registerPlugin("next",
                                     [&](Context context) {
                                         context.onCleanup([&](CleanupContext) { return gate.task(); }).value();
                                         closed.emplace(s.kernel->close());
                                         return completedTask();
                                     })
                    .value();
    auto scope = s.stopped(old);
    auto p = scope.stage(next).value();
    s.run();
    CHECK(closed && !closed->isReady());
    gate.resolve();
    s.run();
    CHECK(closed->tryResult()->value().quiescent && scope.snapshot().value().cancelled &&
          s.kernel->replacements().value().empty());
    CHECK(p.snapshot().value().state == InstanceState::Disposed);
}
void parallel()
{
    Scene s;
    const auto left = RealmMap{}.isolate("a", s.kernel->services().createRealm().value()).value();
    const auto right = RealmMap{}.isolate("a", s.kernel->services().createRealm().value()).value();
    auto a = s.provider("a"), b = s.provider("b"), nextA = s.provider("nextA"), nextB = s.provider("nextB");
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
    auto one = first.stop(), two = second.stop();
    s.run();
    CHECK(one.tryResult()->value().quiescent && two.tryResult()->value().quiescent);
    auto ac = first.stage(consumer, {}, left).value();
    auto rightConsumer = second.stage(consumer, {}, right);
    if (!rightConsumer.hasValue())
        throw std::runtime_error("right consumer: " + rightConsumer.error().message);
    auto bc = std::move(rightConsumer).value();
    auto ap = first.stage(nextA, {}, left).value(), bp = second.stage(nextB, {}, right).value();
    s.run();
    CHECK(ac.ready().tryResult()->hasValue() && bc.ready().tryResult()->hasValue() &&
          ap.ready().tryResult()->hasValue() && bp.ready().tryResult()->hasValue());
    CHECK(s.kernel->mount(consumer).hasValue());
    auto done = first.discard();
    s.run();
    CHECK(done.tryResult()->value().quiescent);
    first.release().value();
    CHECK(second.snapshot().value().state == ReplacementScopeState::Staging && !bp.checkMutation().hasValue());
    auto rest = second.discard();
    s.run();
    CHECK(rest.tryResult()->value().quiescent);
    second.release().value();
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "isolation")
            isolation();
        else if (name == "cleanup")
            cleanup(false);
        else if (name == "cleanup_failure")
            cleanup(true);
        else if (name == "children")
            children();
        else if (name == "guards")
            guards();
        else if (name == "failure")
            failure();
        else if (name == "reuse")
            reuse();
        else if (name == "shutdown")
            shutdown();
        else if (name == "parallel")
            parallel();
        else
            throw std::runtime_error("unknown staging case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
