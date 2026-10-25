/** @file kernel_driver.cpp
 * @brief 使用真实 Kernel、服务租约及显式 gate 验证配置驱动，不以受控驱动冒充插件运行。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/testing/fake_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace cordis;
using namespace cordis::loader;
using cordis::testing::FakeScheduler;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

/** @brief 每个场景拥有独立 Kernel，所有配置先经过真实解析与 schema/定义联合校验。 */
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::vector<KernelPlugin> plugins;
    std::vector<PluginDefinition> definitions;
    std::shared_ptr<KernelReconcileDriver> driver;
    std::unique_ptr<ConfigurationCoordinator> coordinator;
    std::uint64_t revision = 0;
    /** @brief schema 的值声明与实际 Runtime 类型声明分别交给校验层与真实驱动。 */
    Runtime add(const std::string& id, Runtime::Activate activate, std::vector<ServiceRequirement> needs = {},
                std::vector<ServiceOutput> outputs = {}, std::vector<ServiceRequirement> configurable = {})
    {
        auto runtime = kernel->registerPlugin(id, std::move(activate), needs, std::move(outputs)).value();
        plugins.push_back({id, runtime, std::move(configurable)});
        std::vector<InjectSpec> inject;
        for (const auto& need : needs)
            inject.push_back({need.key.name, need.key.interfaceMajor, need.minimumMinor, need.capabilities});
        definitions.push_back({id, std::move(inject), [](const std::string&) { return Result<void>::success(); }});
        return runtime;
    }
    /** @brief 驱动构造不挂载实例，测试显式启动协调器。 */
    void start(std::map<std::string, RealmId> realms = {})
    {
        driver = std::make_shared<KernelReconcileDriver>(scheduler, kernel, plugins, std::move(realms));
        coordinator = std::make_unique<ConfigurationCoordinator>(scheduler, driver);
    }
    /** @brief 传入 plugins 数组，规范配置值由解析器确定。 */
    ValidatedCandidate target(const std::string& entries)
    {
        return validateCandidate(parseConfiguration("formatVersion: 1\nplugins: " + entries + "\n").value(),
                                 definitions)
            .value();
    }
    /** @brief 逐次使用真实 CAS 修订接受目标。 */
    AcceptedTarget accept(const std::string& entries)
    {
        auto accepted = coordinator->accept(target(entries), revision).value();
        revision = accepted.revision;
        return accepted;
    }
    /** @brief 只推进显式队列，不用时间推测异步操作已经结束。 */
    void pump() { CHECK(scheduler->runUntilIdle(30000).hasValue()); }
    /** @brief 协调器先排空所拥有资源，再关闭 Kernel 和调度器。 */
    void close()
    {
        auto stopped = coordinator->close();
        pump();
        CHECK(stopped.isReady() && stopped.tryResult()->hasValue() && stopped.tryResult()->value().quiescent);
        CHECK(driver->snapshot().value().empty());
        auto all = kernel->close();
        pump();
        CHECK(all.isReady() && all.tryResult()->value().quiescent);
        scheduler->close();
    }
};

/** @brief 配置修改重用实例并推进 epoch；禁用后重新启用创建新实例，NoOp 不激活。 */
void lifecycle()
{
    Scene scene;
    int activated = 0, cleaned = 0;
    scene.add("notes", [&](Context context) {
        ++activated;
        context
            .onCleanup([&](CleanupContext) {
                ++cleaned;
                return completedTask();
            })
            .value();
        return completedTask();
    });
    scene.start();
    CHECK(scene.kernel->snapshot().value().instances.empty());
    auto a = scene.accept("[{id: n, plugin: notes, config: one}]");
    scene.pump();
    CHECK(a.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto first = scene.driver->snapshot().value().front();
    CHECK(activated == 1 && first.state == InstanceState::Active);
    auto same = scene.accept("[{id: n, plugin: notes, config: one}]");
    scene.pump();
    CHECK(same.settled.tryResult()->value().entries.front().operation.kind == ChangeKind::NoOp && activated == 1);
    auto changed = scene.accept("[{id: n, plugin: notes, config: two}]");
    scene.pump();
    auto second = scene.driver->snapshot().value().front();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied && second.instance == first.instance &&
          second.epoch > first.epoch && activated == 2 && cleaned == 1);
    auto disabled = scene.accept("[{id: n, plugin: notes, disabled: true}]");
    scene.pump();
    CHECK(disabled.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          scene.driver->snapshot().value().empty());
    auto enabled = scene.accept("[{id: n, plugin: notes}]");
    scene.pump();
    CHECK(enabled.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          scene.driver->snapshot().value().front().instance != first.instance);
    CHECK(scene.kernel->snapshot().value().runtimes == 1);
    scene.close();
    CHECK(cleaned == activated);
}

/** @brief 字典序相反的真实提供者/消费者按依赖启动，清理时 committed 服务仍能访问。 */
void dependencies()
{
    Scene scene;
    std::vector<std::string> log;
    scene.add("provider",
              [&](Context context) {
                  log.push_back("provider+");
                  context.provide<int>({"value"}, std::make_shared<int>(42)).value();
                  context
                      .onCleanup([&](CleanupContext) {
                          log.push_back("provider-");
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"value"})});
    scene.add("consumer",
              [&](Context context) {
                  auto ref = context.consume<int>({"value"}).value();
                  ref.withService([&](int& value) {
                         CHECK(value == 42);
                         log.push_back("consumer+");
                     })
                      .value();
                  context
                      .onCleanup([&, ref](CleanupContext cleanup) {
                          cleanup
                              .withService(ref,
                                           [&](int& value) {
                                               CHECK(value == 42);
                                               log.push_back("consumer-");
                                           })
                              .value();
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"value"})});
    scene.start();
    auto target = scene.accept("[{id: a, plugin: consumer}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(target.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          log == std::vector<std::string>({"provider+", "consumer+"}));
    CHECK(target.settled.tryResult()->value().entries.front().operation.path == "z");
    auto empty = scene.accept("[]");
    scene.pump();
    CHECK(empty.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          empty.settled.tryResult()->value().entries.front().operation.path == "a");
    CHECK(log == std::vector<std::string>({"provider+", "consumer+", "consumer-", "provider-"}));
    scene.close();
}

/** @brief 缺依赖形成真实 Pending，补提供者后原消费者自动恢复，历史 Pending 不改写。 */
void pending()
{
    Scene scene;
    int calls = 0;
    scene.add("consumer",
              [&](Context context) {
                  context.consume<int>({"s"}).value();
                  ++calls;
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"s"})});
    scene.add("provider",
              [](Context context) {
                  context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"s"})});
    scene.start();
    auto missing = scene.accept("[{id: a, plugin: consumer}]");
    scene.pump();
    CHECK(missing.settled.tryResult()->value().outcome == PlanOutcome::Pending && calls == 0);
    auto original = scene.driver->snapshot().value().front().instance;
    auto recovered = scene.accept("[{id: a, plugin: consumer}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(recovered.settled.tryResult()->value().outcome == PlanOutcome::Applied && calls == 1);
    CHECK(scene.driver->snapshot().value().front().instance == original &&
          missing.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    scene.close();
}

/** @brief A 激活在途时 B/C 覆盖，B 不运行，C 从真实 A 结果继续更新。 */
void latest()
{
    Scene scene;
    Promise<void> gate;
    std::vector<std::string> configs;
    scene.add("demo", [&](Context context) {
        configs.push_back(context.config());
        return configs.size() == 1 ? gate.task() : completedTask();
    });
    scene.start();
    auto a = scene.accept("[{id: a, plugin: demo, config: A}]");
    scene.pump();
    CHECK(!a.settled.isReady());
    auto b = scene.accept("[{id: a, plugin: demo, config: B}]");
    auto c = scene.accept("[{id: a, plugin: demo, config: C}]");
    scene.pump();
    CHECK(b.settled.tryResult()->value().outcome == PlanOutcome::Superseded && configs.size() == 1);
    gate.resolve();
    scene.pump();
    CHECK(a.settled.tryResult()->value().outcome == PlanOutcome::Superseded &&
          c.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(configs == std::vector<std::string>({"\"A\"", "\"C\""}));
    scene.close();
}

/** @brief 失败激活不回滚目标，重新接受相同目标走真实 retry 并保留实例身份。 */
void retry()
{
    Scene scene;
    int attempts = 0;
    scene.add("demo", [&](Context) {
        return ++attempts == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "first attempt")) : completedTask();
    });
    scene.start();
    auto first = scene.accept("[{id: a, plugin: demo}]");
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Failed &&
          !scene.coordinator->snapshot().value().unsafe);
    auto original = scene.driver->snapshot().value().front();
    auto second = scene.accept("[{id: a, plugin: demo}]");
    scene.pump();
    auto current = scene.driver->snapshot().value().front();
    CHECK(second.settled.tryResult()->value().outcome == PlanOutcome::Applied && attempts == 2 &&
          current.instance == original.instance && current.epoch > original.epoch);
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Failed);
    scene.close();
}

/** @brief 清理失败保留实际账目、冻结协调器，关闭不虚报资源已经释放。 */
void cleanupFailure()
{
    Scene scene;
    scene.add("demo", [](Context context) {
        context
            .onCleanup(
                [](CleanupContext) { return failedTask(makeError(ErrorCode::CleanupFailed, "retained resource")); })
            .value();
        return completedTask();
    });
    scene.start();
    scene.accept("[{id: a, plugin: demo}]");
    scene.pump();
    auto removed = scene.accept("[]");
    scene.pump();
    CHECK(removed.settled.tryResult()->value().outcome == PlanOutcome::Failed &&
          scene.coordinator->snapshot().value().unsafe);
    CHECK(!scene.driver->snapshot().value().empty());
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(closed.isReady() && closed.tryResult()->hasValue() && !closed.tryResult()->value().quiescent);
    auto kernel = scene.kernel->close();
    scene.pump();
    CHECK(kernel.isReady() && !kernel.tryResult()->value().quiescent);
    scene.scheduler->close();
}

/** @brief 关闭不等 activate 完成才广播 stop，源任务真正结束后才确认排空。 */
void shutdown()
{
    Scene scene;
    Promise<void> gate;
    scene.add("demo", [gate](Context) { return gate.task(); });
    scene.start();
    auto accepted = scene.accept("[{id: a, plugin: demo}]");
    scene.pump();
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(!closed.isReady());
    CHECK(!scene.coordinator->accept(scene.target("[]"), scene.revision).hasValue());
    gate.resolve();
    scene.pump();
    CHECK(closed.isReady() && closed.tryResult()->value().quiescent &&
          accepted.settled.tryResult()->value().outcome == PlanOutcome::Cancelled);
    scene.close();
}

/** @brief realm 变化先完整停止旧实例，再以宿主映射的新精确域挂载。 */
void realms()
{
    Scene scene;
    Promise<void> cleanup;
    int calls = 0;
    scene.add("provider",
              [&](Context context) {
                  context.provide<int>({"s"}, std::make_shared<int>(++calls)).value();
                  if (calls == 1)
                      context.onCleanup([cleanup](CleanupContext) { return cleanup.task(); }).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"s"})});
    const auto one = scene.kernel->services().createRealm().value();
    const auto two = scene.kernel->services().createRealm().value();
    scene.start({{"one", one}, {"two", two}});
    auto first = scene.accept("[{id: a, plugin: provider, realms: {s: one}}]");
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto original = scene.driver->snapshot().value().front();
    auto changed = scene.accept("[{id: a, plugin: provider, realms: {s: two}}]");
    scene.pump();
    CHECK(!changed.settled.isReady() && calls == 1);
    cleanup.resolve();
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied && calls == 2 &&
          scene.driver->snapshot().value().front().instance != original.instance);
    auto bindings = scene.kernel->services().snapshot().value();
    CHECK(std::any_of(bindings.begin(), bindings.end(), [two](const BindingSnapshot& binding) {
        return binding.key.realm == two && binding.state == BindingState::Published;
    }));
    scene.close();

    Scene cancelled;
    Promise<void> retiring;
    int activations = 0;
    cancelled.add("p", [&](Context context) {
        ++activations;
        context.onCleanup([retiring](CleanupContext) { return retiring.task(); }).value();
        return completedTask();
    });
    cancelled.start({{"one", cancelled.kernel->services().createRealm().value()},
                     {"two", cancelled.kernel->services().createRealm().value()}});
    cancelled.accept("[{id: a, plugin: p, realms: {s: one}}]");
    cancelled.pump();
    auto recreating = cancelled.accept("[{id: a, plugin: p, realms: {s: two}}]");
    cancelled.pump();
    CHECK(!recreating.settled.isReady());
    auto closing = cancelled.coordinator->close();
    cancelled.pump();
    CHECK(!closing.isReady());
    retiring.resolve();
    cancelled.pump();
    CHECK(closing.tryResult()->value().quiescent && activations == 1 &&
          recreating.settled.tryResult()->value().outcome == PlanOutcome::Cancelled);
    cancelled.close();
}

/** @brief 未支持的配置在接受前拒绝，不调用插件；输出冲突和类型不匹配不猜测。 */
void support()
{
    Scene scene;
    int calls = 0;
    scene.add("demo", [&](Context) {
        ++calls;
        return completedTask();
    });
    scene.start();
    for (auto text : {"[{id: a, plugin: demo, realms: {s: missing}}]", "[{id: a, plugin: demo, inject: [extra/v1]}]"}) {
        auto result = scene.coordinator->accept(scene.target(text), 0);
        CHECK(!result.hasValue());
    }
    CHECK(calls == 0 && scene.kernel->snapshot().value().instances.empty() &&
          scene.coordinator->snapshot().value().desiredRevision == 0);
    scene.close();
    Scene duplicate;
    duplicate.add("p", [](Context) { return completedTask(); }, {}, {ServiceOutput::of<int>({"s"})});
    duplicate.start();
    CHECK(!duplicate.coordinator->accept(duplicate.target("[{id: a, plugin: p}, {id: b, plugin: p}]"), 0).hasValue());
    duplicate.close();
    Scene types;
    types.add("p", [](Context) { return completedTask(); }, {}, {ServiceOutput::of<int>({"s"})});
    types.add("c", [](Context) { return completedTask(); }, {ServiceRequirement::of<std::string>({"s"})});
    types.start();
    CHECK(!types.coordinator->accept(types.target("[{id: a, plugin: p}, {id: b, plugin: c}]"), 0).hasValue());
    types.close();
    Scene versions;
    versions.add("p", [](Context) { return completedTask(); }, {},
                 {ServiceOutput::of<int>({"s"}, true, {1, {"read"}})});
    versions.add("c", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"s"}, 2, {"write"})});
    versions.start();
    CHECK(!versions.coordinator->accept(versions.target("[{id: a, plugin: p}, {id: b, plugin: c}]"), 0).hasValue());
    versions.close();
    Scene cycle;
    cycle.add("a", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"b"})},
              {ServiceOutput::of<int>({"a"})});
    cycle.add("b", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})},
              {ServiceOutput::of<int>({"b"})});
    cycle.start();
    auto rejected = cycle.coordinator->accept(cycle.target("[{id: a, plugin: a}, {id: b, plugin: b}]"), 0);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::DependencyCycle &&
          cycle.kernel->snapshot().value().instances.empty());
    cycle.close();
}

/** @brief 输出所有权跨路径交换要求显式移除阶段，拒绝在两个仍启用实例间盲目交换。 */
void ownership()
{
    Scene scene;
    scene.add("p",
              [](Context context) {
                  context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"s"})});
    scene.start({{"one", scene.kernel->services().createRealm().value()},
                 {"two", scene.kernel->services().createRealm().value()}});
    auto first = scene.accept("[{id: a, plugin: p, realms: {s: one}}, {id: b, plugin: p, realms: {s: two}}]");
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto rejected = scene.coordinator->accept(
        scene.target("[{id: a, plugin: p, realms: {s: two}}, {id: b, plugin: p, realms: {s: one}}]"), scene.revision);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigConflict &&
          scene.driver->snapshot().value().size() == 2);
    scene.accept("[]");
    scene.pump();
    auto applied = scene.accept("[{id: a, plugin: p, realms: {s: two}}, {id: b, plugin: p, realms: {s: one}}]");
    scene.pump();
    CHECK(applied.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    scene.close();
}

/** @brief 驱动关闭仅停止自己挂载的实例，不接管共享 Kernel 中宿主已有实例。 */
void ownershipBoundary()
{
    Scene scene;
    auto runtime = scene.add("demo", [](Context) { return completedTask(); });
    auto host = scene.kernel->mount(runtime).value();
    scene.pump();
    scene.start();
    scene.accept("[{id: owned, plugin: demo}]");
    scene.pump();
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(closed.tryResult()->value().quiescent && host.snapshot().value().state == InstanceState::Active);
    scene.close();
}

/** @brief 调度归属、跨线程入口、同一动作串行及错误动作标签均有明确拒绝。 */
void guards()
{
    Scene scene;
    Promise<void> gate;
    auto runtime = scene.add("demo", [gate](Context) { return gate.task(); });
    scene.start();
    auto target = std::make_shared<const ValidatedCandidate>(scene.target("[{id: a, plugin: demo}]"));
    auto absent = scene.driver->apply({"a", ChangeKind::NoOp}, target, 1);
    CHECK(absent.isReady() && !absent.tryResult()->hasValue() && scene.kernel->snapshot().value().instances.empty());
    auto first = scene.driver->apply({"a", ChangeKind::Add}, target, 1);
    scene.pump();
    auto second = scene.driver->apply({"a", ChangeKind::Add}, target, 1);
    CHECK(second.isReady() && !second.tryResult()->hasValue());
    bool wrong = false;
    std::thread worker([&] {
        auto result = scene.driver->snapshot();
        wrong = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
        auto realm = scene.kernel->services().ownsRealm(RealmId(1));
        wrong = wrong && !realm.hasValue() && realm.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    gate.resolve();
    scene.pump();
    CHECK(first.tryResult()->value().outcome == StepOutcome::Applied);
    auto changed = std::make_shared<const ValidatedCandidate>(scene.target("[{id: a, plugin: demo, config: changed}]"));
    auto noop = scene.driver->apply({"a", ChangeKind::NoOp}, changed, 2);
    CHECK(noop.isReady() && !noop.tryResult()->hasValue());
    auto foreign = std::make_shared<Kernel>(scene.scheduler);
    bool rejected = false;
    try {
        KernelReconcileDriver invalid(scene.scheduler, foreign, {{"demo", runtime}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    auto foreignRealm = foreign->services().createRealm().value();
    CHECK(!scene.kernel->services().ownsRealm(foreignRealm).value());
    rejected = false;
    try {
        KernelReconcileDriver invalid(scene.scheduler, scene.kernel, scene.plugins, {{"foreign", foreignRealm}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected && scene.kernel->services().ownsRealm(RealmId(1)).value());
    scene.close();
}

/** @brief 错误地先关调度器后，显式关闭驱动终结在途任务并保守保留实例账目。 */
void delivery()
{
    Scene scene;
    Promise<void> gate;
    scene.add("demo", [gate](Context) { return gate.task(); });
    scene.start();
    auto action = scene.driver->apply(
        {"a", ChangeKind::Add}, std::make_shared<const ValidatedCandidate>(scene.target("[{id: a, plugin: demo}]")), 1);
    scene.pump();
    scene.scheduler->close();
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    auto closed = scene.driver->close();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue() &&
          closed.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(action.isReady() && !action.tryResult()->hasValue() &&
          action.tryResult()->error().code == ErrorCode::SchedulerClosed);
    auto unavailable = scene.driver->snapshot();
    CHECK(!unavailable.hasValue() && unavailable.error().code == ErrorCode::SchedulerClosed &&
          !scene.kernel->snapshot().value().instances.empty());

    // 已排队的驱动 continuation 被丢弃时，线程安全失败门禁直接终结动作及关闭 Promise。
    Scene queued;
    queued.add("demo", [](Context) { return completedTask(); });
    queued.start();
    queued.accept("[{id: a, plugin: demo}]");
    queued.pump();
    auto pending =
        queued.driver->apply({"a", ChangeKind::NoOp},
                             std::make_shared<const ValidatedCandidate>(queued.target("[{id: a, plugin: demo}]")), 2);
    CHECK(!pending.isReady());
    std::thread closer([scheduler = queued.scheduler] { scheduler->close(); });
    closer.join();
    CHECK(pending.isReady() && !pending.tryResult()->hasValue());
    auto rejected = queued.driver->close();
    CHECK(rejected.isReady() && !rejected.tryResult()->hasValue() && !queued.driver->snapshot().value().empty());
}

/** @brief 配置附加依赖参与真实拓扑排序和旧代清理租约，不能仅通过 schema 就提前激活。 */
void injectOrder()
{
    Scene scene;
    std::vector<std::string> log;
    scene.add("consumer",
              [&](Context context) {
                  auto service = context.consume<int>({"extra"}).value();
                  log.push_back("consumer+");
                  context
                      .onCleanup([&, service](CleanupContext cleanup) {
                          cleanup.withService(service, [&](int& value) { CHECK(value == 7); }).value();
                          log.push_back("consumer-");
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {}, {}, {ServiceRequirement::of<int>({"extra"})});
    scene.add("provider",
              [&](Context context) {
                  log.push_back("provider+");
                  context.provide<int>({"extra"}, std::make_shared<int>(7)).value();
                  context
                      .onCleanup([&](CleanupContext) {
                          log.push_back("provider-");
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"extra"}, true, {2, {"read"}})});
    scene.start();
    auto accepted = scene.accept("[{id: a, plugin: consumer, inject: [{name: extra, minimumMinor: 2, capabilities: "
                                 "[read]}]}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          accepted.settled.tryResult()->value().entries.front().operation.path == "z");
    CHECK(log == std::vector<std::string>({"provider+", "consumer+"}));
    auto removed = scene.accept("[]");
    scene.pump();
    CHECK(removed.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          log == std::vector<std::string>({"provider+", "consumer+", "consumer-", "provider-"}));
    scene.close();
}

/** @brief 新增依赖按精确 realm 等待及恢复，历史 Pending 不随恢复或退役改写。 */
void injectRecovery()
{
    Scene scene;
    int calls = 0, cleaned = 0;
    scene.add("consumer",
              [&](Context context) {
                  context.consume<int>({"extra"}).value();
                  ++calls;
                  context
                      .onCleanup([&](CleanupContext) {
                          ++cleaned;
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {}, {}, {ServiceRequirement::of<int>({"extra"})});
    const auto realm = scene.kernel->services().createRealm().value();
    scene.start({{"private", realm}});
    auto pending = scene.accept("[{id: a, plugin: consumer, realms: {extra: private}, inject: [extra/v1]}]");
    scene.pump();
    CHECK(pending.settled.tryResult()->value().outcome == PlanOutcome::Pending && calls == 0);
    const auto original = scene.driver->snapshot().value().front().instance;
    auto global = scene.kernel->services().stage<int>({"extra"}, std::make_shared<int>(1)).value();
    global.publish().value();
    scene.pump();
    CHECK(calls == 0);
    auto provider = scene.kernel->services().stage<int>({"extra", 1, realm}, std::make_shared<int>(2)).value();
    provider.publish().value();
    scene.pump();
    CHECK(calls == 1 && scene.driver->snapshot().value().front().instance == original &&
          scene.driver->snapshot().value().front().state == InstanceState::Active);
    provider.retire().value();
    scene.pump();
    provider.release().value();
    CHECK(cleaned == 1 && scene.driver->snapshot().value().front().state == InstanceState::Pending);
    auto replacement = scene.kernel->services().stage<int>({"extra", 1, realm}, std::make_shared<int>(3)).value();
    replacement.publish().value();
    scene.pump();
    CHECK(calls == 2 && scene.driver->snapshot().value().front().instance == original &&
          pending.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    replacement.retire().value();
    scene.pump();
    replacement.release().value();
    global.retire().value();
    global.release().value();
    scene.close();
}

/** @brief 收紧或移除有效依赖必须重建；冗余较弱写法 NoOp，注册 Runtime 数量保持有界。 */
void injectRecreate()
{
    Scene scene;
    Promise<void> gate;
    int calls = 0;
    scene.add("consumer",
              [&](Context context) {
                  ++calls;
                  context.consume<int>({"s"}).value();
                  context.onCleanup([gate](CleanupContext) { return gate.task(); }).value();
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"s"}, 1, {"read"})});
    scene.add("provider",
              [](Context context) {
                  context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"s"}, true, {2, {"read", "write"}})});
    scene.start();
    scene.accept("[{id: a, plugin: consumer}, {id: z, plugin: provider}]");
    scene.pump();
    const auto original = scene.driver->snapshot().value().front().instance;
    auto redundant = scene.accept("[{id: a, plugin: consumer, inject: [s/v1]}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(redundant.settled.tryResult()->value().entries.back().operation.kind == ChangeKind::NoOp && calls == 1);
    auto tightened = scene.accept("[{id: a, plugin: consumer, inject: [{name: s, minimumMinor: 2, capabilities: "
                                  "[write]}]}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(!tightened.settled.isReady() && calls == 1);
    gate.resolve();
    scene.pump();
    CHECK(tightened.settled.tryResult()->value().outcome == PlanOutcome::Applied && calls == 2 &&
          scene.driver->snapshot().value().front().instance != original);
    auto weak = std::make_shared<const ValidatedCandidate>(
        scene.target("[{id: a, plugin: consumer}, {id: z, plugin: provider}]"));
    auto invalid = scene.driver->apply({"a", ChangeKind::NoOp}, weak, scene.revision);
    CHECK(invalid.isReady() && !invalid.tryResult()->hasValue() &&
          invalid.tryResult()->error().code == ErrorCode::ConfigConflict);
    auto loosened = scene.accept("[{id: a, plugin: consumer}, {id: z, plugin: provider}]");
    scene.pump();
    CHECK(loosened.settled.tryResult()->value().outcome == PlanOutcome::Applied && calls == 3 &&
          scene.kernel->snapshot().value().runtimes == 2);
    scene.close();

    // 新提供者已添加、消费者仍持旧高要求时被新目标替代，必须从这份部分状态重新规划。
    Scene replan;
    Promise<void> publishing;
    replan.add("consumer", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"s"})});
    replan.add("provider",
               [publishing](Context context) {
                   context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                   return publishing.task();
               },
               {}, {ServiceOutput::of<int>({"s"}, true, {1, {}})});
    auto external = replan.kernel->services().stage<int>({"s"}, std::make_shared<int>(2), {2, {}}).value();
    external.publish().value();
    replan.start();
    replan.accept("[{id: a, plugin: consumer, inject: [{name: s, minimumMinor: 2}]}]");
    replan.pump();
    external.retire().value();
    replan.pump();
    external.release().value();
    const std::string latest =
        "[{id: a, plugin: consumer, inject: [{name: s, minimumMinor: 1}]}, {id: z, plugin: provider}]";
    auto superseded = replan.accept(latest);
    replan.pump();
    CHECK(!superseded.settled.isReady());
    auto replacement = replan.accept(latest);
    publishing.resolve();
    replan.pump();
    CHECK(superseded.settled.tryResult()->value().outcome == PlanOutcome::Superseded);
    CHECK(replacement.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          replan.driver->snapshot().value().front().state == InstanceState::Active);
    replan.close();
}

/** @brief 宿主接口目录必须无歧义，来源定义不能绕过真实 Runtime 的固定要求。 */
void injectGuards()
{
    Scene scene;
    int calls = 0;
    scene.add("consumer",
              [&](Context) {
                  ++calls;
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"fixed"}, 2, {"read"})}, {},
              {ServiceRequirement::of<int>({"optional"}, 1, {"read"})});
    for (auto invalid :
         std::vector<ServiceRequirement>{ServiceRequirement::of<int>({"fixed"}), ServiceRequirement::of<void>({"bad"}),
                                         ServiceRequirement::of<int>({"bad"}, 0, {"read", "read"})}) {
        auto plugin = scene.plugins.front();
        plugin.configurableInject.push_back(invalid);
        bool rejected = false;
        try {
            KernelReconcileDriver bad(scene.scheduler, scene.kernel, {plugin});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    scene.start();
    CHECK(!scene.coordinator->accept(scene.target("[{id: a, plugin: consumer, inject: [optional/v1]}]"), 0).hasValue());
    CHECK(
        !scene.coordinator->accept(scene.target("[{id: a, plugin: consumer, disabled: true, inject: [unknown/v1]}]"), 0)
             .hasValue());
    scene.definitions.front().requiredInject.clear();
    CHECK(!scene.coordinator->accept(scene.target("[{id: a, plugin: consumer}]"), 0).hasValue());
    CHECK(calls == 0 && scene.coordinator->snapshot().value().desiredRevision == 0 &&
          scene.kernel->snapshot().value().instances.empty());
    scene.close();
}

/** @brief 配置追加边同样检查类型、minor/capability 和环，拒绝时没有插件副作用。 */
void injectGraph()
{
    Scene cycle;
    cycle.add("a", [](Context) { return completedTask(); }, {}, {ServiceOutput::of<int>({"a"})},
              {ServiceRequirement::of<int>({"b"})});
    cycle.add("b", [](Context) { return completedTask(); }, {}, {ServiceOutput::of<int>({"b"})},
              {ServiceRequirement::of<int>({"a"})});
    cycle.start();
    auto rejected = cycle.coordinator->accept(
        cycle.target("[{id: a, plugin: a, inject: [b/v1]}, {id: b, plugin: b, inject: [a/v1]}]"), 0);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::DependencyCycle &&
          cycle.kernel->snapshot().value().instances.empty());
    cycle.close();
    Scene types;
    types.add("c", [](Context) { return completedTask(); }, {}, {}, {ServiceRequirement::of<int>({"s"})});
    types.add("p", [](Context) { return completedTask(); }, {}, {ServiceOutput::of<std::string>({"s"})});
    types.start();
    CHECK(!types.coordinator->accept(types.target("[{id: a, plugin: c, inject: [s/v1]}, {id: z, plugin: p}]"), 0)
               .hasValue());
    types.close();
    Scene versions;
    versions.add("c", [](Context) { return completedTask(); }, {}, {}, {ServiceRequirement::of<int>({"s"})});
    versions.add("p", [](Context) { return completedTask(); }, {},
                 {ServiceOutput::of<int>({"s"}, true, {1, {"read"}})});
    versions.start();
    CHECK(!versions.coordinator
               ->accept(versions.target("[{id: a, plugin: c, inject: [{name: s, minimumMinor: 2, capabilities: "
                                        "[write]}]}, {id: z, plugin: p}]"),
                        0)
               .hasValue());
    versions.close();
}

/** @brief 改变契约时旧实例清理失败，不得挂载新实例或把旧输入覆盖成新目标。 */
void injectCleanupFailure()
{
    Scene scene;
    int calls = 0;
    scene.add("consumer",
              [&](Context context) {
                  ++calls;
                  context
                      .onCleanup([](CleanupContext) {
                          return failedTask(makeError(ErrorCode::CleanupFailed, "retained worker"));
                      })
                      .value();
                  return completedTask();
              },
              {}, {}, {ServiceRequirement::of<int>({"extra"})});
    scene.start();
    scene.accept("[{id: a, plugin: consumer}]");
    scene.pump();
    const auto original = scene.driver->snapshot().value().front().instance;
    auto changed = scene.accept("[{id: a, plugin: consumer, inject: [extra/v1]}]");
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Failed && calls == 1 &&
          scene.coordinator->snapshot().value().unsafe);
    auto running = scene.driver->snapshot().value().front();
    CHECK(running.instance == original && running.configuration.inject.empty());
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(closed.isReady() && !closed.tryResult()->value().quiescent);
    auto kernel = scene.kernel->close();
    scene.pump();
    CHECK(!kernel.tryResult()->value().quiescent);
    scene.scheduler->close();
}

/** @brief 仅拒绝下一次投递；调度器仍开放，用于区分资源内部投递故障与整体关闭。 */
class RejectingScheduler final : public IScheduler {
public:
    std::atomic_bool rejectNext{false};
    FakeScheduler queue;
    /** @brief 拒绝不执行任务，其余工作仍由真实队列推进。 */
    bool post(ScheduledJob job) override
    {
        if (rejectNext.exchange(false))
            return false;
        return queue.post(std::move(job));
    }
    /** @brief owner 身份与内部队列一致。 */
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    /** @brief 单次拒绝不能伪装成全局调度器关闭。 */
    bool isOpen() const override { return queue.isOpen(); }
};

/** @brief 内部激活投递被拒绝时动作必须失败；关闭仍向其他自有实例广播停止。 */
void internalDelivery()
{
    auto scheduler = std::make_shared<RejectingScheduler>();
    auto kernel = std::make_shared<Kernel>(scheduler);
    int activated = 0, cleaned = 0;
    auto runtime = kernel
                       ->registerPlugin("demo",
                                        [&](Context context) {
                                            ++activated;
                                            context
                                                .onCleanup([&](CleanupContext) {
                                                    ++cleaned;
                                                    return completedTask();
                                                })
                                                .value();
                                            return completedTask();
                                        })
                       .value();
    auto driver =
        std::make_shared<KernelReconcileDriver>(scheduler, kernel, std::vector<KernelPlugin>{{"demo", runtime}});
    const std::vector<PluginDefinition> definitions{
        {"demo", {}, [](const std::string&) { return Result<void>::success(); }}};
    auto first = std::make_shared<const ValidatedCandidate>(
        validateCandidate(parseConfiguration("formatVersion: 1\nplugins: [{id: a, plugin: demo}]\n").value(),
                          definitions)
            .value());
    auto active = driver->apply({"a", ChangeKind::Add}, first, 1);
    scheduler->queue.runUntilIdle().value();
    CHECK(active.tryResult()->value().outcome == StepOutcome::Applied && activated == 1);
    auto second = std::make_shared<const ValidatedCandidate>(
        validateCandidate(
            parseConfiguration("formatVersion: 1\nplugins: [{id: a, plugin: demo}, {id: b, plugin: demo}]\n").value(),
            definitions)
            .value());
    scheduler->rejectNext.store(true);
    auto rejected = driver->apply({"b", ChangeKind::Add}, second, 2);
    CHECK(rejected.isReady() && !rejected.tryResult()->hasValue() &&
          rejected.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(scheduler->isOpen() && !driver->snapshot().hasValue());
    auto closed = driver->close();
    scheduler->queue.runUntilIdle().value();
    CHECK(closed.isReady() && !closed.tryResult()->hasValue() && activated == 1 && cleaned == 1);
    auto all = kernel->close();
    scheduler->queue.runUntilIdle().value();
    CHECK(all.tryResult()->value().quiescent);
    scheduler->queue.close();

    auto lateScheduler = std::make_shared<RejectingScheduler>();
    auto lateKernel = std::make_shared<Kernel>(lateScheduler);
    Promise<void> activation;
    auto lateRuntime = lateKernel->registerPlugin("demo", [activation](Context) { return activation.task(); }).value();
    auto lateDriver = std::make_shared<KernelReconcileDriver>(lateScheduler, lateKernel,
                                                              std::vector<KernelPlugin>{{"demo", lateRuntime}});
    auto lateAction = lateDriver->apply({"a", ChangeKind::Add}, first, 1);
    lateScheduler->queue.runUntilIdle().value();
    lateScheduler->rejectNext.store(true);
    activation.resolve();
    CHECK(!lateDriver->snapshot().hasValue());
    auto lateClose = lateDriver->close();
    lateScheduler->queue.runUntilIdle().value();
    CHECK(lateClose.isReady() && !lateClose.tryResult()->hasValue() && lateAction.isReady() &&
          !lateAction.tryResult()->hasValue());
    lateScheduler->queue.close();
}

/** @brief 按规范路径读取真实值快照，不依赖拓扑顺序或实例 id 排序。 */
RunningEntry running(Scene& scene, const std::string& path)
{
    auto values = scene.driver->snapshot().value();
    auto found =
        std::find_if(values.begin(), values.end(), [&](const RunningEntry& value) { return value.path == path; });
    CHECK(found != values.end());
    return *found;
}

/** @brief 父与子独立更新，旧孙/子清理阻挡父新代；禁用与删除不重建子树。 */
void treeLifecycle()
{
    Scene scene;
    Promise<void> gate;
    bool hold = true;
    std::vector<std::string> cleaned;
    scene.add("root", [&](Context context) {
        context
            .onCleanup([&](CleanupContext) {
                cleaned.push_back("root");
                return completedTask();
            })
            .value();
        return completedTask();
    });
    scene.add("child", [&](Context context) {
        context
            .onCleanup([&](CleanupContext) {
                cleaned.push_back("child");
                return hold ? gate.task() : completedTask();
            })
            .value();
        return completedTask();
    });
    scene.add("leaf", [&](Context context) {
        context
            .onCleanup([&](CleanupContext) {
                cleaned.push_back("leaf");
                return completedTask();
            })
            .value();
        return completedTask();
    });
    scene.start();
    const std::string children = ", children: [{id: c, plugin: child, children: [{id: l, plugin: leaf}]}]}]";
    auto first = scene.accept("[{id: r, plugin: root, config: one" + children);
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto root = running(scene, "r"), child = running(scene, "r/c"), leaf = running(scene, "r/c/l");
    CHECK(scene.kernel->snapshot().value().instances.size() == 1 && child.parentEpoch == root.epoch &&
          leaf.parentEpoch == child.epoch);
    auto changed = scene.accept("[{id: r, plugin: root, config: two" + children);
    scene.pump();
    CHECK(!changed.settled.isReady() && cleaned == std::vector<std::string>({"leaf", "child"}));
    hold = false;
    gate.resolve();
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto nextRoot = running(scene, "r"), nextChild = running(scene, "r/c"), nextLeaf = running(scene, "r/c/l");
    CHECK(nextRoot.instance == root.instance && nextRoot.epoch > root.epoch && nextChild.instance != child.instance &&
          nextLeaf.instance != leaf.instance);
    CHECK(cleaned == std::vector<std::string>({"leaf", "child", "root"}));
    auto childUpdate = scene.accept("[{id: r, plugin: root, config: two, children: [{id: c, plugin: child, config: "
                                    "changed, children: [{id: l, plugin: leaf}]}]}]");
    scene.pump();
    CHECK(childUpdate.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(running(scene, "r").epoch == nextRoot.epoch && running(scene, "r/c").instance == nextChild.instance &&
          running(scene, "r/c").epoch > nextChild.epoch);
    CHECK(running(scene, "r/c/l").instance != nextLeaf.instance);
    auto disabled = scene.accept("[{id: r, plugin: root, disabled: true" + children);
    scene.pump();
    CHECK(disabled.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          scene.driver->snapshot().value().empty());
    scene.accept("[{id: r, plugin: root" + children);
    scene.pump();
    CHECK(running(scene, "r").instance != root.instance);
    scene.accept("[]");
    scene.pump();
    CHECK(scene.driver->snapshot().value().empty());
    scene.close();
}

/** @brief 父 Pending 时显式等待，恢复及再次退役后自动重建；历史 Pending 保持。 */
void treePending()
{
    Scene scene;
    int childCalls = 0, leafCalls = 0;
    scene.add("root", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"trigger"})});
    scene.add("child",
              [&](Context) {
                  ++childCalls;
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"store"})});
    scene.add("leaf", [&](Context) {
        ++leafCalls;
        return completedTask();
    });
    scene.start();
    auto first =
        scene.accept("[{id: r, plugin: root, children: [{id: c, plugin: child, children: [{id: l, plugin: leaf}]}]}]");
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    CHECK(running(scene, "r/c").waitingForParent && !running(scene, "r/c").instance.valid() &&
          running(scene, "r/c/l").waitingForParent);
    auto trigger = scene.kernel->services().stage<int>({"trigger"}, std::make_shared<int>(1)).value();
    trigger.publish().value();
    scene.pump();
    CHECK(!running(scene, "r/c").waitingForParent && running(scene, "r/c").state == InstanceState::Pending &&
          childCalls == 0);
    auto store = scene.kernel->services().stage<int>({"store"}, std::make_shared<int>(42)).value();
    store.publish().value();
    scene.pump();
    CHECK(childCalls == 1 && leafCalls == 1 && running(scene, "r/c/l").state == InstanceState::Active);
    auto child = running(scene, "r/c").instance;
    trigger.retire().value();
    scene.pump();
    trigger.release().value();
    CHECK(running(scene, "r/c").waitingForParent);
    auto replacement = scene.kernel->services().stage<int>({"trigger"}, std::make_shared<int>(2)).value();
    replacement.publish().value();
    scene.pump();
    CHECK(childCalls == 2 && leafCalls == 2 && running(scene, "r/c").instance != child);
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    scene.close();
}

/** @brief 附加契约沿父域继承，局部覆盖仍精确匹配，不错误使用全局服务。 */
void treeRealms()
{
    Scene scene;
    auto privateRealm = scene.kernel->services().createRealm().value();
    auto otherRealm = scene.kernel->services().createRealm().value();
    auto thirdRealm = scene.kernel->services().createRealm().value();
    std::vector<int> seen;
    scene.add("root", [](Context) { return completedTask(); });
    scene.add("provider",
              [](Context context) {
                  context.provide<int>({"store"}, std::make_shared<int>(11)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"store"}, true, {2, {"read"}})});
    auto consumer = scene.add(
        "consumer",
        [&](Context context) {
            context.consume<int>({"store"}).value().withService([&](int& value) { seen.push_back(value); }).value();
            return completedTask();
        },
        {}, {}, {ServiceRequirement::of<int>({"store"})});
    scene.start({{"private", privateRealm}, {"other", otherRealm}, {"third", thirdRealm}});
    auto foreign = scene.kernel->services().stage<int>({"store"}, std::make_shared<int>(99)).value();
    foreign.publish().value();
    auto first =
        scene.accept("[{id: r, plugin: root, realms: {store: private}, children: [{id: p, plugin: provider, children: "
                     "[{id: c, plugin: consumer, inject: [{name: store, minimumMinor: 2, capabilities: [read]}]}]}, "
                     "{id: x, plugin: consumer, realms: {store: other}, inject: [store/v1]}]}]");
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Pending && seen == std::vector<int>({11}));
    CHECK(running(scene, "r/p/c").configuration.realms.at("store") == "private" &&
          running(scene, "r/x").state == InstanceState::Pending);
    auto other = scene.kernel->services().stage<int>({"store", 1, otherRealm}, std::make_shared<int>(22)).value();
    other.publish().value();
    scene.pump();
    CHECK(seen == std::vector<int>({11, 22}) && consumer.requirements().empty());
    auto root = running(scene, "r");
    auto child = running(scene, "r/x");
    auto changed = scene.accept("[{id: r, plugin: root, realms: {store: private}, children: [{id: p, plugin: provider, "
                                "children: [{id: c, plugin: consumer, inject: [{name: store, minimumMinor: 2, "
                                "capabilities: [read]}]}]}, {id: x, plugin: consumer, inject: [store/v1]}]}]");
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          running(scene, "r").epoch == root.epoch && running(scene, "r/x").instance != child.instance &&
          seen.back() == 11);
    const auto before = seen.size();
    auto moved = scene.accept("[{id: r, plugin: root, realms: {store: third}, children: [{id: p, plugin: provider, "
                              "children: [{id: c, plugin: consumer, inject: [{name: store, minimumMinor: 2, "
                              "capabilities: [read]}]}]}, {id: x, plugin: consumer, inject: [store/v1]}]}]");
    scene.pump();
    CHECK(moved.settled.tryResult()->value().outcome == PlanOutcome::Applied &&
          running(scene, "r").instance != root.instance && seen.size() == before + 2);
    scene.close();
}

/** @brief 所有权与依赖联合检查拒绝父依赖后代、重复输出及禁用子项的未知域。 */
void treeGraph()
{
    Scene scene;
    scene.add("root", [](Context) { return completedTask(); });
    scene.add("parent", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"s"})});
    scene.add("provider",
              [](Context context) {
                  context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"s"})});
    scene.start();
    auto cycle =
        scene.coordinator->accept(scene.target("[{id: r, plugin: parent, children: [{id: p, plugin: provider}]}]"), 0);
    CHECK(!cycle.hasValue() && cycle.error().code == ErrorCode::DependencyCycle);
    auto duplicate = scene.coordinator->accept(
        scene.target("[{id: r, plugin: root, children: [{id: a, plugin: provider}, {id: b, plugin: provider}]}]"), 0);
    CHECK(!duplicate.hasValue());
    auto disabled = scene.coordinator->accept(
        scene.target(
            "[{id: r, plugin: root, children: [{id: a, plugin: root, disabled: true, realms: {s: missing}}]}]"),
        0);
    CHECK(!disabled.hasValue() && scene.coordinator->snapshot().value().desiredRevision == 0 &&
          scene.kernel->snapshot().value().instances.empty());
    scene.close();
}

/** @brief 父清理中接受 B 再被 C 替代，尚未执行的 B 子项不挂载，删除项不复活。 */
void treeLatest()
{
    Scene scene;
    Promise<void> cleanup;
    std::vector<std::string> children;
    scene.add("root", [](Context) { return completedTask(); });
    scene.add("child", [&](Context context) {
        children.push_back(context.config());
        if (context.config() == "\"old\"")
            context.onCleanup([cleanup](CleanupContext) { return cleanup.task(); }).value();
        return completedTask();
    });
    scene.start();
    scene.accept("[{id: r, plugin: root, config: A, children: [{id: x, plugin: child, config: old}]}]");
    scene.pump();
    auto b = scene.accept("[{id: r, plugin: root, config: B, children: [{id: y, plugin: child, config: B}]}]");
    scene.pump();
    CHECK(!b.settled.isReady());
    auto c = scene.accept("[{id: r, plugin: root, config: C, children: [{id: z, plugin: child, config: C}]}]");
    cleanup.resolve();
    scene.pump();
    CHECK(b.settled.tryResult()->value().outcome == PlanOutcome::Superseded &&
          c.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(children == std::vector<std::string>({"\"old\"", "\"C\""}) && scene.driver->snapshot().value().size() == 2);
    CHECK(running(scene, "r/z").state == InstanceState::Active);
    scene.close();
}

/** @brief 父及子激活失败分别通过目标重试，不自动重复失败激活或增加 Runtime。 */
void treeFailure()
{
    Scene scene;
    int roots = 0, children = 0;
    scene.add("root", [&](Context) {
        return ++roots == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "root")) : completedTask();
    });
    scene.add("child", [&](Context) {
        return ++children == 1 ? failedTask(makeError(ErrorCode::ActivationFailed, "child")) : completedTask();
    });
    scene.start();
    const std::string value = "[{id: r, plugin: root, children: [{id: c, plugin: child}]}]";
    auto a = scene.accept(value);
    scene.pump();
    CHECK(a.settled.tryResult()->value().outcome == PlanOutcome::Failed && children == 0);
    auto b = scene.accept(value);
    scene.pump();
    CHECK(b.settled.tryResult()->value().outcome == PlanOutcome::Failed && running(scene, "r/c").failure.has_value());
    auto child = running(scene, "r/c").instance;
    auto c = scene.accept(value);
    scene.pump();
    CHECK(c.settled.tryResult()->value().outcome == PlanOutcome::Applied && roots == 2 && children == 2 &&
          running(scene, "r/c").instance == child && scene.kernel->snapshot().value().runtimes == 2);
    scene.close();
}

/** @brief 后台恢复的子正在激活时关闭，等待源任务并禁止再创建孙节点。 */
void treeShutdown()
{
    Scene scene;
    Promise<void> activation;
    int leaves = 0;
    scene.add("root", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"trigger"})});
    scene.add("child", [activation](Context) { return activation.task(); });
    scene.add("leaf", [&](Context) {
        ++leaves;
        return completedTask();
    });
    scene.start();
    scene.accept("[{id: r, plugin: root, children: [{id: c, plugin: child, children: [{id: l, plugin: leaf}]}]}]");
    scene.pump();
    auto trigger = scene.kernel->services().stage<int>({"trigger"}, std::make_shared<int>(1)).value();
    trigger.publish().value();
    scene.pump();
    CHECK(running(scene, "r/c").state == InstanceState::Loading && leaves == 0);
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(!closed.isReady());
    activation.resolve();
    scene.pump();
    CHECK(closed.tryResult()->value().quiescent && scene.driver->snapshot().value().empty() && leaves == 0);
    auto all = scene.kernel->close();
    scene.pump();
    CHECK(all.tryResult()->value().quiescent);
    scene.scheduler->close();
}

/** @brief 子清理残留阻挡父重启，关闭必须报告非静止并保留资源账目。 */
void treeCleanupFailure()
{
    Scene scene;
    int roots = 0;
    scene.add("root", [&](Context) {
        ++roots;
        return completedTask();
    });
    scene.add("child", [](Context context) {
        context
            .onCleanup([](CleanupContext) { return failedTask(makeError(ErrorCode::CleanupFailed, "child retained")); })
            .value();
        return completedTask();
    });
    scene.start();
    scene.accept("[{id: r, plugin: root, config: one, children: [{id: c, plugin: child}]}]");
    scene.pump();
    auto update = scene.accept("[{id: r, plugin: root, config: two, children: [{id: c, plugin: child}]}]");
    scene.pump();
    CHECK(update.settled.tryResult()->value().outcome == PlanOutcome::Failed &&
          scene.coordinator->snapshot().value().unsafe && roots == 1);
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(!closed.tryResult()->value().quiescent && !scene.driver->snapshot().value().empty());
    auto all = scene.kernel->close();
    scene.pump();
    CHECK(!all.tryResult()->value().quiescent);
    scene.scheduler->close();
}

/** @brief 历史 Pending 后后台子激活清理失败，冻结新动作并保持历史报告。 */
void treeBackgroundFailure()
{
    Scene scene;
    scene.add("root", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"trigger"})});
    scene.add("child", [](Context context) {
        context.onCleanup([](CleanupContext) { return failedTask(makeError(ErrorCode::CleanupFailed, "retained")); })
            .value();
        return failedTask(makeError(ErrorCode::ActivationFailed, "child"));
    });
    scene.start();
    const std::string value = "[{id: r, plugin: root, children: [{id: c, plugin: child}]}]";
    auto first = scene.accept(value);
    scene.pump();
    auto trigger = scene.kernel->services().stage<int>({"trigger"}, std::make_shared<int>(1)).value();
    trigger.publish().value();
    scene.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Pending &&
          running(scene, "r/c").failure.has_value());
    auto rejected = scene.coordinator->accept(scene.target(value), scene.revision);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::CleanupFailed);
    auto closed = scene.coordinator->close();
    scene.pump();
    CHECK(!closed.tryResult()->value().quiescent);
    auto all = scene.kernel->close();
    scene.pump();
    CHECK(!all.tryResult()->value().quiescent);
    scene.scheduler->close();
}

/** @brief 另一根消费配置子项的输出，父更新沿真实租约先清理消费者，再恢复两棵子树。 */
void treeCrossDependency()
{
    Scene scene;
    std::vector<int> values, cleaned;
    scene.add("root", [](Context) { return completedTask(); });
    scene.add("provider",
              [](Context context) {
                  context.provide<int>({"value"}, std::make_shared<int>(context.config() == "1" ? 1 : 2)).value();
                  return completedTask();
              },
              {}, {ServiceOutput::of<int>({"value"})});
    scene.add("consumer",
              [&](Context context) {
                  auto ref = context.consume<int>({"value"}).value();
                  ref.withService([&](int& value) { values.push_back(value); }).value();
                  context
                      .onCleanup([&, ref](CleanupContext cleanup) {
                          cleanup.withService(ref, [&](int& value) { cleaned.push_back(value); }).value();
                          return completedTask();
                      })
                      .value();
                  return completedTask();
              },
              {ServiceRequirement::of<int>({"value"})});
    scene.add("leaf", [](Context) { return completedTask(); });
    scene.start();
    const std::string prefix =
        "[{id: a, plugin: consumer, children: [{id: l, plugin: leaf}]}, {id: z, plugin: root, config: ";
    scene.accept(prefix + "1, children: [{id: p, plugin: provider, config: 1}]}]");
    scene.pump();
    auto consumer = running(scene, "a"), child = running(scene, "a/l"), provider = running(scene, "z/p");
    CHECK(values == std::vector<int>({1}) && scene.kernel->snapshot().value().instances.size() == 2);
    auto changed = scene.accept(prefix + "2, children: [{id: p, plugin: provider, config: 2}]}]");
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied && values == std::vector<int>({1, 2}) &&
          cleaned == std::vector<int>({1}));
    CHECK(running(scene, "a").instance == consumer.instance && running(scene, "a").epoch > consumer.epoch &&
          running(scene, "a/l").instance != child.instance && running(scene, "z/p").instance != provider.instance);
    scene.close();
    CHECK(cleaned == std::vector<int>({1, 2}));
}

/** @brief 等待父代的条目无实例可退役，插件/域/契约变更直接替换等待意图。 */
void treeDeferredChanges()
{
    Scene scene;
    int oldCalls = 0, newCalls = 0;
    auto privateRealm = scene.kernel->services().createRealm().value();
    auto otherRealm = scene.kernel->services().createRealm().value();
    scene.add("root", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"trigger"})});
    scene.add("old", [&](Context) {
        ++oldCalls;
        return completedTask();
    });
    scene.add("new",
              [&](Context context) {
                  context.consume<int>({"extra"})
                      .value()
                      .withService([&](int& value) {
                          CHECK(value == 7);
                          ++newCalls;
                      })
                      .value();
                  return completedTask();
              },
              {}, {}, {ServiceRequirement::of<int>({"extra"})});
    scene.start({{"private", privateRealm}, {"other", otherRealm}});
    scene.accept("[{id: r, plugin: root, children: [{id: x, plugin: old}]}]");
    scene.pump();
    const auto replacement = std::make_shared<const ValidatedCandidate>(scene.target(
        "[{id: r, plugin: root, children: [{id: x, plugin: new, realms: {extra: private}, inject: [extra/v1]}]}]"));
    auto direct = scene.driver->apply({"r/x", ChangeKind::Add}, replacement, 2);
    scene.pump();
    CHECK(direct.tryResult()->hasValue() && direct.tryResult()->value().outcome == StepOutcome::Pending);
    auto changed = scene.accept(
        "[{id: r, plugin: root, children: [{id: x, plugin: new, realms: {extra: private}, inject: [extra/v1]}]}]");
    scene.pump();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Pending &&
          running(scene, "r/x").configuration.plugin == "new" && running(scene, "r/x").waitingForParent);
    const std::string inherited =
        "[{id: r, plugin: root, realms: {extra: other}, children: [{id: x, plugin: new, inject: [extra/v1]}]}]";
    auto remapped = scene.accept(inherited);
    scene.pump();
    CHECK(remapped.settled.tryResult()->value().outcome == PlanOutcome::Pending &&
          running(scene, "r/x").configuration.realms.at("extra") == "other");
    scene.accept("[]");
    scene.pump();
    CHECK(scene.driver->snapshot().value().empty());
    scene.accept(inherited);
    scene.pump();
    auto trigger = scene.kernel->services().stage<int>({"trigger"}, std::make_shared<int>(1)).value();
    auto extra = scene.kernel->services().stage<int>({"extra", 1, otherRealm}, std::make_shared<int>(7)).value();
    trigger.publish().value();
    extra.publish().value();
    scene.pump();
    CHECK(oldCalls == 0 && newCalls == 1 && running(scene, "r/x").state == InstanceState::Active);
    scene.close();

    Scene partial;
    partial.add("root", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"missing"})});
    partial.add("provider",
                [](Context context) {
                    context.provide<int>({"s"}, std::make_shared<int>(1)).value();
                    return completedTask();
                },
                {}, {ServiceOutput::of<int>({"s"})});
    partial.start();
    partial.accept("[{id: r, plugin: root, children: [{id: x, plugin: provider, children: [{id: g, plugin: root}]}]}]");
    partial.pump();
    auto target = std::make_shared<const ValidatedCandidate>(
        partial.target("[{id: r, plugin: root, children: [{id: y, plugin: provider}]}]"));
    auto added = partial.driver->apply({"r/y", ChangeKind::Add}, target, 2);
    partial.pump();
    CHECK(added.tryResult()->value().outcome == StepOutcome::Pending && partial.driver->plan(*target).hasValue());
    auto removed = partial.driver->apply({"r/x", ChangeKind::Remove}, target, 2);
    partial.pump();
    CHECK(removed.tryResult()->value().outcome == StepOutcome::Applied &&
          partial.driver->snapshot().value().size() == 2);
    auto settled = partial.accept("[{id: r, plugin: root, children: [{id: y, plugin: provider}]}]");
    partial.pump();
    CHECK(settled.settled.tryResult()->value().outcome == PlanOutcome::Pending &&
          partial.driver->snapshot().value().size() == 2);
    partial.close();
}

/** @brief 各场景运行真实 Kernel，测试仅替换调度时间而不替换执行驱动。 */
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "lifecycle")
            lifecycle();
        else if (name == "dependencies")
            dependencies();
        else if (name == "pending")
            pending();
        else if (name == "latest")
            latest();
        else if (name == "retry")
            retry();
        else if (name == "cleanup_failure")
            cleanupFailure();
        else if (name == "shutdown")
            shutdown();
        else if (name == "realms")
            realms();
        else if (name == "support")
            support();
        else if (name == "ownership")
            ownership();
        else if (name == "ownership_boundary")
            ownershipBoundary();
        else if (name == "guards")
            guards();
        else if (name == "delivery")
            delivery();
        else if (name == "internal_delivery")
            internalDelivery();
        else if (name == "inject_order")
            injectOrder();
        else if (name == "inject_recovery")
            injectRecovery();
        else if (name == "inject_recreate")
            injectRecreate();
        else if (name == "inject_guards")
            injectGuards();
        else if (name == "inject_graph")
            injectGraph();
        else if (name == "inject_cleanup_failure")
            injectCleanupFailure();
        else if (name == "tree_lifecycle")
            treeLifecycle();
        else if (name == "tree_pending")
            treePending();
        else if (name == "tree_realms")
            treeRealms();
        else if (name == "tree_graph")
            treeGraph();
        else if (name == "tree_latest")
            treeLatest();
        else if (name == "tree_failure")
            treeFailure();
        else if (name == "tree_shutdown")
            treeShutdown();
        else if (name == "tree_cleanup_failure")
            treeCleanupFailure();
        else if (name == "tree_background_failure")
            treeBackgroundFailure();
        else if (name == "tree_cross_dependency")
            treeCrossDependency();
        else if (name == "tree_deferred_changes")
            treeDeferredChanges();
        else
            return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
