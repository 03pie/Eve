/**
 * @file task_demo.cpp
 * @brief 演示从工作线程完成任务并在所属线程执行回调。
 */
#include <cordis/task.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <memory>
#include <thread>

/**
 * @brief 演示任务回调由所属调度器线程泵送执行。
 * @return 所有任务与回调检查通过时返回 0。
 */
int main()
{
    auto scheduler = std::make_shared<cordis::testing::FakeScheduler>();
    cordis::Promise<int> producer;
    bool handled = false;
    auto receipt = producer.task().thenOn(scheduler, [&](const cordis::Result<int>& result) {
        std::cout << "Owner-thread continuation received: " << result.value() << '\n';
        handled = true;
    });
    std::thread worker([producer] { producer.resolve(42); });
    worker.join();
    // 完成任务不会内联执行回调；必须由调度器所属线程泵送队列。
    if (handled) return 1;
    const auto drained = scheduler->runUntilIdle();
    if (!drained.hasValue() || !receipt.isReady() || !receipt.tryResult()->hasValue()) return 2;
    return handled ? 0 : 3;
}
