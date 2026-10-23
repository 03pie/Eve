/** @file dependency.cpp
 * @brief 验证托管服务的 Pending、恢复、消费者优先清理和依赖隔离。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief A01/A02/A03：声明依赖缺失不启动，提供者退出等待消费者，恢复保留配置。 */
void recovery()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> setupGate;
    Promise<void> cleanupGate;
    int activations = 0;
    int providers = 0;
    std::vector<std::string> events;
    std::optional<ServiceRef<int>> old;
    auto consumer = kernel.registerPlugin("consumer", [&](Context context) {
        CHECK(context.config() == "saved");
        auto service = context.consume<int>({"store"}).value();
        if (++activations == 1) old.emplace(service);
        CHECK(context.onCleanup([&, service](CleanupContext cleanup) {
            CHECK(cleanup.withService(service, [&](int& value) { CHECK(value > 0); events.push_back("consumer read open"); }).hasValue());
            if (activations == 1) return cleanupGate.task().thenOn(scheduler, [&](const Result<void>& result) {
                CHECK(result.hasValue()); events.push_back("consumer cleaned");
            });
            events.push_back("consumer cleaned");
            return completedTask();
        }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"store"})}).value();
    auto instance = kernel.mount(consumer, "saved").value();
    auto initial = instance.operation().value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 0 && instance.snapshot().value().epoch == 0);
    CHECK(instance.snapshot().value().state == InstanceState::Pending);
    CHECK(instance.awaitTarget(initial).tryResult()->value().outcome == StableOutcome::PendingMissingDependency);
    auto provider = kernel.registerPlugin("provider", [&](Context context) {
        auto service = std::make_shared<int>(++providers);
        CHECK(context.provide({"store"}, service).hasValue());
        CHECK(context.onCleanup([&, service](CleanupContext) { *service = 0; events.push_back("provider closed"); return completedTask(); }).hasValue());
        return providers == 1 ? setupGate.task() : completedTask();
    }, {}, {ServiceOutput::of<int>({"store"})}).value();
    auto first = kernel.mount(provider).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto before = kernel.services().snapshot().value();
    CHECK(before.size() == 1 && before[0].state == BindingState::Staged && activations == 0);
    const auto firstBinding = before[0].id;
    setupGate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 1 && instance.snapshot().value().state == InstanceState::Active);
    CHECK(instance.operation().value().id != initial.id);
    CHECK(kernel.services().snapshot().value()[0].committedLeases == 1);
    auto stopped = first.stop();
    CHECK(!old->withService([](int&) {}).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.isReady() && events == std::vector<std::string>{"consumer read open"});
    CHECK(kernel.services().snapshot().value()[0].state == BindingState::Retiring);
    cleanupGate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK((events == std::vector<std::string>{"consumer read open", "consumer cleaned", "provider closed"}));
    CHECK(stopped.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
    CHECK(instance.snapshot().value().state == InstanceState::Pending);
    auto replacement = kernel.mount(provider).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 2 && instance.snapshot().value().epoch == 2);
    CHECK(kernel.services().snapshot().value()[0].id != firstBinding);
    CHECK(!old->withService([](int&) {}).hasValue());
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(closed.tryResult()->value().quiescent && replacement.stop().isReady());
}

/** @brief 传递依赖关闭按 C→B→A 清理，不受挂载顺序或全局 LIFO 支配。 */
void transitive()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    std::vector<int> order;
    auto a = kernel.registerPlugin("A", [&](Context context) {
        CHECK(context.provide({"a"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { order.push_back(1); return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"a"})}).value();
    auto b = kernel.registerPlugin("B", [&](Context context) {
        auto service = context.consume<int>({"a"}).value();
        CHECK(context.provide({"b"}, std::make_shared<int>(2)).hasValue());
        CHECK(context.onCleanup([&, service](CleanupContext cleanup) {
            CHECK(cleanup.withService(service, [](int& value) { CHECK(value == 1); }).hasValue());
            order.push_back(2); return completedTask();
        }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})}).value();
    auto c = kernel.registerPlugin("C", [&](Context context) {
        auto service = context.consume<int>({"b"}).value();
        CHECK(context.onCleanup([&, service](CleanupContext cleanup) {
            CHECK(cleanup.withService(service, [](int& value) { CHECK(value == 2); }).hasValue());
            return gate.task().thenOn(scheduler, [&](const Result<void>&) { order.push_back(3); });
        }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"b"})}).value();
    CHECK(kernel.mount(a).hasValue());
    CHECK(kernel.mount(b).hasValue());
    CHECK(kernel.mount(c).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!closed.isReady() && order.empty());
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK((order == std::vector<int>{3, 2, 1}));
    CHECK(closed.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
}

/** @brief 等待消费者退出时重复更新配置，不得提前关闭提供者资源。 */
void updateWhileDraining()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    bool providerCleaned = false;
    int activations = 0;
    auto p = kernel.registerPlugin("provider", [&](Context context) {
        ++activations;
        CHECK(context.provide({"updating"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { providerCleaned = true; return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"updating"})}).value();
    auto c = kernel.registerPlugin("consumer", [&](Context context) {
        auto service = context.consume<int>({"updating"}).value();
        CHECK(context.onCleanup([&, service](CleanupContext cleanup) {
            return gate.task().thenOn(scheduler, [&, cleanup, service](const Result<void>& result) {
                CHECK(result.hasValue() && !providerCleaned);
                CHECK(cleanup.withService(service, [](int& value) { CHECK(value == 1); }).hasValue());
            });
        }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"updating"})}).value();
    auto provider = kernel.mount(p).value();
    auto consumer = kernel.mount(c).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto first = provider.update("first", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!providerCleaned && !first.target.isReady());
    auto second = provider.update("latest", 2).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!providerCleaned && activations == 1 && !second.target.isReady());
    CHECK(first.target.tryResult()->value().outcome == StableOutcome::Superseded);
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(providerCleaned && activations == 2);
    CHECK(second.target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(provider.snapshot().value().revision == 3);
    CHECK(consumer.snapshot().value().state == InstanceState::Active);
    // 新代次清理不再检查旧代次的资源窗口。
    providerCleaned = false;
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 冲突输出和启动失败均回滚，Staged 从未唤醒消费者。 */
void rollback()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    auto provider = kernel.registerPlugin("provider", [](Context context) {
        (void)context.provide({"unique"}, std::make_shared<int>(1));
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"unique"})}).value();
    auto first = kernel.mount(provider).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto conflict = kernel.mount(provider).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(conflict.ready().tryResult()->error().code == ErrorCode::ServiceConflict);
    CHECK(conflict.ready().tryResult()->error().instance == first.snapshot().value().instance);
    CHECK(conflict.ready().tryResult()->error().message.find("unique") != std::string::npos);
    CHECK(conflict.snapshot().value().state == InstanceState::Failed);
    CHECK(first.snapshot().value().state == InstanceState::Active);
    auto failed = kernel.registerPlugin("failed", [](Context context) {
        CHECK(context.provide({"rollback"}, std::make_shared<int>(2)).hasValue());
        return failedTask(makeError(ErrorCode::ActivationFailed, "setup failed"));
    }, {}, {ServiceOutput::of<int>({"rollback"})}).value();
    auto bad = kernel.mount(failed).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(bad.snapshot().value().state == InstanceState::Failed);
    CHECK(kernel.services().snapshot().value().size() == 1);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 实际依赖图拒绝动态回边，私有 realm 的同名服务保持独立。 */
void cycleAndRealm()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    auto realm = kernel.services().createRealm().value();
    std::optional<Context> aContext;
    auto a = kernel.registerPlugin("A", [&](Context context) {
        aContext.emplace(context);
        CHECK(context.provide({"a"}, std::make_shared<int>(1)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"a"})}).value();
    auto b = kernel.registerPlugin("B", [](Context context) {
        CHECK(context.provide({"b"}, std::make_shared<int>(2)).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})}).value();
    auto isolated = kernel.registerPlugin("isolated", [realm](Context context) {
        CHECK(context.provide({"a", 1, realm}, std::make_shared<int>(7)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"a", 1, realm})}).value();
    auto ai = kernel.mount(a).value();
    auto bi = kernel.mount(b).value();
    auto ii = kernel.mount(isolated).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(aContext->consume<int>({"b"}).error().code == ErrorCode::DependencyCycle);
    CHECK(ai.snapshot().value().committedLeases == 0);
    auto stopped = ai.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.tryResult()->value().quiescent && bi.snapshot().value().state == InstanceState::Pending);
    CHECK(ii.snapshot().value().state == InstanceState::Active);
    CHECK(kernel.services().find<int>({"a", 1, realm}).hasValue());
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 下游清理失败不能提前清理上游或伪报 provider 已终结。 */
void blockedCleanup()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    bool providerCleaned = false;
    auto p = kernel.registerPlugin("provider", [&](Context context) {
        CHECK(context.provide({"blocked"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { providerCleaned = true; return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"blocked"})}).value();
    auto c = kernel.registerPlugin("consumer", [](Context context) {
        CHECK(context.onCleanup([](CleanupContext) { return failedTask(makeError(ErrorCode::CleanupFailed, "consumer retained")); }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"blocked"})}).value();
    auto provider = kernel.mount(p).value();
    auto consumer = kernel.mount(c).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = provider.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!stopped.isReady() && !providerCleaned);
    CHECK(!consumer.stop().tryResult());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!consumer.stop().tryResult()->value().quiescent);
    CHECK(kernel.services().snapshot().value()[0].committedLeases == 1);
    CHECK(kernel.services().snapshot().value()[0].state == BindingState::Retiring);
}

/** @brief 已停止的 Pending 实例不因服务出现复活，声明类型不匹配明确失败。 */
void pendingStopAndType()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    int activations = 0;
    auto c = kernel.registerPlugin("pending", [&](Context) { ++activations; return completedTask(); },
        {ServiceRequirement::of<int>({"later"})}).value();
    auto consumer = kernel.mount(c).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto stopped = consumer.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    auto p = kernel.registerPlugin("provider", [](Context context) {
        CHECK(context.provide({"later"}, std::make_shared<double>(1)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<double>({"later"})}).value();
    CHECK(kernel.mount(p).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 0 && consumer.snapshot().value().state == InstanceState::Disposed);
    auto wrong = kernel.mount(c).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(activations == 0 && wrong.ready().tryResult()->error().code == ErrorCode::DependencyVersion);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 根据场景名执行托管依赖闭环。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "recovery") recovery();
        else if (name == "transitive") transitive();
        else if (name == "update_draining") updateWhileDraining();
        else if (name == "rollback") rollback();
        else if (name == "cycle_realm") cycleAndRealm();
        else if (name == "blocked_cleanup") blockedCleanup();
        else if (name == "pending_stop_type") pendingStopAndType();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
