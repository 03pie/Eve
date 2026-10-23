/**
 * @file effect_demo.cpp
 * @brief 演示 effect 异步清理及超时观察。
 */
#include <cordis/effect.hpp>
#include <cordis/observation.hpp>
#include <cordis/testing/fake_clock.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <memory>

/**
 * @brief 演示异步清理等待、超时观察及最终资源释放。
 * @return 每个演示步骤均成功时返回 0，否则返回失败步骤编号。
 */
int main()
{
    using namespace std::chrono_literals;
    // 以手动调度器和虚拟时钟演示异步清理及超时观察。
    auto scheduler = std::make_shared<cordis::testing::FakeScheduler>();
    cordis::testing::FakeClock clock;
    cordis::EffectScope scope(scheduler);
    cordis::Promise<void> releaseGate;
    auto effect = scope.effect([&](cordis::EffectSetupContext setup) {
        auto first = setup.addCleanup([] {
            std::cout << "Resource 1 released\n";
            return cordis::completedTask();
        });
        if (!first.hasValue()) return cordis::failedTask(first.error());
        auto second = setup.addCleanup([&] {
            std::cout << "Resource 2 waiting for asynchronous release\n";
            return releaseGate.task();
        });
        if (!second.hasValue()) return cordis::failedTask(second.error());
        return cordis::completedTask();
    }, "demo.resources");
    if (!effect.hasValue() || !scheduler->runUntilIdle().hasValue()) return 1;
    auto closed = scope.close();
    if (!scheduler->runUntilIdle().hasValue()) return 2;
    // 超时只结束本次观察，不会取消仍在等待的资源清理。
    auto observed = cordis::observeFor(closed, clock, 10ms);
    if (!clock.advance(10ms).hasValue()) return 3;
    if (observed.tryResult()->value().state != cordis::ObservationState::TimedOut || closed.isReady()) return 4;
    std::cout << "Observation timed out; cleanup remains pending\n";
    releaseGate.resolve();
    if (!scheduler->runUntilIdle().hasValue()) return 5;
    return closed.isReady() && closed.tryResult()->value().quiescent ? 0 : 6;
}
