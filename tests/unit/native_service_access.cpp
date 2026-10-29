/** @file native_service_access.cpp
 * @brief 原生服务桥所需的 Core 旧绑定异步清理、只读快照及自有输出撤回。
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
            throw std::runtime_error(#x);                                                                              \
    } while (false)
void asyncCleanup(bool failure)
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry services(scheduler);
    auto provider = services.stage<int>({"old", 1}, std::make_shared<int>(42)).value();
    CHECK(provider.publish().hasValue());
    std::optional<ServiceRef<int>> ref;
    std::optional<CleanupContext> closing;
    Promise<void> work;
    unsigned calls = 0;
    Runtime runtime([&](Context context) {
        ref = context.consume<int>(services, {"old", 1}).value();
        CHECK(context
                  .onCleanup([&](CleanupContext cleanup) {
                      closing = cleanup;
                      return cleanup.invokeService(*ref, [&](int& value) {
                          CHECK(value == 42);
                          ++calls;
                          return work.task();
                      });
                  })
                  .hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue() && instance.ready().tryResult()->hasValue());
    CHECK(provider.retire().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 1 && !stopped.isReady() && !provider.whenUnused().isReady());
    CHECK(provider.snapshot().value().activeCalls == 1);
    CHECK(!ref->invoke([](int&) { return completedTask(); }).tryResult()->hasValue());
    std::thread worker([&] {
        if (failure)
            work.reject(makeError(ErrorCode::CleanupFailed, "cleanup service rejected"));
        else
            work.resolve();
    });
    worker.join();
    CHECK(provider.snapshot().value().activeCalls == 1 && !stopped.isReady());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent == !failure);
    CHECK(closing->invokeService(*ref, [](int&) { return completedTask(); }).tryResult()->error().code ==
          ErrorCode::StaleContext);
    if (!failure) {
        CHECK(provider.whenUnused().isReady() && provider.release().hasValue());
        CHECK(!ref->snapshot().hasValue());
    } else {
        CHECK(!provider.whenUnused().isReady() && provider.snapshot().value().committedLeases == 1);
    }
}
void guards()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry services(scheduler);
    auto provider = services.stage<int>({"shared", 1}, std::make_shared<int>(1)).value();
    CHECK(provider.publish().hasValue());
    auto ordinary = services.find<int>({"shared", 1}).value();
    std::optional<ServiceRef<int>> first, second;
    std::optional<CleanupContext> closing;
    Promise<void> gate;
    unsigned entered = 0;
    Runtime a([&](Context context) {
        first = context.consume<int>(services, {"shared", 1}).value();
        CHECK(context
                  .onCleanup([&](CleanupContext cleanup) {
                      closing = cleanup;
                      auto callback = [&](int&) {
                          ++entered;
                          return completedTask();
                      };
                      CHECK(cleanup.invokeService(*second, callback).tryResult()->error().code ==
                            ErrorCode::StaleContext);
                      CHECK(cleanup.invokeService(ordinary, callback).tryResult()->error().code ==
                            ErrorCode::StaleContext);
                      std::function<Task<void>(int&)> empty;
                      CHECK(cleanup.invokeService(*first, empty).tryResult()->error().code ==
                            ErrorCode::InvalidArgument);
                      return gate.task();
                  })
                  .hasValue());
        return completedTask();
    });
    Runtime b([&](Context context) {
        second = context.consume<int>(services, {"shared", 1}).value();
        return completedTask();
    });
    auto one = Instance::mount(a, scheduler), two = Instance::mount(b, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = one.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && entered == 0);
    bool wrong = false;
    std::thread worker([&] {
        wrong = closing->invokeService(*first, [](int&) { return completedTask(); }).tryResult()->error().code ==
                ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong && entered == 0);
    auto call = closing->invokeService(*first, [&](int&) {
        ++entered;
        return completedTask();
    });
    CHECK(scheduler->runUntilIdle().hasValue() && call.tryResult()->hasValue() && entered == 1);
    gate.resolve();
    two.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    CHECK(provider.retire().hasValue() && provider.whenUnused().isReady() && provider.release().hasValue());
}
void snapshots()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry services(scheduler);
    auto provider = services.stage<int>({"metadata", 2}, std::make_shared<int>(3), {4, {"a", "b"}}).value();
    CHECK(provider.publish().hasValue());
    auto ref = services.find<int>({"metadata", 2}).value();
    auto snapshot = ref.snapshot().value();
    CHECK(snapshot.metadata.minor == 4 && snapshot.metadata.capabilities == std::vector<std::string>({"a", "b"}));
    CHECK(snapshot.key.interfaceMajor == 2 && snapshot.state == BindingState::Published);
    bool wrong = false;
    std::thread worker([&] { wrong = ref.snapshot().error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong && provider.retire().hasValue());
    CHECK(ref.snapshot().value().state == BindingState::Retiring);
    CHECK(provider.release().hasValue() && ref.snapshot().value().state == BindingState::Released);
    CHECK(!ref.withService([](int&) {}).hasValue());
}
void versionChecks()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto provider = registry.stage<int>({"versioned", 1}, std::make_shared<int>(1), {2, {"echo"}}).value();
    CHECK(provider.publish().hasValue());
    std::optional<Context> context;
    Runtime runtime([&](Context value) {
        context = value;
        CHECK(value.consume<int>(registry, {"versioned", 1}, 3).error().code == ErrorCode::DependencyVersion);
        CHECK(value.consume<int>(registry, {"versioned", 1}, 2, {"missing"}).error().code ==
              ErrorCode::DependencyVersion);
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue() && instance.ready().tryResult()->hasValue());
    CHECK(instance.snapshot().value().committedLeases == 0);
    CHECK(context->consume<int>(registry, {"versioned", 1}, 2, {"echo"}).hasValue());
    CHECK(instance.snapshot().value().committedLeases == 1);
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    CHECK(provider.retire().hasValue() && provider.whenUnused().isReady() && provider.release().hasValue());
}
void withdraw()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    std::optional<Context> provider, consumer;
    BindingId binding;
    Promise<void> cleanup;
    auto p = kernel
                 .registerPlugin("provider",
                                 [&](Context context) {
                                     provider = context;
                                     binding = context.provide<int>({"output", 1}, std::make_shared<int>(3)).value();
                                     return completedTask();
                                 },
                                 {}, {ServiceOutput::of<int>({"output", 1})})
                 .value();
    auto c =
        kernel
            .registerPlugin("consumer",
                            [&](Context context) {
                                consumer = context;
                                CHECK(context.consume<int>({"output", 1}).hasValue());
                                CHECK(context.onCleanup([&](CleanupContext) { return cleanup.task(); }).hasValue());
                                return completedTask();
                            },
                            {ServiceRequirement::of<int>({"output", 1})})
            .value();
    auto providerInstance = kernel.mount(p).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto consumerInstance = kernel.mount(c).value();
    CHECK(scheduler->runUntilIdle().hasValue() && consumerInstance.ready().tryResult()->hasValue());
    CHECK(consumer->withdraw(binding).tryResult()->error().code == ErrorCode::InvalidArgument);
    CHECK(provider->withdraw({}).tryResult()->error().code == ErrorCode::InvalidArgument);
    bool wrong = false;
    std::thread worker(
        [&] { wrong = provider->withdraw(binding).tryResult()->error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    auto withdrawn = provider->withdraw(binding);
    CHECK(cordis::detail::TaskAccess::sameTask(withdrawn, provider->withdraw(binding)));
    CHECK(scheduler->runUntilIdle().hasValue() && !withdrawn.isReady());
    CHECK(kernel.services().snapshot().value()[0].state == BindingState::Retiring);
    cleanup.resolve();
    CHECK(scheduler->runUntilIdle().hasValue() && withdrawn.tryResult()->hasValue());
    CHECK(providerInstance.snapshot().value().state == InstanceState::Active);
    auto stopped = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    CHECK(kernel.services().snapshot().value().empty());
    CHECK(!provider->withdraw(binding).tryResult()->hasValue());
}
int main(int argc, char** argv)
{
    try {
        if (argc != 2)
            return 1;
        const std::string name = argv[1];
        if (name == "async_cleanup")
            asyncCleanup(false);
        else if (name == "async_failure")
            asyncCleanup(true);
        else if (name == "guards")
            guards();
        else if (name == "snapshots")
            snapshots();
        else if (name == "versions")
            versionChecks();
        else if (name == "withdraw")
            withdraw();
        else
            return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
