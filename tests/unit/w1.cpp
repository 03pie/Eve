/**
 * @file w1.cpp
 * @brief 测试结果类型、调度器、异步任务及取消状态。
 */
#include <cordis/cancellation.hpp>
#include <cordis/task.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using cordis::ErrorCode;
using cordis::ErrorInfo;
using cordis::Promise;
using cordis::Result;
using cordis::makeError;
using cordis::testing::FakeScheduler;

/** @brief 条件不成立时抛出包含表达式和行号的测试异常。 */
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("check failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (false)

/**
 * @brief 验证操作会因错误的 Result 分支访问而抛出异常。
 * @tparam F 要执行的访问操作类型。
 * @param function 要验证的访问操作。
 */
template<class F> void badAccess(F function)
{
    bool caught = false;
    try { function(); } catch (const cordis::BadResultAccess&) { caught = true; }
    CHECK(caught);
}

/** @brief 验证 Result 的成功、失败值及错误分支访问行为。 */
void resultValues()
{
    auto value = Result<int>::success(42);
    CHECK(value.hasValue());
    CHECK(value.value() == 42);
    badAccess([&] { (void)value.error(); });
    auto error = Result<int>::failure(makeError(ErrorCode::InvalidArgument, "bad config"));
    CHECK(!error.hasValue());
    CHECK(error.error().message == "bad config");
    badAccess([&] { (void)error.value(); });
    auto success = Result<void>::success();
    success.value();
    badAccess([&] { (void)success.error(); });
    auto failure = Result<void>::failure(error.error());
    badAccess([&] { failure.value(); });
    CHECK(failure.error().code == ErrorCode::InvalidArgument);
    // ErrorInfo 本身也可以作为成功值，即使它同时也是错误类型。
    auto payload = Result<ErrorInfo>::success(error.error());
    CHECK(payload.value().message == "bad config");
}

/** @brief 验证 Result 对仅移动类型和引用限定访问的支持。 */
void resultMoveOnly()
{
    static_assert(!std::is_copy_constructible_v<Result<std::unique_ptr<int>>>);
    static_assert(std::is_move_constructible_v<Result<std::unique_ptr<int>>>);
    static_assert(!std::is_reference_v<decltype(Result<std::string>::success("temporary").value())>);
    static_assert(!std::is_reference_v<decltype(std::declval<const Result<std::string>&&>().value())>);
    static_assert(!std::is_reference_v<decltype(std::declval<const Result<int>&&>().error())>);
    auto value = Result<std::unique_ptr<int>>::success(std::make_unique<int>(9));
    auto moved = std::move(value).value();
    CHECK(*moved == 9);
    auto error = Result<void>::failure(makeError(ErrorCode::WrongThread, "owner only"));
    auto copiedError = std::move(error).error();
    CHECK(copiedError.code == ErrorCode::WrongThread);
}

/** @brief 验证调度器按 FIFO 顺序运行任务并允许回调追加任务。 */
void schedulerFifo()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    std::vector<int> order;
    CHECK(scheduler->post({[&] {
        order.push_back(1);
        CHECK(scheduler->post({[&] { order.push_back(3); }, {}}));
    }, {}}));
    CHECK(scheduler->post({[&] { order.push_back(2); }, {}}));
    CHECK(order.empty());
    CHECK(scheduler->pending() == 2);
    auto first = scheduler->runOne();
    CHECK(first.hasValue() && first.value());
    CHECK(order == std::vector<int>{1});
    CHECK(scheduler->runUntilIdle().value() == 2);
    CHECK((order == std::vector<int>{1, 2, 3}));
    CHECK(!scheduler->runOne().value());
}

/** @brief 验证跨线程投递有效且仅所属线程能够泵送任务。 */
void schedulerThread()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    bool wrongThread = false;
    bool posted = false;
    bool ran = false;
    std::thread worker([&] {
        auto result = scheduler->runOne();
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
        posted = scheduler->post({[&] { ran = scheduler->isCurrentThread(); }, {}});
    });
    worker.join();
    CHECK(wrongThread && posted && !ran);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(ran);
}

/** @brief 验证调度器步数上限及关闭后的拒绝行为。 */
void schedulerLimit()
{
    FakeScheduler scheduler;
    int calls = 0;
    std::function<void()> loop;
    loop = [&] { ++calls; CHECK(scheduler.post({loop, {}})); };
    CHECK(scheduler.post({loop, {}}));
    auto result = scheduler.runUntilIdle(5);
    CHECK(!result.hasValue() && result.error().code == ErrorCode::StepLimitExceeded);
    CHECK(calls == 5 && scheduler.pending() == 1);
    scheduler.close();
    CHECK(scheduler.pending() == 0);
    CHECK(!scheduler.post({loop, {}}));
}

/** @brief 验证调度器拒绝嵌套泵送并可从回调异常后恢复。 */
void schedulerRecovery()
{
    FakeScheduler scheduler;
    bool nestedRejected = false;
    CHECK(scheduler.post({[&] {
        auto result = scheduler.runOne();
        nestedRejected = !result.hasValue();
    }, {}}));
    CHECK(scheduler.runOne().value());
    CHECK(nestedRejected);
    CHECK(scheduler.post({[] { throw std::runtime_error("boom"); }, {}}));
    auto result = scheduler.runOne();
    CHECK(!result.hasValue() && result.error().code == ErrorCode::CallbackFailed);
    bool recovered = false;
    CHECK(scheduler.post({[&] { recovered = true; }, {}}));
    CHECK(scheduler.runUntilIdle().hasValue());
    CHECK(recovered);
}

/** @brief 验证已完成任务的回调仍会排队执行。 */
void taskReadyQueued()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    auto task = producer.task();
    CHECK(!task.isReady() && !task.tryResult());
    CHECK(producer.resolve(21));
    bool ran = false;
    auto delivery = task.thenOn(scheduler, [&](const Result<int>& result) {
        CHECK(scheduler->isCurrentThread());
        CHECK(result.value() == 21);
        ran = true;
    });
    CHECK(task.isReady() && !ran && !delivery.isReady());
    CHECK(scheduler->runUntilIdle().value() == 1);
    CHECK(ran && delivery.tryResult()->hasValue());
}

/** @brief 验证任务失败结果通过调度器回调投递。 */
void taskFailureQueued()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    bool failed = false;
    auto delivery = producer.task().thenOn(scheduler, [&](const Result<int>& result) {
        failed = !result.hasValue() && result.error().code == ErrorCode::ActivationFailed;
    });
    CHECK(!delivery.isReady());
    CHECK(producer.reject(makeError(ErrorCode::ActivationFailed, "test")));
    CHECK(!failed);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(failed && delivery.tryResult()->hasValue());
    CHECK(!producer.resolve(11));
}

/** @brief 验证多个观察者共享仅移动任务值的不可变快照。 */
void taskMoveOnlyObservers()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<std::unique_ptr<int>> producer;
    const int* addresses[2] = {nullptr, nullptr};
    auto first = producer.task().thenOn(scheduler, [&](const Result<std::unique_ptr<int>>& result) {
        addresses[0] = result.value().get();
    });
    auto second = producer.task().thenOn(scheduler, [&](const Result<std::unique_ptr<int>>& result) {
        addresses[1] = result.value().get();
    });
    CHECK(producer.resolve(std::make_unique<int>(73)));
    auto snapshot = producer.task().tryResult();
    CHECK(scheduler->runUntilIdle().value() == 2);
    CHECK(first.tryResult()->hasValue() && second.tryResult()->hasValue());
    CHECK(addresses[0] == addresses[1] && *addresses[0] == 73);
    CHECK(snapshot->value().get() == addresses[0]);
}

/** @brief 验证并发完成竞争中仅一个结果获胜且回调只投递一次。 */
void taskCompletionRace()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    auto task = producer.task();
    int calls = 0;
    auto delivered = task.thenOn(scheduler, [&](const Result<int>&) { ++calls; });
    std::atomic<bool> go{false};
    std::atomic<int> winners{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 12; ++i) {
        workers.emplace_back([producer, &go, &winners, i] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            const bool won = i % 2 == 0 ? producer.resolve(i)
                : producer.reject(makeError(ErrorCode::OperationCancelled, "race"));
            if (won) winners.fetch_add(1);
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    CHECK(winners.load() == 1 && task.isReady());
    CHECK(calls == 0);
    CHECK(scheduler->runUntilIdle().value() == 1);
    CHECK(calls == 1 && delivered.tryResult()->hasValue());
}

/** @brief 验证任务完成与并发注册观察者之间不会丢失通知。 */
void taskRegistrationRace()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    std::atomic<bool> go{false};
    std::atomic<int> calls{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 12; ++i) {
        workers.emplace_back([&, task = producer.task()] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            (void)task.thenOn(scheduler, [&](const Result<int>& result) {
                CHECK(result.value() == 55);
                calls.fetch_add(1);
            });
        });
    }
    go.store(true, std::memory_order_release);
    CHECK(producer.resolve(55));
    for (auto& worker : workers) worker.join();
    CHECK(calls.load() == 0);
    CHECK(scheduler->runUntilIdle().value() == 12);
    CHECK(calls.load() == 12);
}

/** @brief 验证注册回调前已关闭的调度器会报告投递失败。 */
void taskClosedBeforeRegistration()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    scheduler->close();
    Promise<void> producer;
    bool called = false;
    auto delivery = producer.task().thenOn(scheduler, [&](const Result<void>&) { called = true; });
    CHECK(delivery.isReady());
    CHECK(delivery.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(!producer.task().isReady() && !called);
}

/** @brief 验证关闭调度器会丢弃已排队的任务回调。 */
void taskDiscardQueued()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> producer;
    CHECK(producer.resolve());
    bool called = false;
    auto delivery = producer.task().thenOn(scheduler, [&](const Result<void>&) { called = true; });
    CHECK(scheduler->pending() == 1);
    scheduler->close();
    CHECK(!called && scheduler->pending() == 0);
    CHECK(delivery.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(producer.task().tryResult()->hasValue());
}

/** @brief 验证源任务完成后发现调度器关闭时回调不会运行。 */
void taskClosedBeforeCompletion()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    auto delivery = producer.task().thenOn(scheduler, [](const Result<int>&) {
        throw std::runtime_error("must not run");
    });
    scheduler->close();
    CHECK(!delivery.isReady()); // 源任务仍未完成；调度器关闭不会取消源任务。
    CHECK(producer.resolve(7));
    CHECK(delivery.tryResult()->error().code == ErrorCode::SchedulerClosed);
}

/** @brief 验证调度器销毁后待投递回调以关闭错误结束。 */
void taskDestroyedScheduler()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    std::weak_ptr<FakeScheduler> weak = scheduler;
    Promise<int> producer;
    auto delivery = producer.task().thenOn(scheduler, [](const Result<int>&) {});
    scheduler.reset();
    CHECK(weak.expired());
    CHECK(producer.resolve(3));
    CHECK(delivery.tryResult()->error().code == ErrorCode::SchedulerClosed);
}

/** @brief 验证回调异常只影响自身投递结果而不阻止后续回调。 */
void taskCallbackFailure()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> producer;
    auto failed = producer.task().thenOn(scheduler, [](const Result<void>&) { throw 42; });
    bool next = false;
    auto succeeded = producer.task().thenOn(scheduler, [&](const Result<void>&) { next = true; });
    CHECK(producer.resolve());
    CHECK(scheduler->runUntilIdle().value() == 2);
    CHECK(failed.tryResult()->error().code == ErrorCode::CallbackFailed);
    CHECK(succeeded.tryResult()->hasValue() && next);
}

/** @brief 验证最后一个 Promise 销毁时以 BrokenPromise 完成任务。 */
void taskBrokenPromise()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto task = [] { Promise<int> producer; return producer.task(); }();
    CHECK(task.tryResult()->error().code == ErrorCode::BrokenPromise);
    bool called = false;
    auto delivery = task.thenOn(scheduler, [&](const Result<int>& result) {
        called = result.error().code == ErrorCode::BrokenPromise;
    });
    CHECK(!called);
    CHECK(scheduler->runUntilIdle().value() == 1);
    CHECK(called && delivery.tryResult()->hasValue());
}

/** @brief 验证回调投递回执可以继续组成异步链。 */
void taskReceiptChain()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    std::vector<int> timeline;
    auto first = producer.task().thenOn(scheduler, [&](const Result<int>&) { timeline.push_back(1); });
    auto second = first.thenOn(scheduler, [&](const Result<void>& result) {
        result.value();
        timeline.push_back(2);
    });
    CHECK(producer.resolve(1));
    CHECK(scheduler->runOne().value());
    CHECK(timeline == std::vector<int>{1});
    CHECK(!second.isReady());
    CHECK(scheduler->runOne().value());
    CHECK((timeline == std::vector<int>{1, 2}));
}

/** @brief 验证空回调和空调度器会返回明确错误。 */
void taskInvalidObserver()
{
    Promise<int> producer;
    auto scheduler = std::make_shared<FakeScheduler>();
    auto empty = producer.task().thenOn(scheduler, {});
    CHECK(empty.tryResult()->error().code == ErrorCode::InvalidArgument);
    auto missing = producer.task().thenOn(nullptr, [](const Result<int>&) {});
    CHECK(missing.tryResult()->error().code == ErrorCode::SchedulerClosed);
}

/** @brief 验证取消状态与任务完成彼此独立且可跨线程共享。 */
void cancellationIndependent()
{
    cordis::CancellationSource source;
    auto token = source.token();
    CHECK(!token.isCancellationRequested());
    CHECK(!cordis::CancellationToken{}.isCancellationRequested());
    Promise<void> producer;
    bool first = false;
    std::thread worker([copy = source, &first] { first = copy.requestCancel(); });
    worker.join();
    CHECK(first && token.isCancellationRequested());
    CHECK(!source.requestCancel());
    CHECK(!producer.task().isReady());
    CHECK(producer.resolve()); // 协作式取消状态与任务完成彼此独立。
    CHECK(producer.task().tryResult()->hasValue());
    auto surviving = [] {
        cordis::CancellationSource local;
        local.requestCancel();
        return local.token();
    }();
    CHECK(surviving.isCancellationRequested());
}

/** @brief 验证 Promise 副本共享单次完成状态。 */
void taskProducerCopies()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<int> producer;
    auto task = producer.task();
    {
        auto copy = producer;
        CHECK(!task.isReady());
    }
    CHECK(!task.isReady());
    CHECK(producer.resolve(8));
    CHECK(!producer.reject(makeError(ErrorCode::InvalidArgument, "second")));
    CHECK(task.tryResult()->value() == 8);
    int observed = 0;
    auto receipt = task.thenOn(scheduler, [&](const Result<int>& result) { observed = result.value(); });
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(observed == 8 && receipt.tryResult()->hasValue());
}

/** @brief 验证调度器投递抛出异常时回执返回失败。 */
void taskThrowingScheduler()
{
    /**
     * @brief 模拟投递操作抛出异常的调度器。
     */
    class ThrowingScheduler final : public cordis::IScheduler {
    public:
        /** @brief 投递任务时抛出测试异常。 */
        bool post(cordis::ScheduledJob) override { throw std::runtime_error("post failed"); }
        /** @brief 测试调度器始终报告当前线程为所属线程。 */
        bool isCurrentThread() const noexcept override { return true; }
        /** @brief 测试调度器始终报告处于开启状态。 */
        bool isOpen() const override { return true; }
    };
    auto scheduler = std::make_shared<ThrowingScheduler>();
    Promise<void> producer;
    auto receipt = producer.task().thenOn(scheduler, [](const Result<void>&) {});
    CHECK(producer.resolve());
    CHECK(receipt.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(receipt.tryResult()->error().message == "post failed");
    CHECK(producer.task().tryResult()->hasValue());
}

/** @brief 验证并发完成与调度器关闭竞争时所有回执都得到结果。 */
void schedulerCloseRace()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> producer;
    std::vector<cordis::Task<void>> receipts;
    for (int i = 0; i < 24; ++i) {
        receipts.push_back(producer.task().thenOn(scheduler, [](const Result<void>&) {
            throw std::runtime_error("closed queue must not execute");
        }));
    }
    std::atomic<bool> go{false};
    std::thread completing([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        producer.resolve();
    });
    std::thread closing([&] {
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        scheduler->close();
    });
    go.store(true, std::memory_order_release);
    completing.join();
    closing.join();
    CHECK(scheduler->pending() == 0);
    for (const auto& receipt : receipts) {
        CHECK(receipt.isReady());
        CHECK(receipt.tryResult()->error().code == ErrorCode::SchedulerClosed);
    }
}

/** @brief 验证任务丢弃回调可以重入关闭且异常不会中断后续丢弃。 */
void schedulerCloseReentrant()
{
    FakeScheduler scheduler;
    int discarded = 0;
    CHECK(scheduler.post({[] {}, [&] {
        ++discarded;
        CHECK(!scheduler.post({[] {}, {}}));
        scheduler.close();
        throw std::runtime_error("discard observer failed");
    }}));
    CHECK(scheduler.post({[] {}, [&] { ++discarded; }}));
    scheduler.close();
    CHECK(discarded == 2 && scheduler.pending() == 0);
}

/**
 * @brief 根据测试名称运行单个 W1 单元测试。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数；第二项必须是已注册的测试名称。
 * @return 测试通过时返回 EXIT_SUCCESS，否则返回 EXIT_FAILURE。
 */
int main(int argc, char** argv)
{
    const std::vector<std::pair<std::string, std::function<void()>>> cases = {
        {"result_values", resultValues}, {"result_move_only", resultMoveOnly},
        {"scheduler_fifo", schedulerFifo}, {"scheduler_thread", schedulerThread},
        {"scheduler_limit", schedulerLimit}, {"scheduler_recovery", schedulerRecovery},
        {"task_ready_queued", taskReadyQueued}, {"task_failure_queued", taskFailureQueued},
        {"task_move_only_observers", taskMoveOnlyObservers}, {"task_completion_race", taskCompletionRace},
        {"task_registration_race", taskRegistrationRace}, {"task_closed_registration", taskClosedBeforeRegistration},
        {"task_discard_queued", taskDiscardQueued}, {"task_closed_completion", taskClosedBeforeCompletion},
        {"task_destroyed_scheduler", taskDestroyedScheduler}, {"task_callback_failure", taskCallbackFailure},
        {"task_broken_promise", taskBrokenPromise}, {"task_receipt_chain", taskReceiptChain},
        {"task_invalid_observer", taskInvalidObserver}, {"cancellation_independent", cancellationIndependent},
        {"task_producer_copies", taskProducerCopies}, {"task_throwing_scheduler", taskThrowingScheduler},
        {"scheduler_close_race", schedulerCloseRace}, {"scheduler_close_reentrant", schedulerCloseReentrant}
    };
    if (argc != 2) return EXIT_FAILURE;
    for (const auto& test : cases) {
        if (test.first != argv[1]) continue;
        try { test.second(); }
        catch (const std::exception& error) { std::cerr << test.first << ": " << error.what() << '\n'; return EXIT_FAILURE; }
        std::cout << test.first << ": passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "unknown test: " << argv[1] << '\n';
    return EXIT_FAILURE;
}
