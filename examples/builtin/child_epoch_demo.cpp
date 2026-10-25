/** @file child_epoch_demo.cpp
 * @brief 宿主跨父代次重建配置子实例的 Core 桥示例，不替代 Loader 的显式 children 协调器。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace cordis;
namespace {
/** @brief 示例只推进确定性队列，未稳定或失败都不伪装成执行成功。 */
template <class T> T wait(const std::shared_ptr<testing::FakeScheduler>& scheduler, const Task<T>& task)
{
    scheduler->runUntilIdle().value();
    auto result = task.tryResult();
    if (!result || !result->hasValue())
        throw std::runtime_error("operation did not complete successfully");
    return result->value();
}
} // namespace

/** @brief 父配置更新后旧子先终结，宿主用新代弱上下文创建带附加依赖的新子。 */
int main()
{
    try {
        auto scheduler = std::make_shared<testing::FakeScheduler>();
        Kernel kernel(scheduler);
        auto store = kernel.services().stage<int>({"store"}, std::make_shared<int>(42)).value();
        store.publish().value();
        auto parentRuntime = kernel.registerPlugin("parent", [](Context) { return completedTask(); }).value();
        auto childRuntime = kernel
                                .registerPlugin("child",
                                                [](Context context) {
                                                    context.consume<int>({"store"})
                                                        .value()
                                                        .withService([](int& value) {
                                                            if (value != 42)
                                                                throw std::runtime_error("unexpected service");
                                                        })
                                                        .value();
                                                    return completedTask();
                                                })
                                .value();
        auto parent = kernel.mount(parentRuntime, "first").value();
        auto first = wait(scheduler, parent.whenActiveAfter());
        const auto epoch = first.identity().value().epoch;
        auto child = first.inject(childRuntime, first.config(), {ServiceRequirement::of<int>({"store"})}).value();
        auto active = wait(scheduler, child.operation().value().target);
        if (active.outcome != StableOutcome::Active)
            throw std::runtime_error("child did not activate");
        auto next = parent.whenActiveAfter(epoch);
        auto changed = parent.update("second", 1).value();
        auto current = wait(scheduler, next);
        if (child.snapshot().value().state != InstanceState::Disposed || first.identity().hasValue())
            throw std::runtime_error("old subtree still accepts business");
        if (changed.target.tryResult()->value().outcome != StableOutcome::Active)
            throw std::runtime_error("parent update did not activate");
        auto replacement =
            current.inject(childRuntime, current.config(), {ServiceRequirement::of<int>({"store"})}).value();
        if (wait(scheduler, replacement.operation().value().target).outcome != StableOutcome::Active)
            throw std::runtime_error("replacement child did not activate");
        std::cout << "Parent epoch " << epoch << " -> " << current.identity().value().epoch
                  << "; old child disposed; new child=" << replacement.snapshot().value().instance.value() << '\n';
        auto pending = parent.whenActiveAfter(current.identity().value().epoch);
        auto closed = wait(scheduler, kernel.close());
        if (!closed.quiescent || !pending.isReady() || pending.tryResult()->hasValue())
            throw std::runtime_error("shutdown did not cancel future observation and drain resources");
        store.retire().value();
        store.release().value();
        scheduler->close();
        std::cout << "Closed: parent and child resources quiescent\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
