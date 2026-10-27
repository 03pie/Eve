/** @file value_connection_demo.cpp
 * @brief 冻结借用字符串后异步消费类型化 Qt 信号，联合等待业务、连接和时钟资源退出。
 */
#include <QCoreApplication>
#include <QObject>
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

/** @brief 宿主拥有发送者直到完整退出；信号只传递允许冻结的数据。 */
class PacketSource final : public QObject {
    Q_OBJECT
Q_SIGNALS:
    /** @brief 原生输入可借用临时缓冲区，托管连接在返回之前完成深拷贝。 */
    void packet(const QString& text, const QByteArray& bytes, int number);
};

/** @brief headless 示例以真实任务和清理回执退出，超时只作为失败保护。 */
int main(int argc, char** argv)
{
    using namespace cordis;
    using namespace cordis::qt;
    try {
        QCoreApplication app(argc, argv);
        PacketSource source;
        auto scheduler = std::make_shared<QtScheduler>();
        auto kernel = std::make_shared<Kernel>(scheduler);
        QtClock clock(scheduler);
        std::optional<QtConnection> connection;
        auto runtime = kernel->registerPlugin("qt.value.connection.demo", [&](Context context) {
            auto observed = QtConnection::observeValues(context, scheduler, &source, &PacketSource::packet,
                [&](const QString& text, const QByteArray& bytes, int number) {
                    Promise<void> business;
                    auto timer = clock.after(std::chrono::milliseconds(0), {
                        [business, &text, &bytes, number] {
                            if (text != QStringLiteral("Qt") || bytes != QByteArray("ok") || number != 42) {
                                business.reject(makeError(ErrorCode::CallbackFailed, "typed packet changed"));
                                return;
                            }
                            std::cout << "typed Qt packet: Qt / ok / " << number << '\n';
                            business.resolve();
                        },
                        [business] { business.reject(makeError(ErrorCode::OperationCancelled, "timer discarded")); }
                    });
                    if (!timer.hasValue())
                        return failedTask(timer.error());
                    kernel->close().thenOn(scheduler, [&](const Result<CleanupReport>& report) {
                        const auto resource = connection->snapshot().value();
                        if (!report.hasValue() || !report.value().quiescent || !resource.closed ||
                            !resource.nativeRetired || resource.payloads != 0 || resource.callbackErrors != 0) {
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                        clock.close().thenOn(scheduler, [&](const Result<void>& result) {
                            if (!result.hasValue() || !scheduler->close().hasValue()) {
                                QCoreApplication::exit(EXIT_FAILURE);
                                return;
                            }
                            std::cout << "typed connection, clock and kernel closed\n";
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
            QChar text[] = {QChar('Q'), QChar('t')};
            char bytes[] = {'o', 'k'};
            Q_EMIT source.packet(QString::fromRawData(text, 2), QByteArray::fromRawData(bytes, 2), 42);
            text[0] = QChar('X');
            bytes[0] = 'Y';
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
#include "value_connection_demo.moc"
