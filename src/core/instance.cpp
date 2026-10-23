/** @file instance.cpp
 * @brief 通过每代 EffectScope 屏障驱动配置收敛、失败回滚与实例终结。
 */
#include <cordis/instance.hpp>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <limits>
#include <map>

namespace cordis::detail {
/** @brief 为每类标识分配进程内不复用编号，耗尽时拒绝分配。 */
template<class Tag> Id<Tag> allocateId()
{
    static std::atomic<std::uint64_t> next{0};
    auto value = next.load();
    do {
        if (value == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("identity exhausted");
    } while (!next.compare_exchange_weak(value, value + 1));
    return Id<Tag>(value + 1);
}

/** @brief 构造已失败的查询任务，不运行用户回调。 */
template<class T> Task<T> queryFailure(ErrorCode code, const char* message)
{
    Promise<T> result;
    result.reject(makeError(code, message));
    return result.task();
}
/** @brief owner 线程单写的实例控制块；投递失败时保守固定所有权。 */
struct InstanceControl : std::enable_shared_from_this<InstanceControl> {
    std::shared_ptr<IScheduler> scheduler;
    EffectScope scope;
    std::shared_ptr<const Runtime::Activate> activate;
    std::shared_ptr<InstanceControl> pin;
    std::optional<EffectToken> activation;
    InstanceState state = InstanceState::Pending;
    bool stopping = false;
    std::atomic_bool blocked{false};
    Promise<void> ready;
    Promise<CleanupReport> stopped;
    Epoch epoch = 0;
    Epoch targetEpoch = 0;
    ConfigRevision revision = 1;
    std::string config;
    bool retiring = false;
    bool failed = false;
    bool queued = false;
    bool dirty = true;
    Promise<StableReport> target;
    InstanceId id = allocateId<InstanceTag>();
    RuntimeId runtime;
    OperationId operationId = allocateId<OperationTag>();
    std::map<OperationId, Task<StableReport>> operations;
    std::map<Epoch, Promise<ActiveReport>> active;

    /** @brief 创建独立资源作用域。 */
    InstanceControl(std::shared_ptr<IScheduler> target, std::shared_ptr<const Runtime::Activate> callback)
        : scheduler(std::move(target)), scope(scheduler), activate(std::move(callback))
    { operations.emplace(operationId, this->target.task()); }

    /** @brief 保留所有权并监视内部 continuation 的投递。 */
    void watch(const Task<void>& receipt)
    {
        std::weak_ptr<InstanceControl> weak = shared_from_this();
        TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
            if (!result->hasValue()) if (auto self = weak.lock()) self->blocked.store(true);
        });
    }

    /** @brief 将收敛动作排队，避免激活和回滚递归执行。 */
    void enqueue()
    {
        if (queued) return;
        queued = true;
        auto self = shared_from_this();
        try {
            if (!scheduler->post({[self] { self->queued = false; self->start(); },
                                  [self] { self->blocked.store(true); }})) blocked.store(true);
        } catch (...) { blocked.store(true); }
    }

    /** @brief 关闭旧代，只有确认资源静止后才允许创建新的作用域。 */
    void retire()
    {
        if (retiring) return;
        retiring = true;
        if (auto it = active.find(epoch); it != active.end())
            it->second.reject(makeError(ErrorCode::OperationCancelled, "epoch retired before activation"));
        state = InstanceState::Unloading;
        auto self = shared_from_this();
        watch(scope.close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
            if (!result.hasValue()) { self->blocked.store(true); return; }
            if (!result.value().quiescent) {
                if (self->stopping) {
                    self->target.resolve(StableReport{StableOutcome::Failed, self->revision, self->targetEpoch});
                    self->stopped.resolve(result.value());
                }
                else self->target.resolve(StableReport{StableOutcome::Failed, self->revision, self->targetEpoch});
                return;
            }
            self->activation.reset();
            self->retiring = false;
            if (self->stopping) {
                self->state = InstanceState::Disposed;
                self->target.resolve(StableReport{StableOutcome::Disposed, self->revision, self->targetEpoch});
                self->activate.reset();
                self->pin.reset();
                self->stopped.resolve(result.value());
            } else if (self->dirty) {
                self->scope = EffectScope(self->scheduler);
                self->enqueue();
            } else {
                self->state = InstanceState::Failed;
                self->target.resolve(StableReport{StableOutcome::Failed, self->revision, self->epoch});
            }
        }));
    }

    /** @brief 请求关闭，只有确认静止才能解除实例所有权 pin。 */
    Task<CleanupReport> stop()
    {
        if (!stopping) {
            stopping = true;
            ready.reject(makeError(ErrorCode::OperationCancelled, "instance stopped during activation"));
            if (retiring) {
                // 已完成但未静止的清理也必须能被后到 stop 观察。
                auto self = shared_from_this();
                watch(scope.close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
                    if (result.hasValue() && !result.value().quiescent) {
                        self->target.resolve(StableReport{StableOutcome::Failed, self->revision, self->targetEpoch});
                        self->stopped.resolve(result.value());
                    }
                }));
            } else retire();
        }
        return stopped.task();
    }

    /** @brief 在队列中登记激活屏障，setup 同步重入 stop 也受其保护。 */
    void start()
    {
        if (stopping || retiring || !dirty) return;
        dirty = false;
        failed = false;
        if (epoch == std::numeric_limits<Epoch>::max()) {
            ready.reject(makeError(ErrorCode::InvalidArgument, "epoch exhausted"));
            target.resolve(StableReport{StableOutcome::Failed, revision, epoch});
            failed = true;
            state = InstanceState::Failed;
            return;
        }
        ++epoch;
        targetEpoch = epoch;
        active.emplace(epoch, Promise<ActiveReport>());
        const auto captured = epoch;
        state = InstanceState::Loading;
        auto self = shared_from_this();
        auto accepted = scope.effect([self, captured](EffectSetupContext) {
            return (*self->activate)(Context(self, captured, self->config));
        }, "instance.activation");
        if (!accepted.hasValue()) { ready.reject(accepted.error()); stop(); return; }
        activation.emplace(accepted.value());
        watch(activation->ready().thenOn(scheduler, [self, captured](const Result<void>& result) {
            if (self->stopping || self->retiring || self->dirty || self->epoch != captured) return;
            if (result.hasValue()) {
                self->state = InstanceState::Active;
                self->ready.resolve();
                self->active.at(captured).resolve(ActiveReport{self->id, captured, self->revision});
                self->target.resolve(StableReport{StableOutcome::Active, self->revision, captured});
            } else {
                self->state = InstanceState::Failed;
                self->ready.reject(result.error());
                self->active.at(captured).reject(result.error());
                // 失败也要等待所有业务 effect 回滚，不能只释放激活组。
                self->failed = true;
                self->retire();
            }
        }));
    }
};
} // namespace cordis::detail

namespace cordis {
/** @brief 校验插件定义，保存可供多个实例共享的激活函数。 */
Runtime::Runtime(Activate activate)
{
    if (!activate) throw std::invalid_argument("empty plugin activation");
    activate_ = std::make_shared<const Activate>(std::move(activate));
    id_ = detail::allocateId<RuntimeTag>();
}

/** @brief 在调用用户代码前建立实例与激活所有权。 */
Instance Instance::mount(const Runtime& runtime, std::shared_ptr<IScheduler> scheduler, std::string config)
{
    if (!runtime.activate_) throw std::invalid_argument("runtime handle moved from");
    auto control = std::make_shared<detail::InstanceControl>(std::move(scheduler), runtime.activate_);
    control->pin = control;
    control->config = std::move(config);
    control->runtime = runtime.id();
    control->enqueue();
    return Instance(std::move(control));
}

/** @brief 读取共享的激活任务。 */
Task<void> Instance::ready() const { return control_->ready.task(); }

/** @brief owner 线程请求停止，跨线程不修改实例状态。 */
Task<CleanupReport> Instance::stop() const
{
    if (!control_->scheduler->isCurrentThread()) {
        Promise<CleanupReport> failed;
        failed.reject(makeError(ErrorCode::WrongThread, "instance stop requires owner thread"));
        return failed.task();
    }
    return control_->stop();
}

/** @brief 汇总实例和 EffectScope 状态。 */
Result<InstanceSnapshot> Instance::snapshot() const
{
    auto resources = control_->scope.snapshot();
    if (!resources.hasValue()) return Result<InstanceSnapshot>::failure(resources.error());
    return Result<InstanceSnapshot>::success({control_->state, control_->blocked.load(), resources.value(), control_->epoch, control_->revision, control_->id, control_->runtime});
}

/** @brief 检查弱上下文后登记业务资源。 */
Result<EffectToken> Context::effect(EffectSetup setup, std::string label) const
{
    auto control = control_.lock();
    if (!control) return Result<EffectToken>::failure(makeError(ErrorCode::StaleContext, "instance context expired"));
    if (!control->scheduler->isCurrentThread()) return Result<EffectToken>::failure(makeError(ErrorCode::WrongThread, "context requires owner thread"));
    if (control->stopping) return Result<EffectToken>::failure(makeError(ErrorCode::ContextClosing, "instance stopping"));
    if (epoch_ != control->epoch) return Result<EffectToken>::failure(makeError(ErrorCode::StaleContext, "old activation epoch"));
    if (control->retiring || control->dirty) return Result<EffectToken>::failure(makeError(ErrorCode::ContextClosing, "activation retiring"));
    return control->scope.effect(std::move(setup), std::move(label));
}

/** @brief 接受配置命令，旧操作立即被覆盖，但旧资源仍由屏障持有。 */
Result<OperationHandle> Instance::change(std::string config, ConfigRevision expectedRevision, bool revise) const
{
    auto& c = *control_;
    if (!c.scheduler->isCurrentThread()) return Result<OperationHandle>::failure(makeError(ErrorCode::WrongThread, "update requires owner thread"));
    if (c.stopping) return Result<OperationHandle>::failure(makeError(ErrorCode::InstanceDisposed, "instance terminated"));
    if (!c.scheduler->isOpen()) return Result<OperationHandle>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (c.retiring) {
        auto report = c.scope.close().tryResult();
        if (report && report->hasValue() && !report->value().quiescent)
            return Result<OperationHandle>::failure(makeError(ErrorCode::CleanupFailed, "old resources not released"));
    }
    if (expectedRevision != c.revision) return Result<OperationHandle>::failure(makeError(ErrorCode::ConfigConflict, "revision mismatch"));
    if (revise && c.revision == std::numeric_limits<ConfigRevision>::max()) return Result<OperationHandle>::failure(makeError(ErrorCode::InvalidArgument, "revision exhausted"));
    const auto nextOperation = detail::allocateId<OperationTag>();
    c.target.resolve(StableReport{StableOutcome::Superseded, c.revision, c.targetEpoch});
    c.target = Promise<StableReport>();
    c.targetEpoch = 0;
    c.operationId = nextOperation;
    c.operations.emplace(c.operationId, c.target.task());
    if (revise) ++c.revision;
    c.config = std::move(config);
    c.dirty = true;
    if (c.epoch == 0) c.enqueue();
    else if (c.state == InstanceState::Failed && !c.retiring) {
        c.scope = EffectScope(c.scheduler);
        c.enqueue();
    } else c.retire();
    return Result<OperationHandle>::success({c.revision, c.target.task(), c.operationId, c.id});
}

/** @brief 接受新配置并递增配置修订。 */
Result<OperationHandle> Instance::update(std::string config, ConfigRevision expectedRevision) const
{ return change(std::move(config), expectedRevision, true); }

/** @brief 仅在失败回滚完成后重试，不允许绕过清理失败。 */
Result<OperationHandle> Instance::retry() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<OperationHandle>::failure(makeError(ErrorCode::WrongThread, "retry requires owner thread"));
    if (control_->stopping) return Result<OperationHandle>::failure(makeError(ErrorCode::InstanceDisposed, "instance terminated"));
    if (!control_->failed || control_->retiring || control_->state != InstanceState::Failed)
        return Result<OperationHandle>::failure(makeError(ErrorCode::InvalidArgument, "retry requires quiescent failure"));
    return change(control_->config, control_->revision, false);
}

/** @brief 获取当前操作的所属实例和真实观察任务。 */
Result<OperationHandle> Instance::operation() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<OperationHandle>::failure(makeError(ErrorCode::WrongThread, "operation requires owner thread"));
    return Result<OperationHandle>::success({control_->revision, control_->target.task(), control_->operationId, control_->id});
}

/** @brief 根据宿主操作账目查询，避免调用方替换句柄内 Task 影响查询。 */
Task<StableReport> Instance::awaitTarget(const OperationHandle& operation) const
{
    if (!control_->scheduler->isCurrentThread()) return detail::queryFailure<StableReport>(ErrorCode::WrongThread, "awaitTarget requires owner thread");
    auto found = control_->operations.find(operation.id);
    if (operation.instance != control_->id || found == control_->operations.end())
        return detail::queryFailure<StableReport>(ErrorCode::InvalidArgument, "foreign or unknown operation");
    return found->second;
}

/** @brief 返回已登记代次的历史激活任务，不隐式创建未来等待项。 */
Task<ActiveReport> Instance::awaitActive(Epoch epoch) const
{
    if (!control_->scheduler->isCurrentThread()) return detail::queryFailure<ActiveReport>(ErrorCode::WrongThread, "awaitActive requires owner thread");
    auto found = control_->active.find(epoch);
    if (found == control_->active.end()) return detail::queryFailure<ActiveReport>(ErrorCode::InvalidArgument, "unknown epoch");
    return found->second.task();
}
} // namespace cordis
