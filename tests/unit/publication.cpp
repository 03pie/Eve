/** @file publication.cpp
 * @brief 托管发布组的真实激活、可见性、租约、失败与清理边界。
 */
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
    void close()
    {
        auto closed = kernel.close();
        run();
        CHECK(closed.tryResult() && closed.tryResult()->value().quiescent);
        CHECK(kernel.services().snapshot().value().empty());
    }
    Runtime provider(const std::string& name = "a")
    {
        return kernel
            .registerPlugin(name,
                            [name](Context context) {
                                context.provide({name}, std::make_shared<int>(7)).value();
                                return completedTask();
                            },
                            {}, {ServiceOutput::of<int>({name})})
            .value();
    }
};
void closure()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    Promise<void> gate;
    bool internal = false, external = false;
    std::optional<ServiceRef<int>> direct;
    auto provider = scene.provider();
    auto consumer = scene.kernel
                        .registerPlugin("inside",
                                        [&](Context context) {
                                            // 显式传入规范索引也必须保留实例的组内视图。
                                            context.consume<int>(scene.kernel.services(), {"a"})
                                                .value()
                                                .withService([&](int& value) { internal = value == 7; })
                                                .value();
                                            direct.emplace(context.consume<int>({"a"}).value());
                                            direct->withService([](int& value) { CHECK(value == 7); }).value();
                                            auto invoked = direct->invoke([](int&) { return completedTask(); });
                                            context.provide({"b"}, std::make_shared<int>(8)).value();
                                            return gate.task();
                                        },
                                        {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})})
                        .value();
    auto outside = scene.kernel
                       .registerPlugin("outside",
                                       [&](Context) {
                                           CHECK(scene.kernel.services().find<int>({"b"}).hasValue());
                                           external = true;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"a"})})
                       .value();
    auto observed = scene.kernel.mount(outside).value();
    auto p = scene.kernel.mountHeld(group, provider).value();
    auto c = scene.kernel.mountHeld(group, consumer).value();
    CHECK(!group.commit().hasValue());
    scene.run();
    CHECK(internal && !external && p.ready().tryResult()->hasValue() && !c.ready().isReady());
    CHECK(!scene.kernel.services().find<int>({"a"}).hasValue());
    CHECK(!scene.kernel.services().find<int>({"b"}).hasValue());
    CHECK(!group.commit().hasValue() && group.snapshot().value().heldBindings == 1);
    gate.resolve();
    scene.run();
    CHECK(c.ready().tryResult()->hasValue() && !external && group.snapshot().value().heldBindings == 2);
    CHECK(scene.kernel.services().snapshot().value()[0].state == BindingState::Held);
    group.commit().value();
    CHECK(scene.kernel.services().find<int>({"a"}).hasValue() && scene.kernel.services().find<int>({"b"}).hasValue());
    CHECK(!external);
    scene.run();
    CHECK(external && observed.ready().tryResult()->hasValue());
    group.commit().value();
    CHECK(!group.abort().hasValue() && !scene.kernel.mountHeld(group, provider).hasValue());
    scene.close();
}
void abortCleanup()
{
    Scene scene;
    auto external = scene.kernel.services().stage({"public"}, std::make_shared<int>(2)).value();
    external.publish().value();
    auto group = scene.kernel.holdPublication().value();
    Promise<void> inFlight, cleanupGate;
    auto call = completedTask();
    std::optional<ServiceRef<int>> held, publicRef;
    bool cleaned = false;
    auto provider = scene.provider();
    auto consumer =
        scene.kernel
            .registerPlugin("consumer",
                            [&](Context context) {
                                held.emplace(context.consume<int>({"a"}).value());
                                publicRef.emplace(context.consume<int>({"public"}).value());
                                call = held->invoke([&](int&) { return inFlight.task(); });
                                context
                                    .onCleanup([&](CleanupContext cleanup) {
                                        cleanup.withService(*held, [](int& value) { CHECK(value == 7); }).value();
                                        cleanup.withService(*publicRef, [](int& value) { CHECK(value == 2); }).value();
                                        cleaned = true;
                                        return cleanupGate.task();
                                    })
                                    .value();
                                return completedTask();
                            },
                            {ServiceRequirement::of<int>({"a"}), ServiceRequirement::of<int>({"public"})})
            .value();
    auto p = scene.kernel.mountHeld(group, provider).value();
    auto c = scene.kernel.mountHeld(group, consumer).value();
    scene.run();
    CHECK(c.ready().tryResult()->hasValue() && !call.isReady());
    group.abort().value();
    CHECK(!held->withService([](int&) {}).hasValue() && !publicRef->withService([](int&) {}).hasValue());
    CHECK(!group.commit().hasValue() && !scene.kernel.mountHeld(group, provider).hasValue());
    group.abort().value();
    auto stopped = c.stop();
    scene.run();
    CHECK(!stopped.isReady() && cleaned);
    cleanupGate.resolve();
    scene.run();
    CHECK(!stopped.isReady() && !call.isReady());
    inFlight.resolve();
    scene.run();
    CHECK(call.tryResult()->hasValue() && stopped.tryResult()->value().quiescent &&
          p.stop().tryResult()->value().quiescent);
    external.retire().value();
    external.release().value();
    scene.close();
}
void failure()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto provider = scene.provider();
    auto failed =
        scene.kernel
            .registerPlugin(
                "failed", [](Context) { return failedTask(makeError(ErrorCode::ActivationFailed, "consumer failed")); },
                {ServiceRequirement::of<int>({"a"})})
            .value();
    scene.kernel.mountHeld(group, provider).value();
    auto consumer = scene.kernel.mountHeld(group, failed).value();
    scene.run();
    CHECK(!consumer.ready().tryResult()->hasValue());
    CHECK(!group.commit().hasValue() && !scene.kernel.services().find<int>({"a"}).hasValue());
    group.abort().value();
    scene.close();
}
void groups()
{
    Scene scene;
    auto first = scene.kernel.holdPublication().value(), second = scene.kernel.holdPublication().value();
    auto provider = scene.provider();
    scene.kernel.mountHeld(first, provider).value();
    bool activated = false;
    auto runtime = scene.kernel
                       .registerPlugin("other",
                                       [&](Context) {
                                           activated = true;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"a"})})
                       .value();
    auto instance = scene.kernel.mountHeld(second, runtime).value();
    scene.run();
    CHECK(!activated && !second.commit().hasValue());
    first.commit().value();
    scene.run();
    CHECK(activated && instance.ready().tryResult()->hasValue());
    second.commit().value();
    scene.close();
}
void stoppedMember()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto runtime = scene.kernel.registerPlugin("none", [](Context) { return completedTask(); }).value();
    auto member = scene.kernel.mountHeld(group, runtime).value();
    scene.run();
    member.stop();
    scene.run();
    scene.kernel.snapshot().value();
    CHECK(!group.commit().hasValue() && group.snapshot().value().members == 1);
    group.abort().value();
    scene.close();
}
void liveKey()
{
    Scene scene;
    auto runtime = scene.provider();
    auto current = scene.kernel.mount(runtime).value();
    scene.run();
    auto group = scene.kernel.holdPublication().value();
    auto candidate = scene.kernel.mountHeld(group, runtime).value();
    scene.run();
    CHECK(!candidate.ready().tryResult()->hasValue() && !group.commit().hasValue());
    CHECK(current.snapshot().value().state == InstanceState::Active);
    scene.kernel.services().find<int>({"a"}).value().withService([](int& value) { CHECK(value == 7); }).value();
    group.abort().value();
    scene.close();
}
void abortFailure()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto runtime = scene.kernel
                       .registerPlugin("failure",
                                       [](Context context) {
                                           context.provide({"a"}, std::make_shared<int>(1)).value();
                                           context
                                               .onCleanup([](CleanupContext) {
                                                   return failedTask(makeError(ErrorCode::CleanupFailed,
                                                                               "deliberate cleanup failure"));
                                               })
                                               .value();
                                           return completedTask();
                                       },
                                       {}, {ServiceOutput::of<int>({"a"})})
                       .value();
    auto member = scene.kernel.mountHeld(group, runtime).value();
    scene.run();
    group.abort().value();
    auto stopped = member.stop();
    scene.run();
    CHECK(!stopped.tryResult()->value().quiescent && !group.commit().hasValue());
    CHECK(scene.kernel.services().snapshot().value()[0].state == BindingState::Retiring);
    auto closed = scene.kernel.close();
    scene.run();
    CHECK(!closed.tryResult()->value().quiescent);
}
void children()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto childRuntime =
        scene.kernel
            .registerPlugin(
                "child",
                [](Context context) {
                    context.consume<int>({"a"}).value().withService([](int& value) { CHECK(value == 7); }).value();
                    return completedTask();
                },
                {ServiceRequirement::of<int>({"a"})})
            .value();
    std::optional<Instance> child;
    auto parentRuntime = scene.kernel
                             .registerPlugin("parent",
                                             [&](Context context) {
                                                 context.provide({"a"}, std::make_shared<int>(7)).value();
                                                 child.emplace(context.inject(childRuntime).value());
                                                 return completedTask();
                                             },
                                             {}, {ServiceOutput::of<int>({"a"})})
                             .value();
    auto parent = scene.kernel.mountHeld(group, parentRuntime).value();
    scene.run();
    CHECK(child->ready().tryResult()->hasValue() && group.snapshot().value().members == 2);
    group.commit().value();
    parent.stop();
    scene.run();
    CHECK(!scene.kernel.remount(*child).hasValue());
    auto remounted = scene.kernel.remount(parent).value();
    scene.run();
    CHECK(remounted.ready().tryResult()->hasValue());
    scene.close();
}
void guards()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto runtime = scene.provider();
    CHECK(!group.commit().hasValue());
    Kernel other(scene.scheduler);
    auto foreign = other.holdPublication().value();
    CHECK(!scene.kernel.mountHeld(foreign, runtime).hasValue());
    bool rejected = false;
    std::thread worker([&] {
        rejected = !group.commit().hasValue() && !group.abort().hasValue() && !group.snapshot().hasValue() &&
                   !scene.kernel.mountHeld(group, runtime).hasValue();
    });
    worker.join();
    CHECK(rejected);
    auto moved = std::move(group);
    CHECK(!group.snapshot().hasValue() && !group.abort().hasValue());
    moved.abort().value();
    other.close();
    scene.close();
}
void cycle()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    int started = 0;
    auto a = scene.kernel
                 .registerPlugin("a",
                                 [&](Context) {
                                     ++started;
                                     return completedTask();
                                 },
                                 {ServiceRequirement::of<int>({"b"})}, {ServiceOutput::of<int>({"a"})})
                 .value();
    auto b = scene.kernel
                 .registerPlugin("b",
                                 [&](Context) {
                                     ++started;
                                     return completedTask();
                                 },
                                 {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})})
                 .value();
    auto one = scene.kernel.mountHeld(group, a).value(), two = scene.kernel.mountHeld(group, b).value();
    scene.run();
    CHECK(started == 0 && one.ready().tryResult()->error().code == ErrorCode::DependencyCycle &&
          two.ready().tryResult()->error().code == ErrorCode::DependencyCycle);
    CHECK(!group.commit().hasValue());
    group.abort().value();
    scene.close();
}
void notification(bool reject, bool reassign = false)
{
    struct Scheduler final : IScheduler {
        FakeScheduler queue;
        bool reject = false;
        std::function<void()> onPost;
        bool post(ScheduledJob job) override
        {
            if (onPost)
                onPost();
            return !reject && queue.post(std::move(job));
        }
        bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
        bool isOpen() const override { return queue.isOpen(); }
    };
    auto scheduler = std::make_shared<Scheduler>();
    Kernel kernel(scheduler);
    auto group = kernel.holdPublication().value();
    auto handle = std::make_unique<PublicationGroup>(group);
    auto other = kernel.holdPublication().value();
    auto runtime = kernel
                       .registerPlugin("ready",
                                       [](Context context) {
                                           context.provide({"a"}, std::make_shared<int>(1)).value();
                                           context.provide({"b"}, std::make_shared<int>(2)).value();
                                           return completedTask();
                                       },
                                       {}, {ServiceOutput::of<int>({"a"}), ServiceOutput::of<int>({"b"})})
                       .value();
    kernel.mountHeld(group, runtime).value();
    scheduler->queue.runUntilIdle().value();
    scheduler->reject = reject;
    bool reentered = false;
    scheduler->onPost = [&] {
        reentered = true;
        CHECK(kernel.services().find<int>({"a"}).hasValue() && kernel.services().find<int>({"b"}).hasValue());
        CHECK(!group.commit().hasValue());
        if (reassign)
            *handle = other;
    };
    CHECK(handle->commit().hasValue() == !reject);
    CHECK(group.snapshot().value().state == PublicationState::Published);
    CHECK(reentered && group.snapshot().value().deliveryBlocked == reject);
    CHECK(other.snapshot().value().state == PublicationState::Open && !other.snapshot().value().deliveryBlocked);
    scheduler->onPost = {};
    CHECK(group.commit().hasValue() == !reject);
    scheduler->reject = false;
    auto stopped = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(stopped.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "closure")
            closure();
        else if (name == "abort_cleanup")
            abortCleanup();
        else if (name == "failure")
            failure();
        else if (name == "groups")
            groups();
        else if (name == "stopped_member")
            stoppedMember();
        else if (name == "live_key")
            liveKey();
        else if (name == "abort_failure")
            abortFailure();
        else if (name == "children")
            children();
        else if (name == "guards")
            guards();
        else if (name == "cycle")
            cycle();
        else if (name == "notification_failure")
            notification(true);
        else if (name == "notification_reentry")
            notification(false);
        else if (name == "notification_reassign")
            notification(false, true);
        else
            throw std::runtime_error("unknown publication case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
