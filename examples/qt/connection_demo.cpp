/** @file connection_demo.cpp
 * @brief 用托管 Qt 信号触发异步业务，再联合等待 Kernel、连接和时钟资源关闭。
 */
#include <QCoreApplication>
#include <QTimer>
#include <chrono>
#include <cordis/kernel.hpp>
#include <cordis/qt/clock.hpp>
#include <cordis/qt/connection.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
namespace {
using namespace cordis;
using namespace cordis::qt;
/** @brief 宿主明确拥有资源，插件只捕获弱宿主，避免把模块逻辑放进原生信号连接。 */
struct Demo : std::enable_shared_from_this<Demo> {
    std::shared_ptr<QtScheduler> scheduler = std::make_shared<QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    QtClock clock{scheduler};
    QTimer pulse;
    std::optional<QtConnection> connection;
    int calls = 0;
    /** @brief 任何失败都返回非零，不跳过未排空资源。 */
    void fail(const char* message)
    {
        std::cerr << message << '\n';
        QCoreApplication::exit(EXIT_FAILURE);
    }
    /** @brief 回调返回的任务覆盖真实时钟工作，关闭不能只等信号函数返回。 */
    Task<void> triggered()
    {
        ++calls;
        Promise<void> business;
        auto timer =
            clock.after(std::chrono::milliseconds(1),
                        {[business] { business.resolve(); },
                         [business] { business.reject(makeError(ErrorCode::OperationCancelled, "timer discarded")); }});
        if (!timer.hasValue())
            return failedTask(timer.error());
        auto stopped = kernel->close();
        if (stopped.isReady())
            return failedTask(makeError(ErrorCode::CleanupFailed, "kernel closed before callback drained"));
        auto self = shared_from_this();
        stopped.thenOn(scheduler, [self](const Result<CleanupReport>& report) {
            if (!report.hasValue() || !report.value().quiescent || self->calls != 1 ||
                !self->connection->snapshot().value().nativeDestroyed) {
                self->fail("managed connection did not drain");
                return;
            }
            self->clock.close().thenOn(self->scheduler, [self](const Result<void>& clockClosed) {
                if (!clockClosed.hasValue() || self->clock.snapshot().value().running != 0) {
                    self->fail("clock did not drain");
                    return;
                }
                if (!self->scheduler->close().hasValue()) {
                    self->fail("scheduler close failed");
                    return;
                }
                std::cout << "managed Qt callback drained; kernel and clock closed\n";
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
        return business.task();
    }
    /** @brief 安装时已登记本代 effect，信号先进入宿主桥再由 owner 调用业务。 */
    void start()
    {
        auto weak = weak_from_this();
        auto runtime =
            kernel
                ->registerPlugin(
                    "qt.signal.demo",
                    [weak](Context context) {
                        auto self = weak.lock();
                        if (!self)
                            return failedTask(makeError(ErrorCode::ContextClosing, "demo expired"));
                        auto resource =
                            QtConnection::observe(context, self->scheduler, &self->pulse, &QTimer::timeout, [weak] {
                                auto current = weak.lock();
                                return current ? current->triggered()
                                               : failedTask(makeError(ErrorCode::ContextClosing, "demo expired"));
                            });
                        if (!resource.hasValue())
                            return failedTask(resource.error());
                        self->connection.emplace(resource.value());
                        return self->connection->ready();
                    })
                .value();
        auto instance = kernel->mount(runtime).value();
        auto self = shared_from_this();
        instance.ready().thenOn(scheduler, [self](const Result<void>& result) {
            if (!result.hasValue()) {
                self->fail("plugin failed to activate");
                return;
            }
            self->pulse.setSingleShot(true);
            self->pulse.start(0);
        });
    }
};
} // namespace
/** @brief headless 示例只依赖 QtCore，使用实际完成条件退出。 */
int main(int argc, char** argv)
{
    try {
        QCoreApplication app(argc, argv);
        auto demo = std::make_shared<Demo>();
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app,
                         [demo] { demo->fail("connection demo deadline exceeded"); });
        deadline.start(5000);
        demo->start();
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
