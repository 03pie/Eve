/** @file environment.cpp
 * @brief 验证目标 Qt 版本、Widgets/Test 链接及真实事件循环的排队与延迟删除。
 */
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <cordis/event.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

/** @brief 在真实 QApplication 下检查指定 Qt DLL、线程归属及排队行为。 */
int main(int argc, char** argv)
{
    static_assert(QT_VERSION == QT_VERSION_CHECK(5, 15, 2), "exact Qt 5.15.2 required");
    static_assert(sizeof(void*) == 8, "x64 Qt target required");
    QApplication app(argc, argv);
    if (std::string(qVersion()) != "5.15.2") return EXIT_FAILURE;
    auto widget = std::make_unique<QWidget>();
    QSignalSpy widgetDestroyed(widget.get(), &QObject::destroyed);
    widget.reset();
    if (!widgetDestroyed.isValid() || widgetDestroyed.count() != 1) return EXIT_FAILURE;
    // QT_NO_KEYWORDS 保证 Qt 的 emit 宏不会破坏类型化事件 SDK。
    auto scheduler = std::make_shared<cordis::testing::FakeScheduler>();
    cordis::EventBus<int> events(scheduler);
    if (!events.declare({"probe"}, cordis::EventMode::Emit).hasValue() || !events.emit({"probe"}, 1).hasValue()) return EXIT_FAILURE;
    QObject receiver;
    QPointer<QObject> deferred = new QObject;
    QSignalSpy deferredDestroyed(deferred.data(), &QObject::destroyed);
    bool called = false;
    bool owner = false;
    bool deleted = false;
    bool accepted = false;
    std::thread worker([&] {
        accepted = QMetaObject::invokeMethod(&receiver, [&] {
            called = true;
            owner = QThread::currentThread() == app.thread();
            deferred->deleteLater();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            deleted = deferred.isNull() && deferredDestroyed.count() == 1;
            app.exit(EXIT_SUCCESS);
        }, Qt::QueuedConnection);
    });
    worker.join();
    if (!accepted || called) { delete deferred.data(); return EXIT_FAILURE; }
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] { app.exit(EXIT_FAILURE); });
    deadline.start(5000);
    const auto result = app.exec();
    delete deferred.data();
    if (result != EXIT_SUCCESS || !called || !owner || !deleted) return EXIT_FAILURE;
    std::cout << "Qt " << qVersion() << ": Widgets/Test, queued owner delivery, deferred deletion and SDK keyword compatibility passed\n";
    return EXIT_SUCCESS;
}
