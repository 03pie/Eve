/** @file module_state_tree.cpp
 * @brief 状态树场景的唯一模板实例化及真实 Fake/Qt 调度等待。
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../support/module_state_tree.hpp"
#include "../support/module_reservation_scene.hpp"
#include "../support/replacement_state_tree.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QThread>
#include <atomic>
#include <eve/loader/qt_store.hpp>
#include <eve/testing/fake_scheduler.hpp>
#include <iostream>

bool operationStateTreeCase(Scene& scene, const eve::Runtime& old, const eve::Runtime& next,
                            const std::shared_ptr<eve::IScheduler>& scheduler,
                            const std::shared_ptr<eve::Kernel>& kernel, const QString& mode)
{
    const auto waitFor = [&](const auto& task) {
        if (const auto queue = std::dynamic_pointer_cast<eve::testing::FakeScheduler>(scheduler)) {
            queue->runUntilIdle(30000).value();
            CHECK(task.isReady());
            return task.tryResult();
        }
        QElapsedTimer timer;
        timer.start();
        while (!task.isReady()) {
            CHECK(timer.elapsed() < 5000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QThread::msleep(1);
        }
        return task.tryResult();
    };
    return operationStateTreeCase(scene, old, next, scheduler, kernel, waitFor, mode);
}
