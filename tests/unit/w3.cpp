/** @file w3.cpp
 * @brief 验证单次激活实例的停止屏障和弱上下文。
 */
#include <cordis/instance.hpp>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
using namespace cordis;
using cordis::testing::FakeScheduler;

/** @brief 停止尚未进入队列的实例，不执行插件。 */
void stopBeforeStart()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    int calls = 0;
    Runtime runtime([&](Context) { ++calls; return completedTask(); });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(calls == 0);
    auto first = instance.stop();
    auto second = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 0 && first.tryResult() == second.tryResult());
    CHECK(first.tryResult()->value().quiescent);
    CHECK(instance.snapshot().value().state == InstanceState::Disposed);
}

/** @brief 激活中重入停止，晚到 setup 资源必须通过原屏障回收。 */
void reentrantStop()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> gate;
    std::optional<Instance> instance;
    std::optional<EffectSetupContext> late;
    std::optional<Context> old;
    int cleaned = 0;
    Runtime runtime([&](Context context) {
        old.emplace(context);
        CHECK(context.effect([&](EffectSetupContext setup) {
            late.emplace(setup);
            return gate.task();
        }).hasValue());
        instance->stop();
        return gate.task();
    });
    instance.emplace(Instance::mount(runtime, scheduler));
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = instance->stop();
    CHECK(!stopped.isReady());
    CHECK(!old->effect([](EffectSetupContext) { return completedTask(); }).hasValue());
    CHECK(late->addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(cleaned == 1 && stopped.tryResult()->value().quiescent);
    CHECK(instance->ready().tryResult()->error().code == ErrorCode::OperationCancelled);
    CHECK(instance->snapshot().value().state == InstanceState::Disposed);
    instance.reset();
    CHECK(old->effect([](EffectSetupContext) { return completedTask(); }).error().code == ErrorCode::StaleContext);
}

/** @brief 同 Runtime 实例独立，失败激活也回滚业务 effect。 */
void isolationAndFailure()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    int cleaned = 0;
    Runtime runtime([&](Context context) {
        CHECK(context.effect([&](EffectSetupContext setup) {
            CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
            return completedTask();
        }).hasValue());
        return completedTask();
    });
    auto a = Instance::mount(runtime, scheduler);
    auto b = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(a.ready().tryResult()->hasValue() && b.ready().tryResult()->hasValue());
    a.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(cleaned == 1 && b.snapshot().value().state == InstanceState::Active);
    b.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && cleaned == 2);
    Runtime failing([&](Context context) -> Task<void> {
        CHECK(context.effect([&](EffectSetupContext setup) {
            CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
            return completedTask();
        }).hasValue());
        throw std::runtime_error("activation failed");
    });
    auto c = Instance::mount(failing, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(c.ready().tryResult()->error().code == ErrorCode::ActivationFailed);
    auto stopped = c.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && cleaned == 3);
    CHECK(c.stop().tryResult()->value().errors.size() == 1);
}

/** @brief 非 owner 停止无效；调度器丢弃激活任务时不伪报完成。 */
void threadAndDelivery()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Runtime runtime([](Context) { return completedTask(); });
    auto instance = Instance::mount(runtime, scheduler);
    std::thread worker([&] {
        CHECK(instance.stop().tryResult()->error().code == ErrorCode::WrongThread);
        CHECK(instance.snapshot().error().code == ErrorCode::WrongThread);
    });
    worker.join();
    scheduler->close();
    CHECK(instance.snapshot().value().deliveryBlocked);
    CHECK(!instance.ready().isReady());
    CHECK(!instance.stop().isReady());
}

/** @brief 连续更新合并到最新配置，旧代 setup 和 cleanup 阻止新代启动。 */
void updatesAndEpochs()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> setupGate;
    Promise<void> cleanupGate;
    std::vector<std::string> started;
    std::optional<Context> old;
    Runtime runtime([&](Context context) {
        started.push_back(context.config());
        if (context.config() == "one") {
            old.emplace(context);
            CHECK(context.effect([&](EffectSetupContext setup) {
                CHECK(setup.addCleanup([cleanupGate] { return cleanupGate.task(); }).hasValue());
                return setupGate.task();
            }).hasValue());
            return setupGate.task();
        }
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler, "one");
    CHECK(scheduler->runUntilIdle().hasValue());
    auto second = instance.update("two", 1).value();
    auto third = instance.update("three", 2).value();
    CHECK(second.target.tryResult()->value().outcome == StableOutcome::Superseded);
    CHECK(second.target.tryResult()->value().epoch == 0);
    CHECK(instance.update("bad", 1).error().code == ErrorCode::ConfigConflict);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started.size() == 1 && !third.target.isReady());
    CHECK(!old->effect([](EffectSetupContext) { return completedTask(); }).hasValue());
    setupGate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started.size() == 1 && !third.target.isReady());
    cleanupGate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK((started == std::vector<std::string>{"one", "three"}));
    CHECK(third.target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(third.target.tryResult()->value().epoch == 2);
    CHECK(old->config() == "one");
    CHECK(old->effect([](EffectSetupContext) { return completedTask(); }).error().code == ErrorCode::StaleContext);
    instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(instance.update("four", 3).error().code == ErrorCode::InstanceDisposed);
    CHECK(instance.retry().error().code == ErrorCode::InstanceDisposed);
}

/** @brief 激活失败静止后重试保留配置，不因失败终结实例。 */
void retryFailure()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    int calls = 0;
    Runtime runtime([&](Context context) {
        CHECK(context.config() == "kept");
        return ++calls == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "first")) : completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler, "kept");
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(instance.snapshot().value().state == InstanceState::Failed);
    auto before = instance.operation().value();
    auto retried = instance.retry().value();
    CHECK(retried.revision == before.revision && retried.id != before.id);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 2 && retried.target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(instance.snapshot().value().epoch == 2);
    instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
}

/** @brief 清理失败不能被新配置或重试绕过，终结报告保留未静止状态。 */
void cleanupFailureBlocksReplacement()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    int calls = 0;
    Runtime runtime([&](Context context) {
        ++calls;
        CHECK(context.effect([](EffectSetupContext setup) {
            CHECK(setup.addCleanup([] { return failedTask(makeError(ErrorCode::CleanupFailed, "still owned")); }).hasValue());
            return completedTask();
        }).hasValue());
        return completedTask();
    });
    auto instance = Instance::mount(runtime, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto update = instance.update("next", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 1 && update.target.tryResult()->value().outcome == StableOutcome::Failed);
    CHECK(update.target.tryResult()->value().epoch == 0);
    CHECK(instance.update("again", 2).error().code == ErrorCode::CleanupFailed);
    CHECK(!instance.retry().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.tryResult()->value().quiescent);
    CHECK(instance.snapshot().value().state == InstanceState::Unloading);
}

/** @brief 验证内核归属、操作查询、历史激活和全量关闭。 */
void kernelAndQueries()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Kernel other(scheduler);
    Promise<void> gate;
    auto runtime = kernel.registerPlugin("test", [gate](Context context) {
        return context.config() == "wait" ? gate.task() : completedTask();
    }).value();
    CHECK(!kernel.registerPlugin("test", [](Context) { return completedTask(); }).hasValue());
    CHECK(!other.mount(runtime).hasValue());
    auto first = kernel.mount(runtime, "wait").value();
    auto second = kernel.mount(runtime).value();
    auto initial = first.operation().value();
    CHECK(initial.id.valid() && initial.instance.valid());
    CHECK(second.awaitTarget(initial).tryResult()->error().code == ErrorCode::InvalidArgument);
    CHECK(first.awaitActive(1).tryResult()->error().code == ErrorCode::InvalidArgument);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto active = first.awaitActive(1);
    CHECK(!active.isReady());
    CHECK(second.awaitActive(1).tryResult()->value().epoch == 1);
    CHECK(first.snapshot().value().instance != second.snapshot().value().instance);
    CHECK(first.snapshot().value().runtime == runtime.id());
    auto changed = first.update("now", 1).value();
    CHECK(changed.id != initial.id);
    CHECK(active.tryResult()->error().code == ErrorCode::OperationCancelled);
    CHECK(first.awaitTarget(initial).tryResult()->value().outcome == StableOutcome::Superseded);
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(first.awaitTarget(changed).tryResult()->value().outcome == StableOutcome::Active);
    CHECK(first.awaitActive(2).tryResult()->value().revision == 2);
    CHECK(kernel.snapshot().value().instances.size() == 2);
    bool rejected = false;
    std::thread worker([&] {
        rejected = first.awaitTarget(changed).tryResult()->error().code == ErrorCode::WrongThread
            && first.awaitActive(2).tryResult()->error().code == ErrorCode::WrongThread
            && !kernel.snapshot().hasValue();
    });
    worker.join();
    CHECK(rejected);
    auto closed = kernel.close();
    auto again = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(closed.tryResult()->value().quiescent && closed.tryResult() == again.tryResult());
    CHECK(kernel.snapshot().value().instances.empty());
    CHECK(!kernel.mount(runtime).hasValue());
    CHECK(first.awaitActive(2).tryResult()->hasValue());
    CHECK(other.close().tryResult()->value().quiescent);
}

/** @brief 验证 Kernel 持有丢弃句柄的实例，且关闭等待清理和诊断投递失败。 */
void kernelCloseBarrier()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    auto runtime = kernel.registerPlugin("gated", [gate](Context context) {
        auto resource = context.effect([gate](EffectSetupContext setup) {
            auto added = setup.addCleanup([gate] { return gate.task(); });
            return added.hasValue() ? completedTask() : failedTask(added.error());
        });
        return resource.hasValue() ? completedTask() : failedTask(resource.error());
    }).value();
    CHECK(kernel.mount(runtime).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(kernel.snapshot().value().instances.size() == 1);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!closed.isReady() && kernel.snapshot().value().instances.size() == 1);
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(closed.tryResult()->value().quiescent && kernel.snapshot().value().instances.empty());
    Kernel blocked(scheduler);
    auto registered = blocked.registerPlugin("blocked", [](Context) { return completedTask(); }).value();
    CHECK(blocked.mount(registered).hasValue());
    scheduler->close();
    auto waiting = blocked.close();
    CHECK(!waiting.isReady() && blocked.snapshot().value().deliveryBlocked);
    CHECK(blocked.snapshot().value().instances.size() == 1);
}

/** @brief 无效 Runtime 和关闭后的命令必须在修改资源及操作账目前拒绝。 */
void rejectedCommandsPreserveState()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    auto original = kernel.registerPlugin("move", [](Context) { return completedTask(); }).value();
    auto runtime = std::move(original);
    CHECK(!kernel.mount(original).hasValue());
    bool rejected = false;
    try { (void)Instance::mount(original, scheduler); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    auto instance = kernel.mount(runtime).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto before = instance.operation().value();
    scheduler->close();
    CHECK(instance.update("rejected", 1).error().code == ErrorCode::SchedulerClosed);
    auto after = instance.operation().value();
    CHECK(after.id == before.id && after.revision == before.revision);
    CHECK(instance.snapshot().value().state == InstanceState::Active);

    auto failedScheduler = std::make_shared<FakeScheduler>();
    Runtime failing([](Context) { return failedTask(makeError(ErrorCode::ActivationFailed, "failure")); });
    auto failed = Instance::mount(failing, failedScheduler);
    CHECK(failedScheduler->runUntilIdle().hasValue());
    auto previous = failed.operation().value();
    failedScheduler->close();
    CHECK(failed.retry().error().code == ErrorCode::SchedulerClosed);
    CHECK(failed.operation().value().id == previous.id);
    CHECK(failed.snapshot().value().state == InstanceState::Failed);
}

/** @brief 执行 W3 生命周期和多代收敛场景。 */
int main()
{
    try { stopBeforeStart(); reentrantStop(); isolationAndFailure(); threadAndDelivery(); updatesAndEpochs(); retryFailure(); cleanupFailureBlocksReplacement(); kernelAndQueries(); kernelCloseBarrier(); rejectedCommandsPreserveState(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
