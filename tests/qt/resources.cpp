/** @file resources.cpp
 * @brief 使用真实 Qt 循环验证对象完整析构、计时取消、回调释放及 Kernel 资源屏障。
 */
#include <cordis/qt/clock.hpp>
#include <cordis/qt/object.hpp>
#include <cordis/kernel.hpp>
#include <cordis/observation.hpp>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace cordis;
using namespace cordis::qt;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)

/** @brief 原生事件循环持续运行到条件成立；不使用 sleep 或 FakeScheduler。 */
void waitUntil(const std::function<bool()>& ready)
{
    if (ready()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0);
    deadline.start(5000);
    loop.exec();
    CHECK(ready());
}
/** @brief 既检查源任务成功，也检查其排队 continuation 成功。 */
template<class T> void waitTask(const Task<T>& task, const std::shared_ptr<QtScheduler>& scheduler)
{
    auto observed = std::make_shared<bool>(false);
    auto receipt = task.thenOn(scheduler, [observed](const Result<T>&) { *observed = true; });
    waitUntil([&] { return receipt.isReady(); });
    CHECK(*observed && receipt.tryResult()->hasValue() && task.tryResult()->hasValue());
}
/** @brief 子对象析构期间执行探针，模拟析构重入事件循环。 */
class ProbeChild final : public QObject {
public:
    /** @brief parent 接管对象；探针只在当前测试作用域内使用。 */
    ProbeChild(QObject* parent, std::function<void()> probe) : QObject(parent), probe_(std::move(probe)) {}
    /** @brief 测试探针不抛错，以最终布尔断言报告屏障过早完成。 */
    ~ProbeChild() override { probe_(); }
private:
    std::function<void()> probe_;
};

/** @brief destroyed 先到达，子对象析构与嵌套事件处理均不得提前解除删除屏障。 */
void objectBarrier()
{
    auto scheduler = std::make_shared<QtScheduler>();
    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    QPointer<QObject> pointer = root.get();
    std::optional<Task<void>> closed;
    bool seen = false, childDone = false, barrierHeld = false;
    QObject::connect(root.get(), &QObject::destroyed, [&] { seen = true; });
    new ProbeChild(root.get(), [&] {
        barrierHeld = seen && closed && !closed->isReady();
        QCoreApplication::processEvents();
        barrierHeld = barrierHeld && !closed->isReady();
        childDone = true;
    });
    QObjectResource resource(root, scheduler);
    CHECK(!root && resource.snapshot().value().alive);
    closed = resource.close();
    CHECK(!closed->isReady() && pointer);
    waitTask(*closed, scheduler);
    const auto view = resource.snapshot().value();
    CHECK(seen && childDone && barrierHeld && pointer.isNull() && view.destroyedObserved && view.confirmed && !view.alive);
    CHECK(resource.close().tryResult()->hasValue());
    CHECK(scheduler->close().hasValue());
}

/** @brief 拒绝错误线程及 parent 所有权冲突，失败时不转移 unique_ptr。 */
void objectGuards()
{
    auto scheduler = std::make_shared<QtScheduler>();
    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    bool rejected = false;
    std::thread worker([&] {
        try { QObjectResource invalid(root, scheduler); } catch (const std::invalid_argument&) { rejected = true; }
    });
    worker.join();
    CHECK(rejected && root);
    QObject parent;
    root->setParent(&parent);
    rejected = false;
    try { QObjectResource invalid(root, scheduler); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected && root);
    root->setParent(nullptr);
    QObjectResource resource(root, scheduler);
    bool wrong = false;
    std::thread wrongThread([&] {
        wrong = resource.close().tryResult()->error().code == ErrorCode::WrongThread && !resource.snapshot().hasValue();
    });
    wrongThread.join();
    CHECK(wrong && resource.snapshot().value().alive);
    waitTask(resource.close(), scheduler);
    CHECK(scheduler->close().hasValue());
}

/** @brief 外部删除不能冒充受控完整销毁；保留可观察的 CleanupFailed。 */
void objectExternal()
{
    auto scheduler = std::make_shared<QtScheduler>();
    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    auto raw = root.get();
    QObjectResource resource(root, scheduler);
    delete raw;
    const auto view = resource.snapshot().value();
    CHECK(!view.alive && view.destroyedObserved && !view.confirmed);
    CHECK(resource.close().tryResult()->error().code == ErrorCode::CleanupFailed);
    CHECK(scheduler->close().hasValue());
}

/** @brief QWidget 也须通过整个删除调用，不能依赖其较早发出的 destroyed 信号。 */
void widgetBarrier()
{
    auto scheduler = std::make_shared<QtScheduler>();
    std::unique_ptr<QObject> widget = std::make_unique<QWidget>();
    bool childDone = false;
    QPointer<QObject> pointer = widget.get();
    new ProbeChild(widget.get(), [&] { childDone = true; });
    QObjectResource resource(widget, scheduler);
    auto closed = resource.close();
    CHECK(!closed.isReady() && pointer);
    waitTask(closed, scheduler);
    CHECK(pointer.isNull() && childDone && resource.snapshot().value().confirmed);
    CHECK(scheduler->close().hasValue());
}

/** @brief 零延迟和实际计时均异步回 owner；生产时钟可驱动 observeFor。 */
void clockFire()
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    int ran = 0;
    bool owner = true;
    auto first = clock.after(0ms, {[&] { ++ran; owner = owner && scheduler->isCurrentThread(); }, {}}).value();
    auto second = clock.after(5ms, {[&] { ++ran; owner = owner && scheduler->isCurrentThread(); }, {}}).value();
    CHECK(ran == 0 && first.active() && second.active());
    waitUntil([&] { return ran == 2; });
    CHECK(owner && !first.active() && !second.active());
    Promise<void> source;
    auto observation = observeFor(source.task(), clock, 0ms);
    waitTask(observation, scheduler);
    CHECK(observation.tryResult()->value().state == ObservationState::TimedOut);
    source.resolve();
    CHECK(!clock.after(-1ms, {[] {}, {}}).hasValue());
    CHECK(!clock.after(std::chrono::milliseconds(static_cast<long long>(std::numeric_limits<int>::max()) + 1), {[] {}, {}}).hasValue());
    CHECK(!clock.after(0ms, {{}, {}}).hasValue());
    waitTask(clock.close(), scheduler);
    const auto view = clock.snapshot().value();
    CHECK(view.executed == 3 && view.callbackErrors == 0 && view.nativeDestroyed && view.pending == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief worker 取消只改原子状态；关闭才确认载荷释放和原生对象销毁。 */
void clockCancel()
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    bool ownerDeleted = false;
    const auto owner = std::this_thread::get_id();
    auto payload = std::shared_ptr<int>(new int(0), [&](int* value) { delete value; ownerDeleted = std::this_thread::get_id() == owner; });
    std::weak_ptr<int> weak = payload;
    int discarded = 0;
    auto timer = clock.after(1h, {[payload] {}, [&] { ++discarded; }}).value();
    payload.reset();
    bool cancelled = false, wrong = false;
    std::thread worker([&] {
        cancelled = timer.cancel() && !timer.cancel();
        wrong = !clock.after(0ms, {[] {}, {}}).hasValue() && !clock.snapshot().hasValue() &&
                clock.close().tryResult()->error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(cancelled && wrong && !weak.expired());
    auto closed = clock.close();
    CHECK(!closed.isReady() && weak.expired() && ownerDeleted && discarded == 0);
    waitTask(closed, scheduler);
    const auto view = clock.snapshot().value();
    CHECK(view.cancelled == 1 && view.executed == 0 && view.discarded == 0 && view.nativeDestroyed);
    CHECK(scheduler->close().hasValue());
}

/** @brief 丢弃通知可重入和抛错，正常任务异常也被隔离，关闭幂等且拒绝新表。 */
void clockClose()
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    CHECK(clock.after(0ms, {[] { throw std::runtime_error("run"); }, {}}).hasValue());
    waitUntil([&] { return clock.snapshot().value().executed == 1; });
    int discarded = 0;
    bool reentrantPending = false, registrationRejected = false;
    CHECK(clock.after(1h, {[] {}, [&] {
        ++discarded;
        reentrantPending = !clock.close().isReady();
        registrationRejected = !clock.after(0ms, {[] {}, {}}).hasValue();
        throw std::runtime_error("discard");
    }}).hasValue());
    CHECK(clock.after(1h, {[] {}, [&] { ++discarded; }}).hasValue());
    auto closed = clock.close();
    CHECK(discarded == 2 && !closed.isReady() && reentrantPending && registrationRejected);
    waitTask(closed, scheduler);
    const auto view = clock.snapshot().value();
    CHECK(view.callbackErrors == 2 && view.discarded == 2 && view.accepted == view.executed + view.discarded);
    CHECK(clock.close().tryResult()->hasValue() && !clock.after(0ms, {[] {}, {}}).hasValue());
    CHECK(scheduler->close().hasValue());
}

/** @brief 执行中关闭，即使原生对象先被删除，也必须等业务捕获析构返回。 */
void clockInflight()
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    std::optional<Task<void>> closed;
    bool heldInRun = false, heldInDestructor = false;
    auto payload = std::shared_ptr<int>(new int(0), [&](int* value) {
        delete value;
        heldInDestructor = closed && !closed->isReady() && clock.snapshot().value().running == 1;
        QCoreApplication::processEvents();
        heldInDestructor = heldInDestructor && !closed->isReady();
    });
    CHECK(clock.after(0ms, {[&, payload] {
        closed = clock.close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        heldInRun = !closed->isReady() && clock.snapshot().value().nativeDestroyed;
    }, {}}).hasValue());
    payload.reset();
    waitUntil([&] { return closed && closed->isReady(); });
    CHECK(closed->tryResult()->hasValue() && heldInRun && heldInDestructor && clock.snapshot().value().running == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief scheduler 提前关门时到期任务被丢弃；原生资源仍可由 Qt 循环确认删除。 */
void clockSchedulerClosed()
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    int ran = 0, discarded = 0;
    CHECK(clock.after(0ms, {[&] { ++ran; }, [&] { ++discarded; }}).hasValue());
    CHECK(scheduler->close().hasValue());
    waitUntil([&] { return discarded == 1; });
    auto closed = clock.close();
    waitUntil([&] { return closed.isReady(); });
    CHECK(closed.tryResult()->hasValue() && ran == 0 && clock.snapshot().value().nativeDestroyed);
}

/** @brief 已到期排队的任务在关闭时释放业务载荷；残留宿主唤醒不阻止资源静止。 */
void clockQueued()
{
    auto scheduler = std::make_shared<QtScheduler>();
    for (const bool cancel : {false, true}) {
        QtClock clock(scheduler);
        int ran = 0, discarded = 0;
        bool finished = false, confirmed = false;
        auto payload = std::make_shared<int>(0);
        std::weak_ptr<int> weak = payload;
        CHECK(scheduler->post({[&, payload] {
            auto timer = clock.after(0ms, {[&, payload] { ++ran; }, [&] { ++discarded; }}).value();
            // 当前 scheduler 任务保持 running，原生 timeout 仍可由嵌套 Qt 循环投递。
            waitUntil([&] { return scheduler->snapshot().value().queued > 0; });
            if (cancel) CHECK(timer.cancel());
            auto closed = clock.close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            confirmed = closed.isReady() && closed.tryResult()->hasValue() && clock.snapshot().value().nativeDestroyed;
            finished = true;
        }, {}}));
        payload.reset();
        waitUntil([&] { return finished && scheduler->snapshot().value().queued == 0; });
        const auto view = clock.snapshot().value();
        CHECK(confirmed && weak.expired() && ran == 0 && discarded == (cancel ? 0 : 1));
        CHECK(view.pending == 0 && view.cancelled == (cancel ? 1u : 0u) && view.discarded == (cancel ? 0u : 1u));
    }
    CHECK(scheduler->snapshot().value().callbackErrors == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief QObject 和 Timer 的异步资源清理加入 Context，Kernel 只在完整屏障后静止。 */
void kernelShutdown()
{
    auto scheduler = std::make_shared<QtScheduler>();
    Kernel kernel(scheduler);
    bool destroyed = false;
    auto clock = std::make_shared<QtClock>(scheduler);
    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    new ProbeChild(root.get(), [&] { destroyed = true; });
    auto object = std::make_shared<QObjectResource>(root, scheduler);
    int discarded = 0;
    auto runtime = kernel.registerPlugin("qt-resource", [&](Context context) {
        CHECK(clock->after(1h, {[] {}, [&] { ++discarded; }}).hasValue());
        CHECK(context.onCleanup([object](CleanupContext) { return object->close(); }).hasValue());
        CHECK(context.onCleanup([clock](CleanupContext) { return clock->close(); }).hasValue());
        return completedTask();
    }).value();
    auto instance = kernel.mount(runtime).value();
    waitTask(instance.ready(), scheduler);
    auto closed = kernel.close();
    CHECK(!closed.isReady() && !destroyed);
    waitTask(closed, scheduler);
    CHECK(closed.tryResult()->value().quiescent && destroyed && discarded == 1);
    CHECK(clock->snapshot().value().nativeDestroyed && object->snapshot().value().confirmed);
    CHECK(kernel.snapshot().value().instances.empty());
    CHECK(scheduler->close().hasValue());
}

/** @brief 句柄在 worker 释放不销毁 QObject 或取消已接受的 timer；宿主守卫维持其所有权。 */
void droppedHandle()
{
    auto scheduler = std::make_shared<QtScheduler>();
    bool destroyed = false, ran = false;
    int discarded = 0;
    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    new ProbeChild(root.get(), [&] { destroyed = scheduler->isCurrentThread(); });
    auto object = std::make_unique<QObjectResource>(root, scheduler);
    auto closed = object->close();
    auto clock = std::make_unique<QtClock>(scheduler);
    CHECK(clock->after(0ms, {[&] { ran = scheduler->isCurrentThread(); }, {}}).hasValue());
    auto payload = std::make_shared<int>(0);
    std::weak_ptr<int> weak = payload;
    CHECK(clock->after(1h, {[payload] {}, [&] { ++discarded; }}).hasValue());
    payload.reset();
    std::thread worker([target = std::move(object), timer = std::move(clock)]() mutable { target.reset(); timer.reset(); });
    worker.join();
    CHECK(!destroyed && !ran);
    waitTask(closed, scheduler);
    waitUntil([&] { return ran; });
    CHECK(destroyed && !weak.expired());
    // 无 facade 的 clock 由退出兜底删除，本场景不把它记为正常 Kernel 关闭证据。
    auto app = QCoreApplication::instance();
    QTimer::singleShot(0, app, &QCoreApplication::quit);
    app->exec();
    CHECK(!scheduler->isOpen() && weak.expired() && discarded == 1);
}

/** @brief 回调可释放最后一个 facade；整批丢弃、完整对象删除与关闭任务仍须正确完成。 */
void closeReleasesHandles()
{
    auto scheduler = std::make_shared<QtScheduler>();
    auto clock = std::make_shared<QtClock>(scheduler);
    std::weak_ptr<QtClock> clockObserver = clock;
    int discarded = 0;
    CHECK(clock->after(1h, {[] {}, [&] { clock.reset(); ++discarded; }}).hasValue());
    CHECK(clock->after(1h, {[] {}, [&] { ++discarded; }}).hasValue());
    auto clockClosed = clock->close();
    CHECK(clockObserver.expired() && discarded == 2 && !clockClosed.isReady());
    waitTask(clockClosed, scheduler);

    std::unique_ptr<QObject> root = std::make_unique<QObject>();
    std::shared_ptr<QObjectResource> object;
    std::optional<Task<void>> objectClosed;
    bool held = false;
    new ProbeChild(root.get(), [&] {
        object.reset();
        held = objectClosed && !objectClosed->isReady();
    });
    object = std::make_shared<QObjectResource>(root, scheduler);
    std::weak_ptr<QObjectResource> objectObserver = object;
    objectClosed = object->close();
    waitTask(*objectClosed, scheduler);
    CHECK(held && objectObserver.expired());
    CHECK(scheduler->snapshot().value().callbackErrors == 0);
    CHECK(scheduler->close().hasValue());
}

/** @brief aboutToQuit 兜底请求删除，exec 收尾后仍须核对实际资源屏障。 */
void applicationQuit(QCoreApplication& app)
{
    auto scheduler = std::make_shared<QtScheduler>();
    QtClock clock(scheduler);
    int discarded = 0;
    CHECK(clock.after(1h, {[] {}, [&] { ++discarded; }}).hasValue());
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    app.exec();
    CHECK(discarded == 1 && !scheduler->isOpen());
    auto closed = clock.close();
    CHECK(closed.isReady() && closed.tryResult()->hasValue() && clock.snapshot().value().nativeDestroyed);
}

/** @brief 各场景独立使用一个 Qt 应用，失败返回非零。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        QApplication app(argc, argv);
        const std::string name = argv[1];
        if (name == "object_barrier") objectBarrier();
        else if (name == "object_guards") objectGuards();
        else if (name == "object_external") objectExternal();
        else if (name == "widget_barrier") widgetBarrier();
        else if (name == "clock_fire") clockFire();
        else if (name == "clock_cancel") clockCancel();
        else if (name == "clock_close") clockClose();
        else if (name == "clock_inflight") clockInflight();
        else if (name == "clock_scheduler_closed") clockSchedulerClosed();
        else if (name == "clock_queued") clockQueued();
        else if (name == "kernel_shutdown") kernelShutdown();
        else if (name == "dropped_handle") droppedHandle();
        else if (name == "close_releases_handles") closeReleasesHandles();
        else if (name == "application_quit") applicationQuit(app);
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
