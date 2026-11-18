/** @file publication.cpp
 * @brief 托管发布组的真实激活、可见性、租约、失败与清理边界。
 */
#include "../support/notification_scheduler.hpp"
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
using namespace cordis;
using cordis::test::NotificationScheduler;
using cordis::testing::FakeScheduler;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

bool publicationHeld(Kernel& kernel, InstanceId instance)
{
    const auto graph = kernel.services().dependencyGraph().value();
    const auto found = std::find_if(graph.instances.begin(), graph.instances.end(),
                                    [instance](const auto& row) { return row.instance == instance; });
    CHECK(found != graph.instances.end());
    return found->publicationHeld;
}

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
    auto scheduler = std::make_shared<NotificationScheduler>();
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
    const auto member = kernel.mountHeld(group, runtime).value();
    scheduler->queue.runUntilIdle().value();
    const auto identity = member.snapshot().value().instance;
    CHECK(publicationHeld(kernel, identity));
    scheduler->reject = reject;
    bool reentered = false;
    scheduler->onPost = [&] {
        reentered = true;
        CHECK(kernel.services().find<int>({"a"}).hasValue() && kernel.services().find<int>({"b"}).hasValue());
        CHECK(!group.commit().hasValue());
        CHECK(publicationHeld(kernel, identity));
        const auto closure = kernel.replacementClosure({runtime.id()});
        CHECK(!closure.hasValue() && closure.error().code == ErrorCode::ConfigConflict);
        if (reassign)
            *handle = other;
    };
    CHECK(handle->commit().hasValue() == !reject);
    scheduler->checkHook();
    CHECK(group.snapshot().value().state == PublicationState::Published);
    CHECK(reentered && group.snapshot().value().deliveryBlocked == reject);
    CHECK(publicationHeld(kernel, identity) == reject);
    CHECK(other.snapshot().value().state == PublicationState::Open && !other.snapshot().value().deliveryBlocked);
    scheduler->onPost = {};
    CHECK(group.commit().hasValue() == !reject);
    scheduler->reject = false;
    auto stopped = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(stopped.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
}
/** @brief 没有成员或 Binding 的组也通知原索引；失败不撤销其他真实实例的清理能力。 */
void abortNotification(bool reject, bool reassign = false)
{
    auto scheduler = std::make_shared<NotificationScheduler>();
    Kernel kernel(scheduler);
    auto group = kernel.holdPublication().value();
    auto other = kernel.holdPublication().value();
    auto handle = std::make_unique<PublicationGroup>(group);
    const auto runtime = kernel.registerPlugin("observer", [](Context) { return completedTask(); }).value();
    const auto observer = kernel.mount(runtime).value();
    scheduler->queue.runUntilIdle().value();
    CHECK(observer.ready().tryResult()->hasValue() && kernel.services().snapshot().value().empty());
    const auto identity = observer.snapshot().value().instance;
    CHECK(!publicationHeld(kernel, identity));
    CHECK(group.snapshot().value().members == 0);
    bool notified = false;
    scheduler->reject = reject;
    scheduler->onPost = [&] {
        notified = true;
        const auto state = group.snapshot().value();
        CHECK(state.state == PublicationState::Aborted && state.deliveryBlocked);
        const auto nested = group.abort();
        CHECK(!nested.hasValue() && nested.error().code == ErrorCode::SchedulerClosed);
        CHECK(!publicationHeld(kernel, identity));
        if (reassign)
            *handle = other;
    };
    const auto aborted = handle->abort();
    scheduler->checkHook();
    CHECK(aborted.hasValue() == !reject && notified);
    CHECK(!reject || aborted.error().code == ErrorCode::SchedulerClosed);
    CHECK(group.snapshot().value().state == PublicationState::Aborted &&
          group.snapshot().value().deliveryBlocked == reject);
    CHECK(other.snapshot().value().state == PublicationState::Open && !other.snapshot().value().deliveryBlocked);
    scheduler->onPost = {};
    scheduler->reject = false;
    // 恢复接收任务也不能清掉原终态的投递故障或以幂等成功掩盖它。
    const auto repeated = group.abort();
    CHECK(repeated.hasValue() == !reject);
    CHECK(!reject || repeated.error().code == ErrorCode::SchedulerClosed);
    CHECK(group.snapshot().value().deliveryBlocked == reject);
    const auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
}
/** @brief 无服务输出的两个真实成员均收到停止；清理失败后原声明仍保持不可呈现。 */
void abortNoOutputFailure()
{
    Scene scene;
    auto group = scene.kernel.holdPublication().value();
    auto cleaned = std::make_shared<unsigned>(0);
    const auto runtime = scene.kernel
                             .registerPlugin("no-output",
                                             [cleaned](Context context) {
                                                 context
                                                     .onCleanup([cleaned](CleanupContext) {
                                                         ++*cleaned;
                                                         return failedTask(makeError(ErrorCode::CleanupFailed,
                                                                                     "no-output cleanup failure"));
                                                     })
                                                     .value();
                                                 return completedTask();
                                             })
                             .value();
    const auto first = scene.kernel.mountHeld(group, runtime).value();
    const auto second = scene.kernel.mountHeld(group, runtime).value();
    scene.run();
    CHECK(first.ready().tryResult()->hasValue() && second.ready().tryResult()->hasValue());
    CHECK(scene.kernel.services().snapshot().value().empty());
    group.abort().value();
    CHECK(group.snapshot().value().state == PublicationState::Aborted && !group.snapshot().value().deliveryBlocked);
    CHECK(publicationHeld(scene.kernel, first.snapshot().value().instance) &&
          publicationHeld(scene.kernel, second.snapshot().value().instance));
    const auto firstClosed = first.stop(), secondClosed = second.stop();
    scene.run();
    CHECK(*cleaned == 2 && firstClosed.tryResult() && secondClosed.tryResult());
    CHECK(!firstClosed.tryResult()->value().quiescent && !secondClosed.tryResult()->value().quiescent);
    const auto graph = scene.kernel.services().dependencyGraph().value();
    CHECK(graph.bindings.empty() && graph.instances.size() == 2 &&
          std::all_of(graph.instances.begin(), graph.instances.end(),
                      [](const auto& row) { return row.publicationHeld; }));
    const auto closed = scene.kernel.close();
    scene.run();
    CHECK(closed.tryResult() && !closed.tryResult()->value().quiescent);
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
        else if (name == "abort_notification")
            abortNotification(false);
        else if (name == "abort_notification_failure")
            abortNotification(true);
        else if (name == "abort_notification_reassign")
            abortNotification(false, true);
        else if (name == "abort_no_output_failure")
            abortNoOutputFailure();
        else
            throw std::runtime_error("unknown publication case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
