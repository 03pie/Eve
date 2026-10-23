/** @file platform_demo.cpp
 * @brief 演示服务恢复、父子所有权、事件在途屏障与失败残留的内核闭环。
 */
#include <cordis/event.hpp>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace cordis;
using Decision = EventDecision<std::string>;

/** @brief 将示例中的协议断言转为非零退出，供 CTest 验收。 */
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

/** @brief 提供者管理的服务对象，清理前必须保持可访问。 */
struct Store {
    bool open = true;
    std::string document;
};

/** @brief 回调共享的宿主状态；失败保留的闭包不捕获示例栈对象。 */
struct State {
    Promise<Decision> acknowledged;
    std::optional<Context> rootContext;
    std::optional<Instance> consumer;
    std::optional<ServiceRef<Store>> originalService;
    std::weak_ptr<Store> resource;
    std::vector<std::string> timeline;
    int activations = 0;
    int saves = 0;
    bool failCleanup = false;
};

/** @brief 一个真实 Kernel、服务域、子树及事件总线组成的可驱动场景。 */
class Scenario {
public:
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel{scheduler};
    EventBus<std::string> events{scheduler};
    std::shared_ptr<State> state = std::make_shared<State>();
    Runtime provider;
    Instance root;
    std::optional<Instance> firstProvider;

    /** @brief 先创建缺依赖消费者，provider 后注入；配置和域由父 Context 传递。 */
    explicit Scenario(bool failCleanup)
        : provider(makeProvider(state)), root(makeRoot(kernel, events, state))
    {
        state->failCleanup = failCleanup;
        pump();
        require(root.snapshot().value().state == InstanceState::Active, "root must activate independently");
        require(state->consumer->snapshot().value().state == InstanceState::Pending, "consumer must wait for store");
        require(state->activations == 0, "missing dependency must not run consumer");
        print("missing");
        firstProvider.emplace(state->rootContext->inject(provider).value());
        pump();
        require(state->activations == 1 && state->consumer->snapshot().value().state == InstanceState::Active, "consumer did not recover");
        print("active");
    }

    /** @brief 驱动到当前队列静止，真实异步 gate 未完成时不伪造完成。 */
    void pump() { require(scheduler->runUntilIdle().hasValue(), "scheduler failed or exceeded budget"); }

    /** @brief 单提供者场景先检查绑定数，再读取账目，回归时报告原因而非越界。 */
    BindingSnapshot binding()
    {
        const auto bindings = kernel.services().snapshot().value();
        require(bindings.size() == 1, "expected exactly one store binding");
        return bindings.front();
    }

    /** @brief 验证当前 epoch 的事件实际写入当前服务，并完成保存确认。 */
    void saveAndAcknowledge(const std::string& document)
    {
        const auto before = state->saves;
        auto saved = events.serial({"save"}, document);
        pump();
        require(state->saves == before + 1, "save must invoke exactly one current listener");
        const auto store = state->resource.lock();
        require(store && store->open && store->document == document, "save did not reach current store");
        state->acknowledged.resolve(Decision::Stop("saved"));
        pump();
        require(saved.isReady() && saved.tryResult()->hasValue() && saved.tryResult()->value().stopped.value_or("") == "saved", "save acknowledgement lost");
    }

    /** @brief 打印来自实际诊断接口的账目，不通过清空容器修饰残留。 */
    void print(const char* phase)
    {
        const auto bindings = kernel.services().snapshot().value();
        const auto bus = events.snapshot().value();
        std::size_t leases = 0;
        for (const auto& binding : bindings) leases += binding.committedLeases;
        std::cout << phase << ": root_children=" << root.snapshot().value().children
                  << " bindings=" << bindings.size() << " committed=" << leases
                  << " listeners=" << bus.liveListeners << " dispatches=" << bus.activeDispatches << '\n';
    }

    /** @brief 检查正常关闭后的整棵子树、服务、effect、事件及队列均已静止。 */
    void checkClosed(const Task<CleanupReport>& closed)
    {
        require(closed.isReady() && closed.tryResult()->hasValue() && closed.tryResult()->value().quiescent, "kernel did not quiesce");
        require(closed.tryResult()->value().errors.empty(), "unexpected cleanup errors");
        const auto rootView = root.snapshot().value();
        const auto consumerView = state->consumer->snapshot().value();
        const auto providerView = firstProvider->snapshot().value();
        require(rootView.state == InstanceState::Disposed && rootView.children == 0 && rootView.resources.effects.empty() && rootView.committedLeases == 0, "root retained resources");
        require(consumerView.state == InstanceState::Disposed && consumerView.resources.effects.empty() && consumerView.committedLeases == 0, "consumer retained resources");
        require(providerView.state == InstanceState::Disposed && providerView.resources.effects.empty() && providerView.committedLeases == 0, "provider retained resources");
        require(kernel.snapshot().value().instances.empty(), "kernel retained root");
        require(kernel.services().snapshot().value().empty() && state->resource.expired(), "store retained after cleanup");
        const auto bus = events.snapshot().value();
        require(bus.liveListeners == 0 && bus.activeDispatches == 0 && !bus.deliveryBlocked, "event resources retained");
        require(scheduler->pending() == 0, "scheduler still has work");
        print("closed");
    }

private:
    /** @brief 声明并登记存储输出，其资源只能在下游退出后关闭。 */
    static Runtime makeProvider(const std::shared_ptr<State>& shared)
    {
        return Runtime([shared](Context context) {
            auto store = std::make_shared<Store>();
            shared->resource = store;
            require(context.provide({"store"}, store).hasValue(), "provide failed");
            require(context.onCleanup([shared, store](CleanupContext) {
                store->open = false;
                shared->timeline.push_back("provider closed");
                return completedTask();
            }).hasValue(), "provider cleanup registration failed");
            return completedTask();
        }, {}, {ServiceOutput::of<Store>({"store"})});
    }

    /** @brief 根拥有消费者，消费者强依赖服务且拥有异步事件监听器。 */
    static Instance makeRoot(Kernel& kernel, EventBus<std::string> bus, const std::shared_ptr<State>& shared)
    {
        require(bus.declare({"save"}, EventMode::Serial).hasValue(), "event declaration failed");
        Runtime consumer([shared, bus](Context context) {
            require(context.config() == "kept", "consumer configuration changed during recovery");
            auto store = context.consume<Store>({"store"}).value();
            if (++shared->activations == 1) shared->originalService.emplace(store);
            require(context.onCleanup([shared, store](CleanupContext cleanup) {
                auto read = cleanup.withService(store, [](Store& value) { require(value.open, "provider closed before consumer cleanup"); });
                require(read.hasValue(), "old binding unavailable during cleanup");
                shared->timeline.push_back(shared->failCleanup ? "consumer cleanup failed" : "consumer cleaned");
                return shared->failCleanup ? failedTask(makeError(ErrorCode::CleanupFailed, "consumer retained store")) : completedTask();
            }).hasValue(), "consumer cleanup registration failed");
            require(bus.onAsync(context, {"save"}, [shared, store](const std::string& document) {
                require(store.withService([&](Store& value) {
                    require(value.open, "store closed during save");
                    value.document = document;
                }).hasValue(), "save admission failed");
                ++shared->saves;
                // 事件的确认阶段仍在途，整个消费者和提供者必须保留到确认完成。
                return shared->acknowledged.task();
            }).hasValue(), "listener registration failed");
            return completedTask();
        }, {ServiceRequirement::of<Store>({"store"})});
        auto runtime = kernel.registerPlugin("root", [shared, consumer](Context context) {
            shared->rootContext.emplace(context);
            shared->consumer.emplace(context.inject(consumer, "kept").value());
            require(context.onCleanup([shared](CleanupContext) {
                shared->timeline.push_back("root cleaned");
                return completedTask();
            }).hasValue(), "root cleanup registration failed");
            return completedTask();
        }).value();
        return kernel.mount(runtime).value();
    }
};

/** @brief 提供者退出使消费者 Pending，新绑定恢复且旧引用失效，最终正常关闭。 */
void recovery()
{
    Scenario scenario(false);
    scenario.saveAndAcknowledge("first document");
    const auto oldBinding = scenario.binding().id;
    auto stopped = scenario.firstProvider->stop();
    scenario.pump();
    require(stopped.isReady() && stopped.tryResult()->hasValue() && stopped.tryResult()->value().quiescent, "first provider did not stop");
    require(scenario.state->consumer->snapshot().value().state == InstanceState::Pending, "consumer did not return to pending");
    require(scenario.events.snapshot().value().liveListeners == 0, "old listener remained active");
    require(scenario.state->resource.expired(), "first store retained after retirement");
    require((scenario.state->timeline == std::vector<std::string>{"consumer cleaned", "provider closed"}), "cleanup dependency order violated");
    auto replacement = scenario.state->rootContext->inject(scenario.provider).value();
    scenario.pump();
    require(scenario.state->activations == 2 && scenario.state->consumer->snapshot().value().epoch == 2, "consumer epoch did not recover");
    require(scenario.binding().id != oldBinding, "binding identity reused");
    require(!scenario.state->originalService->withService([](Store&) {}).hasValue(), "stale service remained usable");
    require(scenario.events.snapshot().value().liveListeners == 1, "recovery did not replace the listener");
    scenario.saveAndAcknowledge("replacement document");
    scenario.print("recovered");
    auto closed = scenario.kernel.close();
    scenario.pump();
    scenario.checkClosed(closed);
    require(replacement.snapshot().value().state == InstanceState::Disposed, "replacement not disposed");
}

/** @brief 根停止等待已进入的保存事件，确认后消费者先于提供者及根清理。 */
void shutdown()
{
    Scenario scenario(false);
    auto saved = scenario.events.serial({"save"}, "document");
    scenario.pump();
    require(scenario.state->saves == 1 && !saved.isReady(), "save did not enter pending acknowledgement");
    auto closed = scenario.kernel.close();
    scenario.pump();
    require(!closed.isReady() && !scenario.state->resource.expired(), "kernel closed during event");
    require(scenario.binding().state == BindingState::Retiring, "output not retired");
    require(scenario.events.snapshot().value().activeDispatches == 1 && scenario.events.snapshot().value().liveListeners == 0, "event shutdown accounting incorrect");
    require(!scenario.state->originalService->withService([](Store&) {}).hasValue(), "business entry stayed open");
    scenario.print("waiting for event acknowledgement");
    scenario.state->acknowledged.resolve(Decision::Stop("saved"));
    scenario.pump();
    require(saved.isReady() && saved.tryResult()->hasValue() && saved.tryResult()->value().stopped.value_or("") == "saved", "save result lost");
    require((scenario.state->timeline == std::vector<std::string>{"consumer cleaned", "provider closed", "root cleaned"}), "root cleanup order violated");
    scenario.checkClosed(closed);
}

/** @brief 下游清理失败保留旧绑定，上游和根仍等待，诊断不能伪报全局静止。 */
void failure()
{
    Scenario scenario(true);
    auto closed = scenario.kernel.close();
    scenario.pump();
    auto consumer = scenario.state->consumer->stop();
    require(consumer.isReady() && consumer.tryResult()->hasValue() && !consumer.tryResult()->value().quiescent, "consumer failure not reported");
    require(!consumer.tryResult()->value().errors.empty() && consumer.tryResult()->value().errors[0].message == "consumer retained store", "consumer error lost");
    require(!closed.isReady() && !scenario.firstProvider->stop().isReady(), "upstream incorrectly quiesced");
    require(scenario.root.snapshot().value().state == InstanceState::Unloading, "root did not retain child tree");
    const auto bindings = scenario.kernel.services().snapshot().value();
    require(bindings.size() == 1 && bindings[0].state == BindingState::Retiring && bindings[0].committedLeases == 1, "failed cleanup lost binding retention");
    const auto store = scenario.state->resource.lock();
    require(store && store->open, "retained store was closed");
    const auto consumerView = scenario.state->consumer->snapshot().value();
    require(consumerView.failure && consumerView.failure->code == ErrorCode::CleanupFailed, "failure missing from snapshot");
    require((scenario.state->timeline == std::vector<std::string>{"consumer cleanup failed"}), "failure incorrectly reported cleanup success");
    scenario.print("blocked: consumer cleanup failed; provider and root still waiting");
    std::cout << "consumer report: quiescent=false; kernel report: pending\n";
}
} // namespace

/** @brief 选择正常关闭、恢复或清理失败场景；协议断言失败时返回非零退出码。 */
int main(int argc, char** argv)
{
    try {
        const std::string mode = argc == 1 ? "shutdown" : argv[1];
        if (argc > 2) return EXIT_FAILURE;
        if (mode == "shutdown") shutdown();
        else if (mode == "recovery") recovery();
        else if (mode == "failure") failure();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
