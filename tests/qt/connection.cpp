/** @file connection.cpp
 * @brief 真实 Qt 信号、Kernel/effect 与跨线程发射验证托管连接及异步关闭。
 */
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QMetaMethod>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cordis/kernel.hpp>
#include <cordis/qt/connection.hpp>
#include <cordis/qt/object.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
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
    /** @brief 不注册元类型的标准值与 Qt 字符串信号。 */
    void values(const QString& text, const QByteArray& bytes, const std::string& standard, int number);
    /** @brief 标量信号用于预算、取消和并发场景。 */
    void scalar(int number);
    /** @brief 验证 const 信号与枚举/浮点数据。 */
    void qualified(bool flag, double number, Qt::ConnectionType kind) const;
    /** @brief 无数据的类型化入口仍可作为触发器。 */
    void ping();
    /** @brief 未选择的自定义尾参数必须保持不复制。 */
    void withTail(const QString& text, const Payload& ignored);

protected:
    /** @brief 一次性模拟连接建立期间的宿主停止，不在 Qt 内部锁中泵送事件。 */
    void connectNotify(const QMetaMethod& signal) override
    {
        if (signal.name() == "tick" || signal.name() == "scalar") {
            auto hook = std::exchange(connecting, {});
            if (hook)
                hook();
        }
    }
    /** @brief 断连通知仅重入宿主关闭，不在 Qt 通知中主动泵送事件。 */
    void disconnectNotify(const QMetaMethod& signal) override
    {
        if (signal.name() == "tick" || signal.name() == "scalar") {
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
    /** @brief 类型化资源使用与零参数连接相同的 Kernel/effect 和关闭检查。 */
    template <class Signal, class Callback>
    void connectValues(Signal signal, Callback callback, std::size_t limit = 256, std::size_t bytes = 1024 * 1024)
    {
        auto value = QtConnection::observeValues(*context, scheduler, source.get(), signal, std::move(callback),
                                                 "test.values", limit, bytes);
        if (!value.hasValue())
            std::cerr << "typed connection registration: " << value.error().message << '\n';
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
/** @brief 借用 Qt 缓冲区与标准字符串在发射后立即失效，owner 仍收到独立完整数据。 */
void typedFreeze()
{
    static_assert(cordis::qt::detail::signalValue<QString> && cordis::qt::detail::signalValue<QByteArray> &&
                  cordis::qt::detail::signalValue<std::string>);
    static_assert(!cordis::qt::detail::signalValue<Payload> && !cordis::qt::detail::signalValue<QObject*> &&
                  !cordis::qt::detail::signalValue<const char*>);
    Scene scene;
    scene.connectValues(&Source::values, [&](const QString& text, const QByteArray& bytes, const std::string& standard,
                                             int number) {
        CHECK(scene.scheduler->isCurrentThread());
        CHECK(text.size() == 3 && text.at(0) == QChar('A') && text.at(1).isNull() && text.at(2) == QChar('B'));
        CHECK(bytes == QByteArray("C\0D", 3) && standard == std::string("E\0F", 3) && number == 42);
        ++scene.calls;
        return completedTask();
    });
    std::thread worker([&] {
        QChar characters[] = {QChar('A'), QChar(0), QChar('B')};
        char raw[] = {'C', 0, 'D'};
        auto text = QString::fromRawData(characters, 3);
        auto bytes = QByteArray::fromRawData(raw, 3);
        std::string standard("E\0F", 3);
        Q_EMIT scene.source->values(text, bytes, standard, 42);
        characters[0] = QChar('X');
        raw[0] = 'Y';
        standard[0] = 'Z';
    });
    worker.join();
    CHECK(scene.calls == 0 && scene.connection->snapshot().value().payloads == 1);
    until([&] { return scene.calls == 1 && scene.connection->snapshot().value().payloads == 0; });
    scene.close();
}
/** @brief 回调返回后其异步任务仍可读取参数；关闭不得提前释放参数存储。 */
void typedAsync()
{
    Scene scene;
    Promise<void> gate;
    const int* borrowed = nullptr;
    scene.connectValues(&Source::scalar, [&](const int& number) {
        borrowed = &number;
        ++scene.calls;
        return gate.task();
    });
    Q_EMIT scene.source->scalar(73);
    until([&] { return scene.connection->snapshot().value().running == 1; });
    auto closed = scene.connection->close();
    QCoreApplication::processEvents();
    CHECK(!closed.isReady() && borrowed && *borrowed == 73 && scene.connection->snapshot().value().payloads == 1);
    std::thread completer([gate] { gate.resolve(); });
    completer.join();
    wait(closed);
    CHECK(scene.connection->snapshot().value().payloads == 0);
    scene.close();
}
/** @brief 已取消的类型化通知只留下空宿主消息，scheduler 关闭也不误报业务投递失败。 */
void typedCancelled()
{
    Scene scene;
    scene.connectValues(&Source::scalar, [&](int) { ++scene.calls; return completedTask(); });
    Q_EMIT scene.source->scalar(42);
    auto closed = scene.connection->close();
    scene.scheduler->close().value();
    wait(closed);
    const auto state = scene.connection->snapshot().value();
    CHECK(scene.calls == 0 && state.payloads == 0 && state.discarded == 1 && !state.deliveryBlocked);
}
/** @brief 旧代未进入的参数被撤销，新代资源只收到自己的数据。 */
void typedEpochs()
{
    Scene scene;
    auto callback = [&](int number) { scene.calls += number; return completedTask(); };
    scene.connectValues(&Source::scalar, callback);
    auto previous = *scene.connection;
    Q_EMIT scene.source->scalar(100);
    auto operation = scene.instance->update("new", scene.instance->snapshot().value().revision).value();
    wait(scene.instance->awaitTarget(operation));
    CHECK(scene.calls == 0 && previous.snapshot().value().closed && previous.snapshot().value().payloads == 0);
    scene.connectValues(&Source::scalar, callback);
    Q_EMIT scene.source->scalar(2);
    until([&] { return scene.calls == 2; });
    scene.close();
}
/** @brief 排队和在途业务共同占预算，异步工作完成后才释放可用容量。 */
void typedCapacity()
{
    Scene scene;
    Promise<void> gate;
    scene.connectValues(&Source::scalar, [&](int number) {
        ++scene.calls;
        return number < 3 ? gate.task() : completedTask();
    }, 2);
    Q_EMIT scene.source->scalar(1);
    Q_EMIT scene.source->scalar(2);
    Q_EMIT scene.source->scalar(3);
    CHECK(scene.connection->snapshot().value().accepted == 2 && scene.connection->snapshot().value().rejected == 1);
    until([&] { return scene.connection->snapshot().value().running == 2; });
    Q_EMIT scene.source->scalar(4);
    CHECK(scene.connection->snapshot().value().rejected == 2);
    gate.resolve();
    until([&] { return scene.connection->snapshot().value().payloads == 0; });
    Q_EMIT scene.source->scalar(5);
    until([&] { return scene.calls == 3; });
    scene.close();
}
/** @brief 内容字节预算计算所有参数；超额不进入业务，下一份合法数据仍可投递。 */
void typedBytes()
{
    Scene scene;
    scene.connectValues(&Source::values, [&](const QString&, const QByteArray&, const std::string&, int) {
        ++scene.calls;
        return completedTask();
    }, 2, 12);
    Q_EMIT scene.source->values(QStringLiteral("ab"), QByteArray("cde"), std::string("fg"), 1);
    until([&] { return scene.connection->snapshot().value().payloadErrors == 1; });
    CHECK(scene.calls == 0 && scene.connection->snapshot().value().lastError->code == ErrorCode::InvalidArgument);
    Q_EMIT scene.source->values(QString(), QByteArray(), std::string(), 2);
    until([&] { return scene.calls == 1; });
    scene.close();
}
/** @brief 冻结保留 null 与非 null 空字符串的区别，不将空值误当作关闭票据。 */
void typedNull()
{
    Scene scene;
    scene.connectValues(&Source::values, [&](const QString& text, const QByteArray& bytes, const std::string& standard,
                                             int number) {
        CHECK(text.isEmpty() && bytes.isEmpty() && standard.empty());
        CHECK(text.isNull() == (number == 0) && bytes.isNull() == (number == 0));
        ++scene.calls;
        return completedTask();
    });
    Q_EMIT scene.source->values(QString(), QByteArray(), std::string(), 0);
    Q_EMIT scene.source->values(QStringLiteral(""), QByteArray(""), std::string(), 1);
    until([&] { return scene.calls == 2; });
    scene.close();
}
/** @brief 发送者销毁不会撤销已进入异步业务所使用的数据；facade 丢弃也不取消资源。 */
void typedSender()
{
    Scene scene;
    Promise<void> gate;
    const int* value = nullptr;
    scene.connectValues(&Source::scalar, [&](const int& number) { value = &number; return gate.task(); });
    Q_EMIT scene.source->scalar(42);
    until([&] { return value != nullptr; });
    auto connection = *scene.connection;
    scene.connection.reset();
    scene.source.reset();
    QCoreApplication::processEvents();
    CHECK(!connection.snapshot().value().closed && *value == 42 && connection.snapshot().value().payloads == 1);
    gate.resolve();
    until([&] { return connection.snapshot().value().closed; });
    CHECK(connection.snapshot().value().payloads == 0);
    scene.close();
}
/** @brief 业务抛错、失败任务和直接关闭等待环均计账，数据最终排空而不挂起。 */
void typedErrors()
{
    Scene scene;
    scene.connectValues(&Source::scalar, [&](int number) -> Task<void> {
        if (number == 1)
            throw std::runtime_error("typed callback");
        if (number == 2)
            return failedTask(makeError(ErrorCode::CallbackFailed, "typed task"));
        return scene.connection->close();
    });
    Q_EMIT scene.source->scalar(1);
    Q_EMIT scene.source->scalar(2);
    Q_EMIT scene.source->scalar(3);
    until([&] { return scene.connection->snapshot().value().closed; });
    auto state = scene.connection->snapshot().value();
    CHECK(state.callbackErrors == 3 && state.payloads == 0 && state.lastError->code == ErrorCode::CleanupCycle);
    scene.close();
}
/** @brief 多个发射线程的独立数值全部到达 owner，无重复、丢失或 Qt 元类型注册。 */
void typedThreads()
{
    Scene scene;
    std::set<int> observed;
    scene.connectValues(&Source::scalar, [&](int number) {
        CHECK(scene.scheduler->isCurrentThread() && observed.insert(number).second);
        return completedTask();
    }, 512);
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
        producers.emplace_back([&, producer] {
            for (int number = 0; number < 64; ++number)
                Q_EMIT scene.source->scalar(producer * 64 + number);
        });
    for (auto& producer : producers)
        producer.join();
    until([&] { return observed.size() == 256 && scene.connection->snapshot().value().payloads == 0; });
    CHECK(scene.connection->snapshot().value().rejected == 0);
    scene.close();
}
/** @brief 类型化注册沿用 owner/预算/回调门禁，以及 connectNotify 重入停止的关闭屏障。 */
void typedGuards()
{
    Scene scene;
    const auto callback = [](int) { return completedTask(); };
    CHECK(!QtConnection::observeValues(*scene.context, scene.scheduler, static_cast<Source*>(nullptr),
                                       &Source::scalar, callback).hasValue());
    CHECK(!QtConnection::observeValues(*scene.context, scene.scheduler, scene.source.get(),
                                       &Source::scalar, std::function<Task<void>(const int&)>{}).hasValue());
    for (auto bytes : {std::size_t{0}, std::size_t{16 * 1024 * 1024 + 1}})
        CHECK(!QtConnection::observeValues(*scene.context, scene.scheduler, scene.source.get(),
                                           &Source::scalar, callback, "invalid", 256, bytes).hasValue());
    bool wrong = false;
    std::thread worker([&] {
        auto result = QtConnection::observeValues(*scene.context, scene.scheduler, scene.source.get(),
                                                  &Source::scalar, callback);
        wrong = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    std::optional<Task<CleanupReport>> closed;
    scene.source->connecting = [&] { closed.emplace(scene.kernel->close()); };
    CHECK(!QtConnection::observeValues(*scene.context, scene.scheduler, scene.source.get(),
                                       &Source::scalar, callback).hasValue());
    CHECK(closed.has_value());
    wait(*closed);
    scene.close();
}
/** @brief 无参数、const 信号及浮点/枚举白名单都可经过同一类型化入口。 */
void typedQualified()
{
    Scene scene;
    scene.connectValues(&Source::qualified, [&](bool flag, double number, Qt::ConnectionType kind) {
        CHECK(flag && number == 1.5 && kind == Qt::DirectConnection);
        ++scene.calls;
        return completedTask();
    });
    Q_EMIT scene.source->qualified(true, 1.5, Qt::DirectConnection);
    until([&] { return scene.calls == 1; });
    wait(scene.connection->close());
    until([&] { return scene.context->identity().hasValue(); });
    scene.connectValues(&Source::ping, [&] { ++scene.calls; return completedTask(); });
    Q_EMIT scene.source->ping();
    until([&] { return scene.calls == 2; });
    scene.close();
}
/** @brief 关闭仍等待原生调用快照；撤销的数据在 owner 排空，不执行已取消回调。 */
void typedSnapshot()
{
    Scene scene;
    scene.connectValues(&Source::scalar, [&](int) { ++scene.calls; return completedTask(); });
    auto gate = std::make_shared<Gate>();
    auto raw = QObject::connect(scene.source.get(), &Source::scalar, scene.source.get(),
                                [gate] { gate->block(); }, Qt::DirectConnection);
    std::thread producer([&] { Q_EMIT scene.source->scalar(42); });
    struct Join {
        std::shared_ptr<Gate> gate;
        std::thread& producer;
        /** @brief 即使断言失败，也不让测试留下等待中的发射线程。 */
        ~Join() { gate->release(); if (producer.joinable()) producer.join(); }
    } join{gate, producer};
    gate->waitEntered();
    auto closed = scene.connection->close();
    CHECK(!closed.isReady() && scene.connection->snapshot().value().payloads == 0);
    gate->release();
    producer.join();
    QObject::disconnect(raw);
    wait(closed);
    CHECK(scene.calls == 0 && scene.connection->snapshot().value().nativeRetired);
    scene.close();
}
/** @brief owner 关闭与持续跨线程深拷贝竞争，所有已接受数据最终释放。 */
void typedConcurrentClose()
{
    Scene scene;
    std::atomic_int emitted{0};
    scene.connectValues(&Source::values, [&](const QString&, const QByteArray& bytes, const std::string&, int) {
        CHECK(bytes.size() == 64 * 1024);
        return completedTask();
    });
    std::thread producer([&] {
        const QByteArray bytes(64 * 1024, 'x');
        for (int i = 0; i < 256; ++i) {
            Q_EMIT scene.source->values(QStringLiteral("race"), bytes, std::string(), i);
            ++emitted;
        }
    });
    struct Join {
        std::thread& producer;
        /** @brief 持有发送者直到发射线程真实返回，不把资源关闭等同于线程 join。 */
        ~Join() { if (producer.joinable()) producer.join(); }
    } join{producer};
    until([&] { return emitted.load() > 0; });
    auto closed = scene.connection->close();
    producer.join();
    wait(closed);
    auto state = scene.connection->snapshot().value();
    CHECK(state.payloads == 0 && state.nativeRetired && !state.deliveryBlocked);
    scene.close();
}
/** @brief 应用过早退出时，拒绝资源证明并保留仍被异步业务使用的数据。 */
void typedApplicationQuit()
{
    Scene scene;
    Promise<void> gate;
    const int* value = nullptr;
    scene.connectValues(&Source::scalar, [&](const int& number) { value = &number; return gate.task(); });
    Q_EMIT scene.source->scalar(42);
    until([&] { return value != nullptr; });
    QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    QCoreApplication::exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto closed = scene.connection->close();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue());
    CHECK(closed.tryResult()->error().code == ErrorCode::CleanupFailed);
    CHECK(!scene.connection->snapshot().value().closed && scene.connection->snapshot().value().payloads == 1 && *value == 42);
    gate.resolve();
}
/** @brief 只复制所选前缀，借用字符串失效后仍可读取，不复制自定义尾参数。 */
void prefixFreeze()
{
    Scene scene;
    auto observed = QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
        &Source::withTail, [&](const QString& text) {
            CHECK(scene.scheduler->isCurrentThread() && text == QStringLiteral("AB"));
            ++scene.calls;
            return completedTask();
        });
    CHECK(observed.hasValue());
    scene.connection.emplace(observed.value());
    wait(scene.connection->ready());
    std::thread worker([&] {
        QChar storage[] = {QChar('A'), QChar('B')};
        Payload ignored;
        Q_EMIT scene.source->withTail(QString::fromRawData(storage, 2), ignored);
        storage[0] = QChar('X');
    });
    worker.join();
    CHECK(scene.calls == 0 && Payload::copies.load() == 0);
    until([&] { return scene.calls == 1; });
    scene.close();
}
/** @brief 多参数前缀仅按选中数据计字节预算，异步 Task 期间保留全部所选引用。 */
void prefixAsync()
{
    Scene scene;
    Promise<void> gate;
    const QString* text = nullptr;
    const QByteArray* bytes = nullptr;
    auto observed = QtConnection::observePrefixValues<QString, QByteArray>(*scene.context, scene.scheduler,
        scene.source.get(), &Source::values, [&](const QString& currentText, const QByteArray& currentBytes) {
            text = &currentText;
            bytes = &currentBytes;
            return gate.task();
        }, "test.prefix", 1, 6);
    CHECK(observed.hasValue());
    scene.connection.emplace(observed.value());
    wait(scene.connection->ready());
    Q_EMIT scene.source->values(QStringLiteral("ab"), QByteArray("cd"), std::string(2 * 1024 * 1024, 'x'), 42);
    until([&] { return text && bytes; });
    auto closed = scene.kernel->close();
    QCoreApplication::processEvents();
    CHECK(!closed.isReady() && *text == QStringLiteral("ab") && *bytes == QByteArray("cd"));
    CHECK(scene.connection->snapshot().value().payloadErrors == 0);
    gate.resolve();
    wait(closed);
    scene.close();
}
/** @brief 原生 QObject 的私有尾标记不进入数据桥，连续名称变化仍按原值排队。 */
void prefixObjectName()
{
    Scene scene;
    std::vector<QString> names;
    auto observed = QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
        &QObject::objectNameChanged, [&](const QString& name) {
            names.push_back(name);
            return completedTask();
        });
    CHECK(observed.hasValue());
    scene.connection.emplace(observed.value());
    wait(scene.connection->ready());
    scene.source->setObjectName(QStringLiteral("first"));
    scene.source->setObjectName(QStringLiteral("second"));
    CHECK(names.empty());
    until([&] { return names.size() == 2; });
    CHECK(names == std::vector<QString>({QStringLiteral("first"), QStringLiteral("second")}));
    scene.close();
}
/** @brief 测试通过真实外部写入触发 Qt 文件/目录监听，不直接调用其私有信号。 */
void writeWatchedFile(const QString& path, const QByteArray& text, bool initial = false)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(text) == text.size());
    // 显式区分初始修改时间，避免依赖 Windows watcher 的毫秒级时间变化。
    if (initial)
        CHECK(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60), QFileDevice::FileModificationTime));
    file.close();
}
/** @brief 原生 watcher、异步参数及 Kernel 联合关闭；资源完整销毁后才报告静止。 */
void prefixNativeWatcher(bool directory)
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const auto file = temporary.filePath("watched.txt");
    writeWatchedFile(file, "initial", true);
    Scene scene;
    auto native = std::make_unique<QFileSystemWatcher>();
    const auto expected = directory ? temporary.path() : file;
    CHECK(native->addPath(expected));
    QPointer<QFileSystemWatcher> weak = native.get();
    auto sender = native.get();
    std::unique_ptr<QObject> object(std::move(native));
    auto resource = std::make_shared<QObjectResource>(object, scene.scheduler);
    CHECK(!object);
    scene.context->onCleanup([resource](CleanupContext) { return resource->close(); }, "test.watcher").value();
    Promise<void> gate;
    const QString* borrowed = nullptr;
    auto signal = directory ? &QFileSystemWatcher::directoryChanged : &QFileSystemWatcher::fileChanged;
    auto observed = QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, sender, signal,
        [&](const QString& path) {
            CHECK(scene.scheduler->isCurrentThread() && path == expected);
            borrowed = &path;
            return gate.task();
        });
    CHECK(observed.hasValue());
    scene.connection.emplace(observed.value());
    wait(scene.connection->ready());
    writeWatchedFile(directory ? temporary.filePath("created.txt") : file, "changed native notification");
    until([&] { return borrowed != nullptr; });
    auto closed = scene.kernel->close();
    QCoreApplication::processEvents();
    CHECK(!closed.isReady() && *borrowed == expected);
    gate.resolve();
    wait(closed);
    CHECK(closed.tryResult()->value().quiescent && weak.isNull() && resource->snapshot().value().confirmed);
    CHECK(scene.connection->snapshot().value().payloads == 0);
    scene.close();
}
/** @brief 前缀入口共享原有注册门禁，不绕过线程、容量或 connectNotify 重入停止。 */
void prefixGuards()
{
    Scene scene;
    auto callback = [](const QString&) { return completedTask(); };
    CHECK(!QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, static_cast<Source*>(nullptr),
        &QObject::objectNameChanged, callback).hasValue());
    CHECK(!QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
        &QObject::objectNameChanged, std::function<Task<void>(const QString&)>{}).hasValue());
    CHECK(!QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
        &QObject::objectNameChanged, callback, "invalid", 0).hasValue());
    CHECK(!QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
        &QObject::objectNameChanged, callback, "invalid", 1, 0).hasValue());
    bool wrong = false;
    std::thread worker([&] {
        auto value = QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
            &QObject::objectNameChanged, callback);
        wrong = !value.hasValue() && value.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    std::optional<Task<CleanupReport>> closed;
    scene.source->connecting = [&] { closed.emplace(scene.kernel->close()); };
    auto value = QtConnection::observePrefixValues<int>(*scene.context, scene.scheduler, scene.source.get(),
        &Source::scalar, [](int) { return completedTask(); });
    CHECK(!value.hasValue() && closed);
    wait(*closed);
    scene.close();
}
/** @brief 原生私有尾标记信号的旧代通知同样撤销，新代从自身前缀连接接收数据。 */
void prefixEpochs()
{
    Scene scene;
    std::vector<QString> names;
    auto connect = [&] {
        auto value = QtConnection::observePrefixValues<QString>(*scene.context, scene.scheduler, scene.source.get(),
            &QObject::objectNameChanged, [&](const QString& name) { names.push_back(name); return completedTask(); });
        CHECK(value.hasValue());
        scene.connection.emplace(value.value());
        wait(scene.connection->ready());
    };
    connect();
    auto old = *scene.connection;
    scene.source->setObjectName(QStringLiteral("old"));
    auto operation = scene.instance->update("new", scene.instance->snapshot().value().revision).value();
    wait(scene.instance->awaitTarget(operation));
    CHECK(names.empty() && old.snapshot().value().closed && old.snapshot().value().payloads == 0);
    connect();
    scene.source->setObjectName(QStringLiteral("new"));
    until([&] { return names.size() == 1; });
    CHECK(names.front() == QStringLiteral("new"));
    scene.close();
}
/** @brief const 信号的多项前缀保持精确类型顺序，未选枚举不计数据预算。 */
void prefixQualified()
{
    Scene scene;
    auto value = QtConnection::observePrefixValues<bool, double>(*scene.context, scene.scheduler, scene.source.get(),
        &Source::qualified, [&](bool flag, double number) {
            CHECK(flag && number == 1.5);
            ++scene.calls;
            return completedTask();
        }, "test.prefix", 1, sizeof(bool) + sizeof(double));
    CHECK(value.hasValue());
    scene.connection.emplace(value.value());
    wait(scene.connection->ready());
    Q_EMIT scene.source->qualified(true, 1.5, Qt::DirectConnection);
    until([&] { return scene.calls == 1; });
    scene.close();
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
        else if (name == "typed_freeze")
            typedFreeze();
        else if (name == "typed_async")
            typedAsync();
        else if (name == "typed_cancelled")
            typedCancelled();
        else if (name == "typed_epochs")
            typedEpochs();
        else if (name == "typed_capacity")
            typedCapacity();
        else if (name == "typed_bytes")
            typedBytes();
        else if (name == "typed_null")
            typedNull();
        else if (name == "typed_sender")
            typedSender();
        else if (name == "typed_errors")
            typedErrors();
        else if (name == "typed_threads")
            typedThreads();
        else if (name == "typed_guards")
            typedGuards();
        else if (name == "typed_qualified")
            typedQualified();
        else if (name == "typed_snapshot")
            typedSnapshot();
        else if (name == "typed_concurrent_close")
            typedConcurrentClose();
        else if (name == "typed_application_quit")
            typedApplicationQuit();
        else if (name == "prefix_freeze")
            prefixFreeze();
        else if (name == "prefix_async")
            prefixAsync();
        else if (name == "prefix_object_name")
            prefixObjectName();
        else if (name == "prefix_file")
            prefixNativeWatcher(false);
        else if (name == "prefix_directory")
            prefixNativeWatcher(true);
        else if (name == "prefix_guards")
            prefixGuards();
        else if (name == "prefix_epochs")
            prefixEpochs();
        else if (name == "prefix_qualified")
            prefixQualified();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
#include "connection.moc"
