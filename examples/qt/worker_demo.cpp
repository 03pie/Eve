/** @file worker_demo.cpp
 * @brief 真实工作线程、Qt 信号与 Kernel 联合退出，原生线程退出后才允许应用结束。
 */
#include <QCoreApplication>
#include <QTimer>
#include <atomic>
#include <cordis/kernel.hpp>
#include <cordis/qt/connection.hpp>
#include <cordis/qt/worker.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
namespace {
using namespace cordis;
using namespace cordis::qt;
/** @brief 示例宿主强持资源，插件工作只捕获线程安全的宿主状态。 */
struct Demo : std::enable_shared_from_this<Demo> {
    std::shared_ptr<QtScheduler> scheduler = std::make_shared<QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<std::atomic_bool> entered = std::make_shared<std::atomic_bool>(false);
    QTimer pulse;
    std::optional<QtConnection> signal;
    std::optional<QtWorker> worker;
    /** @brief 故障明确以非零状态退出，不报告清理成功。 */
    void fail(const char* message)
    {
        std::cerr << message << '\n';
        QCoreApplication::exit(EXIT_FAILURE);
    }
    /** @brief 原生信号对应的业务回调不等待自身所属 Kernel 关闭，避免形成等待环。 */
    void stop()
    {
        auto self = shared_from_this();
        kernel->close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
            auto worker = self->worker->snapshot().value();
            auto workResult = self->worker->result().tryResult();
            if (!result.hasValue() || !result.value().quiescent || !worker.joined || !worker.nativeHandleClosed ||
                !worker.capturesReleased || !worker.hostDestroyed || !workResult || workResult->hasValue() ||
                workResult->error().code != ErrorCode::OperationCancelled ||
                !self->signal->snapshot().value().nativeDestroyed) {
                self->fail("worker or signal did not drain");
                return;
            }
            if (!self->scheduler->close().hasValue()) {
                self->fail("scheduler did not close");
                return;
            }
            std::cout << "worker cancelled and joined; captures, signal and kernel closed\n";
            QCoreApplication::exit(EXIT_SUCCESS);
        });
    }
    /** @brief 根据真实进入标记触发 Qt 信号，不靠固定延迟猜测线程已经运行。 */
    void waitForWorker()
    {
        if (entered->load()) {
            pulse.setSingleShot(true);
            pulse.start(0);
            return;
        }
        auto self = shared_from_this();
        QTimer::singleShot(1, QCoreApplication::instance(), [self] { self->waitForWorker(); });
    }
    /** @brief 在插件激活期间登记两个 effect，停止时等待两者自己的资源证明。 */
    void start()
    {
        auto weak = weak_from_this();
        auto runtime =
            kernel
                ->registerPlugin("qt.worker.demo",
                                 [weak](Context context) {
                                     auto self = weak.lock();
                                     if (!self)
                                         return failedTask(makeError(ErrorCode::ContextClosing, "demo expired"));
                                     auto work = QtWorker::start(context, self->scheduler,
                                                                 [entered = self->entered](WorkerCancellation stop) {
                                                                     entered->store(true);
                                                                     while (!stop.isCancellationRequested())
                                                                         std::this_thread::yield();
                                                                     return Result<void>::success();
                                                                 });
                                     if (!work.hasValue())
                                         return failedTask(work.error());
                                     self->worker.emplace(work.value());
                                     auto connection = QtConnection::observe(context, self->scheduler, &self->pulse,
                                                                             &QTimer::timeout, [weak] {
                                                                                 if (auto current = weak.lock())
                                                                                     current->stop();
                                                                                 return completedTask();
                                                                             });
                                     if (!connection.hasValue())
                                         return failedTask(connection.error());
                                     self->signal.emplace(connection.value());
                                     return self->signal->ready();
                                 })
                .value();
        auto instance = kernel->mount(runtime).value();
        auto self = shared_from_this();
        instance.ready().thenOn(scheduler, [self](const Result<void>& result) {
            if (!result.hasValue()) {
                self->fail("worker plugin did not activate");
                return;
            }
            self->waitForWorker();
        });
    }
};
} // namespace
/** @brief headless 示例使用真实完成条件退出，定时器只提供故障截止时间。 */
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        auto demo = std::make_shared<Demo>();
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app, [demo] { demo->fail("worker demo deadline exceeded"); });
        deadline.start(5000);
        demo->start();
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
