/** @file scheduler.cpp
 * @brief 以真实 Qt 循环验证调度、关闭仲裁、异常边界及内核异步停止。
 */
#include <cordis/qt/scheduler.hpp>
#include <cordis/kernel.hpp>
#include "../support/reentrant_callable.hpp"
#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace cordis;
using cordis::qt::QtScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 驱动真实循环到指定任务通知抵达，超时只让测试失败，不伪造任务完成。 */
template<class T> void waitTask(const Task<T>& task, const std::shared_ptr<QtScheduler>& scheduler)
{
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool delivered = false;
    auto receipt = task.thenOn(scheduler, [&](const Result<T>&) { delivered = true; loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(5000);
    loop.exec();
    CHECK(delivered && receipt.isReady() && receipt.tryResult()->hasValue());
}

/** @brief owner 和回调内 post 始终排队，嵌套 processEvents 不重入业务。 */
void fifo()
{
    auto scheduler = std::make_shared<QtScheduler>();
    std::vector<int> order;
    Promise<void> done;
    CHECK(scheduler->post({[&] {
        order.push_back(1);
        CHECK(scheduler->post({[&] { order.push_back(3); done.resolve(); }, {}}));
        QCoreApplication::processEvents();
        CHECK(order == std::vector<int>{1});
    }, {}}));
    CHECK(scheduler->post({[&] { order.push_back(2); }, {}}));
    CHECK(order.empty());
    waitTask(done.task(), scheduler);
    CHECK((order == std::vector<int>{1, 2, 3}));
    auto view = scheduler->snapshot().value();
    CHECK(!view.running && view.queued == 0 && view.accepted == view.executed && view.callbackErrors == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief 已完成任务和 worker 完成通知均不内联，错误线程不能关闭或构造。 */
void tasks()
{
    auto scheduler = std::make_shared<QtScheduler>();
    bool ran = false;
    auto receipt = completedTask().thenOn(scheduler, [&](const Result<void>& value) { CHECK(value.hasValue()); ran = true; });
    CHECK(!ran && !receipt.isReady());
    waitTask(receipt, scheduler);
    CHECK(ran);
    Promise<void> source;
    bool owner = false, wrong = false, constructorRejected = false;
    auto completed = source.task().thenOn(scheduler, [&](const Result<void>&) { owner = scheduler->isCurrentThread(); });
    std::thread worker([&] {
        wrong = scheduler->close().error().code == ErrorCode::WrongThread && !scheduler->snapshot().hasValue();
        try { QtScheduler invalid; } catch (const std::invalid_argument&) { constructorRejected = true; }
        source.resolve();
    });
    worker.join();
    CHECK(!owner && !completed.isReady());
    waitTask(completed, scheduler);
    CHECK(owner && wrong && constructorRejected);
    CHECK(scheduler->close().hasValue());
}

/** @brief 关闭锁外通知全部丢弃，抛错或重入不影响其他项，拒绝不冒充丢弃。 */
void discard()
{
    auto scheduler = std::make_shared<QtScheduler>();
    int removed = 0;
    CHECK(!scheduler->post({{}, [&] { ++removed; }}));
    CHECK(scheduler->post({[] { CHECK(false); }, [&] {
        ++removed;
        CHECK(!scheduler->post({[] {}, {}}));
        CHECK(scheduler->close().hasValue());
        throw std::runtime_error("discard failed");
    }}));
    CHECK(scheduler->post({[] { CHECK(false); }, [&] { ++removed; }}));
    auto receipt = completedTask().thenOn(scheduler, [](const Result<void>&) { CHECK(false); });
    CHECK(scheduler->close().hasValue());
    CHECK(removed == 2 && receipt.tryResult()->error().code == ErrorCode::SchedulerClosed);
    const auto view = scheduler->snapshot().value();
    CHECK(!view.open && view.accepted == 3 && view.discarded == 3 && view.executed == 0 && view.callbackErrors == 1);
}

/** @brief lifetime 销毁和并发 post 竞争时，每个接受项恰好丢弃一次。 */
void lifetimeRace()
{
    auto lifetime = std::make_unique<QObject>();
    auto scheduler = std::make_shared<QtScheduler>(lifetime.get());
    std::promise<void> started;
    auto ready = started.get_future();
    std::atomic<int> accepted{0};
    std::vector<int> counts(500, 0);
    std::thread worker([&] {
        for (std::size_t i = 0; i < counts.size(); ++i) {
            if (scheduler->post({[] { CHECK(false); }, [&, i] { ++counts[i]; }})) ++accepted;
            if (i == 0) started.set_value();
        }
    });
    ready.wait();
    lifetime.reset();
    worker.join();
    int discarded = 0;
    for (const auto count : counts) { CHECK(count <= 1); discarded += count; }
    const auto view = scheduler->snapshot().value();
    CHECK(!view.open && view.queued == 0 && discarded == accepted.load());
    CHECK(view.accepted == view.discarded && view.executed == 0 && view.discarded == static_cast<std::size_t>(discarded));
    CHECK(!scheduler->post({[] {}, {}}));
}

/** @brief 最后一个 facade 在 worker 释放也不取消已接受任务或跨线程析构闭包。 */
void droppedHandle()
{
    QObject lifetime;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool called = false;
    const auto owner = std::this_thread::get_id();
    auto scheduler = std::make_shared<QtScheduler>(&lifetime);
    CHECK(scheduler->post({[&] { called = std::this_thread::get_id() == owner; loop.quit(); }, {}}));
    std::thread worker([handle = std::move(scheduler)]() mutable { handle.reset(); });
    worker.join();
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(5000);
    loop.exec();
    CHECK(called);
}

/** @brief 业务异常不越过 Qt event 边界，随后任务仍执行；执行中关闭丢弃后项。 */
void exceptions()
{
    auto scheduler = std::make_shared<QtScheduler>();
    Promise<void> done;
    CHECK(scheduler->post({[] { throw std::runtime_error("run failed"); }, {}}));
    CHECK(scheduler->post({[&] { done.resolve(); }, {}}));
    waitTask(done.task(), scheduler);
    CHECK(scheduler->snapshot().value().callbackErrors == 1);
    QEventLoop loop;
    bool discarded = false;
    CHECK(scheduler->post({[&] {
        CHECK(scheduler->snapshot().value().running);
        CHECK(scheduler->close().hasValue());
        loop.quit();
    }, {}}));
    CHECK(scheduler->post({[] { CHECK(false); }, [&] { discarded = true; }}));
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    CHECK(discarded && scheduler->snapshot().value().queued == 0);
}

/** @brief 保持 Qt 循环到 Kernel 的异步清理成功，再关闭 scheduler。 */
void kernelShutdown()
{
    auto scheduler = std::make_shared<QtScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    bool cleaned = false;
    auto runtime = kernel.registerPlugin("qt-plugin", [&](Context context) {
        CHECK(context.onCleanup([&](CleanupContext) {
            return gate.task().thenOn(scheduler, [&](const Result<void>& result) { CHECK(result.hasValue()); cleaned = true; });
        }).hasValue());
        return completedTask();
    }).value();
    auto instance = kernel.mount(runtime).value();
    waitTask(instance.ready(), scheduler);
    auto closed = kernel.close();
    CHECK(!closed.isReady());
    CHECK(scheduler->post({[&] {
        CHECK(!closed.isReady() && !cleaned);
        std::thread worker([gate] { gate.resolve(); });
        worker.join();
    }, {}}));
    waitTask(closed, scheduler);
    CHECK(closed.tryResult()->value().quiescent && cleaned);
    CHECK(kernel.snapshot().value().instances.empty());
    CHECK(scheduler->snapshot().value().queued == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief aboutToQuit 同步关门并丢弃普通事件，不把未执行任务记为运行成功。 */
void applicationQuit(QCoreApplication& app)
{
    auto scheduler = std::make_shared<QtScheduler>();
    bool posted = false, discarded = false, ran = false;
    QTimer::singleShot(0, &app, [&] {
        posted = scheduler->post({[&] { ran = true; }, [&] { discarded = true; }});
        app.quit();
    });
    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    app.exec();
    CHECK(posted && discarded && !ran && !scheduler->isOpen());
    CHECK(scheduler->snapshot().value().discarded == 1);
}

/** @brief 标准库小对象移动/析构中的用户代码始终在队列锁外，且不改变执行账目。 */
void callableReentry()
{
    auto scheduler = std::make_shared<QtScheduler>();
    int ran = 0, discarded = 0;
    Promise<void> done;
    CHECK(scheduler->post({cordis::test::ReentrantCallable(*scheduler, ran), cordis::test::ReentrantCallable(*scheduler, discarded)}));
    CHECK(scheduler->post({[&] { done.resolve(); }, {}}));
    waitTask(done.task(), scheduler);
    CHECK(ran == 1 && discarded == 0);
    CHECK(scheduler->post({cordis::test::ReentrantCallable(*scheduler, ran), cordis::test::ReentrantCallable(*scheduler, discarded)}));
    CHECK(scheduler->close().hasValue());
    CHECK(!scheduler->post({cordis::test::ReentrantCallable(*scheduler, ran), cordis::test::ReentrantCallable(*scheduler, discarded)}));
    const auto view = scheduler->snapshot().value();
    CHECK(ran == 1 && discarded == 1 && view.discarded == 1 && view.queued == 0 && view.callbackErrors == 0);
}

/** @brief 丢弃通知释放最后的句柄及 Qt lifetime 后，仍能隔离异常并完成整批通知。 */
void closeReleasesOwner()
{
    auto lifetime = std::make_unique<QObject>();
    auto scheduler = std::make_shared<QtScheduler>(lifetime.get());
    std::weak_ptr<QtScheduler> observer = scheduler;
    int discarded = 0;
    CHECK(scheduler->post({[] {}, [&] {
        ++discarded;
        scheduler.reset();
        lifetime.reset();
        throw std::runtime_error("discard released owners");
    }}));
    CHECK(scheduler->post({[] {}, [&] { ++discarded; }}));
    CHECK(scheduler->close().hasValue());
    CHECK(observer.expired() && !lifetime && discarded == 2);
}

/** @brief 每个场景使用独立进程及应用，构造前无应用场景也必须拒绝。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        bool rejected = false;
        try { QtScheduler invalid; } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
        QCoreApplication app(argc, argv);
        const std::string name = argv[1];
        if (name == "fifo") fifo();
        else if (name == "tasks") tasks();
        else if (name == "discard") discard();
        else if (name == "lifetime_race") lifetimeRace();
        else if (name == "dropped_handle") droppedHandle();
        else if (name == "exceptions") exceptions();
        else if (name == "kernel_shutdown") kernelShutdown();
        else if (name == "application_quit") applicationQuit(app);
        else if (name == "callable_reentry") callableReentry();
        else if (name == "close_releases_owner") closeReleasesOwner();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
