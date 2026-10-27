/** @file worker.cpp
 * @brief 用真实原生线程、TLS、Kernel/epoch 和 Qt 循环验证工作资源的完整退出。
 */
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cordis/kernel.hpp>
#include <cordis/qt/clock.hpp>
#include <cordis/qt/connection.hpp>
#include <cordis/qt/worker.hpp>
#include <cordis/qt/value_worker.hpp>
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
using namespace cordis;
using namespace cordis::qt;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
/** @brief 完成条件来自实际状态，Qt 定时器仅负责轮询和超时诊断。 */
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
    poll.start(1);
    deadline.start(5000);
    loop.exec();
    CHECK(done());
}
/** @brief 等待任务真实终结，失败任务由各场景自行检查语义。 */
template <class T> void settled(const Task<T>& task)
{
    until([&] { return task.isReady(); });
}
/** @brief 对预期成功的源任务核对结果，不把仅 ready 当作成功。 */
template <class T> void wait(const Task<T>& task)
{
    settled(task);
    CHECK(task.tryResult()->hasValue());
}
/** @brief 测试门允许 worker 或 TLS 析构阻塞，owner 继续运行 Qt。 */
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    /** @brief 线程只等待显式放行，不依靠固定延迟推测完成。 */
    void block()
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    /** @brief owner 在不泵送事件的短窗口内等待真实进入。 */
    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
    }
    /** @brief 即使断言抛错也能安全放行测试工作。 */
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }
};
/** @brief 避免失败路径留下等待栈上对象的后台工作。 */
struct Release {
    std::shared_ptr<Gate> gate;
    /** @brief 测试退出时只放行共享门，不伪装资源已 join。 */
    ~Release() { gate->release(); }
};
/** @brief 捕获只能在 owner 最终释放，允许析构重入清理。 */
struct Probe {
    std::thread::id owner = std::this_thread::get_id();
    std::function<void()> destroyed;
    /** @brief 错误线程析构立即失败，正常析构执行注入观察。 */
    ~Probe()
    {
        if (std::this_thread::get_id() != owner)
            std::abort();
        if (destroyed)
            destroyed();
    }
};
/** @brief 工作函数返回后仍可停在原生 TLS 析构，检测伪造的线程完成。 */
struct Tail {
    std::shared_ptr<Gate> gate;
    /** @brief 真实线程局部析构期间继续持有门。 */
    ~Tail()
    {
        if (gate)
            gate->block();
    }
};
/** @brief Kernel 激活交付当前代 Context，资源随后由真实插件代接管。 */
struct Scene {
    std::shared_ptr<QtScheduler> scheduler = std::make_shared<QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::optional<Context> context;
    std::optional<Instance> instance;
    std::optional<QtWorker> worker;
    /** @brief 首个实例已激活后才向测试交付场景。 */
    Scene()
    {
        auto runtime = kernel
                           ->registerPlugin("worker",
                                            [&](Context value) {
                                                context.emplace(value);
                                                return completedTask();
                                            })
                           .value();
        instance.emplace(kernel->mount(runtime).value());
        wait(instance->ready());
    }
    /** @brief 安装工作并核对 effect 初始化，不能把 ready 当作函数返回。 */
    void start(std::function<Result<void>(WorkerCancellation)> body)
    {
        auto value = QtWorker::start(*context, scheduler, std::move(body));
        CHECK(value.hasValue());
        worker.emplace(value.value());
        wait(worker->ready());
    }
    /** @brief 整体清理和资源原生证明都成功后才关闭 scheduler。 */
    void close()
    {
        auto task = kernel->close();
        wait(task);
        CHECK(task.tryResult()->value().quiescent);
        if (worker) {
            auto value = worker->snapshot().value();
            CHECK(value.closed && value.joined && value.nativeHandleClosed && value.capturesReleased &&
                  value.hostDestroyed);
        }
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
    }
};
/** @brief 仅核对本场景的 worker 记录，实例自身的激活 effect 继续保持存活。 */
bool workerRetired(const Instance& instance)
{
    const auto resources = instance.snapshot().value().resources;
    for (const auto& effect : resources.effects)
        if (effect.label == "qt.worker")
            return false;
    return true;
}
/** @brief 工作实际位于后台，结果只在 join/捕获/宿主删除后交付。 */
void natural()
{
    Scene scene;
    auto owner = std::this_thread::get_id();
    auto data = std::make_shared<std::atomic_int>(0);
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.start([probe, data, owner](WorkerCancellation) {
        CHECK(std::this_thread::get_id() != owner);
        data->store(42);
        return Result<void>::success();
    });
    probe.reset();
    bool delivered = false;
    scene.worker->result().thenOn(scene.scheduler, [&](const Result<void>& value) {
        CHECK(value.hasValue() && scene.scheduler->isCurrentThread());
        delivered = true;
    });
    until([&] { return delivered; });
    CHECK(data->load() == 42 && weak.expired());
    auto state = scene.worker->snapshot().value();
    CHECK(state.bodyEntered && state.bodyReturned && !state.lastError);
    until([&] { return workerRetired(*scene.instance); });
    scene.close();
}
/** @brief 所属代取消立即可查询，协作退出不依赖 effect 清理已经开始。 */
void cancellation()
{
    Scene scene;
    scene.start([](WorkerCancellation stop) {
        while (!stop.isCancellationRequested())
            std::this_thread::yield();
        return Result<void>::success();
    });
    until([&] { return scene.worker->snapshot().value().bodyEntered; });
    auto closed = scene.kernel->close();
    CHECK(!closed.isReady());
    wait(closed);
    auto result = scene.worker->result().tryResult();
    CHECK(result && !result->hasValue() && result->error().code == ErrorCode::OperationCancelled);
    scene.close();
}
/** @brief 本地取消独立于 Kernel 取消传播，worker 必须实际观察到该请求才会退出。 */
void localCancel()
{
    Scene scene;
    scene.start([](WorkerCancellation stop) {
        while (!stop.isCancellationRequested())
            std::this_thread::yield();
        return Result<void>::success();
    });
    until([&] { return scene.worker->snapshot().value().bodyEntered; });
    bool first = false, repeated = true;
    std::thread canceller([&] {
        first = scene.worker->requestCancel();
        repeated = scene.worker->requestCancel();
    });
    canceller.join();
    settled(scene.worker->result());
    CHECK(first && !repeated && !scene.worker->result().tryResult()->hasValue());
    scene.close();
}
/** @brief 不合作工作真实退出前 Kernel 不能宣称静止，owner 关闭调用本身不阻塞。 */
void pending()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    scene.start([gate](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    gate->waitEntered();
    auto closed = scene.kernel->close();
    until([&] { return scene.worker->snapshot().value().cancellationRequested; });
    CHECK(!closed.isReady() && !scene.worker->result().isReady());
    gate->release();
    wait(closed);
    CHECK(closed.tryResult()->value().quiescent);
    scene.close();
}
/** @brief 函数返回和 finished 类标记不能绕过 TLS 析构及原生线程退出。 */
void tls()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.start([gate, probe](WorkerCancellation) {
        thread_local Tail tail;
        tail.gate = gate;
        return Result<void>::success();
    });
    probe.reset();
    gate->waitEntered();
    auto value = scene.worker->snapshot().value();
    CHECK(value.bodyReturned && !value.joined && !scene.worker->result().isReady() && !weak.expired());
    auto closed = scene.worker->close();
    QCoreApplication::processEvents();
    CHECK(!closed.isReady());
    gate->release();
    wait(closed);
    CHECK(weak.expired());
    until([&] { return workerRetired(*scene.instance); });
    scene.close();
}
/** @brief 捕获析构重入 close/Qt 循环，只有析构栈完全返回才允许发布完成。 */
void captures()
{
    Scene scene;
    bool protectedStack = false;
    std::optional<Task<void>> closed;
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    probe->destroyed = [&] {
        auto state = scene.worker->snapshot().value();
        closed.emplace(scene.worker->close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        protectedStack = state.joined && state.releasingCaptures && !state.capturesReleased && !closed->isReady() &&
                         !scene.worker->result().isReady();
    };
    scene.start([probe, gate](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    probe.reset();
    gate->release();
    until([&] { return closed.has_value(); });
    wait(*closed);
    CHECK(protectedStack && weak.expired());
    scene.close();
}
/** @brief 更新等待旧 worker 的原生退出，旧代结果不能发布成功，新代独立运行。 */
void epochs()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    scene.start([gate](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    gate->waitEntered();
    auto old = *scene.context;
    auto previous = *scene.worker;
    auto operation = scene.instance->update("new", scene.instance->snapshot().value().revision).value();
    auto target = scene.instance->awaitTarget(operation);
    CHECK(!target.isReady());
    gate->release();
    wait(target);
    CHECK(previous.result().isReady() && !previous.result().tryResult()->hasValue());
    CHECK(
        !QtWorker::start(old, scene.scheduler, [](WorkerCancellation) { return Result<void>::success(); }).hasValue());
    scene.start([](WorkerCancellation) { return Result<void>::success(); });
    wait(scene.worker->result());
    scene.close();
}
/** @brief 业务失败和抛错保留结果诊断，已经退出的资源仍可成功关闭。 */
void failure(bool throwing)
{
    Scene scene;
    scene.start([throwing](WorkerCancellation) -> Result<void> {
        if (throwing)
            throw std::runtime_error("worker failure");
        return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "bad input"));
    });
    settled(scene.worker->result());
    auto result = scene.worker->result().tryResult();
    CHECK(!result->hasValue() &&
          result->error().code == (throwing ? ErrorCode::CallbackFailed : ErrorCode::ConfigInvalid));
    CHECK(scene.worker->snapshot().value().lastError);
    scene.close();
}
/** @brief 最后一个 facade 可以在非 owner 释放，所属 effect 仍负责最终退出。 */
void dropped()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.start([gate, probe](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    probe.reset();
    gate->waitEntered();
    auto result = scene.worker->result();
    auto facade = std::move(scene.worker);
    scene.worker.reset();
    std::thread releaser([facade = std::move(facade)]() mutable { facade.reset(); });
    releaser.join();
    gate->release();
    wait(result);
    CHECK(weak.expired());
    until([&] { return workerRetired(*scene.instance); });
    scene.close();
}
/** @brief scheduler 提前关闭时私有 Qt 生命周期仍可 join，但不能冒称 Core 整体已静止。 */
void schedulerClosed()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    scene.start([gate](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    gate->waitEntered();
    scene.scheduler->close().value();
    auto closed = scene.worker->close();
    CHECK(!closed.isReady());
    gate->release();
    wait(closed);
    CHECK(scene.worker->snapshot().value().joined && !scene.worker->result().tryResult()->hasValue());
    auto core = scene.kernel->close();
    CHECK(!core.isReady() || !core.tryResult()->hasValue() || !core.tryResult()->value().quiescent);
}
/** @brief 错误 scheduler、空函数及错误线程拒绝，跨线程本地取消只改变协作状态。 */
void guards()
{
    Scene scene;
    auto other = std::make_shared<QtScheduler>();
    auto body = [](WorkerCancellation) { return Result<void>::success(); };
    CHECK(!QtWorker::start(*scene.context, other, body).hasValue());
    CHECK(!QtWorker::start(*scene.context, scene.scheduler, {}).hasValue());
    bool wrong = false;
    std::thread creator([&] {
        auto result = QtWorker::start(*scene.context, scene.scheduler, body);
        wrong = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    creator.join();
    CHECK(wrong);
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    scene.start([gate](WorkerCancellation) {
        gate->block();
        return Result<void>::success();
    });
    gate->waitEntered();
    bool refused = false;
    std::thread closer([&] {
        auto closed = scene.worker->close();
        auto snapshot = scene.worker->snapshot();
        refused = closed.isReady() && !closed.tryResult()->hasValue() && !snapshot.hasValue();
    });
    closer.join();
    CHECK(refused && !scene.worker->snapshot().value().closing);
    std::thread canceller([&] { scene.worker->requestCancel(); });
    canceller.join();
    CHECK(scene.worker->snapshot().value().cancellationRequested);
    gate->release();
    settled(scene.worker->result());
    other->close().value();
    scene.close();
}
/** @brief 激活可等待工作结果，激活中停止仍先取消并 join，不能遗漏 setup 持有的 worker。 */
void activation()
{
    auto scheduler = std::make_shared<QtScheduler>();
    Kernel kernel(scheduler);
    std::optional<QtWorker> worker;
    auto runtime = kernel
                       .registerPlugin("activation",
                                       [&](Context context) {
                                           auto value =
                                               QtWorker::start(context, scheduler, [](WorkerCancellation stop) {
                                                   while (!stop.isCancellationRequested())
                                                       std::this_thread::yield();
                                                   return Result<void>::success();
                                               });
                                           CHECK(value.hasValue());
                                           worker.emplace(value.value());
                                           return worker->result();
                                       })
                       .value();
    auto instance = kernel.mount(runtime).value();
    until([&] { return worker && worker->snapshot().value().bodyEntered; });
    CHECK(!instance.ready().isReady());
    auto closed = kernel.close();
    wait(closed);
    CHECK(closed.tryResult()->value().quiescent && worker->snapshot().value().joined);
    scheduler->close().value();
}
/** @brief 激活栈内嵌套循环可先完成原生工作，自动退役必须等 setup 回执而不能取消 ready。 */
void setupReentry()
{
    auto scheduler = std::make_shared<QtScheduler>();
    Kernel kernel(scheduler);
    std::optional<QtWorker> worker;
    bool pendingInside = false;
    auto runtime = kernel
                       .registerPlugin("setup-reentry",
                                       [&](Context context) {
                                           auto value = QtWorker::start(context, scheduler, [](WorkerCancellation) {
                                               return Result<void>::success();
                                           });
                                           CHECK(value.hasValue());
                                           worker.emplace(value.value());
                                           until([&] { return worker->snapshot().value().closed; });
                                           pendingInside = !worker->ready().isReady();
                                           return worker->ready();
                                       })
                       .value();
    auto instance = kernel.mount(runtime).value();
    wait(instance.ready());
    wait(worker->result());
    CHECK(pendingInside && worker->result().tryResult()->hasValue());
    until([&] { return workerRetired(instance); });
    auto closed = kernel.close();
    wait(closed);
    CHECK(closed.tryResult()->value().quiescent);
    scheduler->close().value();
}
/** @brief 快速工作在 owner 当前任务内完成后，ready/result 延续仍可使用所属 Context。 */
void completionContext(bool ready)
{
    Scene scene;
    bool delivered = false, allowed = false;
    auto context = *scene.context;
    CHECK(scene.scheduler->post(
        {[&, context] {
             auto value =
                 QtWorker::start(context, scene.scheduler, [](WorkerCancellation) { return Result<void>::success(); });
             CHECK(value.hasValue());
             scene.worker.emplace(value.value());
             until([&] { return scene.worker->snapshot().value().closed; });
             auto observe = [&, context](const Result<void>& result) {
                 allowed = result.hasValue() && context.identity().hasValue();
                 delivered = true;
             };
             if (ready)
                 scene.worker->ready().thenOn(scene.scheduler, observe);
             else {
                 CHECK(scene.scheduler->post(
                     {[&, observe] { scene.worker->result().thenOn(scene.scheduler, observe); }, {}}));
             }
         },
         {}}));
    until([&] { return delivered; });
    CHECK(allowed);
    scene.close();
}
/** @brief 原生完成时其他 effect 仍在清理，但发布时门禁已经恢复，不保留过早的暂态拒绝。 */
void cleanupOverlap()
{
    Scene scene;
    auto nativeGate = std::make_shared<Gate>();
    Release release{nativeGate};
    scene.start([nativeGate](WorkerCancellation) {
        nativeGate->block();
        return Result<void>::success();
    });
    nativeGate->waitEntered();
    Promise<void> cleanup;
    auto other = scene.context
                     ->effect(
                         [cleanup](EffectSetupContext setup) {
                             auto added = setup.addCleanup([cleanup] { return cleanup.task(); });
                             return added.hasValue() ? completedTask() : failedTask(added.error());
                         },
                         "other-cleanup")
                     .value();
    wait(other.ready());
    auto otherClosed = other.close();
    until([&] { return other.snapshot().value().cleanupPending; });
    bool returned = false;
    CHECK(scene.scheduler->post({[&] {
                                     nativeGate->release();
                                     until([&] { return scene.worker->snapshot().value().closed; });
                                     cleanup.resolve();
                                     returned = true;
                                 },
                                 {}}));
    until([&] { return returned; });
    wait(scene.worker->result());
    wait(otherClosed);
    scene.close();
}
/** @brief 资源已经退出、Core 清理尚未确认时关闭 scheduler，结果须失败终结而非永久 pending。 */
void retirementDelivery()
{
    Scene scene;
    bool stoppedScheduler = false;
    auto context = *scene.context;
    CHECK(scene.scheduler->post({[&, context] {
                                     auto value = QtWorker::start(context, scene.scheduler, [](WorkerCancellation) {
                                         return Result<void>::success();
                                     });
                                     CHECK(value.hasValue());
                                     scene.worker.emplace(value.value());
                                     until([&] { return scene.worker->snapshot().value().closed; });
                                     CHECK(scene.scheduler->post({[&] {
                                                                      scene.scheduler->close().value();
                                                                      stoppedScheduler = true;
                                                                  },
                                                                  {}}));
                                 },
                                 {}}));
    until([&] { return stoppedScheduler; });
    auto result = scene.worker->result().tryResult();
    auto state = scene.worker->snapshot().value();
    CHECK(result && !result->hasValue() && result->error().code == ErrorCode::SchedulerClosed);
    CHECK(state.closed && state.joined && state.retirementBlocked && !state.retired);
    wait(scene.worker->close());
    auto core = scene.kernel->close();
    CHECK(!core.isReady() || !core.tryResult()->hasValue() || !core.tryResult()->value().quiescent);
}
/** @brief worker、托管信号和 QtClock 合并清理，不能用任一单项完成代替其他资源。 */
void combined()
{
    Scene scene;
    QtClock clock(scene.scheduler);
    QTimer pulse;
    scene.start([](WorkerCancellation stop) {
        while (!stop.isCancellationRequested())
            std::this_thread::yield();
        return Result<void>::success();
    });
    until([&] { return scene.worker->snapshot().value().bodyEntered; });
    std::optional<Task<CleanupReport>> closed;
    auto connection = QtConnection::observe(*scene.context, scene.scheduler, &pulse, &QTimer::timeout, [&] {
                          closed.emplace(scene.kernel->close());
                          return completedTask();
                      }).value();
    wait(connection.ready());
    clock
        .after(std::chrono::milliseconds(0), {[&] {
                                                  pulse.setSingleShot(true);
                                                  pulse.start(0);
                                              },
                                              {}})
        .value();
    until([&] { return closed.has_value(); });
    wait(*closed);
    CHECK(closed->tryResult()->value().quiescent && connection.snapshot().value().nativeDestroyed &&
          scene.worker->snapshot().value().joined);
    wait(clock.close());
    scene.close();
}
/** @brief 应用错误提前退出时，适配器报告失败并保留捕获；测试另行等待原生线程以清理测试环境。 */
void applicationExit(std::unique_ptr<QCoreApplication>& app, bool quit)
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto id = std::make_shared<std::atomic<DWORD>>(0);
    auto probe = std::make_shared<Probe>();
    std::weak_ptr<Probe> weak = probe;
    scene.start([gate, id, probe](WorkerCancellation) {
        id->store(GetCurrentThreadId());
        gate->block();
        return Result<void>::success();
    });
    probe.reset();
    gate->waitEntered();
    HANDLE native = OpenThread(SYNCHRONIZE, FALSE, id->load());
    CHECK(native);
    if (quit) {
        QTimer::singleShot(0, app.get(), &QCoreApplication::quit);
        app->exec();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    } else
        app.reset();
    auto closed = scene.worker->close();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue() && scene.worker->snapshot().value().quarantined &&
          !weak.expired());
    gate->release();
    const auto joined = WaitForSingleObject(native, 5000);
    CloseHandle(native);
    CHECK(joined == WAIT_OBJECT_0);
    CHECK(!scene.worker->snapshot().value().joined && !weak.expired());
}
/** @brief 不可复制数据禁止在 owner 移动，验证发布只转移快照而不执行结果构造代码。 */
struct FrozenData {
    std::thread::id owner;
    std::unique_ptr<int> number;
    bool throwOnMove = false;
    /** @brief 值只在 worker 构造，owner 标识仅用于检测非法后续移动。 */
    FrozenData(std::thread::id ownerThread, int value) : owner(ownerThread), number(std::make_unique<int>(value)) {}
    /** @brief 任意 owner 移动或显式注入的冻结失败都抛错。 */
    FrozenData(FrozenData&& other) : owner(other.owner)
    {
        if (std::this_thread::get_id() == owner || other.throwOnMove)
            throw std::runtime_error("result moved outside freeze boundary");
        number = std::move(other.number);
    }
    /** @brief 不允许通过复制绕过不可变快照传递。 */
    FrozenData(const FrozenData&) = delete;
};
/** @brief 不可复制结果通过共享快照交付，延续能使用已完成退役后的当前 Context。 */
void typedValue()
{
    Scene scene;
    const auto owner = std::this_thread::get_id();
    auto value = QtValueWorker<FrozenData>::start(*scene.context, scene.scheduler, [owner](WorkerCancellation) {
        return Result<FrozenData>::success(FrozenData(owner, 42));
    }).value();
    bool delivered = false;
    auto continuation = value.result().thenOn(scene.scheduler, [&](const Result<FrozenData>& result) {
        CHECK(result.hasValue() && *result.value().number == 42);
        CHECK(scene.context->identity().hasValue());
        const auto state = value.snapshot().value();
        CHECK(state.joined && state.capturesReleased && state.hostDestroyed && state.retired);
        delivered = true;
    });
    wait(continuation);
    CHECK(delivered);
    scene.close();
    CHECK(*value.result().tryResult()->value().number == 42);
}
/** @brief 冻结结果时的移动异常转换为业务失败，不留下永不完成的数据任务。 */
void typedFreezeFailure()
{
    Scene scene;
    const auto owner = std::this_thread::get_id();
    auto value = QtValueWorker<FrozenData>::start(*scene.context, scene.scheduler, [owner](WorkerCancellation) {
        auto result = Result<FrozenData>::success(FrozenData(owner, 42));
        result.value().throwOnMove = true;
        return result;
    }).value();
    until([&] { return value.result().isReady(); });
    CHECK(!value.result().tryResult()->hasValue());
    CHECK(value.result().tryResult()->error().code == ErrorCode::CallbackFailed);
    wait(value.close());
    scene.close();
}
/** @brief 冻结的数据已经构造但 TLS 仍在析构时，不发布数据或成功关闭。 */
void typedTls()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto value = QtValueWorker<std::string>::start(*scene.context, scene.scheduler, [gate](WorkerCancellation) {
        thread_local Tail tail;
        tail.gate = gate;
        return Result<std::string>::success("after TLS");
    }).value();
    wait(value.ready());
    gate->waitEntered();
    const auto state = value.snapshot().value();
    CHECK(state.bodyReturned && !state.joined && !value.result().isReady());
    gate->release();
    wait(value.result());
    CHECK(value.result().tryResult()->value() == "after TLS");
    scene.close();
}
/** @brief 本地取消或旧代更新都覆盖后台返回的成功数据，且等待真实原生退出。 */
void typedCancel(bool update)
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto value = QtValueWorker<int>::start(*scene.context, scene.scheduler, [gate](WorkerCancellation) {
        gate->block();
        return Result<int>::success(42);
    }).value();
    wait(value.ready());
    gate->waitEntered();
    std::optional<Task<StableReport>> target;
    if (update) {
        auto operation = scene.instance->update("new", scene.instance->snapshot().value().revision).value();
        target.emplace(scene.instance->awaitTarget(operation));
        CHECK(!target->isReady());
    } else {
        CHECK(value.requestCancel());
        CHECK(!value.requestCancel());
    }
    CHECK(!value.result().isReady());
    gate->release();
    until([&] { return value.result().isReady(); });
    CHECK(!value.result().tryResult()->hasValue());
    if (target)
        wait(*target);
    else
        CHECK(value.result().tryResult()->error().code == ErrorCode::OperationCancelled);
    wait(value.close());
    scene.close();
}
/** @brief 类型化失败与抛错均完成业务任务，已排空资源仍可正常关闭。 */
void typedFailure(bool throws)
{
    Scene scene;
    auto value = QtValueWorker<int>::start(*scene.context, scene.scheduler, [throws](WorkerCancellation) -> Result<int> {
        if (throws)
            throw std::runtime_error("typed body");
        return Result<int>::failure(makeError(ErrorCode::InvalidArgument, "typed error"));
    }).value();
    until([&] { return value.result().isReady(); });
    const auto result = value.result().tryResult();
    CHECK(!result->hasValue());
    CHECK(result->error().code == (throws ? ErrorCode::CallbackFailed : ErrorCode::InvalidArgument));
    wait(value.close());
    scene.close();
}
/** @brief facade 释放不取消工作；单独保留的 Task 在 Kernel 关闭后仍拥有数据。 */
void typedDropped()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    using ValueWorker = QtValueWorker<std::unique_ptr<int>>;
    std::optional<ValueWorker> value(ValueWorker::start(*scene.context, scene.scheduler, [gate](WorkerCancellation) {
        gate->block();
        return Result<std::unique_ptr<int>>::success(std::make_unique<int>(73));
    }).value());
    auto result = value->result();
    wait(value->ready());
    gate->waitEntered();
    value.reset();
    gate->release();
    wait(result);
    scene.close();
    CHECK(*result.tryResult()->value() == 73);
}
/** @brief scheduler 提前关闭时类型化任务也必须失败终结，资源退出另行确认。 */
void typedDelivery()
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto value = QtValueWorker<int>::start(*scene.context, scene.scheduler, [gate](WorkerCancellation) {
        gate->block();
        return Result<int>::success(42);
    }).value();
    wait(value.ready());
    gate->waitEntered();
    scene.scheduler->close().value();
    gate->release();
    until([&] { return value.result().isReady(); });
    CHECK(!value.result().tryResult()->hasValue());
    wait(value.close());
    CHECK(value.snapshot().value().joined && value.snapshot().value().retirementBlocked);
}
/** @brief 协议失败可先拒绝数据任务；后台后续写入仍有保留所有权，不发生并发读取。 */
void typedApplicationExit(std::unique_ptr<QCoreApplication>& app)
{
    Scene scene;
    auto gate = std::make_shared<Gate>();
    Release release{gate};
    auto id = std::make_shared<std::atomic<DWORD>>(0);
    auto value = QtValueWorker<std::unique_ptr<int>>::start(*scene.context, scene.scheduler, [gate, id](WorkerCancellation) {
        id->store(GetCurrentThreadId());
        gate->block();
        return Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));
    }).value();
    wait(value.ready());
    gate->waitEntered();
    HANDLE native = OpenThread(SYNCHRONIZE, FALSE, id->load());
    CHECK(native);
    app.reset();
    CHECK(value.result().isReady() && !value.result().tryResult()->hasValue());
    CHECK(value.snapshot().value().quarantined && !value.snapshot().value().joined);
    gate->release();
    const auto joined = WaitForSingleObject(native, 5000);
    CloseHandle(native);
    CHECK(joined == WAIT_OBJECT_0 && !value.result().tryResult()->hasValue());
}
/** @brief 每项使用独立 Qt 应用，异常退出场景显式控制应用销毁时机。 */
int main(int argc, char** argv)
{
    try {
        auto app = std::make_unique<QCoreApplication>(argc, argv);
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "natural")
            natural();
        else if (name == "cancel")
            cancellation();
        else if (name == "local_cancel")
            localCancel();
        else if (name == "pending")
            pending();
        else if (name == "tls")
            tls();
        else if (name == "captures")
            captures();
        else if (name == "epochs")
            epochs();
        else if (name == "failure")
            failure(false);
        else if (name == "exceptions")
            failure(true);
        else if (name == "dropped")
            dropped();
        else if (name == "scheduler_closed")
            schedulerClosed();
        else if (name == "guards")
            guards();
        else if (name == "activation")
            activation();
        else if (name == "setup_reentry")
            setupReentry();
        else if (name == "ready_context")
            completionContext(true);
        else if (name == "result_context")
            completionContext(false);
        else if (name == "retirement_delivery")
            retirementDelivery();
        else if (name == "cleanup_overlap")
            cleanupOverlap();
        else if (name == "combined")
            combined();
        else if (name == "application_exit")
            applicationExit(app, false);
        else if (name == "application_quit")
            applicationExit(app, true);
        else if (name == "typed_value")
            typedValue();
        else if (name == "typed_tls")
            typedTls();
        else if (name == "typed_cancel")
            typedCancel(false);
        else if (name == "typed_epoch")
            typedCancel(true);
        else if (name == "typed_failure")
            typedFailure(false);
        else if (name == "typed_exception")
            typedFailure(true);
        else if (name == "typed_dropped")
            typedDropped();
        else if (name == "typed_delivery")
            typedDelivery();
        else if (name == "typed_application_exit")
            typedApplicationExit(app);
        else if (name == "typed_freeze_failure")
            typedFreezeFailure();
        else
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
