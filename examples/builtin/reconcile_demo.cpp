/** @file reconcile_demo.cpp
 * @brief 解析两份目标并用真实 Kernel 启动、更新和停止内置插件，打印期望修订及实际服务值。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cordis;
using namespace cordis::loader;
namespace {
/** @brief 显式推进队列并检查稳定结果，不使用 sleep 或把接受成功当作运行成功。 */
void settle(const std::shared_ptr<testing::FakeScheduler>& scheduler, const AcceptedTarget& target)
{
    scheduler->runUntilIdle().value();
    auto report = target.settled.tryResult();
    if (!report || !report->hasValue() || report->value().outcome != PlanOutcome::Applied)
        throw std::runtime_error("configuration did not reach Applied");
}
/** @brief 示例退出必须确认全部资源静止，不能仅丢弃 facade。 */
void drain(const std::shared_ptr<testing::FakeScheduler>& scheduler, const Task<CleanupReport>& task)
{
    scheduler->runUntilIdle().value();
    auto report = task.tryResult();
    if (!report || !report->hasValue() || !report->value().quiescent)
        throw std::runtime_error("cleanup did not establish quiescence");
}
} // namespace

/** @brief 无 Qt 示例使用确定性队列驱动真实插件；生产宿主可替换为 owner 事件循环。 */
int main()
{
    try {
        auto scheduler = std::make_shared<testing::FakeScheduler>();
        auto kernel = std::make_shared<Kernel>(scheduler);
        int observed = 0;
        auto provider =
            kernel
                ->registerPlugin("demo.provider",
                                 [](Context context) {
                                     const int value = context.config() == "1" ? 1 : 2;
                                     context.provide<int>({"counter"}, std::make_shared<int>(value)).value();
                                     return completedTask();
                                 },
                                 {}, {ServiceOutput::of<int>({"counter"})})
                .value();
        auto consumer = kernel
                            ->registerPlugin("demo.consumer",
                                             [&](Context context) {
                                                 context.consume<int>({"counter"})
                                                     .value()
                                                     .withService([&](int& value) { observed = value; })
                                                     .value();
                                                 return completedTask();
                                             })
                            .value();
        const std::vector<PluginDefinition> catalog{
            {"demo.provider",
             {},
             [](const std::string& value) {
                 if (value == "1" || value == "2")
                     return Result<void>::success();
                 return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "provider config must be 1 or 2"));
             }},
            {"demo.consumer", {}, [](const std::string& value) {
                 if (value == "{}")
                     return Result<void>::success();
                 return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "consumer config must be empty"));
             }}};
        auto driver = std::make_shared<KernelReconcileDriver>(
            scheduler, kernel,
            std::vector<KernelPlugin>{{"demo.provider", provider},
                                      {"demo.consumer", consumer, {ServiceRequirement::of<int>({"counter"})}}});
        ConfigurationCoordinator coordinator(scheduler, driver);
        // 消费者故意排在文件前面，实际启动顺序由精确依赖确定。
        const std::string prefix = "formatVersion: 1\nplugins:\n"
                                   "  - {id: a-consumer, plugin: demo.consumer, inject: [counter/v1]}\n"
                                   "  - {id: z-provider, plugin: demo.provider, config: ";
        auto first =
            coordinator.accept(validateCandidate(parseConfiguration(prefix + "1}\n").value(), catalog).value(), 0)
                .value();
        settle(scheduler, first);
        if (observed != 1)
            throw std::runtime_error("consumer did not observe initial service");
        std::cout << "desiredRevision=" << first.revision << ", running=" << driver->snapshot().value().size()
                  << ", service=" << observed << '\n';
        auto second =
            coordinator
                .accept(validateCandidate(parseConfiguration(prefix + "2}\n").value(), catalog).value(), first.revision)
                .value();
        settle(scheduler, second);
        if (observed != 2)
            throw std::runtime_error("consumer did not recover after provider restart");
        if (!consumer.requirements().empty() || kernel->snapshot().value().runtimes != 2)
            throw std::runtime_error("configuration changed immutable Runtime definitions");
        std::cout << "desiredRevision=" << second.revision << ", running=" << driver->snapshot().value().size()
                  << ", service=" << observed << '\n';
        drain(scheduler, coordinator.close());
        drain(scheduler, kernel->close());
        if (!driver->snapshot().value().empty())
            throw std::runtime_error("driver retained live instances");
        scheduler->close();
        std::cout << "Closed: all owned instances quiescent\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
