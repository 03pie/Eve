/** @file instance.cpp
 * @brief 通过每代 EffectScope 屏障驱动配置收敛、失败回滚与实例终结。
 */
#include <cordis/instance.hpp>
#include <atomic>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <limits>
#include <map>
#include <set>

namespace cordis::detail {
/** @brief 一次清理调用的访问窗口；完成可在工作线程原子撤销。 */
struct CleanupAccess {
    std::weak_ptr<IScheduler> scheduler;
    CleanupSnapshot snapshot;
    std::atomic_bool valid{true};
};
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
    std::vector<std::shared_ptr<CommittedLease>> committed;
    std::shared_ptr<ServiceRegistry> services;
    std::vector<ServiceRequirement> dependencies;
    std::vector<ServiceOutput> declarations;
    RealmMap realms;
    std::vector<ProviderHandle> outputs;
    std::shared_ptr<ScheduledJob> subscription;
    std::optional<ErrorInfo> provisionError;
    std::optional<ErrorInfo> failure;
    std::vector<std::shared_ptr<InstanceControl>> children;
    std::optional<CleanupReport> retirementFailure;
    std::vector<ErrorInfo> childErrors;

    /** @brief 为自动恢复或被覆盖的 Pending 结果建立独立操作，不改变配置修订。 */
    void newOperation()
    {
        target.resolve(StableReport{StableOutcome::Superseded, revision, targetEpoch});
        target = Promise<StableReport>();
        operationId = allocateId<OperationTag>();
        operations.emplace(operationId, target.task());
        targetEpoch = 0;
    }

    /** @brief 依赖退役立即关闭本代业务，保留最新配置并等待可恢复目标。 */
    void invalidate()
    {
        if (stopping || retiring) return;
        if (!dirty) newOperation();
        dirty = true;
        retire();
    }

    /** @brief 将服务固定到本代，并为托管注册表连接失效传播。 */
    Result<std::shared_ptr<CommittedLease>> commit(const ServiceRegistry& registry, const ServiceKey& key, std::type_index type)
    {
        for (const auto& lease : committed) if (matchesCommitted(lease, key, type, registry.control_)) return Result<std::shared_ptr<CommittedLease>>::success(lease);
        std::weak_ptr<InstanceControl> weak = shared_from_this();
        const auto captured = epoch;
        std::function<void()> invalidated;
        if (services && services->control_ == registry.control_) invalidated = [weak] { if (auto self = weak.lock()) self->invalidate(); };
        auto lease = registry.commitErased(key, type, scheduler, id, epoch, [weak, captured] {
            auto self = weak.lock();
            return self ? self->business(captured) : Result<void>::failure(makeError(ErrorCode::StaleContext, "instance expired"));
        }, std::move(invalidated));
        if (lease.hasValue()) committed.push_back(lease.value());
        return lease;
    }

    /** @brief 在激活作用域内保留输出，插件不获得发布或释放控制。 */
    Result<BindingId> provide(ServiceKey key, std::shared_ptr<void> object, std::type_index type)
    {
        if (!services) return Result<BindingId>::failure(makeError(ErrorCode::InvalidArgument, "no managed registry"));
        if (state != InstanceState::Loading) return Result<BindingId>::failure(makeError(ErrorCode::InvalidArgument, "provide requires activation setup"));
        ServiceMetadata metadata;
        {
            auto found = std::find_if(declarations.begin(), declarations.end(), [&](const ServiceOutput& output) {
                const auto resolved = realms.resolve(output.key);
                return !(resolved < key) && !(key < resolved);
            });
            if (found == declarations.end() || found->type != type) {
                auto error = makeError(found == declarations.end() ? ErrorCode::ProvisionIncomplete : ErrorCode::DependencyVersion,
                    "undeclared or incorrectly typed output: " + key.name);
                provisionError.emplace(error);
                return Result<BindingId>::failure(std::move(error));
            }
            metadata = found->metadata;
        }
        auto staged = services->stageErased(std::move(key), std::move(object), type, std::move(metadata));
        if (!staged.hasValue()) { provisionError.emplace(staged.error()); return Result<BindingId>::failure(staged.error()); }
        auto output = staged.value();
        output.setOwner(id);
        const auto binding = output.snapshot().value().id;
        outputs.push_back(std::move(output));
        return Result<BindingId>::success(binding);
    }

    /** @brief 校验本代业务入口；清理中也不能用捕获的普通引用发起新调用。 */
    Result<void> business(Epoch captured) const
    {
        if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "context requires owner thread"));
        if (!scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
        if (epoch != captured) return Result<void>::failure(makeError(ErrorCode::StaleContext, "old epoch"));
        if (stopping || retiring || dirty) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "epoch closing"));
        for (const auto& effect : scope.snapshot().value().effects)
            if (effect.cleanupPending) return Result<void>::failure(makeError(ErrorCode::ContextClosing, "cleanup in progress"));
        return Result<void>::success();
    }

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

    /** @brief 资源清理及租约调用均静止后，释放 committed 并收敛实例目标。 */
    void finishRetire(const CleanupReport& report)
    {
        activation.reset();
        for (const auto& output : outputs) {
            auto released = output.release();
            if (!released.hasValue()) { blocked.store(true); return; }
        }
        outputs.clear();
        for (const auto& lease : committed) releaseCommitted(lease);
        committed.clear();
        children.clear();
        retiring = false;
        if (stopping) {
            state = InstanceState::Disposed;
            target.resolve(StableReport{StableOutcome::Disposed, revision, targetEpoch});
            activate.reset();
            subscription.reset();
            if (services) services->forgetInstance(id);
            pin.reset();
            stopped.resolve(report);
        } else if (dirty) {
            scope = EffectScope(scheduler);
            enqueue();
        } else {
            state = InstanceState::Failed;
            target.resolve(StableReport{StableOutcome::Failed, revision, epoch});
        }
    }

    /** @brief 子实例或本代清理未静止时保留资源，并向当前目标及后到 stop 报告。 */
    void failRetire(const CleanupReport& report)
    {
        auto error = makeError(ErrorCode::CleanupFailed, "epoch or child cleanup did not quiesce");
        error.causes = report.errors;
        failure.emplace(std::move(error));
        retirementFailure.emplace(report);
        target.resolve(StableReport{StableOutcome::Failed, revision, targetEpoch});
        if (stopping) stopped.resolve(report);
    }

    /** @brief 消费者和子实例全部退出后才启动提供者自己的 effect 清理。 */
    void beginCleanup()
    {
        auto self = shared_from_this();
        watch(scope.close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
            if (!result.hasValue()) { self->blocked.store(true); return; }
            auto report = result.value();
            report.errors.insert(report.errors.begin(), self->childErrors.begin(), self->childErrors.end());
            if (!result.value().quiescent) {
                self->failRetire(report);
                return;
            }
            self->watch(drainCommitted(self->committed).thenOn(self->scheduler, [self, report](const Result<void>& drained) {
                if (drained.hasValue()) self->finishRetire(report);
                else self->blocked.store(true);
            }));
        }));
    }

    /** @brief 先广播取消与依赖失效，再等待输出的全部消费者，最后清理自身。 */
    void retire()
    {
        if (retiring) return;
        retiring = true;
        if (auto it = active.find(epoch); it != active.end())
            it->second.reject(makeError(ErrorCode::OperationCancelled, "epoch retired before activation"));
        state = InstanceState::Unloading;
        scope.requestCancel();
        std::vector<Task<CleanupReport>> childTasks;
        for (const auto& child : children) childTasks.push_back(child->stop());
        for (const auto& output : outputs) output.retire();
        auto pending = std::make_shared<std::size_t>(outputs.size());
        Promise<void> unused;
        if (outputs.empty()) unused.resolve();
        for (const auto& output : outputs) {
            TaskAccess::observe(output.whenUnused(), [pending, unused](Task<void>::Snapshot result) {
                if (!result->hasValue()) { unused.reject(result->error()); return; }
                if (--*pending == 0) unused.resolve();
            });
        }
        auto self = shared_from_this();
        Promise<CleanupReport> childrenDone;
        auto childPending = std::make_shared<std::size_t>(childTasks.size());
        auto childReport = std::make_shared<CleanupReport>(CleanupReport{true, {}});
        if (childTasks.empty()) childrenDone.resolve(*childReport);
        for (const auto& task : childTasks) {
            TaskAccess::observe(task, [childPending, childReport, childrenDone](Task<CleanupReport>::Snapshot result) {
                if (result->hasValue()) {
                    childReport->quiescent = childReport->quiescent && result->value().quiescent;
                    childReport->errors.insert(childReport->errors.end(), result->value().errors.begin(), result->value().errors.end());
                } else { childReport->quiescent = false; childReport->errors.push_back(result->error()); }
                if (--*childPending == 0) childrenDone.resolve(*childReport);
            });
        }
        watch(childrenDone.task().thenOn(scheduler, [self, unused](const Result<CleanupReport>& result) {
            if (!result.hasValue()) { self->blocked.store(true); return; }
            if (!result.value().quiescent) { self->failRetire(result.value()); return; }
            self->childErrors = result.value().errors;
            self->watch(unused.task().thenOn(self->scheduler, [self](const Result<void>& available) {
                if (available.hasValue()) self->beginCleanup();
                else self->blocked.store(true);
            }));
        }));
    }

    /** @brief 请求关闭，只有确认静止才能解除实例所有权 pin。 */
    Task<CleanupReport> stop()
    {
        if (!stopping) {
            stopping = true;
            ready.reject(makeError(ErrorCode::OperationCancelled, "instance stopped during activation"));
            if (retirementFailure) {
                auto self = shared_from_this();
                watch(completedTask().thenOn(scheduler, [self](const Result<void>&) { self->stopped.resolve(*self->retirementFailure); }));
                return stopped.task();
            }
            if (retiring) {
                // 已完成但未静止的清理也必须能被后到 stop 观察。
                auto self = shared_from_this();
                auto existing = scope.snapshot().value().closing;
                if (existing) watch(scope.close().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
                    if (result.hasValue() && !result.value().quiescent) {
                        self->target.resolve(StableReport{StableOutcome::Failed, self->revision, self->targetEpoch});
                        self->stopped.resolve(result.value());
                    }
                }));
            } else retire();
        }
        return stopped.task();
    }

    /** @brief 预检失败形成新的收敛操作，已返回的 Pending 结果保持历史语义。 */
    void failPreflight(const ErrorInfo& error)
    {
        if (auto previous = target.task().tryResult())
            if (previous->hasValue() && previous->value().outcome == StableOutcome::PendingMissingDependency) newOperation();
        failed = true;
        dirty = false;
        state = InstanceState::Failed;
        failure.emplace(error);
        ready.reject(error);
        target.resolve(StableReport{StableOutcome::Failed, revision, 0});
    }

    /** @brief 在队列中登记激活屏障，setup 同步重入 stop 也受其保护。 */
    void start()
    {
        if (stopping || retiring || !dirty) return;
        failure.reset();
        childErrors.clear();
        if (!dependencies.empty() && !services) {
            failPreflight(makeError(ErrorCode::InvalidArgument, "declared dependencies require managed registry"));
            return;
        }
        if (services) {
            auto graph = services->checkDeclarations(id);
            if (!graph.hasValue()) {
                failPreflight(graph.error());
                return;
            }
        }
        for (const auto& dependency : dependencies) {
            auto binding = services->findErased(realms.resolve(dependency.key), dependency.type, dependency.minimumMinor, dependency.capabilities);
            if (!binding.hasValue()) {
                if (binding.error().code == ErrorCode::DependencyMissing) {
                    state = InstanceState::Pending;
                    target.resolve(StableReport{StableOutcome::PendingMissingDependency, revision, 0});
                } else {
                    failPreflight(binding.error());
                }
                return;
            }
        }
        if (auto previous = target.task().tryResult())
            if (previous->hasValue() && previous->value().outcome == StableOutcome::PendingMissingDependency) newOperation();
        dirty = false;
        failed = false;
        provisionError.reset();
        if (epoch == std::numeric_limits<Epoch>::max()) {
            failure.emplace(makeError(ErrorCode::InvalidArgument, "epoch exhausted"));
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
        for (const auto& dependency : dependencies) {
            auto lease = commit(*services, realms.resolve(dependency.key), dependency.type);
            if (!lease.hasValue()) {
                failure.emplace(lease.error());
                ready.reject(lease.error()); active.at(epoch).reject(lease.error());
                failed = true; retire(); return;
            }
        }
        auto self = shared_from_this();
        auto accepted = scope.effect([self, captured](EffectSetupContext) {
            return (*self->activate)(Context(self, captured, self->config, self->realms));
        }, "instance.activation");
        if (!accepted.hasValue()) { failure.emplace(accepted.error()); ready.reject(accepted.error()); stop(); return; }
        activation.emplace(accepted.value());
        watch(activation->ready().thenOn(scheduler, [self, captured](const Result<void>& result) {
            if (self->stopping || self->retiring || self->dirty || self->epoch != captured) return;
            if (result.hasValue()) {
                if (!self->provisionError) for (const auto& declaration : self->declarations) {
                    if (!declaration.required) continue;
                    const auto key = self->realms.resolve(declaration.key);
                    const bool present = std::any_of(self->outputs.begin(), self->outputs.end(), [&](const ProviderHandle& output) {
                        const auto actual = output.snapshot().value().key;
                        return !(actual < key) && !(key < actual);
                    });
                    if (!present) {
                        self->provisionError.emplace(makeError(ErrorCode::ProvisionIncomplete, "required output missing: " + key.name));
                        break;
                    }
                }
                if (!self->provisionError) for (const auto& output : self->outputs) {
                    auto published = output.publish();
                    if (!published.hasValue()) { self->provisionError.emplace(published.error()); break; }
                }
                if (self->provisionError) {
                    self->failure.emplace(*self->provisionError);
                    self->ready.reject(*self->provisionError);
                    self->active.at(captured).reject(*self->provisionError);
                    self->failed = true; self->retire(); return;
                }
                self->state = InstanceState::Active;
                self->ready.resolve();
                self->active.at(captured).resolve(ActiveReport{self->id, captured, self->revision});
                self->target.resolve(StableReport{StableOutcome::Active, self->revision, captured});
            } else {
                self->state = InstanceState::Failed;
                self->failure.emplace(result.error());
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
/** @brief 资源命名先验证 owner/epoch/门禁，避免用旧 Context 创建新贡献。 */
Result<ContextIdentity> Context::identity() const
{
    auto self = control_.lock();
    if (!self) return Result<ContextIdentity>::failure(makeError(ErrorCode::StaleContext, "instance expired"));
    auto allowed = self->business(epoch_);
    if (!allowed.hasValue()) return Result<ContextIdentity>::failure(allowed.error());
    return Result<ContextIdentity>::success({self->id, epoch_});
}
/** @brief 事件记录只捕获弱实例与代次，分发时重新检查退役及清理权限。 */
Result<std::function<Result<void>()>> Context::eventGuard(const std::shared_ptr<IScheduler>& scheduler) const
{
    using Output = Result<std::function<Result<void>()>>;
    auto self = control_.lock();
    if (!self) return Output::failure(makeError(ErrorCode::StaleContext, "instance expired"));
    auto allowed = self->business(epoch_);
    if (!allowed.hasValue()) return Output::failure(allowed.error());
    if (self->scheduler != scheduler) return Output::failure(makeError(ErrorCode::InvalidArgument, "resource adapter requires same scheduler"));
    return Output::success([weak = control_, captured = epoch_] {
        auto owner = weak.lock();
        return owner ? owner->business(captured) : Result<void>::failure(makeError(ErrorCode::StaleContext, "event owner expired"));
    });
}
/** @brief 子实例沿用父视图与索引，排队激活前即纳入父代所有权。 */
Result<Instance> Context::inject(const Runtime& runtime, std::string config) const
{
    auto self = control_.lock();
    if (!self) return Result<Instance>::failure(makeError(ErrorCode::StaleContext, "instance expired"));
    auto allowed = self->business(epoch_);
    if (!allowed.hasValue()) return Result<Instance>::failure(allowed.error());
    try {
        auto child = Instance::mount(runtime, self->scheduler, std::move(config), self->services, realms_);
        if (self->services) self->services->attachParent(child.control_->id, self->id);
        self->children.push_back(child.control_);
        return Result<Instance>::success(std::move(child));
    } catch (const std::invalid_argument& error) {
        return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, error.what()));
    }
}
/** @brief 保留父视图的其余覆盖项，只有当前代业务有效时允许派生。 */
Result<Context> Context::isolate(std::string name, RealmId realm) const
{
    auto self = control_.lock();
    if (!self) return Result<Context>::failure(makeError(ErrorCode::StaleContext, "instance expired"));
    auto allowed = self->business(epoch_);
    if (!allowed.hasValue()) return Result<Context>::failure(allowed.error());
    if (!self->services || !self->services->containsRealm(realm)) return Result<Context>::failure(makeError(ErrorCode::InvalidArgument, "foreign realm"));
    auto derived = realms_.isolate(std::move(name), realm);
    if (!derived.hasValue()) return Result<Context>::failure(derived.error());
    return Result<Context>::success(Context(control_, epoch_, config_, derived.value()));
}
/** @brief 托管输出也受本代业务权限检查，停止后不得新增绑定。 */
Result<BindingId> Context::provideErased(ServiceKey key, std::shared_ptr<void> object, std::type_index type) const
{
    auto self = control_.lock();
    if (!self) return Result<BindingId>::failure(makeError(ErrorCode::StaleContext, "instance expired"));
    auto allowed = self->business(epoch_);
    if (!allowed.hasValue()) return Result<BindingId>::failure(allowed.error());
    return self->provide(realms_.resolve(std::move(key)), std::move(object), type);
}

/** @brief 消费托管索引，不允许无注册表实例假定存在默认服务。 */
Result<std::shared_ptr<detail::CommittedLease>> Context::consumeManaged(const ServiceKey& key, std::type_index type) const
{
    auto self = control_.lock();
    if (!self || !self->services) return Result<std::shared_ptr<detail::CommittedLease>>::failure(makeError(ErrorCode::StaleContext, "managed registry unavailable"));
    return consumeErased(*self->services, key, type);
}
/** @brief 固定本代服务租约，清理失败时保留账目而非提前释放提供者。 */
Result<std::shared_ptr<detail::CommittedLease>> Context::consumeErased(const ServiceRegistry& registry,
    const ServiceKey& key, std::type_index type) const
{
    using Output = Result<std::shared_ptr<detail::CommittedLease>>;
    auto control = control_.lock();
    if (!control) return Output::failure(makeError(ErrorCode::StaleContext, "instance context expired"));
    auto allowed = control->business(epoch_);
    if (!allowed.hasValue()) return Output::failure(allowed.error());
    return control->commit(registry, realms_.resolve(key), type);
}

/** @brief 检查清理调用窗口和线程，不允许异步清理结束后复用旧能力。 */
Result<CleanupSnapshot> CleanupContext::snapshot() const
{
    auto access = access_.lock();
    if (!access || !access->valid.load()) return Result<CleanupSnapshot>::failure(makeError(ErrorCode::StaleContext, "cleanup access expired"));
    auto scheduler = access->scheduler.lock();
    if (!scheduler || !scheduler->isCurrentThread()) return Result<CleanupSnapshot>::failure(makeError(ErrorCode::WrongThread, "cleanup context requires owner thread"));
    return Result<CleanupSnapshot>::success(access->snapshot);
}

/** @brief 用 Effect 屏障登记清理；源任务真正完成即撤销清理访问窗口。 */
Result<EffectToken> Context::onCleanup(InstanceCleanup cleanup, std::string label) const
{
    if (!cleanup) return Result<EffectToken>::failure(makeError(ErrorCode::InvalidArgument, "empty instance cleanup"));
    auto control = control_.lock();
    if (!control) return Result<EffectToken>::failure(makeError(ErrorCode::StaleContext, "instance context expired"));
    if (!control->scheduler->isCurrentThread()) return Result<EffectToken>::failure(makeError(ErrorCode::WrongThread, "cleanup registration requires owner thread"));
    const CleanupSnapshot snapshot{control->id, control->runtime, epoch_, control->revision, config_};
    std::weak_ptr<IScheduler> scheduler = control->scheduler;
    return effect([cleanup = std::move(cleanup), snapshot, scheduler](EffectSetupContext setup) {
        auto added = setup.addCleanup([cleanup, snapshot, scheduler] {
            auto access = std::make_shared<detail::CleanupAccess>();
            access->scheduler = scheduler;
            access->snapshot = snapshot;
            try {
                auto task = cleanup(CleanupContext(access));
                detail::TaskAccess::observe(task, [access](Task<void>::Snapshot) { access->valid.store(false); });
                return task;
            } catch (...) {
                access->valid.store(false);
                throw;
            }
        });
        return added.hasValue() ? completedTask() : failedTask(added.error());
    }, std::move(label));
}

/** @brief 校验插件定义，保存可供多个实例共享的激活函数。 */
Runtime::Runtime(Activate activate, std::vector<ServiceRequirement> dependencies, std::vector<ServiceOutput> outputs)
{
    if (!activate) throw std::invalid_argument("empty plugin activation");
    auto valid = [](const ServiceKey& key, std::type_index type, const std::vector<std::string>& capabilities) {
        if (key.name.empty() || key.interfaceMajor == 0 || !key.realm.valid() || type == typeid(void)) return false;
        std::set<std::string> names;
        for (const auto& name : capabilities) if (name.empty() || !names.insert(name).second) return false;
        return true;
    };
    std::set<ServiceKey> dependencyKeys, outputKeys;
    for (const auto& dependency : dependencies)
        if (!valid(dependency.key, dependency.type, dependency.capabilities) || !dependencyKeys.insert(dependency.key).second)
            throw std::invalid_argument("invalid or duplicate service requirement");
    for (const auto& output : outputs)
        if (!valid(output.key, output.type, output.metadata.capabilities) || !outputKeys.insert(output.key).second)
            throw std::invalid_argument("invalid or duplicate service output declaration");
    activate_ = std::make_shared<const Activate>(std::move(activate));
    id_ = detail::allocateId<RuntimeTag>();
    dependencies_ = std::move(dependencies);
    outputs_ = std::move(outputs);
}

/** @brief 在调用用户代码前建立实例与激活所有权。 */
Instance Instance::mount(const Runtime& runtime, std::shared_ptr<IScheduler> scheduler, std::string config, std::shared_ptr<ServiceRegistry> services, RealmMap realms)
{
    if (!runtime.activate_) throw std::invalid_argument("runtime handle moved from");
    auto control = std::make_shared<detail::InstanceControl>(std::move(scheduler), runtime.activate_);
    if (services && !services->usesScheduler(control->scheduler)) throw std::invalid_argument("managed registry requires same scheduler");
    for (const auto& entry : realms.overrides())
        if (!services || !services->containsRealm(entry.second)) throw std::invalid_argument("realm view contains foreign realm");
    std::set<ServiceKey> outputKeys;
    for (const auto& declaration : runtime.outputs_) {
        const auto key = realms.resolve(declaration.key);
        if (!services || !services->containsRealm(key.realm) || !outputKeys.insert(key).second)
            throw std::invalid_argument("invalid or duplicate resolved output realm/key");
    }
    for (const auto& dependency : runtime.dependencies_)
        if (services && !services->containsRealm(realms.resolve(dependency.key).realm)) throw std::invalid_argument("dependency uses foreign realm");
    control->pin = control;
    control->config = std::move(config);
    control->runtime = runtime.id();
    control->dependencies = runtime.dependencies_;
    control->declarations = runtime.outputs_;
    control->realms = std::move(realms);
    control->services = std::move(services);
    if (control->services) {
        std::weak_ptr<detail::InstanceControl> weak = control;
        control->subscription = control->services->watch({
            [weak] { if (auto self = weak.lock()) self->enqueue(); },
            [weak] { if (auto self = weak.lock()) self->blocked.store(true); }
        });
        std::vector<ServiceKey> required, outputs;
        for (const auto& dependency : control->dependencies) required.push_back(control->realms.resolve(dependency.key));
        for (const auto& output : control->declarations) if (output.required) outputs.push_back(control->realms.resolve(output.key));
        control->services->declareInstance(control->id, std::move(required), std::move(outputs));
    }
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
    return Result<InstanceSnapshot>::success({control_->state, control_->blocked.load(), resources.value(), control_->epoch, control_->revision, control_->id, control_->runtime, control_->committed.size(), control_->failure, control_->children.size()});
}

/** @brief 复用激活预检的精确查找，诊断不调用服务或改变 committed 保留。 */
Result<std::vector<DependencySnapshot>> Instance::dependencies() const
{
    if (!control_->scheduler->isCurrentThread())
        return Result<std::vector<DependencySnapshot>>::failure(makeError(ErrorCode::WrongThread, "dependency snapshot requires owner thread"));
    std::vector<DependencySnapshot> result;
    for (const auto& dependency : control_->dependencies) {
        DependencySnapshot item{control_->realms.resolve(dependency.key), dependency.minimumMinor, dependency.capabilities, {}};
        if (!control_->services) item.error = makeError(ErrorCode::DependencyMissing, "instance has no managed registry");
        else {
            auto found = control_->services->findErased(item.key, dependency.type, dependency.minimumMinor, dependency.capabilities);
            if (!found.hasValue()) item.error = found.error();
        }
        result.push_back(std::move(item));
    }
    return Result<std::vector<DependencySnapshot>>::success(std::move(result));
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
    if (c.retirementFailure) return Result<OperationHandle>::failure(makeError(ErrorCode::CleanupFailed, "old resources or children not released"));
    // 等待下游退出期间只能观察已启动的清理，不能用查询提前启动 close。
    if (c.retiring && c.scope.snapshot().value().closing) {
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
