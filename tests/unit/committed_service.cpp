/** @file committed_service.cpp
 * @brief 验证消费者 committed 账目、旧绑定清理调用及跨代失效。
 */
#include <cordis/instance.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 提供者退役后旧服务保留到异步消费者清理完成，清理期间允许旧绑定调用。 */
void retirement()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto object = std::make_shared<int>(42);
    std::weak_ptr<int> weak = object;
    auto provider = registry.stage({"storage"}, object).value();
    object.reset();
    CHECK(provider.publish().hasValue());
    Promise<void> gate;
    std::optional<ServiceRef<int>> service;
    std::optional<CleanupContext> cleanup;
    bool read = false;
    Runtime runtime([&](Context context) {
        service.emplace(context.consume<int>(registry, {"storage"}).value());
        CHECK(context.onCleanup([&](CleanupContext closing) {
            cleanup.emplace(closing);
            CHECK(!service->withService([](int&) {}).hasValue());
            CHECK(closing.withService(*service, [&](int& value) { read = value == 42 && !weak.expired(); }).hasValue());
            return gate.task();
        }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(provider.snapshot().value().committedLeases == 1);
    CHECK(service->withService([](int& value) { CHECK(value == 42); }).hasValue());
    CHECK(provider.retire().hasValue());
    CHECK(!service->withService([](int&) {}).hasValue());
    CHECK(provider.release().error().code == ErrorCode::ContextClosing);
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(read && !stopped.isReady() && !weak.expired());
    CHECK(provider.snapshot().value().committedLeases == 1);
    gate.resolve();
    CHECK(!cleanup->withService(*service, [](int&) {}).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && provider.snapshot().value().committedLeases == 0);
    CHECK(provider.release().hasValue() && weak.expired());
    CHECK(!service->withService([](int&) {}).hasValue());
}

/** @brief 清理权限只接受所属实例代次的 committed 引用，查找引用不能冒充。 */
void boundaries()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    ServiceRegistry foreign(std::make_shared<FakeScheduler>());
    auto provider = registry.stage({"shared"}, std::make_shared<int>(1)).value();
    CHECK(provider.publish().hasValue());
    auto plain = registry.find<int>({"shared"}).value();
    std::optional<ServiceRef<int>> first;
    std::optional<ServiceRef<int>> second;
    int activations = 0;
    InstanceId aId;
    Runtime runtime([&](Context context) {
        CHECK(!context.consume<int>(foreign, {"shared"}).hasValue());
        auto ref = context.consume<int>(registry, {"shared"}).value();
        if (++activations == 1) first.emplace(ref);
        else second.emplace(ref);
        CHECK(context.onCleanup([&, ref](CleanupContext closing) {
            CHECK(closing.withService(ref, [](int&) {}).hasValue());
            CHECK(!closing.withService(plain, [](int&) {}).hasValue());
            if (activations == 2) {
                const auto& other = closing.snapshot().value().instance == aId ? *second : *first;
                CHECK(!closing.withService(other, [](int&) {}).hasValue());
            }
            return completedTask();
        }).hasValue());
        return completedTask();
    });
    auto a = Instance::mount(runtime, scheduler);
    aId = a.snapshot().value().instance;
    auto b = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    bool rejected = false;
    std::thread worker([&] { rejected = first->withService([](int&) {}).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(rejected);
    a.stop();
    b.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(provider.snapshot().value().committedLeases == 0);
    CHECK(provider.retire().hasValue() && provider.release().hasValue());
}

/** @brief 正常换代释放旧租约，清理失败保留租约并阻止提供者释放。 */
void epochsAndFailure()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto provider = registry.stage({"epochs"}, std::make_shared<int>(3)).value();
    CHECK(provider.publish().hasValue());
    std::optional<ServiceRef<int>> old;
    int calls = 0;
    Runtime runtime([&](Context context) {
        auto ref = context.consume<int>(registry, {"epochs"}).value();
        if (++calls == 1) old.emplace(ref);
        else CHECK(!old->withService([](int&) {}).hasValue());
        CHECK(context.onCleanup([ref, config = context.config()](CleanupContext closing) {
            CHECK(closing.withService(ref, [](int&) {}).hasValue());
            return config == "fail" ? failedTask(makeError(ErrorCode::CleanupFailed, "retained")) : completedTask();
        }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto changed = instance.update("fail", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(changed.target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(provider.snapshot().value().committedLeases == 1);
    CHECK(provider.retire().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.tryResult()->value().quiescent);
    CHECK(provider.snapshot().value().committedLeases == 1);
    CHECK(!provider.release().hasValue());
}

/** @brief 调用内部泵送停止队列也不能使实例早于在途租约调用报告静止。 */
void reentrantCall()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto provider = registry.stage({"inflight"}, std::make_shared<int>(11)).value();
    CHECK(provider.publish().hasValue());
    std::optional<ServiceRef<int>> ref;
    Runtime runtime([&](Context context) {
        ref.emplace(context.consume<int>(registry, {"inflight"}).value());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    std::optional<Task<CleanupReport>> stopped;
    CHECK(ref->withService([&](int& value) {
        stopped.emplace(instance.stop());
        CHECK(scheduler->runUntilIdle().hasValue());
        CHECK(!stopped->isReady() && value == 11);
        CHECK(instance.snapshot().value().state == InstanceState::Unloading);
        CHECK(instance.snapshot().value().committedLeases == 1);
        CHECK(provider.snapshot().value().committedLeases == 1);
    }).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped->tryResult()->value().quiescent && provider.snapshot().value().committedLeases == 0);
    CHECK(provider.retire().hasValue() && provider.release().hasValue());
}

/** @brief 执行单个 committed 租约场景。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "retirement") retirement();
        else if (name == "boundaries") boundaries();
        else if (name == "epochs_failure") epochsAndFailure();
        else if (name == "reentrant_call") reentrantCall();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
