/** @file instance_finalizers.cpp
 * @brief 验证依赖本代资源静止的所有者销毁、换代和失败保留。
 */
#include <cordis/instance.hpp>
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
void order()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> resource, finalize;
    bool entered = false;
    Runtime runtime([&](Context context) {
        CHECK(context
                  .afterCleanup([&] {
                      entered = true;
                      return finalize.task();
                  })
                  .hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { return resource.task(); }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!entered && !stopped.isReady());
    resource.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(entered && !stopped.isReady());
    CHECK(instance.snapshot().value().finalizing);
    finalize.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent);
    CHECK(!instance.snapshot().value().finalizers);
}
void failure(bool finalizer)
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> retained = owner;
    int calls = 0;
    Runtime runtime([&](Context context) {
        CHECK(context
                  .afterCleanup([keep = owner, &calls, finalizer] {
                      ++calls;
                      return finalizer ? failedTask(makeError(ErrorCode::CleanupFailed, "finalizer failure"))
                                       : completedTask();
                  })
                  .hasValue());
        if (!finalizer)
            CHECK(context
                      .onCleanup([](CleanupContext) {
                          return failedTask(makeError(ErrorCode::CleanupFailed, "resource failure"));
                      })
                      .hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    owner.reset();
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.tryResult()->value().quiescent && !retained.expired());
    CHECK(calls == (finalizer ? 1 : 0));
    const auto snapshot = instance.snapshot().value();
    CHECK(snapshot.finalizers && snapshot.finalizing == finalizer && snapshot.hasRetainedResources());
    CHECK(!instance.retry().hasValue());
}
void lifo(bool fail)
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    bool owner = false, inner = false;
    Runtime runtime([&](Context context) {
        CHECK(context
                  .afterCleanup([&] {
                      owner = true;
                      return completedTask();
                  })
                  .hasValue());
        CHECK(context
                  .afterCleanup([&] {
                      inner = true;
                      return fail ? failedTask(makeError(ErrorCode::CleanupFailed, "inner retained")) : gate.task();
                  })
                  .hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(inner && !owner);
    if (fail) {
        CHECK(!stopped.tryResult()->value().quiescent && instance.snapshot().value().hasRetainedResources());
        return;
    }
    CHECK(!stopped.isReady());
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(owner && stopped.tryResult()->value().quiescent);
}
void epoch()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> first;
    std::optional<Context> old;
    int activations = 0, finalizations = 0;
    Runtime runtime([&](Context context) {
        ++activations;
        if (!old)
            old = context;
        auto config = context.config();
        CHECK(context
                  .afterCleanup([&, config] {
                      ++finalizations;
                      return config == "first" ? first.task() : completedTask();
                  })
                  .hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler, "first");
    CHECK(scheduler->runUntilIdle().hasValue());
    auto change = instance.update("second", 1);
    CHECK(change.hasValue() && scheduler->runUntilIdle().hasValue());
    CHECK(activations == 1 && finalizations == 1 && !change.value().target.isReady());
    CHECK(instance.snapshot().value().finalizing);
    first.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 2 && change.value().target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(old->afterCleanup([] { return completedTask(); }).error().code == ErrorCode::StaleContext);
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && finalizations == 2);
}
void guards()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    std::optional<Context> context;
    Runtime runtime([&](Context value) {
        context = value;
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(context->usesScheduler(scheduler));
    CHECK(!context->usesScheduler(std::make_shared<FakeScheduler>()));
    CHECK(context->afterCleanup({}).error().code == ErrorCode::InvalidArgument);
    bool rejected = false;
    std::thread worker([&] {
        rejected = context->afterCleanup([] { return completedTask(); }).error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(rejected && !instance.snapshot().value().finalizers);
    auto stopped = instance.stop();
    CHECK(!context->afterCleanup([] { return completedTask(); }).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
}
void children()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    bool childFinalized = false, parentFinalized = false;
    Runtime child([&](Context context) {
        CHECK(context.onCleanup([&](CleanupContext) { return gate.task(); }).hasValue());
        CHECK(context
                  .afterCleanup([&] {
                      childFinalized = true;
                      return completedTask();
                  })
                  .hasValue());
        return completedTask();
    });
    Runtime parent([&](Context context) {
        CHECK(context
                  .afterCleanup([&] {
                      CHECK(childFinalized);
                      parentFinalized = true;
                      return completedTask();
                  })
                  .hasValue());
        auto injected = context.inject(child);
        CHECK(injected.hasValue());
        return injected.value().ready();
    });
    auto instance = Instance::mount(parent, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!childFinalized && !parentFinalized && !stopped.isReady());
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(childFinalized && parentFinalized && stopped.tryResult()->value().quiescent);
}
void setup()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> activation;
    bool finalized = false;
    Runtime runtime([&](Context context) {
        CHECK(context
                  .afterCleanup([&] {
                      finalized = true;
                      return completedTask();
                  })
                  .hasValue());
        return activation.task();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.isReady() && !finalized);
    std::thread worker([activation] { activation.resolve(); });
    worker.join();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && finalized);
}
void dependencies()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    bool providerClean = false, consumerTail = false;
    auto provider = kernel
                        .registerPlugin("provider",
                                        [&](Context context) {
                                            context.provide<int>({"value"}, std::make_shared<int>(7)).value();
                                            context
                                                .onCleanup([&](CleanupContext) {
                                                    providerClean = true;
                                                    return completedTask();
                                                })
                                                .value();
                                            return completedTask();
                                        },
                                        {}, {ServiceOutput::of<int>({"value"})})
                        .value();
    auto consumer = kernel
                        .registerPlugin("consumer",
                                        [&](Context context) {
                                            context
                                                .afterCleanup([&] {
                                                    consumerTail = true;
                                                    return gate.task();
                                                })
                                                .value();
                                            return completedTask();
                                        },
                                        {ServiceRequirement::of<int>({"value"})})
                        .value();
    auto a = kernel.mount(provider).value();
    auto b = kernel.mount(consumer).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(a.ready().tryResult()->hasValue() && b.ready().tryResult()->hasValue());
    auto stopped = a.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(consumerTail && !providerClean && !stopped.isReady());
    CHECK(b.snapshot().value().finalizing && b.snapshot().value().committedLeases == 1);
    auto consumerStopped = b.stop();
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && consumerStopped.tryResult()->value().quiescent && providerClean);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}
void delivery()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    Runtime runtime([&](Context context) {
        CHECK(context.afterCleanup([gate] { return gate.task(); }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(instance.snapshot().value().finalizers->effects[0].cleanupPending);
    scheduler->close();
    gate.resolve();
    CHECK(!stopped.isReady());
    CHECK(instance.snapshot().value().state != InstanceState::Disposed);
    CHECK(instance.snapshot().value().finalizers->effects[0].deliveryBlocked &&
          instance.snapshot().value().deliveryBlocked);
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "order")
            order();
        else if (name == "resource_failure")
            failure(false);
        else if (name == "finalizer_failure")
            failure(true);
        else if (name == "lifo")
            lifo(false);
        else if (name == "lifo_failure")
            lifo(true);
        else if (name == "epoch")
            epoch();
        else if (name == "guards")
            guards();
        else if (name == "children")
            children();
        else if (name == "setup")
            setup();
        else if (name == "dependencies")
            dependencies();
        else if (name == "delivery")
            delivery();
        else
            CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
