/** @file children.cpp
 * @brief 验证可选依赖子实例、父代屏障与所有权联合环诊断。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 可选子能力独立 Pending/恢复，不重启父代，父停止后不能恢复或再注入。 */
void optionalRecovery()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    int parents = 0, children = 0;
    std::optional<Context> parentContext;
    std::optional<Instance> child;
    Runtime optional([&](Context context) {
        CHECK(context.config() == "child config");
        ++children;
        return completedTask();
    }, {ServiceRequirement::of<int>({"optional"})});
    auto runtime = kernel.registerPlugin("parent", [&](Context context) {
        ++parents;
        parentContext.emplace(context);
        child.emplace(context.inject(optional, "child config").value());
        return completedTask();
    }).value();
    auto parent = kernel.mount(runtime).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(parent.snapshot().value().state == InstanceState::Active && parents == 1 && children == 0);
    CHECK(parent.snapshot().value().children == 1 && child->snapshot().value().state == InstanceState::Pending);
    auto provider = kernel.services().stage({"optional"}, std::make_shared<int>(1)).value();
    CHECK(provider.publish().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue() && children == 1);
    CHECK(provider.retire().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(provider.release().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(child->snapshot().value().state == InstanceState::Pending && parents == 1);
    auto stopped = parent.stop();
    CHECK(!parentContext->inject(optional).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    auto replacement = kernel.services().stage({"optional"}, std::make_shared<int>(2)).value();
    CHECK(replacement.publish().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(children == 1 && child->snapshot().value().state == InstanceState::Disposed);
    CHECK(parent.snapshot().value().children == 0);
    CHECK(replacement.retire().hasValue() && replacement.release().hasValue());
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 父拥有 A/B/C，关闭先广播全部子实例，再按依赖 C→B→A 及父清理。 */
void orderedShutdown()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    std::vector<int> order;
    std::vector<Instance> children;
    Runtime a([&](Context context) {
        CHECK(context.provide({"a"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { order.push_back(1); return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"a"})});
    Runtime b([&](Context context) {
        CHECK(context.provide({"b"}, std::make_shared<int>(2)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { order.push_back(2); return completedTask(); }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})});
    Runtime c([&](Context context) {
        auto service = context.consume<int>({"b"}).value();
        CHECK(context.onCleanup([&, service](CleanupContext cleanup) {
            return gate.task().thenOn(scheduler, [&, cleanup, service](const Result<void>& result) {
                CHECK(result.hasValue());
                CHECK(cleanup.withService(service, [](int& value) { CHECK(value == 2); }).hasValue());
                order.push_back(3);
            });
        }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"b"})});
    auto p = kernel.registerPlugin("parent", [&](Context context) {
        // 消费者先登记，确保顺序由依赖图而非创建顺序决定。
        children.push_back(context.inject(c).value());
        children.push_back(context.inject(a).value());
        children.push_back(context.inject(b).value());
        CHECK(context.onCleanup([&](CleanupContext) { order.push_back(0); return completedTask(); }).hasValue());
        return completedTask();
    }).value();
    auto parent = kernel.mount(p).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    for (const auto& child : children) CHECK(child.snapshot().value().state == InstanceState::Active);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(order.empty() && !closed.isReady());
    CHECK(parent.snapshot().value().children == 3);
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK((order == std::vector<int>{3, 2, 1, 0}));
    CHECK(closed.tryResult()->value().quiescent && kernel.services().snapshot().value().empty());
    for (const auto& child : children) CHECK(child.snapshot().value().state == InstanceState::Disposed);
}

/** @brief 子可依赖父服务，父不能反向消费子输出；错误包含每一条所有权和服务边。 */
void ownershipCycle()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    std::optional<Context> parentContext;
    std::optional<Instance> child;
    Runtime leaf([](Context context) {
        CHECK(context.provide({"leaf"}, std::make_shared<int>(2)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"leaf"})});
    Runtime middle([&](Context context) {
        CHECK(context.consume<int>({"parent"}).hasValue());
        child.emplace(context.inject(leaf).value());
        return completedTask();
    }, {ServiceRequirement::of<int>({"parent"})});
    auto p = kernel.registerPlugin("parent", [&](Context context) {
        parentContext.emplace(context);
        CHECK(context.provide({"parent"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.inject(middle).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"parent"})}).value();
    auto parent = kernel.mount(p).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(child->snapshot().value().state == InstanceState::Active);
    auto rejected = parentContext->consume<int>({"leaf"});
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::DependencyCycle);
    CHECK(rejected.error().causes.size() == 3);
    CHECK(rejected.error().message.find("service leaf/v1") != std::string::npos);
    CHECK(rejected.error().message.find("--parent-->") != std::string::npos);
    CHECK(parent.snapshot().value().committedLeases == 0);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 父更新等待子清理后创建新子代，派生视图继承且旧 Context 不得注入。 */
void updateAndRealms()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    const auto realm = kernel.services().createRealm().value();
    Promise<void> gate;
    std::optional<Context> old;
    std::vector<Instance> children;
    int calls = 0;
    Runtime optional([&](Context context) {
        auto service = context.consume<int>({"private"}).value();
        CHECK(service.withService([](int& value) { CHECK(value == 42); }).hasValue());
        ++calls;
        CHECK(context.onCleanup([&](CleanupContext) { return gate.task(); }).hasValue());
        return completedTask();
    }, {ServiceRequirement::of<int>({"private"})});
    auto p = kernel.registerPlugin("parent", [&](Context context) {
        if (!old) old.emplace(context);
        children.push_back(context.isolate("private", realm).value().inject(optional).value());
        return completedTask();
    }).value();
    auto host = kernel.services().stage({"private", 1, realm}, std::make_shared<int>(42)).value();
    CHECK(host.publish().hasValue());
    auto parent = kernel.mount(p).value();
    CHECK(scheduler->runUntilIdle().hasValue() && calls == 1);
    auto changed = parent.update("next", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 1 && !changed.target.isReady() && !old->inject(optional).hasValue());
    gate.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 2 && children[0].snapshot().value().state == InstanceState::Disposed);
    CHECK(changed.target.tryResult()->value().outcome == StableOutcome::Active);
    CHECK(old->inject(optional).error().code == ErrorCode::StaleContext);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
    CHECK(host.retire().hasValue() && host.release().hasValue());
}

/** @brief 子清理失败保留父资源，错误传播至父停止，后到停止及更新不能绕过屏障。 */
void failedChild()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    bool parentCleaned = false;
    Runtime child([](Context context) {
        CHECK(context.onCleanup([](CleanupContext) { return failedTask(makeError(ErrorCode::CleanupFailed, "child retained")); }).hasValue());
        return completedTask();
    });
    auto p = kernel.registerPlugin("parent", [&](Context context) {
        CHECK(context.inject(child).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { parentCleaned = true; return completedTask(); }).hasValue());
        return completedTask();
    }).value();
    auto parent = kernel.mount(p).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto changed = parent.update("next", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(changed.target.tryResult()->value().outcome == StableOutcome::Failed && !parentCleaned);
    CHECK(parent.snapshot().value().failure->code == ErrorCode::CleanupFailed);
    CHECK(parent.update("bypass", 2).error().code == ErrorCode::CleanupFailed);
    auto stopped = parent.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(stopped.isReady() && !stopped.tryResult()->value().quiescent && !parentCleaned);
    CHECK(stopped.tryResult()->value().errors[0].message == "child retained");
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && !closed.tryResult()->value().quiescent);
}

/** @brief 无效 Runtime 和错误线程不登记子实例，启动中停止也阻止后续注入。 */
void injectionGuards()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Runtime child([](Context) { return completedTask(); });
    Runtime moved(std::move(child));
    std::optional<Context> context;
    Runtime p([&](Context current) { context.emplace(current); return completedTask(); });
    auto parent = Instance::mount(p, scheduler);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(context->inject(child).error().code == ErrorCode::InvalidArgument);
    CHECK(parent.snapshot().value().children == 0);
    bool wrong = false;
    std::thread worker([&] { wrong = context->inject(moved).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong && parent.snapshot().value().children == 0);
    auto stopped = parent.stop();
    CHECK(!context->inject(moved).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
}

/** @brief 激活中注入后重入停止，子实例不启动，父仍等待真实 setup 完成。 */
void reentrantStop()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Promise<void> setup;
    std::optional<Instance> parent, child;
    int calls = 0;
    bool cleaned = false;
    Runtime optional([&](Context) { ++calls; return completedTask(); });
    Runtime runtime([&](Context context) {
        CHECK(context.onCleanup([&](CleanupContext) { cleaned = true; return completedTask(); }).hasValue());
        child.emplace(context.inject(optional).value());
        CHECK(!parent->stop().isReady());
        CHECK(!context.inject(optional).hasValue());
        return setup.task();
    });
    parent.emplace(Instance::mount(runtime, scheduler));
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(calls == 0 && !parent->stop().isReady());
    CHECK(child->snapshot().value().state == InstanceState::Disposed);
    setup.resolve();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(parent->stop().tryResult()->value().quiescent && cleaned && calls == 0);
}

/** @brief 三个尚未激活的声明形成环时输出完整路径，队列有限且不运行插件。 */
void declaredCycle()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    int calls = 0;
    const auto activate = [&](Context) { ++calls; return completedTask(); };
    Runtime a(activate, {ServiceRequirement::of<int>({"b"})}, {ServiceOutput::of<int>({"a"})});
    Runtime b(activate, {ServiceRequirement::of<int>({"c"})}, {ServiceOutput::of<int>({"b"})});
    Runtime c(activate, {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"c"})});
    auto services = std::make_shared<ServiceRegistry>(kernel.services());
    auto ai = Instance::mount(a, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue() && ai.snapshot().value().state == InstanceState::Pending);
    const auto initialA = ai.operation().value();
    auto bi = Instance::mount(b, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue() && bi.snapshot().value().state == InstanceState::Pending);
    auto ci = Instance::mount(c, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue() && calls == 0);
    CHECK(ai.operation().value().id != initialA.id);
    CHECK(ai.awaitTarget(initialA).tryResult()->value().outcome == StableOutcome::PendingMissingDependency);
    for (const auto& instance : {ai, bi, ci}) {
        const auto snapshot = instance.snapshot().value();
        CHECK(snapshot.state == InstanceState::Failed && snapshot.epoch == 0);
        CHECK(snapshot.failure->code == ErrorCode::DependencyCycle && snapshot.failure->causes.size() == 3);
        CHECK(instance.operation().value().target.tryResult()->value().outcome == StableOutcome::Failed);
        for (const auto& name : {"a/v1", "b/v1", "c/v1"}) CHECK(snapshot.failure->message.find(name) != std::string::npos);
        CHECK(snapshot.failure->message.find("realm=1") != std::string::npos);
        instance.stop();
    }
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(ai.stop().tryResult()->value().quiescent && bi.stop().tryResult()->value().quiescent && ci.stop().tryResult()->value().quiescent);
    CHECK(kernel.close().tryResult()->value().quiescent);
}

/** @brief 自依赖只在精确域匹配时判环，移除声明后 retry 保持正常缺依赖语义。 */
void declarationBoundaries()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto services = std::make_shared<ServiceRegistry>(scheduler);
    const auto realm = services->createRealm().value();
    Runtime self([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"self"})}, {ServiceOutput::of<int>({"self"})});
    auto failed = Instance::mount(self, scheduler, {}, services);
    Runtime split([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"split"})}, {ServiceOutput::of<int>({"split", 1, realm})});
    auto pending = Instance::mount(split, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(failed.snapshot().value().failure->causes.size() == 1);
    CHECK(pending.snapshot().value().state == InstanceState::Pending);
    Runtime a([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"b"})}, {ServiceOutput::of<int>({"a"})});
    Runtime b([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})});
    auto ai = Instance::mount(a, scheduler, {}, services);
    auto bi = Instance::mount(b, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(ai.snapshot().value().state == InstanceState::Failed);
    bi.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    auto retried = ai.retry().value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(retried.target.tryResult()->value().outcome == StableOutcome::PendingMissingDependency);
    CHECK(!ai.snapshot().value().failure);
    Runtime ambiguous([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"ambiguous"})}, {ServiceOutput::of<int>({"ambiguous"})});
    auto first = Instance::mount(ambiguous, scheduler, {}, services);
    auto second = Instance::mount(ambiguous, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(first.snapshot().value().state == InstanceState::Pending && second.snapshot().value().state == InstanceState::Pending);
    first.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(second.snapshot().value().failure->code == ErrorCode::DependencyCycle);
    second.stop();
    for (const auto& instance : {failed, pending, ai}) instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(services->snapshot().value().empty());
}

/** @brief Pending 后出现不兼容提供者时，新失败操作与历史 Pending 结果分别可查。 */
void pendingFailureOperation()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    auto services = std::make_shared<ServiceRegistry>(scheduler);
    int calls = 0;
    Runtime runtime([&](Context) { ++calls; return completedTask(); }, {ServiceRequirement::of<int>({"late"})});
    auto instance = Instance::mount(runtime, scheduler, {}, services);
    CHECK(scheduler->runUntilIdle().hasValue());
    const auto pending = instance.operation().value();
    CHECK(pending.target.tryResult()->value().outcome == StableOutcome::PendingMissingDependency);
    auto provider = services->stage({"late"}, std::make_shared<double>(1)).value();
    CHECK(provider.publish().hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    const auto failed = instance.operation().value();
    CHECK(failed.id != pending.id);
    CHECK(failed.revision == pending.revision && failed.target.tryResult()->value().epoch == 0);
    CHECK(instance.awaitTarget(failed).tryResult()->value().outcome == StableOutcome::Failed);
    CHECK(instance.awaitTarget(pending).tryResult()->value().outcome == StableOutcome::PendingMissingDependency);
    CHECK(instance.snapshot().value().failure->code == ErrorCode::DependencyVersion && calls == 0);
    CHECK(provider.retire().hasValue() && provider.release().hasValue());
    auto stopped = instance.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
}

/** @brief 按名称运行父子实例与可选依赖测试。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "optional") optionalRecovery();
        else if (name == "ordered") orderedShutdown();
        else if (name == "cycle") ownershipCycle();
        else if (name == "update_realms") updateAndRealms();
        else if (name == "failure") failedChild();
        else if (name == "guards") injectionGuards();
        else if (name == "reentrant_stop") reentrantStop();
        else if (name == "declared_cycle") declaredCycle();
        else if (name == "declaration_boundaries") declarationBoundaries();
        else if (name == "pending_failure") pendingFailureOperation();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
