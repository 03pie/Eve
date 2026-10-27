/** @file native_watcher_demo.cpp
 * @brief 通过前缀值连接接入真实 Qt 文件监听，联合等待异步业务、watcher、时钟与 Kernel 退出。
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <chrono>
#include <cordis/kernel.hpp>
#include <cordis/qt/clock.hpp>
#include <cordis/qt/connection.hpp>
#include <cordis/qt/object.hpp>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

/** @brief 仅写专属临时文件，关闭后由真实文件系统事件驱动监听。 */
void writeTemporaryFile(const QString& path, const QByteArray& bytes, bool initial = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        throw std::runtime_error("temporary file write failed");
    // 初始时间显式错开，保证 Qt 5.15.2 Windows 元数据比较能观察到后续改写。
    if (initial && !file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60), QFileDevice::FileModificationTime))
        throw std::runtime_error("temporary file timestamp failed");
    file.close();
}

/** @brief 不依赖 Ela，以实际原生通知和资源确认结束；超时仅为故障保护。 */
int main(int argc, char** argv)
{
    using namespace cordis;
    using namespace cordis::qt;
    try {
        QCoreApplication app(argc, argv);
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("temporary directory unavailable");
        const auto expected = temporary.filePath("watched.txt");
        writeTemporaryFile(expected, "initial", true);
        auto scheduler = std::make_shared<QtScheduler>();
        auto kernel = std::make_shared<Kernel>(scheduler);
        QtClock clock(scheduler);
        auto watcher = std::make_unique<QFileSystemWatcher>();
        if (!watcher->addPath(expected))
            throw std::runtime_error("file watch rejected");
        auto sender = watcher.get();
        QPointer<QFileSystemWatcher> weak = sender;
        std::unique_ptr<QObject> object(std::move(watcher));
        auto resource = std::make_shared<QObjectResource>(object, scheduler);
        std::optional<QtConnection> connection;
        auto runtime = kernel->registerPlugin("qt.native.watcher.demo", [&](Context context) {
            auto cleanup = context.onCleanup([resource](CleanupContext) { return resource->close(); }, "native.watcher");
            if (!cleanup.hasValue())
                return failedTask(cleanup.error());
            auto observed = QtConnection::observePrefixValues<QString>(context, scheduler, sender,
                &QFileSystemWatcher::fileChanged, [&](const QString& path) {
                    Promise<void> business;
                    auto timer = clock.after(std::chrono::milliseconds(0), {
                        [business, &path, expected] {
                            if (path != expected) {
                                business.reject(makeError(ErrorCode::CallbackFailed, "unexpected watcher path"));
                                return;
                            }
                            std::cout << "native file notification delivered to owner\n";
                            business.resolve();
                        },
                        [business] { business.reject(makeError(ErrorCode::OperationCancelled, "timer discarded")); }
                    });
                    if (!timer.hasValue())
                        return failedTask(timer.error());
                    kernel->close().thenOn(scheduler, [&](const Result<CleanupReport>& report) {
                        const auto signal = connection->snapshot().value();
                        if (!report.hasValue() || !report.value().quiescent || !weak.isNull() ||
                            !resource->snapshot().value().confirmed || !signal.closed || signal.payloads != 0 ||
                            signal.callbackErrors != 0) {
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                        clock.close().thenOn(scheduler, [&](const Result<void>& result) {
                            if (!result.hasValue() || !scheduler->close().hasValue()) {
                                QCoreApplication::exit(EXIT_FAILURE);
                                return;
                            }
                            std::cout << "watcher, connection, clock and kernel closed\n";
                            QCoreApplication::exit(EXIT_SUCCESS);
                        });
                    });
                    return business.task();
                });
            if (!observed.hasValue())
                return failedTask(observed.error());
            connection.emplace(observed.value());
            return connection->ready();
        }).value();
        auto instance = kernel->mount(runtime).value();
        instance.ready().thenOn(scheduler, [&](const Result<void>& ready) {
            if (!ready.hasValue()) {
                QCoreApplication::exit(EXIT_FAILURE);
                return;
            }
            writeTemporaryFile(expected, "updated native notification");
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
