/** @file coordinator.cpp
 * @brief 以 owner 队列协调目标接受、最新目标替代及驱动清理，投递故障只在原子门禁和 Promise 上发布。
 */
#include <cordis/loader/coordinator.hpp>
#include <atomic>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

namespace cordis::loader::detail {
/** @brief 跨线程投递故障只终结线程安全 Promise，不读取 owner 工作中的报告或驱动。 */
struct DeliveryGate {
    std::atomic_bool blocked{false};
    std::mutex mutex;
    std::map<std::uint64_t, Promise<PlanReport>> waiting;
    Promise<CleanupReport> closed;
    /** @brief 在接受提交前登记，和工作线程报告投递失败互斥。 */
    bool add(std::uint64_t revision, Promise<PlanReport> done)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (blocked.load()) return false;
        waiting.emplace(revision, std::move(done));
        return true;
    }
    /** @brief 调用者仍强持工作项及其 Promise，移除登记不会在锁内释放最后一个回调所有者。 */
    void remove(std::uint64_t revision)
    {
        std::lock_guard<std::mutex> lock(mutex);
        waiting.erase(revision);
    }
    /** @brief 先在锁内换出值，再在锁外终结全部已接受任务，禁止跨线程改 owner 账目。 */
    void fail(ErrorInfo error)
    {
        std::map<std::uint64_t, Promise<PlanReport>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (blocked.exchange(true)) return;
            pending.swap(waiting);
        }
        error.phase = "loader.delivery";
        for (const auto& item : pending) item.second.reject(error);
        closed.reject(std::move(error));
    }
};
/** @brief 工作项在途期间强持自身目标，历史报告完成后不保留目标和驱动。 */
struct Work {
    std::shared_ptr<const ValidatedCandidate> target;
    Promise<PlanReport> done;
    PlanReport report;
    std::vector<EntryOperation> operations;
    std::size_t next = 0;
    bool planned = false;
    bool inFlight = false;
    bool superseded = false;
};
/** @brief 全部运行状态只在 owner 修改，非静止或投递阻断时保守保留控制块及驱动。 */
struct CoordinatorControl : std::enable_shared_from_this<CoordinatorControl> {
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<IReconcileDriver> driver;
    std::shared_ptr<DeliveryGate> delivery = std::make_shared<DeliveryGate>();
    std::shared_ptr<CoordinatorControl> keepAlive;
    std::shared_ptr<const ValidatedCandidate> desired;
    std::shared_ptr<Work> active, pending;
    std::uint64_t revision = 0;
    bool closing = false, unsafe = false, scheduled = false, pumping = false, requested = false;
    bool driverClosing = false, driverDone = false, closePublished = false;
    std::size_t driverDepth = 0;
    std::optional<CleanupReport> cleanup;
    std::optional<ErrorInfo> closeError;
    /** @brief 运行驱动同步调用期间，嵌套事件循环不能提前进入下一次驱动调用。 */
    struct DriverAccess {
        CoordinatorControl& state;
        /** @brief 驱动调用入口增加深度。 */
        explicit DriverAccess(CoordinatorControl& owner) : state(owner) { ++state.driverDepth; }
        /** @brief 最外层驱动返回后，恢复曾因重入而延迟的 owner 泵送。 */
        ~DriverAccess() { if (--state.driverDepth == 0 && state.requested) state.schedule(); }
    };
    /** @brief 检查 owner；诊断和关闭允许在 scheduler 已停止后报告失败。 */
    Result<void> thread() const
    {
        if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "coordinator requires owner thread"));
        return Result<void>::success();
    }
    /** @brief 接受目标前检查执行入口，清理不确定时禁止继续应用新目标。 */
    Result<void> business() const
    {
        auto checked = thread();
        if (!checked.hasValue()) return checked;
        if (closing) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "coordinator closing"));
        if (delivery->blocked.load() || !scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "coordinator delivery unavailable"));
        if (unsafe) return Result<void>::failure(makeError(ErrorCode::CleanupFailed, "driver did not establish a safe checkpoint"));
        return Result<void>::success();
    }
    /** @brief 同步驱动或完成通知可能重入并关闭调度器，下一步执行前重新核对投递能力。 */
    bool deliveryOpen()
    {
        if (delivery->blocked.load()) return false;
        if (scheduler->isOpen()) return true;
        delivery->fail(makeError(ErrorCode::SchedulerClosed, "scheduler closed during coordination"));
        return false;
    }
    /** @brief 所有回执故障共用同一个线程安全门禁。 */
    void observeReceipt(const Task<void>& receipt)
    {
        cordis::detail::TaskAccess::observe(receipt, [gate = delivery](Task<void>::Snapshot result) {
            if (!result->hasValue()) gate->fail(result->error());
        });
    }
    /** @brief owner 投递只排队一次；同步驱动调用和当前 pump 内只记请求，不忙轮询。 */
    void schedule()
    {
        if (closePublished || delivery->blocked.load()) return;
        requested = true;
        if (scheduled || pumping || driverDepth != 0) return;
        scheduled = true;
        auto self = shared_from_this();
        keepAlive = self;
        ScheduledJob job;
        job.run = [self] {
            self->scheduled = false;
            try { self->pump(); }
            catch (const std::exception& error) { self->delivery->fail(makeError(ErrorCode::CallbackFailed, error.what())); }
            catch (...) { self->delivery->fail(makeError(ErrorCode::CallbackFailed, "coordinator pump threw")); }
        };
        job.discarded = [gate = delivery] { gate->fail(makeError(ErrorCode::SchedulerClosed, "coordinator job discarded")); };
        try {
            if (!scheduler->post(std::move(job))) delivery->fail(makeError(ErrorCode::SchedulerClosed, "coordinator post rejected"));
        } catch (const std::exception& error) { delivery->fail(makeError(ErrorCode::SchedulerClosed, error.what())); }
        catch (...) { delivery->fail(makeError(ErrorCode::SchedulerClosed, "coordinator post threw")); }
    }
    /** @brief 先移除 owner 工作索引再完成 Promise，容忍完成观察者重入接受新目标。 */
    void finish(std::shared_ptr<Work> work, PlanOutcome outcome)
    {
        work->report.outcome = outcome;
        for (auto& item : work->report.entries) if (item.status == EntryStatus::NotStarted)
            item.status = outcome == PlanOutcome::Superseded ? EntryStatus::Superseded : EntryStatus::Skipped;
        if (active == work) active.reset();
        if (pending == work) pending.reset();
        delivery->remove(work->report.revision);
        work->done.resolve(std::move(work->report));
    }
    /** @brief 后端 Task 失败无法证明资源状态，保守停在失败动作，不继续调用驱动。 */
    void completeStep(const std::shared_ptr<Work>& work, const Result<StepReport>& result)
    {
        work->inFlight = false;
        auto& item = work->report.entries.at(work->next);
        bool safe = false;
        if (result.hasValue()) {
            const auto& step = result.value();
            const bool valid = step.outcome == StepOutcome::Applied || step.outcome == StepOutcome::Pending || step.outcome == StepOutcome::Failed;
            safe = valid && step.safeToContinue;
            item.error = step.error;
            item.status = step.outcome == StepOutcome::Applied ? EntryStatus::Applied :
                          step.outcome == StepOutcome::Pending ? EntryStatus::Pending : EntryStatus::Failed;
            if (item.status == EntryStatus::Failed && !item.error)
                item.error = makeError(ErrorCode::CallbackFailed, valid ? "driver failure omitted its error" : "driver returned invalid step outcome");
            if (!safe && !item.error) item.error = makeError(ErrorCode::CleanupFailed, "driver checkpoint is unsafe");
            if (item.error) item.status = EntryStatus::Failed;
        } else { item.status = EntryStatus::Failed; item.error = result.error(); }
        if (item.error) work->report.errors.push_back(*item.error);
        unsafe = unsafe || !safe;
        ++work->next;
        if (!closing && !work->superseded && item.status == EntryStatus::Failed) finish(work, PlanOutcome::Failed);
        schedule();
    }
    /** @brief 关闭可在动作异步等待期间启动，不能等该动作先稳定后才广播停止。 */
    void startClose()
    {
        if (driverClosing) return;
        driverClosing = true;
        auto self = shared_from_this();
        try {
            Task<CleanupReport> task = [&] {
                DriverAccess access(*this);
                return driver->close();
            }();
            observeReceipt(task.thenOn(scheduler, [self](const Result<CleanupReport>& result) {
                self->driverDone = true;
                if (result.hasValue()) self->cleanup = result.value();
                else self->closeError = result.error();
                self->schedule();
            }));
        } catch (const std::exception& error) {
            driverDone = true;
            closeError = makeError(ErrorCode::CleanupFailed, error.what());
        } catch (...) { driverDone = true; closeError = makeError(ErrorCode::CleanupFailed, "driver close threw"); }
    }
    /** @brief 真正排空驱动且所有在途动作回执已处理后才完成关闭。 */
    void checkClosed()
    {
        if (!closing || !driverDone || active || pending || driverDepth != 0 || closePublished) return;
        closePublished = true;
        if (closeError) delivery->closed.reject(*closeError);
        else delivery->closed.resolve(*cleanup);
        if (!closeError && cleanup->quiescent) keepAlive.reset();
    }
    /** @brief 规划由驱动从实际状态完成，拒绝重复/空路径，限制报告大小。 */
    void plan(const std::shared_ptr<Work>& work)
    {
        try {
            auto result = [&] { DriverAccess access(*this); return driver->plan(*work->target); }();
            if (!result.hasValue()) work->report.errors.push_back(result.error());
            else {
                if (result.value().size() > 65536) throw std::invalid_argument("driver plan exceeds operation limit");
                std::set<std::string> paths;
                for (const auto& operation : result.value()) {
                    switch (operation.kind) {
                    case ChangeKind::NoOp: case ChangeKind::Add: case ChangeKind::Remove:
                    case ChangeKind::Disable: case ChangeKind::Restart: case ChangeKind::Recreate: break;
                    default: throw std::invalid_argument("driver returned invalid operation kind");
                    }
                    if (operation.path.empty() || !paths.insert(operation.path).second)
                        throw std::invalid_argument("driver plan contains empty or duplicate path");
                    work->report.entries.push_back({operation, EntryStatus::NotStarted, {}});
                }
                work->operations = std::move(result).value();
                work->planned = true;
            }
        } catch (const std::exception& error) { work->report.errors.push_back(makeError(ErrorCode::ConfigInvalid, error.what())); }
        catch (...) { work->report.errors.push_back(makeError(ErrorCode::CallbackFailed, "driver planning threw")); }
        if (!deliveryOpen()) return;
        if (!work->planned) finish(work, closing ? PlanOutcome::Cancelled : work->superseded ? PlanOutcome::Superseded : PlanOutcome::Failed);
    }
    /** @brief 每次泵送最多启动一个异步动作，新目标只在动作稳定后替换旧计划。 */
    void pump()
    {
        if (pumping || driverDepth != 0) { requested = true; return; }
        pumping = true;
        requested = false;
        /** @brief 泵送退出后才发布下一份 owner 工作，阻止嵌套循环形成双执行器。 */
        struct Exit {
            CoordinatorControl& state;
            /** @brief 最后一次 owner 调用返回后处理被延迟的排队请求。 */
            ~Exit() { state.pumping = false; if (state.requested) state.schedule(); }
        } exit{*this};
        if (!deliveryOpen()) return;
        if (closing) {
            startClose();
            if (pending) finish(pending, PlanOutcome::Cancelled);
            if (active && !active->inFlight) finish(active, PlanOutcome::Cancelled);
            checkClosed();
            return;
        }
        if (active && active->inFlight) return;
        if (active && active->superseded) finish(active, PlanOutcome::Superseded);
        if (!deliveryOpen()) return;
        if (closing) { schedule(); return; }
        if (unsafe) {
            if (active) finish(active, PlanOutcome::Failed);
            if (pending) { pending->report.errors.push_back(makeError(ErrorCode::CleanupFailed, "driver checkpoint is unsafe")); finish(pending, PlanOutcome::Failed); }
            return;
        }
        if (!active) { active = std::move(pending); pending.reset(); }
        if (!active) return;
        auto work = active;
        if (!work->planned) plan(work);
        if (!deliveryOpen()) return;
        if (active != work) { if (pending || closing) schedule(); return; }
        if (closing || work->superseded) { schedule(); return; }
        if (work->next == work->operations.size()) {
            bool hasPending = false;
            for (const auto& item : work->report.entries) hasPending = hasPending || item.status == EntryStatus::Pending;
            finish(work, hasPending ? PlanOutcome::Pending : PlanOutcome::Applied);
            if (pending || closing) schedule();
            return;
        }
        work->inFlight = true;
        work->report.entries.at(work->next).status = EntryStatus::Running;
        auto self = shared_from_this();
        try {
            auto task = [&] {
                DriverAccess access(*this);
                return driver->apply(work->operations[work->next], work->target, work->report.revision);
            }();
            observeReceipt(task.thenOn(scheduler, [self, work](const Result<StepReport>& result) { self->completeStep(work, result); }));
        } catch (const std::exception& error) {
            completeStep(work, Result<StepReport>::failure(makeError(ErrorCode::CallbackFailed, error.what())));
        } catch (...) { completeStep(work, Result<StepReport>::failure(makeError(ErrorCode::CallbackFailed, "driver apply threw"))); }
    }
};
} // namespace cordis::loader::detail

namespace cordis::loader {
/** @brief 验证调用约束，不在构造期触碰驱动业务状态。 */
ConfigurationCoordinator::ConfigurationCoordinator(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<IReconcileDriver> driver)
{
    if (!scheduler || !driver || !scheduler->isCurrentThread() || !scheduler->isOpen())
        throw std::invalid_argument("coordinator requires driver and open owner scheduler");
    control_ = std::make_shared<detail::CoordinatorControl>();
    control_->scheduler = std::move(scheduler);
    control_->driver = std::move(driver);
}
/** @brief 已开始的工作保持自保留，facade 释放不在错误线程销毁驱动资源。 */
ConfigurationCoordinator::~ConfigurationCoordinator() = default;
/** @brief 同步预检之后再次比较修订，接受后执行失败也保留该目标。 */
Result<AcceptedTarget> ConfigurationCoordinator::accept(ValidatedCandidate target, std::uint64_t expected)
{
    auto state = control_;
    auto checked = state->business();
    if (!checked.hasValue()) return Result<AcceptedTarget>::failure(checked.error());
    if (!target.source().isValid()) return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigInvalid, "candidate was moved from"));
    if (state->revision != expected || state->revision == std::numeric_limits<std::uint64_t>::max())
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigConflict, "desired revision conflict or exhausted"));
    auto candidate = std::make_shared<const ValidatedCandidate>(std::move(target));
    try {
        detail::CoordinatorControl::DriverAccess access(*state);
        auto supported = state->driver->preflight(*candidate);
        if (!supported.hasValue()) return Result<AcceptedTarget>::failure(supported.error());
    } catch (const std::exception& error) { return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigInvalid, error.what())); }
    catch (...) { return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigInvalid, "driver preflight threw")); }
    auto finalCheck = state->business();
    if (!finalCheck.hasValue()) return Result<AcceptedTarget>::failure(finalCheck.error());
    if (state->revision != expected) return Result<AcceptedTarget>::failure(makeError(ErrorCode::ConfigConflict, "desired changed during preflight"));
    auto work = std::make_shared<detail::Work>();
    work->target = candidate;
    work->report.revision = state->revision + 1;
    if (!state->delivery->add(work->report.revision, work->done))
        return Result<AcceptedTarget>::failure(makeError(ErrorCode::SchedulerClosed, "coordinator delivery unavailable"));
    auto previous = state->pending;
    state->pending = work;
    state->desired = std::move(candidate);
    state->revision = work->report.revision;
    if (state->active) state->active->superseded = true;
    if (previous) state->finish(previous, PlanOutcome::Superseded);
    state->schedule();
    return Result<AcceptedTarget>::success({work->report.revision, work->done.task()});
}
/** @brief 快照的实际部分严格来自驱动，不通过已接受文本推断。 */
Result<CoordinatorSnapshot> ConfigurationCoordinator::snapshot() const
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue()) return Result<CoordinatorSnapshot>::failure(checked.error());
    std::vector<RunningEntry> running;
    std::optional<ErrorInfo> runningError;
    try {
        auto observed = [&] { detail::CoordinatorControl::DriverAccess access(*state); return state->driver->snapshot(); }();
        if (observed.hasValue()) running = std::move(observed).value();
        else runningError = observed.error();
    } catch (const std::exception& error) { runningError = makeError(ErrorCode::CallbackFailed, error.what()); }
    catch (...) { runningError = makeError(ErrorCode::CallbackFailed, "driver snapshot threw"); }
    return Result<CoordinatorSnapshot>::success({state->revision, state->desired,
        state->active ? std::optional<std::uint64_t>(state->active->report.revision) : std::nullopt,
        state->closing, state->unsafe, state->delivery->blocked.load(), std::move(running), std::move(runningError)});
}
/** @brief 关闭不撤销用户目标，只关闭执行入口并等待驱动及在途报告排空。 */
Task<CleanupReport> ConfigurationCoordinator::close()
{
    auto state = control_;
    auto checked = state->thread();
    if (!checked.hasValue()) { Promise<CleanupReport> failed; failed.reject(checked.error()); return failed.task(); }
    if (!state->closing) {
        state->closing = true;
        if (!state->scheduler->isOpen()) state->delivery->fail(makeError(ErrorCode::SchedulerClosed, "scheduler closed before coordinator"));
        state->schedule();
    }
    return state->delivery->closed.task();
}
} // namespace cordis::loader
