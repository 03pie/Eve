/** @file events.cpp
 * @brief 验证事件协议、快照失活、同步 Next 及异步资源屏障。
 */
#include <cordis/event.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
using Decision = EventDecision<int>;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 构造同步继续结果。 */
Result<Decision> continued() { return Result<Decision>::success(Decision::Continue()); }
/** @brief 构造已成功完成的异步决定。 */
Task<Decision> decided(Decision decision = Decision::Continue())
{ Promise<Decision> result; result.resolve(std::move(decision)); return result.task(); }

/** @brief emit 保持位置顺序，快照期间新增不执行，注销项进入前跳过，首错停止。 */
void syncSnapshot()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"emit"}, EventMode::Emit).hasValue());
    std::vector<int> order;
    bool added = false;
    std::optional<EffectToken> removed;
    CHECK(bus.on(scope, {"emit"}, [&](const int&) {
        order.push_back(1);
        removed->close();
        if (!added) {
            added = true;
            CHECK(bus.on(scope, {"emit"}, [&](const int&) { order.push_back(3); return continued(); }).hasValue());
        }
        return Result<Decision>::success(Decision::Stop(0));
    }).hasValue());
    removed.emplace(bus.on(scope, {"emit"}, [&](const int&) { order.push_back(2); return continued(); }).value());
    CHECK(bus.on(scope, {"emit"}, [&](const int&) { order.push_back(0); return continued(); }, {false, true, {}}).hasValue());
    CHECK(bus.emit({"emit"}, 7).hasValue());
    CHECK((order == std::vector<int>{0, 1}));
    order.clear();
    CHECK(bus.emit({"emit"}, 7).hasValue());
    CHECK((order == std::vector<int>{0, 1, 3}));
    CHECK(bus.on(scope, {"emit"}, [](const int&) -> Result<Decision> { throw std::runtime_error("emit failed"); }).hasValue());
    CHECK(bus.on(scope, {"emit"}, [](const int&) { CHECK(false); return continued(); }).hasValue());
    CHECK(bus.emit({"emit"}, 1).error().code == ErrorCode::CallbackFailed);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
    CHECK(bus.snapshot().value().liveListeners == 0 && bus.snapshot().value().activeDispatches == 0);
}

/** @brief once 先失活再调用，回调内重入 emit 只触发一次。 */
void onceAndFilters()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"once"}, EventMode::Emit).hasValue());
    int once = 0, ordinary = 0;
    CHECK(bus.on(scope, {"once"}, [&](const int& value) {
        ++once;
        CHECK(bus.emit({"once"}, value).hasValue());
        return continued();
    }, {true, false, [](const int& value) { return value > 0; }}).hasValue());
    CHECK(bus.on(scope, {"once"}, [&](const int&) { ++ordinary; return continued(); }).hasValue());
    CHECK(bus.emit({"once"}, 0).hasValue() && once == 0);
    CHECK(bus.emit({"once"}, 1).hasValue() && once == 1 && ordinary == 3);
    CHECK(bus.emit({"once"}, 1).hasValue() && once == 1 && ordinary == 4);
    std::optional<EffectToken> filtered;
    filtered.emplace(bus.on(scope, {"once"}, [](const int&) { CHECK(false); return continued(); },
        {false, false, [&](const int&) { filtered->close(); return true; }}).value());
    CHECK(bus.emit({"once"}, 1).hasValue());
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief bail 对零与空字符串使用显式 Stop，Observer 隔离所有失败。 */
void bailAndObservers()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"bail"}, EventMode::Bail).hasValue());
    CHECK(bus.on(scope, {"bail"}, [](const int&) { return Result<Decision>::success(Decision::Stop(0)); }).hasValue());
    CHECK(bus.on(scope, {"bail"}, [](const int&) { CHECK(false); return continued(); }).hasValue());
    CHECK(bus.bail({"bail"}, 1).value().stopped.value() == 0);
    EventBus<std::string> text(scheduler);
    CHECK(text.declare({"text"}, EventMode::Bail).hasValue());
    CHECK(text.on(scope, {"text"}, [](const std::string&) { return Result<EventDecision<std::string>>::success(EventDecision<std::string>::Stop("")); }).hasValue());
    CHECK(text.bail({"text"}, "input").value().stopped.value().empty());
    CHECK(bus.declare({"observe"}, EventMode::Observer).hasValue());
    int calls = 0;
    CHECK(bus.on(scope, {"observe"}, [](const int&) -> Result<Decision> { throw std::runtime_error("first"); }).hasValue());
    CHECK(bus.on(scope, {"observe"}, [&](const int&) { ++calls; return Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, "second")); }).hasValue());
    CHECK(bus.on(scope, {"observe"}, [&](const int&) { ++calls; return continued(); }).hasValue());
    auto report = bus.observe({"observe"}, 1).value();
    CHECK(calls == 2 && report.size() == 2 && report[0].message == "first" && report[1].message == "second");
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief waterfall 变换参数，veto 不进入下游，Next 重复与逃逸均失败。 */
void waterfallNext()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"flow"}, EventMode::Waterfall).hasValue());
    std::optional<EventNext<int>> escaped;
    CHECK(bus.onWaterfall(scope, {"flow"}, [&](const int& value, EventNext<int> next) {
        escaped.emplace(next);
        auto downstream = next(value + 1);
        CHECK(next(value).error().code == ErrorCode::NextInvalid);
        return downstream;
    }).hasValue());
    CHECK(bus.onWaterfall(scope, {"flow"}, [](const int& value, EventNext<int>) { return Result<Decision>::success(Decision::Stop(value * 2)); }).hasValue());
    CHECK(bus.onWaterfall(scope, {"flow"}, [](const int&, EventNext<int>) { CHECK(false); return continued(); }).hasValue());
    CHECK(bus.waterfall({"flow"}, 3).value().stopped.value() == 8);
    CHECK((*escaped)(1).error().code == ErrorCode::NextInvalid);
    bool wrong = false;
    std::thread worker([&] { wrong = (*escaped)(1).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    CHECK(bus.declare({"throw"}, EventMode::Waterfall).hasValue());
    CHECK(bus.onWaterfall(scope, {"throw"}, [&](const int&, EventNext<int> next) -> Result<Decision> { escaped.emplace(next); throw std::runtime_error("flow failed"); }).hasValue());
    CHECK(bus.waterfall({"throw"}, 1).error().code == ErrorCode::CallbackFailed);
    CHECK((*escaped)(1).error().code == ErrorCode::NextInvalid);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief serial 逐项等待，暂停期间注销的后项跳过，首 Stop 停止。 */
void serialOrder()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"serial"}, EventMode::Serial).hasValue());
    Promise<Decision> gate;
    std::vector<int> order;
    CHECK(bus.onAsync(scope, {"serial"}, [&](const int& value) { CHECK(value == 42); order.push_back(1); return gate.task(); }).hasValue());
    auto removed = bus.onAsync(scope, {"serial"}, [](const int&) { CHECK(false); return decided(); }).value();
    CHECK(bus.onAsync(scope, {"serial"}, [&](const int&) { order.push_back(3); return decided(Decision::Stop(0)); }).hasValue());
    CHECK(bus.onAsync(scope, {"serial"}, [](const int&) { CHECK(false); return decided(); }).hasValue());
    auto result = bus.serial({"serial"}, 42);
    CHECK(!result.isReady() && order.empty());
    CHECK(scheduler->runUntilIdle().hasValue() && order == std::vector<int>{1});
    auto removedDone = removed.close();
    CHECK(scheduler->runUntilIdle().hasValue() && !removedDone.isReady());
    std::thread worker([gate] { gate.resolve(Decision::Continue()); });
    worker.join();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK((order == std::vector<int>{1, 3}) && result.tryResult()->value().stopped.value() == 0);
    CHECK(removedDone.tryResult()->value().quiescent);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief parallel 启动全部项，乱序完成仍按 listener id 收集失败和停止值。 */
void parallelResults()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"parallel"}, EventMode::Parallel).hasValue());
    Promise<Decision> first, second;
    int calls = 0;
    CHECK(bus.onAsync(scope, {"parallel"}, [&](const int&) { ++calls; return first.task(); }).hasValue());
    CHECK(bus.onAsync(scope, {"parallel"}, [&](const int&) { ++calls; return second.task(); }, {false, true, {}}).hasValue());
    CHECK(bus.onAsync(scope, {"parallel"}, [&](const int&) -> Task<Decision> { ++calls; throw std::runtime_error("third"); }).hasValue());
    auto result = bus.parallel({"parallel"}, 7);
    CHECK(scheduler->runUntilIdle().hasValue() && calls == 3 && !result.isReady());
    second.resolve(Decision::Stop(0));
    CHECK(scheduler->runUntilIdle().hasValue() && !result.isReady());
    first.reject(makeError(ErrorCode::ActivationFailed, "first"));
    CHECK(scheduler->runUntilIdle().hasValue());
    const auto& outcomes = result.tryResult()->value().listeners;
    CHECK(outcomes.size() == 3 && outcomes[0].listener < outcomes[1].listener && outcomes[1].listener < outcomes[2].listener);
    CHECK(outcomes[0].result.error().message == "first" && outcomes[1].result.value().stopped.value() == 0);
    CHECK(outcomes[2].result.error().code == ErrorCode::CallbackFailed);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 监听器注销后快照仍持有闭包，异步源完成并由 owner 处理才可清理。 */
void lifetimeBarrier()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"life"}, EventMode::Serial).hasValue());
    Promise<Decision> gate;
    auto resource = std::make_shared<int>(7);
    std::weak_ptr<int> weak = resource;
    auto listener = bus.onAsync(scope, {"life"}, [resource, gate](const int& value) {
        CHECK(value == 9 && *resource == 7);
        return gate.task();
    }).value();
    resource.reset();
    auto result = bus.serial({"life"}, 9);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto closed = listener.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!closed.isReady() && !weak.expired());
    gate.resolve(Decision::Continue());
    CHECK(!closed.isReady() && !result.isReady());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(result.tryResult()->hasValue() && closed.tryResult()->value().quiescent && weak.expired());
    auto all = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && all.tryResult()->value().quiescent);
}

/** @brief 实例退役即拒绝快照中的旧回调，新代登记独立监听器。 */
void instanceEpoch()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"event"}, EventMode::Emit).hasValue());
    std::optional<Instance> instance;
    int calls = 0, activations = 0;
    Runtime runtime([&](Context context) {
        ++activations;
        CHECK(bus.on(context, {"event"}, [&](const int&) { ++calls; CHECK(instance->update("new", 1).hasValue()); return continued(); }).hasValue());
        CHECK(bus.on(context, {"event"}, [](const int&) { CHECK(false); return continued(); }).hasValue());
        return completedTask();
    });
    instance.emplace(Instance::mount(runtime, scheduler));
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(bus.emit({"event"}, 1).hasValue() && calls == 1);
    CHECK(scheduler->runUntilIdle().hasValue() && activations == 2);
    auto stopped = instance->stop();
    CHECK(bus.emit({"event"}, 1).hasValue() && calls == 1);
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
}

/** @brief scheduler 关闭阻断源结果投递时保留快照和回调，不伪报完成。 */
void deliveryBlocked()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"blocked"}, EventMode::Parallel).hasValue());
    Promise<Decision> gate;
    auto resource = std::make_shared<int>(1);
    std::weak_ptr<int> weak = resource;
    auto listener = bus.onAsync(scope, {"blocked"}, [resource, gate](const int&) { return gate.task(); }).value();
    resource.reset();
    auto result = bus.parallel({"blocked"}, 1);
    CHECK(scheduler->runUntilIdle().hasValue());
    auto closed = listener.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    scheduler->close();
    std::thread worker([gate] { gate.resolve(Decision::Continue()); });
    worker.join();
    CHECK(!result.isReady() && !closed.isReady() && !weak.expired());
    CHECK(bus.snapshot().value().deliveryBlocked && bus.snapshot().value().activeDispatches == 1);
    auto queuedScheduler = std::make_shared<FakeScheduler>();
    EventBus<int> queued(queuedScheduler);
    CHECK(queued.declare({"empty"}, EventMode::Serial).hasValue());
    auto discarded = queued.serial({"empty"}, 1);
    queuedScheduler->close();
    CHECK(!discarded.isReady() && queued.snapshot().value().deliveryBlocked);
    CHECK(queued.snapshot().value().activeDispatches == 1);
}

/** @brief 同步快照中已注销但未进入的闭包仍阻止 effect 报告静止。 */
void snapshotLifetime()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"snapshot"}, EventMode::Emit).hasValue());
    auto resource = std::make_shared<int>(1);
    std::weak_ptr<int> weak = resource;
    std::optional<EffectToken> removed;
    std::optional<Task<CleanupReport>> closing;
    CHECK(bus.on(scope, {"snapshot"}, [&](const int&) {
        closing.emplace(removed->close());
        CHECK(scheduler->runUntilIdle().hasValue());
        CHECK(!closing->isReady() && !weak.expired());
        return continued();
    }).hasValue());
    removed.emplace(bus.on(scope, {"snapshot"}, [resource](const int&) { CHECK(false); return continued(); }).value());
    resource.reset();
    CHECK(bus.emit({"snapshot"}, 1).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(closing->tryResult()->value().quiescent && weak.expired());
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief serial 首错退出、parallel 过滤器失败隔离、多个排队分发共享 once 状态。 */
void asyncEdges()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<int> bus(scheduler);
    CHECK(bus.declare({"error"}, EventMode::Serial).hasValue());
    Promise<Decision> failure;
    CHECK(bus.onAsync(scope, {"error"}, [failure](const int&) { return failure.task(); }).hasValue());
    CHECK(bus.onAsync(scope, {"error"}, [](const int&) { CHECK(false); return decided(); }).hasValue());
    auto failed = bus.serial({"error"}, 1);
    CHECK(scheduler->runUntilIdle().hasValue());
    failure.reject(makeError(ErrorCode::ActivationFailed, "source failed"));
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(failed.tryResult()->error().code == ErrorCode::ActivationFailed);
    CHECK(bus.declare({"once"}, EventMode::Parallel).hasValue());
    int calls = 0;
    CHECK(bus.onAsync(scope, {"once"}, [&](const int&) { ++calls; return decided(); }, {true, false, {}}).hasValue());
    auto first = bus.parallel({"once"}, 1);
    auto second = bus.parallel({"once"}, 1);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 1 && first.tryResult()->value().listeners.size() == 1 && second.tryResult()->value().listeners.empty());
    CHECK(bus.declare({"filter"}, EventMode::Parallel).hasValue());
    CHECK(bus.onAsync(scope, {"filter"}, [](const int&) { CHECK(false); return decided(); },
        {false, false, [](const int&) -> bool { throw std::runtime_error("filter failed"); }}).hasValue());
    CHECK(bus.onAsync(scope, {"filter"}, [](const int&) { return decided(); }).hasValue());
    auto filtered = bus.parallel({"filter"}, 1);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(filtered.tryResult()->value().listeners.size() == 2);
    CHECK(filtered.tryResult()->value().listeners[0].result.error().code == ErrorCode::CallbackFailed);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 描述协议、空回调、线程、scheduler 归属及精确域校验。 */
void validation()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto other = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler), foreign(other);
    EventBus<int> bus(scheduler);
    CHECK(!bus.declare({""}, EventMode::Emit).hasValue());
    CHECK(!bus.declare({"x", RealmId{}}, EventMode::Emit).hasValue());
    CHECK(bus.declare({"x"}, EventMode::Emit).hasValue());
    CHECK(bus.declare({"x"}, EventMode::Emit).hasValue());
    CHECK(!bus.declare({"x"}, EventMode::Bail).hasValue());
    CHECK(!bus.on(scope, {"x"}, {}).hasValue());
    CHECK(!bus.onAsync(scope, {"x"}, [](const int&) { return decided(); }).hasValue());
    CHECK(bus.on(foreign, {"x"}, [](const int&) { return continued(); }).error().code == ErrorCode::InvalidArgument);
    std::optional<Context> context;
    Runtime runtime([&](Context value) { context.emplace(value); return completedTask(); });
    auto instance = Instance::mount(runtime, other);
    CHECK(other->runUntilIdle().hasValue());
    CHECK(!bus.on(*context, {"x"}, [](const int&) { return continued(); }).hasValue());
    CHECK(!bus.emit({"x", RealmId{2}}, 1).hasValue());
    bool wrong = false;
    std::thread worker([&] { wrong = bus.emit({"x"}, 1).error().code == ErrorCode::WrongThread && !bus.on(scope, {"x"}, [](const int&) { return continued(); }).hasValue(); });
    worker.join();
    CHECK(wrong && bus.snapshot().value().liveListeners == 0);
    auto stopped = instance.stop();
    CHECK(other->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    CHECK(bus.on(scope, {"x"}, [&](const int&) { scheduler->close(); return continued(); }).hasValue());
    CHECK(bus.on(scope, {"x"}, [](const int&) { CHECK(false); return continued(); }).hasValue());
    CHECK(bus.emit({"x"}, 1).error().code == ErrorCode::SchedulerClosed);
    CHECK(bus.emit({"x"}, 1).error().code == ErrorCode::SchedulerClosed);
}

/** @brief 可在异步源完成后启用复制异常的测试负载，移动始终成功。 */
struct ThrowingValue {
    std::shared_ptr<bool> fail;
    /** @brief 保存独立于事件状态的测试开关。 */
    explicit ThrowingValue(std::shared_ptr<bool> flag) : fail(std::move(flag)) {}
    /** @brief 在指定复制边界抛错，模拟合法但可能失败的用户负载。 */
    ThrowingValue(const ThrowingValue& other) : fail(other.fail) { if (*fail) throw std::runtime_error("payload copy failed"); }
    /** @brief 移动负载不抛错，避免源任务生产混入测试复制边界。 */
    ThrowingValue(ThrowingValue&&) noexcept = default;
};

/** @brief serial 与 parallel 的结果复制抛错也必须完成任务并解除清理屏障。 */
void resultCopyFailure()
{
    using ValueDecision = EventDecision<ThrowingValue>;
    auto scheduler = std::make_shared<FakeScheduler>();
    EffectScope scope(scheduler);
    EventBus<ThrowingValue> bus(scheduler);
    auto fail = std::make_shared<bool>(false);
    Promise<ValueDecision> serialSource, parallelSource;
    CHECK(bus.declare({"serial"}, EventMode::Serial).hasValue());
    CHECK(bus.declare({"parallel"}, EventMode::Parallel).hasValue());
    CHECK(bus.onAsync(scope, {"serial"}, [serialSource](const ThrowingValue&) { return serialSource.task(); }).hasValue());
    CHECK(bus.onAsync(scope, {"parallel"}, [parallelSource](const ThrowingValue&) { return parallelSource.task(); }).hasValue());
    auto serial = bus.serial({"serial"}, ThrowingValue(fail));
    auto parallel = bus.parallel({"parallel"}, ThrowingValue(fail));
    CHECK(scheduler->runUntilIdle().hasValue());
    serialSource.resolve(ValueDecision::Stop(ThrowingValue(fail)));
    parallelSource.resolve(ValueDecision::Stop(ThrowingValue(fail)));
    *fail = true;
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(serial.isReady() && serial.tryResult()->error().code == ErrorCode::CallbackFailed);
    CHECK(parallel.isReady() && parallel.tryResult()->hasValue());
    CHECK(parallel.tryResult()->value().listeners[0].result.error().code == ErrorCode::CallbackFailed);
    CHECK(bus.snapshot().value().activeDispatches == 0 && !bus.snapshot().value().deliveryBlocked);
    auto closed = scope.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 按场景执行 W6 事件验证。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "snapshot") syncSnapshot();
        else if (name == "once_filter") onceAndFilters();
        else if (name == "bail_observe") bailAndObservers();
        else if (name == "next") waterfallNext();
        else if (name == "serial") serialOrder();
        else if (name == "parallel") parallelResults();
        else if (name == "lifetime") lifetimeBarrier();
        else if (name == "epoch") instanceEpoch();
        else if (name == "blocked") deliveryBlocked();
        else if (name == "validation") validation();
        else if (name == "snapshot_lifetime") snapshotLifetime();
        else if (name == "async_edges") asyncEdges();
        else if (name == "result_copy") resultCopyFailure();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
