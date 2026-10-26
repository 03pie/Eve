/** @file connection.cpp
 * @brief 真实 Qt 信号、Kernel/effect 与跨线程发射验证托管连接及异步关闭。
 */
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QMetaMethod>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cordis/kernel.hpp>
#include <cordis/qt/connection.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
using namespace cordis;
using namespace cordis::qt;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 不注册 Qt metatype，确保零参数桥不会偷偷复制信号负载。 */
struct Payload {
    static std::atomic_int copies;
    /** @brief 发射端直接构造局部负载。 */
    Payload() = default;
    /** @brief 仅用于检测不应出现的异步 Qt 参数复制。 */
    Payload(const Payload&) { ++copies; }
};
std::atomic_int Payload::copies{0};
/** @brief 发送者可以在 worker 发射，但构造/销毁由测试显式同步。 */
class Source final : public QObject {
    Q_OBJECT
public:
    std::function<void()> connecting;
    std::function<void()> disconnecting;
    /** @brief 只有线程局部负载和信号激活，不修改 QObject 属性。 */
    void fire()
    {
        Payload payload;
        Q_EMIT tick(payload);
    }
Q_SIGNALS:
    /** @brief 带引用负载的测试信号，适配器必须忽略参数。 */
    void tick(const Payload& payload);

protected:
    /** @brief 一次性模拟连接建立期间的宿主停止，不在 Qt 内部锁中泵送事件。 */
    void connectNotify(const QMetaMethod& signal) override
    {
        if (signal.name() == "tick") {
            auto hook = std::exchange(connecting, {});
            if (hook)
                hook();
        }
    }
    /** @brief 断连通知仅重入宿主关闭，不在 Qt 通知中主动泵送事件。 */
    void disconnectNotify(const QMetaMethod& signal) override
    {
        if (signal.name() == "tick") {
            auto hook = std::exchange(disconnecting, {});
            if (hook)
                hook();
        }
    }
};
/** @brief 仅使用实际状态完成；超时是故障诊断，不推测回执已处理。 */
void until(const std::function<bool()>& done)
{
    if (done())
        return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(0);
    deadline.start(5000);
    loop.exec();
    CHECK(done());
}
/** @brief 检查源任务的实际结果。 */
template <class T> void wait(const Task<T>& task)
{
    until([&] { return task.isReady(); });
    CHECK(task.tryResult()->hasValue());
}
/** @brief 测试 gate 共享状态可跨线程保存，不在超时后留下栈引用。 */
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    /** @brief worker 进入后等 owner 显式放行。 */
    void block()
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    /** @brief owner 等待真实进入而不泵送 Qt，保证可控的原生调用快照。 */
    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
    }
    /** @brief 显式结束 worker 原生调用。 */
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }
};
/** @brief 捕获析构必须在 owner，允许清理期间重入 Qt。 */
struct Probe {
    std::thread::id owner = std::this_thread::get_id();
    std::function<void()> destroyed;
    /** @brief 记录线程并注入真实捕获析构重入。 */
    ~Probe()
    {
        if (std::this_thread::get_id() != owner)
            std::abort();
        if (destroyed)
            destroyed();
    }
};
/** @brief 每项使用独立 Kernel，连接自动挂到当前 Context 代的 effect。 */
struct Scene {
    std::unique_ptr<Source> source = std::make_unique<Source>();
    std::shared_ptr<QtScheduler> scheduler = std::make_shared<QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::optional<Context> context;
    std::optional<Instance> instance;
    std::optional<QtConnection> connection;
    int calls = 0;
    /** @brief 激活只交付弱 Context，资源由测试随后登记。 */
    Scene()
    {
        auto runtime = kernel
                           ->registerPlugin("signal",
                                            [&](Context current) {
                                                context.emplace(current);
                                                return completedTask();
                                            })
                           .value();
        instance.emplace(kernel->mount(runtime).value());
        wait(instance->ready());
    }
    /** @brief 安装真实信号连接，不用直接调用适配器内部状态。 */
    void connect(std::function<Task<void>()> callback = {}, std::size_t limit = 256)
    {
        if (!callback)
            callback = [&] {
                CHECK(scheduler->isCurrentThread());
                ++calls;
                return completedTask();
            };
        auto value = QtConnection::observe(*context, scheduler, source.get(), &Source::tick, std::move(callback),
                                           "test.connection", limit);
        CHECK(value.hasValue());
        connection.emplace(value.value());
        wait(connection->ready());
    }
    /** @brief Kernel 屏障和独立原生资源确认同时成功。 */
    void close()
    {
        auto done = kernel->close();
        wait(done);
        CHECK(done.tryResult()->value().quiescent);
        if (connection) {
            auto state = connection->snapshot().value();
            CHECK(state.closed && state.nativeDestroyed && state.running == 0);
        }
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};
/** @brief 同线程及跨线程发射均排队到 owner，无 Qt 信号参数复制。 */
void queued()
{
    Scene scene;
    scene.connect();
    scene.source->fire();
    CHECK(scene.calls == 0);
    std::thread worker([&] { scene.source->fire(); });
    worker.join();
    until([&] { return scene.calls == 2 && scene.connection->snapshot().value().running == 0; });
    CHECK(Payload::copies.load() == 0);
    scene.close();
}
/** @brief 异步业务仍在途时 Kernel 不得成功关闭，取消后发射不进入业务。 */
void asynchronous()
{
    Scene scene;
    Promise<void> gate;
    scene.connect([&] {
        ++scene.calls;
        return gate.task();
    });
    scene.source->fire();
    until([&] { return scene.connection->snapshot().value().running == 1; });
    auto closed = scene.kernel->close();
    CHECK(!closed.isReady());
    scene.source->fire();
    gate.resolve();
    wait(closed);
    CHECK(closed.tryResult()->value().quiescent && scene.calls == 1);
    scene.close();
}
/** @brief 原生发射快照被其他直接监听阻塞时，disconnect 不能提前宣称原生租约释放。 */
void nativeSnapshot()
{
    Scene scene;
    scene.connect();
    auto gate = std::make_shared<Gate>();
    auto raw = QObject::connect(
        scene.source.get(), &Source::tick, scene.source.get(), [gate] { gate->block(); }, Qt::DirectConnection);
    std::thread worker([&] { scene.source->fire(); });
    struct Join {
        std::shared_ptr<Gate> gate;
        std::thread& worker;
        /** @brief 断言抛错时也放行并 join，保留原始测试失败诊断。 */
        ~Join()
        {
            gate->release();
            if (worker.joinable())
                worker.join();
        }
    } join{gate, worker};
    gate->waitEntered();
    auto closed = scene.connection->close();
    until([&] { return scene.connection->snapshot().value().closing; });
    const bool waited = !closed.isReady();
    gate->release();
    worker.join();
    QObject::disconnect(raw);
    wait(closed);
    CHECK(waited && scene.calls == 0 && scene.connection->snapshot().value().nativeRetired);
    scene.close();
}
/** @brief 业务体重入自身关闭并泵送事件，关闭仍等本次业务 Task 完成。 */
void reentry()
{
    Scene scene;
    std::optional<Task<void>> closed;
    bool pending = false;
    scene.connect([&] {
        ++scene.calls;
        closed.emplace(scene.connection->close());
        QCoreApplication::processEvents();
        pending = !closed->isReady();
        return completedTask();
    });
    scene.source->fire();
    until([&] { return closed.has_value(); });
    wait(*closed);
    CHECK(pending && scene.calls == 1);
    scene.close();
}
/** @brief 捕获析构也是受保护的 owner 清理栈，不能在析构中完成资源关闭。 */
void captures()
{
    Scene scene;
    bool survived = false;
    std::optional<Task<void>> closed;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    probe->destroyed = [&] {
        auto state = scene.connection->snapshot().value();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        survived = state.running > 0 && (!closed || !closed->isReady());
    };
    scene.connect([probe] { return completedTask(); });
    probe.reset();
    closed.emplace(scene.connection->close());
    wait(*closed);
    CHECK(survived && weak.expired());
    scene.close();
}
/** @brief 旧代排队通知被取消，新代只能登记自己的资源，旧 Context 不能复用。 */
void epochs()
{
    Scene scene;
    scene.connect();
    auto old = *scene.context;
    auto previous = *scene.connection;
    scene.source->fire();
    auto operation = scene.instance->update("next", scene.instance->snapshot().value().revision).value();
    wait(scene.instance->awaitTarget(operation));
    CHECK(scene.calls == 0 && previous.snapshot().value().closed);
    auto stale =
        QtConnection::observe(old, scene.scheduler, scene.source.get(), &Source::tick, [] { return completedTask(); });
    CHECK(!stale.hasValue());
    scene.connect();
    scene.source->fire();
    until([&] { return scene.calls == 1; });
    scene.close();
}
/** @brief 所属 scheduler、队列预算、线程及空回调/信号在登记前拒绝。 */
void guards()
{
    Scene scene;
    auto other = std::make_shared<QtScheduler>();
    CHECK(!QtConnection::observe(*scene.context, scene.scheduler, static_cast<Source*>(nullptr), &Source::tick, [] {
               return completedTask();
           }).hasValue());
    using Signal = void (Source::*)(const Payload&);
    CHECK(!QtConnection::observe(*scene.context, scene.scheduler, scene.source.get(), static_cast<Signal>(nullptr), [] {
               return completedTask();
           }).hasValue());
    CHECK(!QtConnection::observe(*scene.context, other, scene.source.get(), &Source::tick, [] {
               return completedTask();
           }).hasValue());
    CHECK(!QtConnection::observe(*scene.context, scene.scheduler, scene.source.get(), &Source::tick, {}, "bad", 1)
               .hasValue());
    CHECK(!QtConnection::observe(
               *scene.context, scene.scheduler, scene.source.get(), &Source::tick, [] { return completedTask(); },
               "bad", 0)
               .hasValue());
    CHECK(!QtConnection::observe(
               *scene.context, scene.scheduler, scene.source.get(), &Source::tick, [] { return completedTask(); },
               "bad", 65537)
               .hasValue());
    bool wrong = false;
    std::thread worker([&] {
        auto result = QtConnection::observe(*scene.context, scene.scheduler, scene.source.get(), &Source::tick,
                                            [] { return completedTask(); });
        wrong = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    scene.connect();
    bool refused = false;
    std::thread closer([&] {
        auto result = scene.connection->close();
        refused = result.isReady() && !result.tryResult()->hasValue();
    });
    closer.join();
    CHECK(refused && scene.connection->snapshot().value().open);
    other->close().value();
    scene.close();
}
/** @brief 发送者销毁后自动释放插件捕获，Core 的总体清理仍单独确认。 */
void senderDestroyed()
{
    Scene scene;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.connect([probe] { return completedTask(); });
    probe.reset();
    scene.source.reset();
    until([&] { return scene.connection->snapshot().value().closed; });
    CHECK(weak.expired());
    scene.close();
}
/** @brief 丢弃 facade 不取消已登记的连接，Kernel 仍拥有清理责任。 */
void dropped()
{
    Scene scene;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.connect([&, probe] {
        ++scene.calls;
        return completedTask();
    });
    probe.reset();
    scene.connection.reset();
    scene.source->fire();
    until([&] { return scene.calls == 1; });
    scene.close();
    CHECK(weak.expired());
}
/** @brief 抛错和失败任务只计业务错误，已释放资源的关闭仍成功。 */
void exceptions()
{
    Scene scene;
    scene.connect([&]() -> Task<void> {
        if (++scene.calls == 1)
            throw std::runtime_error("signal error");
        return failedTask(makeError(ErrorCode::ConfigInvalid, "rejected"));
    });
    scene.source->fire();
    until([&] { return scene.connection->snapshot().value().callbackErrors == 1; });
    scene.source->fire();
    until([&] { return scene.connection->snapshot().value().callbackErrors == 2; });
    CHECK(scene.connection->snapshot().value().lastError);
    scene.close();
}
/** @brief 在途任务也占预算，溢出不增长队列或重复执行业务。 */
void capacity()
{
    Scene scene;
    Promise<void> gate;
    scene.connect(
        [&] {
            ++scene.calls;
            return gate.task();
        },
        1);
    scene.source->fire();
    scene.source->fire();
    scene.source->fire();
    CHECK(scene.connection->snapshot().value().accepted == 1 && scene.connection->snapshot().value().rejected == 2);
    until([&] { return scene.connection->snapshot().value().running == 1; });
    scene.source->fire();
    CHECK(scene.connection->snapshot().value().rejected == 3);
    auto closed = scene.connection->close();
    gate.resolve();
    wait(closed);
    CHECK(scene.calls == 1);
    scene.close();
}
/** @brief 业务 scheduler 先关闭后，资源仍能在私有 Qt 通知上排空异步完成与捕获。 */
void schedulerClosed()
{
    Scene scene;
    Promise<void> gate;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.connect([gate, probe] { return gate.task(); });
    probe.reset();
    scene.source->fire();
    until([&] { return scene.connection->snapshot().value().running == 1; });
    scene.source->fire();
    scene.scheduler->close().value();
    auto closed = scene.connection->close();
    CHECK(!closed.isReady());
    std::thread finisher([gate] { gate.resolve(); });
    finisher.join();
    wait(closed);
    CHECK(weak.expired() && scene.connection->snapshot().value().deliveryBlocked &&
          scene.connection->snapshot().value().nativeDestroyed);
    auto core = scene.kernel->close();
    CHECK(!core.isReady() || !core.tryResult()->hasValue() || !core.tryResult()->value().quiescent);
}
/** @brief native connectNotify 重入停止时，登记中的原生连接也在 Kernel 屏障内。 */
void registration()
{
    Scene scene;
    std::optional<Task<CleanupReport>> closed;
    scene.source->connecting = [&] { closed.emplace(scene.kernel->close()); };
    auto result = QtConnection::observe(*scene.context, scene.scheduler, scene.source.get(), &Source::tick,
                                        [] { return completedTask(); });
    CHECK(!result.hasValue() && closed);
    wait(*closed);
    CHECK(closed->tryResult()->value().quiescent);
    scene.close();
}
/** @brief 直接返回本连接的 close 任务形成等待环，须报告业务错误并排空而不是永久等待。 */
void closeCycle()
{
    Scene scene;
    scene.connect([&] { return scene.connection->close(); });
    scene.source->fire();
    until([&] { return scene.connection->snapshot().value().closed; });
    auto state = scene.connection->snapshot().value();
    CHECK(state.callbackErrors == 1 && state.lastError && state.lastError->code == ErrorCode::CleanupCycle);
    scene.close();
}
/** @brief 原生 disconnect 返回前，嵌套 close 不得释放可能拥有发送者的业务捕获。 */
void disconnectReentry()
{
    Scene scene;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.connect([probe] { return completedTask(); });
    probe.reset();
    bool retainedInside = false;
    scene.source->disconnecting = [&] {
        auto nested = scene.connection->close();
        retainedInside = !weak.expired() && !nested.isReady();
    };
    auto closed = scene.connection->close();
    wait(closed);
    CHECK(retainedInside && weak.expired());
    scene.close();
}
/** @brief 已取消业务的宿主空通知被 scheduler 丢弃，不应再记作业务投递故障。 */
void cancelledDelivery()
{
    Scene scene;
    scene.connect();
    scene.source->fire();
    auto closed = scene.connection->close();
    scene.scheduler->close().value();
    wait(closed);
    auto state = scene.connection->snapshot().value();
    CHECK(scene.calls == 0 && state.discarded == 1 && !state.deliveryBlocked && !state.lastError);
    auto core = scene.kernel->close();
    CHECK(!core.isReady() || !core.tryResult()->hasValue() || !core.tryResult()->value().quiescent);
}
/** @brief 每项真实 Qt 应用独立，避免全局信号或资源污染。 */
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "disconnect_reentry")
            disconnectReentry();
        else if (name == "cancelled_delivery")
            cancelledDelivery();
        else if (name == "close_cycle")
            closeCycle();
        else if (name == "queued")
            queued();
        else if (name == "async")
            asynchronous();
        else if (name == "native_snapshot")
            nativeSnapshot();
        else if (name == "reentry")
            reentry();
        else if (name == "captures")
            captures();
        else if (name == "epochs")
            epochs();
        else if (name == "guards")
            guards();
        else if (name == "sender_destroyed")
            senderDestroyed();
        else if (name == "dropped")
            dropped();
        else if (name == "exceptions")
            exceptions();
        else if (name == "capacity")
            capacity();
        else if (name == "scheduler_closed")
            schedulerClosed();
        else if (name == "registration")
            registration();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
#include "connection.moc"
