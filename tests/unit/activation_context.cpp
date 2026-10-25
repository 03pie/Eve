/** @file activation_context.cpp
 * @brief 使用真实实例/子树和显式 gate 验证宿主跨代观察，不借用过期插件上下文。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + std::to_string(__LINE__));                         \
    } while (false)

/** @brief 每个场景独立持有内核，只手动推进时间，不替换实例或清理屏障。 */
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel{scheduler};
    /** @brief 队列耗尽才读取稳定状态，不使用 sleep 推测完成。 */
    void pump() { scheduler->runUntilIdle().value(); }
    /** @brief 关闭资源先于调度器关闭。 */
    void close()
    {
        auto closed = kernel.close();
        pump();
        CHECK(closed.isReady() && closed.tryResult()->value().quiescent);
        scheduler->close();
    }
};

/** @brief 保持真实 FIFO，只在 owner 投递入口模拟宿主重入，不提前执行被投递的任务。 */
class ReentrantScheduler final : public IScheduler {
public:
    FakeScheduler queue;
    std::function<void()> onPost;
    /** @brief 虚拟调度边界可重入宿主，实际工作仍交给队列。 */
    bool post(ScheduledJob job) override
    {
        if (isCurrentThread() && onPost)
            onPost();
        return queue.post(std::move(job));
    }
    /** @brief 归属判断委托真实调度器。 */
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    /** @brief 不因测试钩子伪报关闭。 */
    bool isOpen() const override { return queue.isOpen(); }
};

/** @brief 首个观察的投递重入 stop 时，其余尚未完成观察立即取消。 */
void reentrantStop()
{
    auto scheduler = std::make_shared<ReentrantScheduler>();
    Kernel kernel(scheduler);
    auto runtime = kernel.registerPlugin("parent", [](Context) { return completedTask(); }).value();
    auto instance = kernel.mount(runtime).value();
    auto first = instance.whenActiveAfter();
    auto second = instance.whenActiveAfter();
    auto delivered = first.thenOn(scheduler, [](const Result<Context>&) {});
    bool entered = false;
    bool cancelledDuringStop = false;
    std::optional<Task<CleanupReport>> stopped;
    scheduler->onPost = [&] {
        if (!entered && first.isReady()) {
            entered = true;
            stopped.emplace(instance.stop());
            auto result = second.tryResult();
            cancelledDuringStop =
                result && !result->hasValue() && result->error().code == ErrorCode::OperationCancelled;
        }
    };
    scheduler->queue.runUntilIdle().value();
    scheduler->onPost = {};
    CHECK(entered && cancelledDuringStop);
    CHECK(first.tryResult()->hasValue() && !first.tryResult()->value().identity().hasValue());
    CHECK(second.tryResult()->error().code == ErrorCode::OperationCancelled);
    CHECK(delivered.tryResult()->hasValue() && stopped->tryResult()->value().quiescent);
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
    scheduler->queue.close();
}

/** @brief 稳定目标通知中更新父配置，随后登记的观察不能回放已退役的旧代。 */
void reentrantUpdate()
{
    auto scheduler = std::make_shared<ReentrantScheduler>();
    Kernel kernel(scheduler);
    auto runtime = kernel.registerPlugin("parent", [](Context) { return completedTask(); }).value();
    auto instance = kernel.mount(runtime, "one").value();
    auto first = instance.whenActiveAfter();
    auto target = instance.operation().value().target;
    auto delivered = target.thenOn(scheduler, [](const Result<StableReport>&) {});
    bool entered = false;
    std::optional<Task<Context>> later;
    scheduler->onPost = [&] {
        if (!entered && target.isReady()) {
            entered = true;
            instance.update("two", 1).value();
            later.emplace(instance.whenActiveAfter());
            CHECK(!later->isReady());
        }
    };
    scheduler->queue.runUntilIdle().value();
    scheduler->onPost = {};
    CHECK(entered && later->isReady() && later->tryResult()->value().config() == "two");
    CHECK(later->tryResult()->value().identity().value().epoch == 2);
    CHECK(first.tryResult()->value().config() == "one" && !first.tryResult()->value().identity().hasValue());
    CHECK(delivered.tryResult()->hasValue());
    auto closed = kernel.close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
    scheduler->queue.close();
}

/** @brief 父恢复新代次后宿主才挂载新子树，旧子清理阻挡父重启，子附加依赖继承精确域。 */
void treeEpochs()
{
    Scene scene;
    const auto realm = scene.kernel.services().createRealm().value();
    int calls = 0;
    Promise<void> childCleanup;
    std::vector<std::string> cleaned;
    auto parentRuntime = scene.kernel
                             .registerPlugin("parent",
                                             [&](Context context) {
                                                 context
                                                     .onCleanup([&, config = context.config()](CleanupContext) {
                                                         cleaned.push_back("parent-" + config);
                                                         return completedTask();
                                                     })
                                                     .value();
                                                 return completedTask();
                                             },
                                             {ServiceRequirement::of<int>({"trigger"})})
                             .value();
    auto childRuntime =
        scene.kernel
            .registerPlugin(
                "child",
                [&](Context context) {
                    context.consume<int>({"store"}).value().withService([](int& value) { CHECK(value == 42); }).value();
                    ++calls;
                    context
                        .onCleanup([&, config = context.config()](CleanupContext) {
                            cleaned.push_back("child-" + config);
                            return config == "one" ? childCleanup.task() : completedTask();
                        })
                        .value();
                    return completedTask();
                })
            .value();
    auto parent = scene.kernel.mount(parentRuntime, "one", RealmMap{}.isolate("store", realm).value()).value();
    auto first = parent.whenActiveAfter();
    scene.pump();
    CHECK(!first.isReady() && parent.snapshot().value().state == InstanceState::Pending);
    auto trigger = scene.kernel.services().stage<int>({"trigger"}, std::make_shared<int>(1)).value();
    trigger.publish().value();
    scene.pump();
    auto context = first.tryResult()->value();
    const auto epoch = context.identity().value().epoch;
    auto child = context.inject(childRuntime, "one", {ServiceRequirement::of<int>({"store"}, 2, {"read"})}).value();
    scene.pump();
    CHECK(calls == 0 && parent.snapshot().value().children == 1 &&
          child.dependencies().value().front().key.realm == realm);
    auto global = scene.kernel.services().stage<int>({"store"}, std::make_shared<int>(42), {2, {"read"}}).value();
    global.publish().value();
    scene.pump();
    CHECK(calls == 0);
    auto store =
        scene.kernel.services().stage<int>({"store", 1, realm}, std::make_shared<int>(42), {2, {"read"}}).value();
    store.publish().value();
    scene.pump();
    CHECK(calls == 1);
    auto next = parent.whenActiveAfter(epoch);
    auto updated = parent.update("two", parent.snapshot().value().revision).value();
    scene.pump();
    CHECK(!next.isReady() && !context.inject(childRuntime).hasValue() &&
          cleaned == std::vector<std::string>({"child-one"}));
    childCleanup.resolve();
    scene.pump();
    CHECK(updated.target.tryResult()->value().outcome == StableOutcome::Active &&
          cleaned == std::vector<std::string>({"child-one", "parent-one"}));
    auto current = next.tryResult()->value();
    CHECK(current.config() == "two" && current.identity().value().epoch > epoch && !context.identity().hasValue());
    CHECK(first.tryResult()->value().config() == "one" && child.snapshot().value().state == InstanceState::Disposed);
    auto replacement =
        current.inject(childRuntime, "two", {ServiceRequirement::of<int>({"store"}, 2, {"read"})}).value();
    scene.pump();
    CHECK(calls == 2 && replacement.snapshot().value().instance != child.snapshot().value().instance &&
          parent.snapshot().value().children == 1 && childRuntime.requirements().empty());
    auto waiting = parent.whenActiveAfter(current.identity().value().epoch);
    auto stopped = parent.stop();
    CHECK(waiting.isReady() && waiting.tryResult()->error().code == ErrorCode::OperationCancelled);
    scene.pump();
    CHECK(stopped.tryResult()->value().quiescent && parent.snapshot().value().children == 0);
    trigger.retire().value();
    trigger.release().value();
    global.retire().value();
    global.release().value();
    store.retire().value();
    store.release().value();
    scene.close();
}

/** @brief 等待成功上下文不把失败当作激活；显式重试后观察新代次，历史 ready 结果保持。 */
void failureRetry()
{
    Scene scene;
    int calls = 0;
    auto runtime = scene.kernel
                       .registerPlugin("retry",
                                       [&](Context) {
                                           return ++calls == 1
                                                      ? failedTask(makeError(ErrorCode::ActivationFailed, "first"))
                                                      : completedTask();
                                       })
                       .value();
    auto instance = scene.kernel.mount(runtime, "first").value();
    auto waiting = instance.whenActiveAfter();
    scene.pump();
    CHECK(!waiting.isReady() && instance.snapshot().value().state == InstanceState::Failed);
    auto retried = instance.retry().value();
    scene.pump();
    auto context = waiting.tryResult()->value();
    CHECK(retried.target.tryResult()->value().outcome == StableOutcome::Active &&
          context.identity().value().epoch == 2);
    CHECK(!instance.ready().tryResult()->hasValue());
    auto immediate = instance.whenActiveAfter();
    CHECK(immediate.isReady() && immediate.tryResult()->value().identity().value().epoch == 2);
    auto next = instance.whenActiveAfter(2);
    auto updated = instance.update("latest", 1).value();
    scene.pump();
    CHECK(updated.target.tryResult()->value().outcome == StableOutcome::Active &&
          next.tryResult()->value().config() == "latest");
    CHECK(!context.identity().hasValue() && waiting.tryResult()->value().config() == "first");
    scene.close();
}

/** @brief owner、阈值及停止门禁均在登记前验证，停止不必等待激活源任务才取消观察。 */
void guards()
{
    Scene scene;
    Promise<void> activation;
    auto runtime = scene.kernel.registerPlugin("pending", [activation](Context) { return activation.task(); }).value();
    auto instance = scene.kernel.mount(runtime).value();
    auto first = instance.whenActiveAfter();
    auto future = instance.whenActiveAfter(1);
    CHECK(future.isReady() && future.tryResult()->error().code == ErrorCode::InvalidArgument);
    bool wrong = false;
    std::thread worker([&] {
        auto task = instance.whenActiveAfter();
        wrong = task.isReady() && task.tryResult()->error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    scene.pump();
    auto next = instance.whenActiveAfter(1);
    auto stopped = instance.stop();
    CHECK(first.isReady() && next.isReady() && first.tryResult()->error().code == ErrorCode::OperationCancelled &&
          !stopped.isReady());
    auto closed = instance.whenActiveAfter();
    CHECK(closed.isReady() && closed.tryResult()->error().code == ErrorCode::InstanceDisposed);
    activation.resolve();
    scene.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    scene.close();
}

/** @brief 历史上下文只弱持来源；非法子契约不能增加子资源记录。 */
void weakLifetime()
{
    Scene scene;
    auto runtime = scene.kernel.registerPlugin("parent", [](Context) { return completedTask(); }).value();
    auto childRuntime = scene.kernel.registerPlugin("child", [](Context) { return completedTask(); }).value();
    std::optional<Instance> instance = scene.kernel.mount(runtime, "snapshot").value();
    scene.pump();
    auto observed = instance->whenActiveAfter();
    auto context = observed.tryResult()->value();
    auto moved = *instance;
    std::optional<Instance> held(std::move(moved));
    auto invalid = moved.whenActiveAfter();
    CHECK(invalid.isReady() && invalid.tryResult()->error().code == ErrorCode::InvalidArgument);
    CHECK(!context.inject(childRuntime, {}, {ServiceRequirement::of<void>({"bad"})}).hasValue());
    CHECK(instance->snapshot().value().children == 0);
    auto stopped = instance->stop();
    scene.pump();
    CHECK(stopped.tryResult()->value().quiescent);
    CHECK(scene.kernel.snapshot().value().instances.empty());
    instance.reset();
    held.reset();
    CHECK(!context.identity().hasValue() && context.identity().error().code == ErrorCode::StaleContext &&
          observed.tryResult()->value().config() == "snapshot");
    scene.close();
}

/** @brief 每项场景以独立进程验证真实 Core 生命周期。 */
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "tree")
            treeEpochs();
        else if (name == "retry")
            failureRetry();
        else if (name == "guards")
            guards();
        else if (name == "weak")
            weakLifetime();
        else if (name == "reentry_stop")
            reentrantStop();
        else if (name == "reentry_update")
            reentrantUpdate();
        else
            return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
