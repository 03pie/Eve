/** @file event_demo.cpp
 * @brief 演示实例所有的异步事件如何阻止内核提前报告清理完成。
 */
#include <cordis/event.hpp>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>

/** @brief 发布任务、停止内核、完成源任务，验证事件与实例的真实清理屏障。 */
int main()
{
    using namespace cordis;
    using Decision = EventDecision<std::string>;
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel(scheduler);
    EventBus<std::string> events(scheduler);
    if (!events.declare({"save"}, EventMode::Serial).hasValue()) return EXIT_FAILURE;
    Promise<Decision> saved;
    auto plugin = kernel.registerPlugin("save-listener", [&](Context context) {
        auto listener = events.onAsync(context, {"save"}, [saved](const std::string& value) {
            std::cout << "saving " << value << '\n';
            return saved.task();
        });
        return listener.hasValue() ? completedTask() : failedTask(listener.error());
    });
    if (!plugin.hasValue() || !kernel.mount(plugin.value()).hasValue() || !scheduler->runUntilIdle().hasValue()) return EXIT_FAILURE;
    auto request = events.serial({"save"}, "document");
    if (!scheduler->runUntilIdle().hasValue()) return EXIT_FAILURE;
    auto closed = kernel.close();
    if (!scheduler->runUntilIdle().hasValue() || closed.isReady()) return EXIT_FAILURE;
    saved.resolve(Decision::Stop("saved"));
    if (!scheduler->runUntilIdle().hasValue() || !request.isReady() || !closed.isReady()) return EXIT_FAILURE;
    if (!request.tryResult()->hasValue() || request.tryResult()->value().stopped.value_or("") != "saved" || !closed.tryResult()->value().quiescent) return EXIT_FAILURE;
    std::cout << "saved; listeners and instance cleaned\n";
    return EXIT_SUCCESS;
}
