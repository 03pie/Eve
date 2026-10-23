/**
 * @file w2.cpp
 * @brief 测试 effect 生命周期、虚拟时钟及任务超时观察。
 */
#include <cordis/effect.hpp>
#include <cordis/observation.hpp>
#include <cordis/testing/fake_clock.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace cordis;
using cordis::testing::FakeClock;
using cordis::testing::FakeScheduler;
using namespace std::chrono_literals;

/** @brief 条件不成立时抛出包含表达式和行号的测试异常。 */
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("check failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (false)

/**
 * @brief 为 effect 测试提供共享调度器、作用域和队列泵送操作。
 */
struct Fixture {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope{scheduler};
    /** @brief 执行调度器队列中的所有待处理任务。 */
    void drain() { CHECK(scheduler->runUntilIdle().hasValue()); }
};

/** @brief 验证 effect 句柄共享关闭结果且清理只执行一次。 */
void effectOwnerAndOnce()
{
    Fixture f;
    int cleaned = 0;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(f.scope.snapshot().value().effects.size() == 1);
        CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    CHECK(effect.ready().tryResult()->hasValue());
    auto copy = effect;
    auto first = effect.close();
    auto second = copy.close();
    auto owner = f.scope.close();
    f.drain();
    CHECK(cleaned == 1);
    CHECK(first.tryResult() == second.tryResult());
    CHECK(first.tryResult()->value().quiescent && owner.tryResult()->value().quiescent);
    CHECK(f.scope.snapshot().value().effects.empty());
}

/** @brief 验证丢弃 effect 句柄不会自动关闭资源。 */
void effectDroppedToken()
{
    Fixture f;
    int cleaned = 0;
    {
        auto discarded = f.scope.effect([&](EffectSetupContext setup) {
            CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
            return completedTask();
        });
        CHECK(discarded.hasValue());
    }
    f.drain();
    CHECK(cleaned == 0);
    auto closed = f.scope.close();
    f.drain();
    CHECK(cleaned == 1 && closed.tryResult()->value().quiescent);
}

/** @brief 验证清理函数按注册顺序的逆序执行。 */
void effectReverseOrder()
{
    Fixture f;
    std::vector<int> order;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        for (int i = 1; i <= 3; ++i) CHECK(setup.addCleanup([&, i] {
            order.push_back(i); return completedTask();
        }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    CHECK((order == std::vector<int>{3, 2, 1}));
    CHECK(closed.tryResult()->value().quiescent);
    CHECK(effect.snapshot().value().state == EffectState::Closed);
}

/** @brief 验证初始化重入关闭作用域时仍允许注册清理并取消初始化结果。 */
void effectReentrantSetup()
{
    Fixture f;
    Promise<void> gate;
    int cleaned = 0;
    std::optional<EffectSetupContext> late;
    std::optional<Task<CleanupReport>> closed;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        late.emplace(setup);
        CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
        closed.emplace(f.scope.close());
        CHECK(setup.cancellation().isCancellationRequested());
        CHECK(!closed->isReady());
        return gate.task();
    }).value();
    f.drain();
    CHECK(cleaned == 0);
    CHECK(effect.snapshot().value().setupPending);
    CHECK(late->addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
    gate.resolve();
    f.drain();
    CHECK(cleaned == 2 && closed->tryResult()->value().quiescent);
    CHECK(effect.ready().tryResult()->error().code == ErrorCode::OperationCancelled);
    CHECK(!late->addCleanup([] { return completedTask(); }).hasValue());
}

/** @brief 验证同步初始化异常会记录错误并执行已注册清理。 */
void effectSetupFailure()
{
    Fixture f;
    int cleaned = 0;
    auto effect = f.scope.effect([&](EffectSetupContext setup) -> Task<void> {
        CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
        throw std::runtime_error("setup exception");
    }).value();
    f.drain();
    CHECK(cleaned == 1);
    CHECK(effect.ready().tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(effect.close().tryResult()->value().quiescent);
    auto report = f.scope.close().tryResult();
    CHECK(report->value().quiescent && report->value().errors.size() == 1);
    CHECK(report->value().errors[0].phase == "effect.setup");
}

/** @brief 验证异步初始化失败后执行清理并完成作用域关闭。 */
void effectAsyncSetupFailure()
{
    Fixture f;
    Promise<void> setupGate;
    int cleaned = 0;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { ++cleaned; return completedTask(); }).hasValue());
        return setupGate.task();
    }).value();
    auto closed = f.scope.close();
    f.drain();
    CHECK(!closed.isReady());
    std::thread worker([setupGate] { setupGate.reject(makeError(ErrorCode::ActivationFailed, "async failed")); });
    worker.join();
    f.drain();
    CHECK(cleaned == 1 && closed.tryResult()->value().quiescent);
    CHECK(!effect.ready().tryResult()->hasValue());
}

/** @brief 验证异步清理会等待完成后再执行前序清理。 */
void effectAsyncCleanup()
{
    Fixture f;
    Promise<void> cleanupGate;
    std::vector<int> order;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { order.push_back(1); return completedTask(); }).hasValue());
        CHECK(setup.addCleanup([&] { order.push_back(2); return cleanupGate.task(); }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    auto closed = effect.close();
    f.drain();
    CHECK((order == std::vector<int>{2}));
    CHECK(!closed.isReady() && effect.snapshot().value().cleanupPending);
    CHECK(f.scope.snapshot().value().effects.size() == 1);
    cleanupGate.resolve();
    f.drain();
    CHECK((order == std::vector<int>{2, 1}));
    CHECK(closed.tryResult()->value().quiescent);
    CHECK(f.scope.close().tryResult()->value().quiescent);
}

/** @brief 验证清理失败会被汇总且不确定资源保持保留。 */
void effectCleanupErrors()
{
    Fixture f;
    std::vector<int> order;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { order.push_back(1); return completedTask(); }).hasValue());
        CHECK(setup.addCleanup([&]() -> Task<void> { order.push_back(2); throw 17; }).hasValue());
        CHECK(setup.addCleanup([&] { order.push_back(3); return failedTask(makeError(ErrorCode::CleanupFailed, "async cleanup failure")); }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    CHECK((order == std::vector<int>{3, 2, 1}));
    CHECK(closed.isReady() && !closed.tryResult()->value().quiescent);
    CHECK(closed.tryResult()->value().errors.size() == 2);
    CHECK(effect.snapshot().value().state == EffectState::Closing);
    CHECK(f.scope.snapshot().value().effects.size() == 1); // 清理失败时保留状态不确定的资源。
}

/** @brief 验证清理期间拒绝注册新 effect。 */
void effectNoRegistrationDuringCleanup()
{
    Fixture f;
    bool rejected = false;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] {
            auto candidate = f.scope.effect([](EffectSetupContext) { return completedTask(); });
            rejected = !candidate.hasValue() && candidate.error().code == ErrorCode::ContextClosing;
            return completedTask();
        }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    effect.close(); // 仅关闭此 effect，不关闭整个作用域。
    f.drain();
    CHECK(rejected);
    CHECK(f.scope.close().tryResult()->value().quiescent);
    CHECK(!f.scope.effect([](EffectSetupContext) { return completedTask(); }).hasValue());
}

/** @brief 验证作用域在启动清理前向所有 effect 广播取消。 */
void effectBroadcastBeforeCleanup()
{
    Fixture f;
    std::optional<CancellationToken> first;
    std::optional<CancellationToken> second;
    bool sawBoth = false;
    auto a = f.scope.effect([&](EffectSetupContext setup) {
        first.emplace(setup.cancellation());
        CHECK(setup.addCleanup([&] {
            sawBoth = first->isCancellationRequested() && second->isCancellationRequested();
            return completedTask();
        }).hasValue());
        return completedTask();
    });
    auto b = f.scope.effect([&](EffectSetupContext setup) {
        second.emplace(setup.cancellation());
        return completedTask();
    });
    CHECK(a.hasValue() && b.hasValue());
    f.drain();
    auto closed = f.scope.close();
    CHECK(first->isCancellationRequested() && second->isCancellationRequested());
    f.drain();
    CHECK(sawBoth && closed.tryResult()->value().quiescent);
}

/** @brief 验证父 effect 接管子 effect 后按依赖顺序关闭。 */
void effectAdoptedGroup()
{
    Fixture f;
    Promise<void> childGate;
    std::vector<int> order;
    auto child = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { order.push_back(2); return childGate.task(); }).hasValue());
        return completedTask();
    }).value();
    auto parent = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { order.push_back(1); return completedTask(); }).hasValue());
        CHECK(setup.adopt(child).hasValue());
        return completedTask();
    }).value();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    CHECK((order == std::vector<int>{2}));
    CHECK(!parent.close().isReady() && !closed.isReady());
    childGate.resolve();
    f.drain();
    CHECK((order == std::vector<int>{2, 1}));
    CHECK(closed.tryResult()->value().quiescent);
}

/** @brief 验证接管关系拒绝形成循环依赖。 */
void effectAdoptionCycle()
{
    Fixture f;
    Promise<void> aGate;
    Promise<void> bGate;
    std::optional<EffectSetupContext> aContext;
    std::optional<EffectSetupContext> bContext;
    auto a = f.scope.effect([&](EffectSetupContext setup) { aContext.emplace(setup); return aGate.task(); }).value();
    auto b = f.scope.effect([&](EffectSetupContext setup) { bContext.emplace(setup); return bGate.task(); }).value();
    CHECK(aContext->adopt(b).hasValue());
    auto cycle = bContext->adopt(a);
    CHECK(!cycle.hasValue() && cycle.error().code == ErrorCode::CleanupCycle);
    CHECK(!aContext->adopt(a).hasValue());
    aGate.resolve();
    bGate.resolve();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    CHECK(closed.tryResult()->value().quiescent);
}

/** @brief 验证调度器关闭会阻断生命周期回调并反映在快照中。 */
void effectDeliveryBlocked()
{
    Fixture f;
    auto effect = f.scope.effect([](EffectSetupContext) { return completedTask(); }).value();
    f.drain();
    auto closed = f.scope.close();
    f.scheduler->close();
    CHECK(!closed.isReady());
    CHECK(effect.snapshot().value().deliveryBlocked);
    CHECK(f.scope.snapshot().value().deliveryBlocked);
    CHECK(effect.snapshot().value().state == EffectState::Closing);
}

/** @brief 验证作用域操作从非所属线程调用时返回线程错误。 */
void effectWrongThread()
{
    Fixture f;
    bool refused = false;
    std::thread worker([&] {
        auto registration = f.scope.effect([](EffectSetupContext) { return completedTask(); });
        auto closed = f.scope.close();
        auto snapshot = f.scope.snapshot();
        refused = !registration.hasValue() && registration.error().code == ErrorCode::WrongThread
            && closed.tryResult()->error().code == ErrorCode::WrongThread && !snapshot.hasValue();
    });
    worker.join();
    CHECK(refused);
    CHECK(f.scope.close().tryResult()->value().quiescent);
}

/** @brief 验证虚拟时钟按截止时间和注册顺序触发任务。 */
void clockOrdering()
{
    FakeClock clock;
    std::vector<int> order;
    CHECK(clock.after(10ms, {[&] { order.push_back(1); }, {}}).hasValue());
    CHECK(clock.after(10ms, {[&] { order.push_back(2); }, {}}).hasValue());
    auto cancelled = clock.after(5ms, {[&] { order.push_back(99); }, {}}).value();
    CHECK(cancelled.cancel());
    CHECK(clock.advance(9ms).value() == 0);
    CHECK(clock.advance(1ms).value() == 2);
    CHECK((order == std::vector<int>{1, 2}));
    CHECK(clock.pending() == 0 && clock.nowMilliseconds() == 10);
}

/** @brief 验证虚拟时钟拒绝无效时长并汇总回调错误。 */
void clockLimits()
{
    FakeClock clock;
    CHECK(!clock.advance(-1ms).hasValue());
    CHECK(!clock.after(-1ms, {[] {}, {}}).hasValue());
    CHECK(clock.after(1ms, {[&] { throw std::runtime_error("timer error"); }, {}}).hasValue());
    bool next = false;
    CHECK(clock.after(1ms, {[&] { next = true; }, {}}).hasValue());
    auto failed = clock.advance(1ms);
    CHECK(!failed.hasValue() && failed.error().causes.size() == 1 && next);
    CHECK(clock.advance(std::chrono::milliseconds(std::numeric_limits<std::int64_t>::max() - 1)).hasValue());
    CHECK(!clock.advance(1ms).hasValue());
}

/** @brief 验证超时观察不会取消仍在等待的资源清理。 */
void observationTimeout()
{
    Fixture f;
    FakeClock clock;
    Promise<void> gate;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { return gate.task(); }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    auto observation = observeFor(closed, clock, 10ms);
    CHECK(clock.advance(10ms).value() == 1);
    CHECK(observation.tryResult()->value().state == ObservationState::TimedOut);
    CHECK(!closed.isReady());
    CHECK(effect.snapshot().value().cleanupPending);
    CHECK(f.scope.snapshot().value().effects.size() == 1);
    gate.resolve();
    f.drain();
    CHECK(closed.tryResult()->value().quiescent);
    CHECK(observation.tryResult()->value().state == ObservationState::TimedOut);
}

/** @brief 验证任务完成优先于超时并保留仅移动结果。 */
void observationCompletion()
{
    FakeClock clock;
    Promise<std::unique_ptr<int>> source;
    auto observation = observeFor(source.task(), clock, 10ms);
    std::thread worker([source] { source.resolve(std::make_unique<int>(42)); });
    worker.join();
    CHECK(observation.tryResult()->value().state == ObservationState::Completed);
    CHECK(*observation.tryResult()->value().result->value() == 42);
    CHECK(clock.pending() == 0);
    CHECK(clock.advance(20ms).hasValue());
    CHECK(observation.tryResult()->value().state == ObservationState::Completed);
}

/** @brief 验证关闭时钟会使待观察操作以错误结束。 */
void observationClockClosed()
{
    FakeClock clock;
    Promise<void> source;
    auto observation = observeFor(source.task(), clock, 1ms);
    clock.close();
    CHECK(observation.tryResult()->error().code == ErrorCode::ClockClosed);
    CHECK(!source.task().isReady());
    source.resolve();
    CHECK(observation.tryResult()->error().code == ErrorCode::ClockClosed);
}

/** @brief 验证观察已完成的失败任务不会额外注册计时器。 */
void observationImmediateFailure()
{
    FakeClock clock;
    auto source = failedTask(makeError(ErrorCode::ActivationFailed, "already failed"));
    auto observation = observeFor(source, clock, 0ms);
    CHECK(observation.tryResult()->value().state == ObservationState::Completed);
    CHECK(observation.tryResult()->value().result->error().code == ErrorCode::ActivationFailed);
    CHECK(clock.pending() == 0);
}

/** @brief 验证异步清理期间禁止注册且清理完成后允许重新注册。 */
void effectAsyncRegistrationGuard()
{
    Fixture f;
    Promise<void> gate;
    auto effect = f.scope.effect([&](EffectSetupContext setup) {
        CHECK(setup.addCleanup([&] { return gate.task(); }).hasValue());
        return completedTask();
    }).value();
    f.drain();
    effect.close();
    f.drain();
    auto refused = f.scope.effect([](EffectSetupContext) { return completedTask(); });
    CHECK(!refused.hasValue() && refused.error().code == ErrorCode::ContextClosing);
    gate.resolve();
    f.drain();
    auto allowed = f.scope.effect([](EffectSetupContext) { return completedTask(); });
    CHECK(allowed.hasValue());
    auto closed = f.scope.close();
    f.drain();
    CHECK(closed.tryResult()->value().quiescent);
}

/** @brief 验证初始化上下文过期后拒绝新增清理函数。 */
void effectStaleContext()
{
    Fixture f;
    std::optional<EffectSetupContext> context;
    {
        auto effect = f.scope.effect([&](EffectSetupContext setup) {
            context.emplace(setup);
            CHECK(!setup.addCleanup({}).hasValue());
            return completedTask();
        }).value();
        f.drain();
        effect.close();
        f.drain();
    }
    auto stale = context->addCleanup([] { return completedTask(); });
    CHECK(!stale.hasValue() && stale.error().code == ErrorCode::StaleContext);
    CHECK(f.scope.close().tryResult()->value().quiescent);
}

/** @brief 验证初始化结果无法投递时作用域保留未完成状态。 */
void effectBlockedSetupDelivery()
{
    Fixture f;
    Promise<void> gate;
    auto effect = f.scope.effect([&](EffectSetupContext) { return gate.task(); }).value();
    auto closed = f.scope.close();
    f.drain();
    f.scheduler->close();
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    CHECK(effect.snapshot().value().deliveryBlocked);
    CHECK(effect.snapshot().value().setupPending && !closed.isReady());
}

/** @brief 验证清理失败后资源闭包保持存活以防资源被提前释放。 */
void effectRetainsFailedClosure()
{
    Fixture f;
    auto resource = std::make_shared<int>(3);
    std::weak_ptr<int> weak = resource;
    auto effect = f.scope.effect([resource](EffectSetupContext setup) {
        auto added = setup.addCleanup([resource] {
            return failedTask(makeError(ErrorCode::CleanupFailed, "resource release not confirmed"));
        });
        if (!added.hasValue()) return failedTask(added.error());
        return completedTask();
    }).value();
    resource.reset();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    CHECK(!closed.tryResult()->value().quiescent);
    CHECK(!weak.expired());
    CHECK(effect.snapshot().value().state == EffectState::Closing);
    CHECK(effect.snapshot().value().retainedCleanups == 1);
}

/** @brief 验证清理结果无法投递时仍保留活动清理和资源所有权。 */
void effectBlockedCleanupDelivery()
{
    Fixture f;
    Promise<void> gate;
    auto resource = std::make_shared<int>(4);
    std::weak_ptr<int> weak = resource;
    auto effect = f.scope.effect([resource, gate](EffectSetupContext setup) {
        auto added = setup.addCleanup([resource, gate] { return gate.task(); });
        return added.hasValue() ? completedTask() : failedTask(added.error());
    }).value();
    resource.reset();
    f.drain();
    auto closed = f.scope.close();
    f.drain();
    f.scheduler->close();
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    CHECK(!closed.isReady() && !weak.expired());
    const auto snapshot = effect.snapshot().value();
    CHECK(snapshot.deliveryBlocked && snapshot.cleanupPending);
    CHECK(snapshot.state == EffectState::Closing);
}

/** @brief 验证清理闭包析构重入时仍拒绝新增 effect。 */
void effectCleanupDestructorGuard()
{
    Fixture f;
    /**
     * @brief 析构时执行测试操作的局部资源对象。
     */
    struct OnDestroy {
        std::function<void()> action;
        /** @brief 析构时执行测试提供的重入操作。 */
        ~OnDestroy() { action(); }
    };
    bool refused = false;
    auto resource = std::make_shared<OnDestroy>();
    resource->action = [&] {
        auto candidate = f.scope.effect([](EffectSetupContext) { return completedTask(); });
        refused = !candidate.hasValue() && candidate.error().code == ErrorCode::ContextClosing;
    };
    auto effect = f.scope.effect([resource](EffectSetupContext setup) {
        auto added = setup.addCleanup([resource] { return completedTask(); });
        return added.hasValue() ? completedTask() : failedTask(added.error());
    }).value();
    resource.reset();
    f.drain();
    auto closed = effect.close();
    f.drain();
    CHECK(refused && closed.tryResult()->value().quiescent);
    CHECK(f.scope.close().tryResult()->value().quiescent);
}

/** @brief 验证虚拟时钟步数上限可终止零延迟递归任务。 */
void clockStepBudget()
{
    FakeClock clock;
    std::function<void()> repeat;
    repeat = [&] { CHECK(clock.after(0ms, {repeat, {}}).hasValue()); };
    CHECK(clock.after(0ms, {repeat, {}}).hasValue());
    auto limited = clock.advance(0ms, 5);
    CHECK(!limited.hasValue() && limited.error().code == ErrorCode::StepLimitExceeded);
    CHECK(clock.pending() == 1 && clock.nowMilliseconds() == 0);
    clock.close();
    CHECK(clock.pending() == 0);
}

/** @brief 验证虚拟时钟的线程约束和回调重入保护。 */
void clockThreadAndReentry()
{
    FakeClock clock;
    bool rejected = false;
    std::thread worker([&] {
        auto result = clock.after(0ms, {[] {}, {}});
        auto advance = clock.advance(0ms);
        auto closed = clock.close();
        rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread
            && !advance.hasValue() && !closed.hasValue();
    });
    worker.join();
    CHECK(rejected);
    bool reentryRejected = false;
    CHECK(clock.after(0ms, {[&] { reentryRejected = !clock.advance(0ms).hasValue(); }, {}}).hasValue());
    CHECK(clock.advance(0ms).hasValue() && reentryRejected);
}

/** @brief 验证任务失败立即完成观察且无效超时被拒绝。 */
void observationDelayedFailure()
{
    FakeClock clock;
    Promise<void> source;
    auto observed = observeFor(source.task(), clock, 5ms);
    source.reject(makeError(ErrorCode::CleanupFailed, "actual failure"));
    CHECK(observed.tryResult()->value().state == ObservationState::Completed);
    CHECK(observed.tryResult()->value().result->error().code == ErrorCode::CleanupFailed);
    CHECK(clock.advance(10ms).hasValue());
    Promise<void> pending;
    auto invalid = observeFor(pending.task(), clock, -1ms);
    CHECK(invalid.tryResult()->error().code == ErrorCode::InvalidArgument);
    CHECK(!pending.task().isReady());
}

/** @brief 验证任务完成与零时限截止竞争时观察结果一致有效。 */
void observationDeadlineRace()
{
    for (int i = 0; i < 30; ++i) {
        FakeClock clock;
        Promise<int> source;
        auto observed = observeFor(source.task(), clock, 0ms);
        std::atomic_bool go{false};
        std::thread worker([&] {
            while (!go.load()) std::this_thread::yield();
            source.resolve(7);
        });
        go.store(true);
        const auto advanced = clock.advance(0ms);
        worker.join();
        CHECK(advanced.hasValue());
        CHECK(observed.isReady() && source.task().tryResult()->value() == 7);
        const auto& outcome = observed.tryResult()->value();
        if (outcome.state == ObservationState::Completed) CHECK(outcome.result->value() == 7);
        else CHECK(!outcome.result);
    }
}

/**
 * @brief 根据测试名称运行单个 W2 单元测试。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数；第二项必须是已注册的测试名称。
 * @return 测试通过时返回 EXIT_SUCCESS，否则返回 EXIT_FAILURE。
 */
int main(int argc, char** argv)
{
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"owner_once", effectOwnerAndOnce}, {"dropped_token", effectDroppedToken},
        {"reverse_order", effectReverseOrder}, {"reentrant_setup", effectReentrantSetup},
        {"setup_failure", effectSetupFailure}, {"async_setup_failure", effectAsyncSetupFailure},
        {"async_cleanup", effectAsyncCleanup}, {"cleanup_errors", effectCleanupErrors},
        {"cleanup_registration", effectNoRegistrationDuringCleanup}, {"broadcast_cancel", effectBroadcastBeforeCleanup},
        {"adopted_group", effectAdoptedGroup}, {"adoption_cycle", effectAdoptionCycle},
        {"delivery_blocked", effectDeliveryBlocked}, {"wrong_thread", effectWrongThread},
        {"clock_ordering", clockOrdering}, {"clock_limits", clockLimits},
        {"observation_timeout", observationTimeout}, {"observation_completion", observationCompletion},
        {"observation_clock_closed", observationClockClosed}, {"observation_immediate_failure", observationImmediateFailure},
        {"async_registration_guard", effectAsyncRegistrationGuard}, {"stale_context", effectStaleContext},
        {"blocked_setup_delivery", effectBlockedSetupDelivery}, {"retains_failed_closure", effectRetainsFailedClosure},
        {"blocked_cleanup_delivery", effectBlockedCleanupDelivery},
        {"cleanup_destructor_guard", effectCleanupDestructorGuard},
        {"clock_step_budget", clockStepBudget}, {"clock_thread_reentry", clockThreadAndReentry},
        {"observation_delayed_failure", observationDelayedFailure}, {"observation_deadline_race", observationDeadlineRace}
    };
    if (argc != 2) return EXIT_FAILURE;
    for (const auto& test : cases) {
        if (test.first != argv[1]) continue;
        try { test.second(); }
        catch (const std::exception& error) { std::cerr << test.first << ": " << error.what() << '\n'; return EXIT_FAILURE; }
        std::cout << test.first << ": passed\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
