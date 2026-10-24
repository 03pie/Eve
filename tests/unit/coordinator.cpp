/** @file coordinator.cpp
 * @brief 以受控运行状态和显式 gate 验证 CAS、串行检查点、最新目标、故障及关闭协议。
 */
#include <cordis/loader/coordinator.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace cordis;
using namespace cordis::loader;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 测试目标始终经过真实 YAML/内置定义校验。 */
ValidatedCandidate target(const std::string& entries)
{
    auto parsed = parseConfiguration("formatVersion: 1\nplugins: " + entries).value();
    return validateCandidate(std::move(parsed), {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}).value();
}
/** @brief 将一次 gate 控制的驱动动作及不可变目标保留到显式完成。 */
struct HeldAction {
    EntryOperation operation;
    std::shared_ptr<const ValidatedCandidate> target;
    std::uint64_t revision;
    Promise<StepReport> done;
    bool completed = false;
};
/** @brief 受控驱动保存实际完成的值，不是 Kernel 适配器；用于检测协调器调用顺序和检查点。 */
struct ControlledDriver final : IReconcileDriver {
    std::map<std::string, RunningEntry> rows;
    std::vector<std::shared_ptr<HeldAction>> held;
    std::vector<std::pair<std::uint64_t, EntryOperation>> calls;
    std::vector<std::map<std::string, RunningEntry>> plannedFrom;
    std::function<void()> onPreflight, onPlan, onApply, onClose;
    bool hold = false, safeFailure = true, closeFails = false, duplicatePlan = false;
    bool throwPlan = false, throwApply = false, throwSnapshot = false, throwClose = false;
    bool invalidKind = false, invalidOutcome = false;
    std::string failurePath, pendingPath;
    int inFlight = 0, maximumInFlight = 0, closeCalls = 0, depth = 0;
    bool closeOnApplyStack = false;
    /** @brief 单次重入钩子先移出，避免递归调用同一个钩子。 */
    static void invoke(std::function<void()>& hook) { auto callback = std::move(hook); hook = {}; if (callback) callback(); }
    /** @brief 测试驱动仅支持无附加依赖、无域映射的顶层条目，其他能力明确拒绝。 */
    Result<void> preflight(const ValidatedCandidate& value) override
    {
        invoke(onPreflight);
        for (const auto& entry : value.source().structure().entries())
            if (entry.first.find('/') != std::string::npos || !entry.second.realms.empty() || !entry.second.inject.empty())
                return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "controlled driver supports root values only"));
        return Result<void>::success();
    }
    /** @brief 每次规划都读取已完成的 rows，旧计划在途目标不得冒充已运行配置。 */
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate& value) override
    {
        invoke(onPlan);
        if (throwPlan) throw std::runtime_error("planning failed");
        plannedFrom.push_back(rows);
        std::vector<EntryOperation> operations;
        const auto& desired = value.source().structure().entries();
        for (const auto& row : rows) if (!desired.count(row.first) || desired.at(row.first).effectiveDisabled)
            operations.push_back({row.first, ChangeKind::Remove});
        for (const auto& entry : desired) {
            if (entry.second.effectiveDisabled) continue;
            auto previous = rows.find(entry.first);
            auto kind = previous == rows.end() ? ChangeKind::Add : previous->second.configuration.config == entry.second.config ? ChangeKind::NoOp : ChangeKind::Restart;
            operations.push_back({entry.first, kind});
        }
        if (duplicatePlan && !operations.empty()) operations.push_back(operations.front());
        if (invalidKind && !operations.empty()) operations.front().kind = static_cast<ChangeKind>(99);
        return Result<std::vector<EntryOperation>>::success(std::move(operations));
    }
    /** @brief 只有动作完成时才修改实际运行值；NoOp 读取当前 Pending 状态。 */
    void record(const HeldAction& action, const StepReport& result)
    {
        if (result.outcome == StepOutcome::Failed) return;
        if (action.operation.kind == ChangeKind::NoOp) return;
        if (action.operation.kind == ChangeKind::Remove) { rows.erase(action.operation.path); return; }
        const auto& value = action.target->source().structure().entries().at(action.operation.path);
        rows.insert_or_assign(action.operation.path, RunningEntry{action.operation.path, value,
            result.outcome == StepOutcome::Pending ? InstanceState::Pending : InstanceState::Active, InstanceId(1), action.revision});
    }
    /** @brief 默认返回真实记录状态，故障和 Pending 由测试显式配置。 */
    StepReport resultFor(const EntryOperation& operation) const
    {
        if (invalidOutcome) return {static_cast<StepOutcome>(99), true, {}};
        if (operation.path == failurePath) return {StepOutcome::Failed, safeFailure, makeError(ErrorCode::ActivationFailed, "configured failure")};
        if (operation.path == pendingPath) return {StepOutcome::Pending, true, {}};
        auto previous = rows.find(operation.path);
        if (operation.kind == ChangeKind::NoOp && previous != rows.end() && previous->second.state == InstanceState::Pending)
            return {StepOutcome::Pending, true, {}};
        return {};
    }
    /** @brief gate 使动作保持在途；即时结果仍由协调器 thenOn 排队接收。 */
    Task<StepReport> apply(EntryOperation operation, std::shared_ptr<const ValidatedCandidate> value, std::uint64_t revision) override
    {
        ++depth;
        invoke(onApply);
        --depth;
        if (throwApply) throw std::runtime_error("apply failed");
        calls.emplace_back(revision, operation);
        auto action = std::make_shared<HeldAction>(HeldAction{std::move(operation), std::move(value), revision, {}, false});
        if (hold) {
            ++inFlight;
            maximumInFlight = std::max(maximumInFlight, inFlight);
            held.push_back(action);
        } else {
            auto report = resultFor(action->operation);
            record(*action, report);
            action->done.resolve(std::move(report));
        }
        return action->done.task();
    }
    /** @brief owner 完成指定 gate，先更新实际资源再发布完成通知。 */
    void complete(std::size_t index)
    {
        auto action = held.at(index);
        CHECK(!action->completed);
        action->completed = true;
        --inFlight;
        auto result = resultFor(action->operation);
        record(*action, result);
        action->done.resolve(std::move(result));
    }
    /** @brief 运行快照只复制实际 rows。 */
    Result<std::vector<RunningEntry>> snapshot() const override
    {
        if (throwSnapshot) throw std::runtime_error("snapshot failed");
        std::vector<RunningEntry> result;
        for (const auto& row : rows) result.push_back(row.second);
        return Result<std::vector<RunningEntry>>::success(std::move(result));
    }
    /** @brief 关闭先终结所有 gate 再返回清理证据，不等待协调器的动作回执。 */
    Task<CleanupReport> close() override
    {
        ++closeCalls;
        closeOnApplyStack = closeOnApplyStack || depth != 0;
        invoke(onClose);
        if (throwClose) throw std::runtime_error("close failed");
        for (const auto& action : held) if (!action->completed) {
            action->completed = true;
            --inFlight;
            action->done.resolve(StepReport{StepOutcome::Failed, true, makeError(ErrorCode::OperationCancelled, "driver closing")});
        }
        if (!closeFails) rows.clear();
        Promise<CleanupReport> done;
        done.resolve(CleanupReport{!closeFails, closeFails ? std::vector<ErrorInfo>{makeError(ErrorCode::CleanupFailed, "resource retained")} : std::vector<ErrorInfo>{}});
        return done.task();
    }
};
/** @brief 场景显式保留 owner scheduler、驱动及协调器。 */
struct Scenario {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    std::shared_ptr<ControlledDriver> driver = std::make_shared<ControlledDriver>();
    ConfigurationCoordinator coordinator{scheduler, driver};
    /** @brief 不用 sleep，泵送到当前显式 gate。 */
    void pump() { scheduler->runUntilIdle().value(); }
    /** @brief 正常关闭证明先于 scheduler 停止。 */
    void close() { auto task = coordinator.close(); pump(); CHECK(task.isReady() && task.tryResult()->hasValue() && task.tryResult()->value().quiescent); scheduler->close(); }
};
/** @brief CAS 拒绝过期修订、移动后候选和驱动不支持的能力，拒绝不执行动作或修改目标。 */
void cas()
{
    Scenario scene;
    auto desired = target("[{id: a, plugin: demo, config: one}]");
    auto accepted = scene.coordinator.accept(desired, 0).value();
    CHECK(accepted.revision == 1 && !accepted.settled.isReady() && scene.driver->calls.empty());
    CHECK(scene.coordinator.snapshot().value().desiredRevision == 1 && scene.coordinator.snapshot().value().running.empty());
    CHECK(scene.coordinator.accept(desired, 0).error().code == ErrorCode::ConfigConflict);
    auto moved = desired;
    auto retained = std::move(moved);
    CHECK(scene.coordinator.accept(std::move(moved), 1).error().code == ErrorCode::ConfigInvalid);
    auto unsupported = target("[{id: a, plugin: demo, children: [{id: child, plugin: demo}]}]");
    CHECK(!scene.coordinator.accept(unsupported, 1).hasValue());
    CHECK(scene.coordinator.snapshot().value().desiredRevision == 1 && retained.source().isValid());
    scene.close();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Cancelled);
}
/** @brief 同一时刻只能有一个动作，接受目标不会把未运行的后续条目写入实际快照。 */
void serial()
{
    Scenario scene;
    scene.driver->hold = true;
    auto desired = target("[{id: a, plugin: demo, config: one}, {id: b, plugin: demo, config: two}]");
    auto accepted = scene.coordinator.accept(desired, 0).value();
    scene.pump();
    CHECK(scene.driver->held.size() == 1 && !accepted.settled.isReady());
    scene.driver->complete(0); scene.pump();
    CHECK(scene.driver->held.size() == 2 && scene.driver->rows.size() == 1 && !accepted.settled.isReady());
    scene.driver->complete(1); scene.pump();
    CHECK(scene.driver->maximumInFlight == 1 && accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(scene.coordinator.snapshot().value().running.size() == 2);
    scene.driver->hold = false;
    auto same = scene.coordinator.accept(desired, 1).value(); scene.pump();
    CHECK(same.revision == 2 && same.settled.tryResult()->value().entries[0].operation.kind == ChangeKind::NoOp);
    CHECK(scene.driver->rows.at("a").epoch == 1 && scene.driver->rows.at("b").epoch == 1);
    scene.close();
}
/** @brief A 在途时 B/C 连续到达，只完成 A 当前动作，然后从实际半成品为 C 重建计划。 */
void latest()
{
    Scenario scene;
    scene.driver->hold = true;
    auto first = scene.coordinator.accept(target("[{id: a, plugin: demo, config: A}, {id: b, plugin: demo, config: A}]"), 0).value();
    scene.pump();
    auto second = scene.coordinator.accept(target("[{id: a, plugin: demo, config: B}]"), 1).value();
    auto third = scene.coordinator.accept(target("[{id: a, plugin: demo, config: C}, {id: c, plugin: demo, config: C}]"), 2).value();
    CHECK(second.settled.tryResult()->value().outcome == PlanOutcome::Superseded && !first.settled.isReady());
    scene.driver->complete(0); scene.pump();
    const auto firstReport = first.settled.tryResult()->value();
    CHECK(firstReport.outcome == PlanOutcome::Superseded && firstReport.entries[0].status == EntryStatus::Applied && firstReport.entries[1].status == EntryStatus::Superseded);
    CHECK(scene.driver->plannedFrom.size() == 2 && scene.driver->plannedFrom[1].at("a").configuration.config == "\"A\"");
    CHECK(scene.driver->calls.back().first == 3 && scene.driver->calls.back().second.kind == ChangeKind::Restart);
    scene.driver->complete(1); scene.pump();
    scene.driver->complete(2); scene.pump();
    CHECK(third.settled.tryResult()->value().outcome == PlanOutcome::Applied && scene.driver->rows.size() == 2 && !scene.driver->rows.count("b"));
    for (const auto& call : scene.driver->calls) CHECK(call.first != 2);
    CHECK(scene.driver->rows.at("a").configuration.config == "\"C\"" && scene.coordinator.snapshot().value().desiredRevision == 3);
    scene.close();
}
/** @brief 默认遇错停止后续项，保留已应用部分与目标；下一次接受同目标从真实部分继续。 */
void partialFailure()
{
    Scenario scene;
    scene.driver->failurePath = "b";
    auto desired = target("[{id: a, plugin: demo}, {id: b, plugin: demo}, {id: c, plugin: demo}]");
    auto accepted = scene.coordinator.accept(desired, 0).value(); scene.pump();
    const auto& report = accepted.settled.tryResult()->value();
    CHECK(report.outcome == PlanOutcome::Failed && report.entries[0].status == EntryStatus::Applied && report.entries[1].status == EntryStatus::Failed && report.entries[2].status == EntryStatus::Skipped);
    CHECK(scene.driver->rows.size() == 1 && scene.coordinator.snapshot().value().desired->source().structure().entries().size() == 3);
    scene.driver->failurePath.clear();
    auto retried = scene.coordinator.accept(desired, 1).value(); scene.pump();
    CHECK(retried.settled.tryResult()->value().outcome == PlanOutcome::Applied && scene.driver->rows.size() == 3);
    CHECK(retried.settled.tryResult()->value().entries.front().operation.kind == ChangeKind::NoOp);
    scene.close();
}
/** @brief Pending 形成稳定历史报告，无关条目继续应用；实际恢复不能改写旧 Task。 */
void pending()
{
    Scenario scene;
    scene.driver->pendingPath = "a";
    auto desired = target("[{id: a, plugin: demo}, {id: b, plugin: demo}]");
    auto accepted = scene.coordinator.accept(desired, 0).value(); scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Pending && scene.driver->rows.at("b").state == InstanceState::Active);
    scene.driver->pendingPath.clear(); scene.driver->rows.at("a").state = InstanceState::Active;
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    auto next = scene.coordinator.accept(desired, 1).value(); scene.pump();
    CHECK(next.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    scene.close();
}
/** @brief 关闭优先广播，不能等待被 gate 阻塞的动作先完成；同步重入也不能在 apply 栈内 close。 */
void shutdown()
{
    Scenario scene;
    scene.driver->hold = true;
    std::optional<Task<CleanupReport>> closed;
    bool deferred = false;
    scene.driver->onApply = [&] { closed = scene.coordinator.close(); deferred = scene.driver->closeCalls == 0 && !closed->isReady(); };
    auto accepted = scene.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value();
    scene.pump();
    CHECK(deferred && closed && closed->tryResult()->value().quiescent && scene.driver->closeCalls == 1 && !scene.driver->closeOnApplyStack);
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Cancelled && scene.driver->inFlight == 0);
    CHECK(scene.coordinator.accept(target("[]"), 1).error().code == ErrorCode::ContextClosing);
    CHECK(scene.coordinator.close().tryResult()->value().quiescent);
    scene.scheduler->close();
    Scenario waiting;
    waiting.driver->hold = true;
    auto first = waiting.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); waiting.pump();
    auto next = waiting.coordinator.accept(target("[]"), 1).value();
    auto close = waiting.coordinator.close(); waiting.pump();
    CHECK(close.tryResult()->value().quiescent && next.settled.tryResult()->value().outcome == PlanOutcome::Cancelled && first.settled.tryResult()->value().outcome == PlanOutcome::Cancelled);
    waiting.scheduler->close();
}
/** @brief 资源静止未知时停止新接受，清理失败也不得释放驱动或伪造完整关闭。 */
void unsafe()
{
    Scenario scene;
    scene.driver->failurePath = "a"; scene.driver->safeFailure = false; scene.driver->closeFails = true;
    auto accepted = scene.coordinator.accept(target("[{id: a, plugin: demo}, {id: b, plugin: demo}]"), 0).value(); scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Failed && scene.coordinator.snapshot().value().unsafe);
    CHECK(scene.coordinator.accept(target("[]"), 1).error().code == ErrorCode::CleanupFailed && scene.driver->calls.size() == 1);
    auto closed = scene.coordinator.close(); scene.pump();
    CHECK(closed.isReady() && closed.tryResult()->hasValue() && !closed.tryResult()->value().quiescent);
    scene.scheduler->close();
    auto owner = std::make_shared<FakeScheduler>();
    auto retained = std::make_shared<ControlledDriver>();
    retained->closeFails = true;
    std::weak_ptr<ControlledDriver> weak = retained;
    auto facade = std::make_unique<ConfigurationCoordinator>(owner, retained);
    auto mounted = facade->accept(target("[{id: a, plugin: demo}]"), 0).value(); owner->runUntilIdle().value();
    CHECK(mounted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto failedClose = facade->close(); owner->runUntilIdle().value();
    CHECK(!failedClose.tryResult()->value().quiescent);
    facade.reset(); retained.reset();
    CHECK(!weak.expired() && weak.lock()->rows.count("a"));
    owner->close();
    std::weak_ptr<ControlledDriver> released;
    { Scenario successful; released = successful.driver; successful.close(); }
    CHECK(released.expired());
}
/** @brief worker 完成时 scheduler 已关闭，所有已接受目标都终结失败且不从 worker 改实际快照。 */
void delivery()
{
    Scenario scene;
    scene.driver->hold = true;
    auto first = scene.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); scene.pump();
    auto second = scene.coordinator.accept(target("[{id: b, plugin: demo}]"), 1).value(); scene.pump();
    CHECK(scene.scheduler->pending() == 0 && !first.settled.isReady() && !second.settled.isReady());
    scene.scheduler->close();
    auto action = scene.driver->held.front();
    std::thread worker([action] { action->done.resolve(StepReport{}); }); worker.join();
    CHECK(first.settled.tryResult()->error().code == ErrorCode::SchedulerClosed && second.settled.tryResult()->error().code == ErrorCode::SchedulerClosed);
    CHECK(scene.coordinator.snapshot().value().deliveryBlocked && !scene.coordinator.close().tryResult()->hasValue());
    Scenario queued;
    auto accepted = queued.coordinator.accept(target("[]"), 0).value(); queued.scheduler->close();
    CHECK(accepted.settled.isReady() && !accepted.settled.tryResult()->hasValue());
    Scenario preflight;
    preflight.driver->hold = true;
    auto previous = preflight.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); preflight.pump();
    preflight.driver->onPreflight = [&] {
        auto held = preflight.driver->held.front();
        std::thread completer([held, scheduler = preflight.scheduler] { scheduler->close(); held->done.resolve(StepReport{}); });
        completer.join();
    };
    auto rejected = preflight.coordinator.accept(target("[{id: b, plugin: demo}]"), 1);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::SchedulerClosed);
    CHECK(preflight.coordinator.snapshot().value().desiredRevision == 1 && !previous.settled.tryResult()->hasValue());
}
/** @brief 预检中接受新目标或关闭后，外层必须重查 CAS 和关门；规划重入也不能应用旧目标。 */
void reentry()
{
    Scenario scene;
    std::optional<AcceptedTarget> inner;
    scene.driver->onPreflight = [&] { inner.emplace(scene.coordinator.accept(target("[{id: inner, plugin: demo}]"), 0).value()); };
    auto outer = scene.coordinator.accept(target("[{id: outer, plugin: demo}]"), 0);
    CHECK(!outer.hasValue() && outer.error().code == ErrorCode::ConfigConflict && inner);
    scene.pump(); CHECK(scene.driver->rows.count("inner") && !scene.driver->rows.count("outer")); scene.close();
    Scenario closed;
    closed.driver->onPreflight = [&] { closed.coordinator.close(); };
    CHECK(closed.coordinator.accept(target("[]"), 0).error().code == ErrorCode::ContextClosing);
    closed.pump(); CHECK(closed.coordinator.snapshot().value().desiredRevision == 0); closed.scheduler->close();
    Scenario plan;
    std::optional<AcceptedTarget> replacement;
    auto first = plan.coordinator.accept(target("[{id: old, plugin: demo}]"), 0).value();
    plan.driver->onPlan = [&] { replacement.emplace(plan.coordinator.accept(target("[{id: current, plugin: demo}]"), 1).value()); };
    plan.pump();
    CHECK(first.settled.tryResult()->value().outcome == PlanOutcome::Superseded && replacement->settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(plan.driver->calls.size() == 1 && plan.driver->rows.count("current")); plan.close();
}
/** @brief 规划重入关闭后不能启动动作；调度器停止时也不能发布虚假的正常计划结果。 */
void planningGate()
{
    for (bool throws : {false, true}) {
        Scenario scene;
        scene.driver->throwPlan = throws;
        scene.driver->onPlan = [&] { scene.scheduler->close(); };
        auto accepted = scene.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value();
        scene.pump();
        CHECK(scene.driver->calls.empty() && scene.driver->rows.empty());
        CHECK(accepted.settled.isReady() && !accepted.settled.tryResult()->hasValue());
        CHECK(accepted.settled.tryResult()->error().code == ErrorCode::SchedulerClosed);
        CHECK(scene.coordinator.snapshot().value().deliveryBlocked);
        CHECK(!scene.coordinator.close().tryResult()->hasValue());
    }
    Scenario closed;
    std::optional<Task<CleanupReport>> close;
    closed.driver->onPlan = [&] { close = closed.coordinator.close(); };
    auto accepted = closed.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value();
    closed.pump();
    CHECK(closed.driver->calls.empty() && closed.driver->closeCalls == 1);
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Cancelled);
    CHECK(close && close->tryResult()->value().quiescent);
    closed.scheduler->close();
}
/** @brief 计划非法与驱动异常都可观察；worker 不能调用任何 owner 接口。 */
void guards()
{
    Scenario scene;
    scene.driver->duplicatePlan = true;
    auto accepted = scene.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); scene.pump();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Failed && scene.driver->calls.empty());
    scene.driver->duplicatePlan = false; scene.driver->throwPlan = true;
    auto failed = scene.coordinator.accept(target("[]"), 1).value(); scene.pump();
    CHECK(failed.settled.tryResult()->value().outcome == PlanOutcome::Failed);
    scene.driver->throwPlan = false;
    bool wrongThread = false;
    std::thread worker([&] {
        wrongThread = scene.coordinator.accept(target("[]"), 2).error().code == ErrorCode::WrongThread &&
            scene.coordinator.snapshot().error().code == ErrorCode::WrongThread && scene.coordinator.close().tryResult()->error().code == ErrorCode::WrongThread;
    }); worker.join(); CHECK(wrongThread);
    scene.driver->throwSnapshot = true;
    const auto unavailable = scene.coordinator.snapshot().value();
    CHECK(unavailable.runningError && unavailable.runningError->code == ErrorCode::CallbackFailed && unavailable.desiredRevision == 2 && unavailable.desired);
    scene.driver->throwSnapshot = false; scene.close();
    Scenario apply;
    apply.driver->throwApply = true;
    auto thrown = apply.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); apply.pump();
    CHECK(thrown.settled.tryResult()->value().outcome == PlanOutcome::Failed && apply.coordinator.snapshot().value().unsafe);
    apply.driver->throwClose = true;
    auto closed = apply.coordinator.close(); apply.pump();
    CHECK(!closed.tryResult()->hasValue() && closed.tryResult()->error().code == ErrorCode::CleanupFailed);
    apply.scheduler->close();
    Scenario malformed;
    malformed.driver->invalidKind = true;
    auto badPlan = malformed.coordinator.accept(target("[{id: a, plugin: demo}]"), 0).value(); malformed.pump();
    CHECK(badPlan.settled.tryResult()->value().outcome == PlanOutcome::Failed && malformed.driver->calls.empty());
    malformed.driver->invalidKind = false; malformed.driver->invalidOutcome = true;
    auto badStep = malformed.coordinator.accept(target("[{id: a, plugin: demo}]"), 1).value(); malformed.pump();
    CHECK(badStep.settled.tryResult()->value().entries.front().error && malformed.coordinator.snapshot().value().unsafe);
    malformed.close();
}
/** @brief 场景使用显式任务和 gate，不依赖时间推测竞争顺序。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "cas") cas();
        else if (name == "serial") serial();
        else if (name == "latest") latest();
        else if (name == "failure") partialFailure();
        else if (name == "pending") pending();
        else if (name == "shutdown") shutdown();
        else if (name == "unsafe") unsafe();
        else if (name == "delivery") delivery();
        else if (name == "reentry") reentry();
        else if (name == "planning_gate") planningGate();
        else if (name == "guards") guards();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
