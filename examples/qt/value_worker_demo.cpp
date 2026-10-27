/** @file value_worker_demo.cpp
 * @brief 后台计算返回类型化数据，等待插件激活和完整 Kernel 清理后退出 Qt 循环。
 */
#include <QCoreApplication>
#include <QTimer>
#include <cordis/kernel.hpp>
#include <cordis/qt/value_worker.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

/** @brief headless 示例真实交付计算结果，超时仅作为故障保护。 */
int main(int argc, char** argv)
{
    using namespace cordis;
    using namespace cordis::qt;
    try {
        QCoreApplication app(argc, argv);
        auto scheduler = std::make_shared<QtScheduler>();
        auto kernel = std::make_shared<Kernel>(scheduler);
        std::optional<QtValueWorker<std::uint64_t>> worker;
        auto runtime = kernel->registerPlugin("qt.value.worker.demo", [&](Context context) {
            auto started = QtValueWorker<std::uint64_t>::start(context, scheduler, [](WorkerCancellation stop) {
                std::uint64_t sum = 0;
                for (std::uint64_t i = 1; i <= 100; ++i) {
                    if (stop.isCancellationRequested())
                        return Result<std::uint64_t>::failure(makeError(ErrorCode::OperationCancelled, "cancelled"));
                    sum += i;
                }
                return Result<std::uint64_t>::success(sum);
            });
            if (!started.hasValue())
                return failedTask(started.error());
            worker.emplace(started.value());
            return worker->result().thenOn(scheduler, [](const Result<std::uint64_t>& result) {
                if (!result.hasValue() || result.value() != 5050)
                    throw std::runtime_error("typed worker result was not delivered");
                std::cout << "sum(1..100) = " << result.value() << '\n';
            });
        }).value();
        auto instance = kernel->mount(runtime).value();
        instance.ready().thenOn(scheduler, [&](const Result<void>& ready) {
            if (!ready.hasValue()) {
                QCoreApplication::exit(EXIT_FAILURE);
                return;
            }
            kernel->close().thenOn(scheduler, [&](const Result<CleanupReport>& report) {
                const auto resource = worker->snapshot().value();
                if (!report.hasValue() || !report.value().quiescent || !resource.joined || !resource.retired ||
                    !resource.capturesReleased || !resource.hostDestroyed) {
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (!scheduler->close().hasValue()) {
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                std::cout << "typed worker and kernel closed\n";
                QCoreApplication::exit(EXIT_SUCCESS);
            });
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &app, [] { QCoreApplication::exit(EXIT_FAILURE); });
        deadline.start(5000);
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
