/**
 * @file effect.cpp
 * @brief 实现 effect 初始化、资源清理及作用域关闭流程。
 */
#include <cordis/effect.hpp>
#include <atomic>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace cordis::detail {
struct ScopeControl;

/**
 * @brief 管理单个 effect 的异步初始化、取消及清理状态。
 */
struct EffectControl : std::enable_shared_from_this<EffectControl> {
    std::shared_ptr<IScheduler> scheduler;
    std::weak_ptr<ScopeControl> owner;
    EffectId id;
    std::string label;
    EffectState state = EffectState::SettingUp;
    bool setupPending = true;
    bool cleanupPending = false;
    bool closing = false;
    bool settled = false;
    bool cleanupFailed = false;
    bool queued = false;
    std::atomic_bool deliveryBlocked{false};
    CancellationSource cancellation;
    std::vector<CleanupFunction> cleanups;
    std::optional<CleanupFunction> activeCleanup;
    std::vector<CleanupFunction> failedCleanups;
    std::vector<std::weak_ptr<EffectControl>> children;
    std::vector<ErrorInfo> errors;
    Promise<void> setupResult;
    Promise<CleanupReport> closed;

    /** @brief 判断当前线程是否为关联调度器的所属线程。 */
    bool ownerThread() const { return scheduler->isCurrentThread(); }
    /** @brief 启动初始化并观察其异步结果。 */
    void start(EffectSetup setup);
    /** @brief 请求关闭 effect，并广播协作式取消状态。 */
    void requestClose();
    /** @brief 将后续生命周期工作投递到关联调度器。 */
    void enqueue();
    /** @brief 按逆序推进待执行的清理函数。 */
    void advance();
    /** @brief 完成关闭任务并通知所属作用域。 */
    void finish();
    /** @brief 生成当前 effect 的诊断快照。 */
    EffectSnapshot snapshot() const
    {
        return {id, label, state, setupPending, cleanupPending, deliveryBlocked.load(), cleanups.size(), failedCleanups.size(), errors};
    }

    /** @brief 监视调度器是否成功投递生命周期回调。 */
    void watchDelivery(const Task<void>& receipt)
    {
        std::weak_ptr<EffectControl> weak = shared_from_this();
        TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
            // 投递失败可能发生在工作线程上，或由已关闭的调度器报告。
            if (!result->hasValue()) if (auto self = weak.lock()) self->deliveryBlocked.store(true);
        });
    }
};

/**
 * @brief 管理作用域中的 effect 集合及其总体关闭状态。
 */
struct ScopeControl : std::enable_shared_from_this<ScopeControl> {
    /** @brief 创建由指定调度器管理的作用域状态。 */
    explicit ScopeControl(std::shared_ptr<IScheduler> target) : scheduler(std::move(target)) {}
    std::shared_ptr<IScheduler> scheduler;
    std::map<EffectId, std::shared_ptr<EffectControl>> effects;
    // 保守地固定作用域所有权；只有确认资源已释放后才能解除引用。
    std::shared_ptr<ScopeControl> resourcePin;
    bool closing = false;
    std::size_t activeCleanups = 0;
    std::uint64_t nextId = 0;
    std::vector<ErrorInfo> errors;
    Promise<CleanupReport> closed;

    /** @brief 收集 effect 错误并更新作用域中的存活集合。 */
    void completed(const std::shared_ptr<EffectControl>& effect)
    {
        errors.insert(errors.end(), effect->errors.begin(), effect->errors.end());
        if (!effect->cleanupFailed) effects.erase(effect->id);
        checkClosed();
    }

    /** @brief 在满足关闭条件时完成作用域关闭任务。 */
    void checkClosed()
    {
        if (effects.empty()) resourcePin.reset();
        if (!closing) return;
        for (const auto& entry : effects) if (!entry.second->settled) return;
        closed.resolve(CleanupReport{effects.empty(), errors});
    }
};

/**
 * @brief 将 effect 生命周期推进任务排入所属调度器。
 */
void EffectControl::enqueue()
{
    if (queued || settled) return;
    queued = true;
    auto self = shared_from_this();
    try {
        if (!scheduler->post({
            [self] { self->queued = false; self->advance(); },
            [self] { self->deliveryBlocked.store(true); }
        })) deliveryBlocked.store(true);
    } catch (...) { deliveryBlocked.store(true); }
}

/**
 * @brief 将 effect 标记为关闭并请求取消。
 */
void EffectControl::requestClose()
{
    if (closing || settled) return;
    closing = true;
    state = EffectState::Closing;
    cancellation.requestCancel();
    enqueue();
}

/**
 * @brief 执行初始化函数并将其完成结果投递回所属调度器。
 */
void EffectControl::start(EffectSetup setup)
{
    auto self = shared_from_this();
    auto observe = [self](const Task<void>& source) {
        self->watchDelivery(source.thenOn(self->scheduler, [self](const Result<void>& result) {
            self->setupPending = false;
            if (!result.hasValue()) {
                auto error = result.error();
                error.phase = "effect.setup";
                self->errors.push_back(error);
                self->setupResult.reject(std::move(error));
                self->requestClose();
            } else if (self->closing) {
                self->setupResult.reject(makeError(ErrorCode::OperationCancelled, "effect closed during setup"));
            } else {
                self->state = EffectState::Live;
                self->setupResult.resolve();
            }
            if (self->closing) self->enqueue();
        }));
    };
    try { observe(setup(EffectSetupContext(self))); }
    catch (const std::exception& error) { observe(failedTask(makeError(ErrorCode::ActivationFailed, error.what()))); }
    catch (...) { observe(failedTask(makeError(ErrorCode::ActivationFailed, "effect setup threw"))); }
}

/**
 * @brief 执行下一个待清理资源，并异步等待其完成。
 */
void EffectControl::advance()
{
    if (!closing || setupPending || cleanupPending || settled) return;
    if (cleanups.empty()) { finish(); return; }
    activeCleanup.emplace(std::move(cleanups.back()));
    cleanups.pop_back();
    cleanupPending = true;
    auto self = shared_from_this();
    auto observe = [self](const Task<void>& source) {
        self->watchDelivery(source.thenOn(self->scheduler, [self](const Result<void>& result) {
            if (!result.hasValue()) {
                auto error = result.error();
                error.phase = "effect.cleanup";
                self->errors.push_back(std::move(error));
                self->cleanupFailed = true;
            }
            // 清理失败时资源可能仍被占用，因此保留闭包及其所有权引用。
            if (!result.hasValue() && self->activeCleanup) self->failedCleanups.push_back(std::move(*self->activeCleanup));
            self->activeCleanup.reset();
            self->cleanupPending = false;
            if (auto scope = self->owner.lock()) --scope->activeCleanups;
            self->enqueue();
        }));
    };
    auto scope = owner.lock();
    if (scope) ++scope->activeCleanups;
    try { observe((*activeCleanup)()); }
    catch (const std::exception& error) { observe(failedTask(makeError(ErrorCode::CleanupFailed, error.what()))); }
    catch (...) { observe(failedTask(makeError(ErrorCode::CleanupFailed, "effect cleanup threw"))); }
}

/**
 * @brief 固化关闭结果并通知所属作用域。
 */
void EffectControl::finish()
{
    settled = true;
    if (!cleanupFailed) state = EffectState::Closed;
    closed.resolve(CleanupReport{!cleanupFailed, errors});
    if (auto scope = owner.lock()) scope->completed(shared_from_this());
}
} // namespace cordis::detail

namespace cordis {
/** @brief 调度器身份在作用域创建后保持不变。 */
bool EffectScope::usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const { return control_->scheduler == scheduler; }
/** @brief 仅广播取消；资源清理由之后的 close 屏障推进。 */
Result<void> EffectScope::requestCancel() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "cancel requires owner thread"));
    for (const auto& entry : control_->effects) entry.second->cancellation.requestCancel();
    return Result<void>::success();
}
/**
 * @brief 接管子 effect，并把关闭子 effect 注册为当前 effect 的清理步骤。
 * @param child 要接管的子 effect。
 * @return 接管成功时返回成功结果；上下文、线程或依赖图无效时返回错误。
 */
Result<void> EffectSetupContext::adopt(const EffectToken& child) const
{
    auto parent = control_.lock();
    if (!parent) return Result<void>::failure(makeError(ErrorCode::StaleContext, "effect context expired"));
    if (!parent->ownerThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "adopt requires owner thread"));
    if (parent->scheduler != child.control_->scheduler) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "adopt requires the same scheduler"));
    std::vector<std::shared_ptr<detail::EffectControl>> pending{child.control_};
    std::unordered_set<const detail::EffectControl*> visited;
    while (!pending.empty()) {
        auto candidate = std::move(pending.back());
        pending.pop_back();
        if (candidate == parent) return Result<void>::failure(makeError(ErrorCode::CleanupCycle, "effect adoption cycle"));
        if (!visited.insert(candidate.get()).second) continue;
        for (const auto& weak : candidate->children) if (auto next = weak.lock()) pending.push_back(std::move(next));
    }
    auto result = addCleanup([control = child.control_] {
        control->requestClose();
        Promise<void> completed;
        detail::TaskAccess::observe(control->closed.task(), [completed](Task<CleanupReport>::Snapshot report) {
            if (report->hasValue() && report->value().quiescent) completed.resolve();
            else {
                auto error = makeError(ErrorCode::CleanupFailed, "adopted effect did not release resources");
                if (report->hasValue()) error.causes = report->value().errors;
                else error.causes.push_back(report->error());
                completed.reject(std::move(error));
            }
        });
        return completed.task();
    });
    if (result.hasValue()) parent->children.push_back(child.control_);
    return result;
}

/**
 * @brief 将清理函数加入当前 effect 的清理栈。
 * @param cleanup 要注册的异步清理函数。
 * @return 注册成功时返回成功结果；上下文无效、线程不匹配或初始化已结束时返回错误。
 */
Result<void> EffectSetupContext::addCleanup(CleanupFunction cleanup) const
{
    auto control = control_.lock();
    if (!control) return Result<void>::failure(makeError(ErrorCode::StaleContext, "effect context expired"));
    if (!control->ownerThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "effect requires owner thread"));
    if (!control->setupPending) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "setup has settled"));
    if (!cleanup) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "empty cleanup"));
    control->cleanups.push_back(std::move(cleanup));
    return Result<void>::success();
}

/**
 * @brief 获取当前 effect 的取消令牌。
 * @return 当前 effect 的取消令牌；上下文过期时返回空令牌。
 */
CancellationToken EffectSetupContext::cancellation() const
{
    if (auto control = control_.lock()) return control->cancellation.token();
    return {};
}

/**
 * @brief 获取表示 effect 初始化结果的任务。
 * @return effect 初始化任务。
 */
Task<void> EffectToken::ready() const { return control_->setupResult.task(); }

/**
 * @brief 请求关闭 effect，并返回其关闭报告任务。
 * @return 关闭报告任务；从非所属线程调用时任务以 WrongThread 错误结束。
 */
Task<CleanupReport> EffectToken::close() const
{
    if (!control_->ownerThread()) {
        Promise<CleanupReport> failed;
        failed.reject(makeError(ErrorCode::WrongThread, "effect close requires owner thread"));
        return failed.task();
    }
    control_->requestClose();
    return control_->closed.task();
}

/**
 * @brief 在所属调度器线程读取 effect 状态。
 * @return 成功时返回状态快照，非所属线程调用时返回错误。
 */
Result<EffectSnapshot> EffectToken::snapshot() const
{
    if (!control_->ownerThread()) return Result<EffectSnapshot>::failure(makeError(ErrorCode::WrongThread, "effect snapshot requires owner thread"));
    return Result<EffectSnapshot>::success(control_->snapshot());
}

/**
 * @brief 创建受开放调度器管理的 effect 作用域。
 * @param scheduler 开放且所属线程为当前线程的调度器。
 * @throws std::invalid_argument 调度器为空、关闭或线程不匹配时抛出。
 */
EffectScope::EffectScope(std::shared_ptr<IScheduler> scheduler)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen()) throw std::invalid_argument("EffectScope requires an open owner-thread scheduler");
    control_ = std::make_shared<detail::ScopeControl>(std::move(scheduler));
}

/**
 * @brief 在作用域中注册并启动 effect。
 * @param setup effect 初始化函数。
 * @param label 用于诊断的 effect 标签。
 * @return 注册成功时返回 effect 句柄；不符合作用域状态或线程约束时返回错误。
 */
Result<EffectToken> EffectScope::effect(EffectSetup setup, std::string label) const
{
    if (!control_->scheduler->isCurrentThread()) return Result<EffectToken>::failure(makeError(ErrorCode::WrongThread, "effect registration requires owner thread"));
    if (control_->closing || control_->activeCleanups != 0) return Result<EffectToken>::failure(makeError(ErrorCode::ContextClosing, "scope closing or awaiting cleanup"));
    if (!control_->scheduler->isOpen()) return Result<EffectToken>::failure(makeError(ErrorCode::SchedulerClosed, "scope scheduler closed"));
    if (!setup || control_->nextId == std::numeric_limits<std::uint64_t>::max()) return Result<EffectToken>::failure(makeError(ErrorCode::InvalidArgument, "invalid setup or exhausted effect ids"));
    auto effect = std::make_shared<detail::EffectControl>();
    effect->scheduler = control_->scheduler;
    effect->owner = control_;
    effect->id = EffectId(++control_->nextId);
    effect->label = std::move(label);
    control_->resourcePin = control_;
    // 必须先登记 effect，再调用初始化代码，避免重入时快照遗漏它。
    control_->effects.emplace(effect->id, effect);
    effect->start(std::move(setup));
    return Result<EffectToken>::success(EffectToken(std::move(effect)));
}

/**
 * @brief 请求关闭作用域中的所有 effect。
 * @return 等待所有 effect 清理结果的任务。
 */
Task<CleanupReport> EffectScope::close() const
{
    if (!control_->scheduler->isCurrentThread()) {
        Promise<CleanupReport> failed;
        failed.reject(makeError(ErrorCode::WrongThread, "scope close requires owner thread"));
        return failed.task();
    }
    if (!control_->closing) {
        control_->closing = true;
        // 先广播所有取消请求；清理代码只会在后续排队任务中启动。
        for (const auto& entry : control_->effects) entry.second->requestClose();
        control_->checkClosed();
    }
    return control_->closed.task();
}

/**
 * @brief 在所属调度器线程读取作用域及其 effect 的状态。
 * @return 成功时返回作用域快照，非所属线程调用时返回错误。
 */
Result<ScopeSnapshot> EffectScope::snapshot() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<ScopeSnapshot>::failure(makeError(ErrorCode::WrongThread, "scope snapshot requires owner thread"));
    ScopeSnapshot snapshot{control_->closing, false, {}, control_->errors};
    for (const auto& entry : control_->effects) {
        snapshot.effects.push_back(entry.second->snapshot());
        snapshot.deliveryBlocked = snapshot.deliveryBlocked || snapshot.effects.back().deliveryBlocked;
    }
    return Result<ScopeSnapshot>::success(std::move(snapshot));
}
} // namespace cordis
