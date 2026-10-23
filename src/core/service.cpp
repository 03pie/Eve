/** @file service.cpp
 * @brief 实现服务键保留、发布退役、同步/异步调用屏障及显式释放。
 */
#include <cordis/service.hpp>
#include <atomic>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace cordis::detail {
/** @brief 注册表持有绑定，资源未释放时固定整个索引的所有权。 */
struct ServiceRegistryControl {
    std::shared_ptr<IScheduler> scheduler;
    std::map<ServiceKey, std::shared_ptr<BindingControl>> bindings;
    std::set<RealmId> realms{RealmId{1}};
    std::shared_ptr<ServiceRegistryControl> pin;
    std::vector<std::weak_ptr<ScheduledJob>> watchers;
};
/** @brief 绑定的单写状态与在途同步/异步调用计数。 */
struct BindingControl {
    std::shared_ptr<IScheduler> scheduler;
    std::weak_ptr<ServiceRegistryControl> owner;
    BindingId id;
    ServiceKey key;
    BindingState state = BindingState::Staged;
    std::size_t calls = 0;
    std::size_t committed = 0;
    std::shared_ptr<void> object;
    std::type_index type{typeid(void)};
    ServiceMetadata metadata;
    InstanceId provider;
    std::vector<std::weak_ptr<CommittedLease>> consumers;
    Promise<void> unused;
    std::atomic_bool deliveryBlocked{false};
    /** @brief 读取 owner 线程的不可变诊断副本。 */
    BindingSnapshot snapshot() const { return {id, key, state, calls, committed, metadata, deliveryBlocked.load()}; }
};

/** @brief 实例代次持有的绑定保留，普通业务与清理共用该旧绑定。 */
struct CommittedLease {
    std::shared_ptr<BindingControl> binding;
    InstanceId instance;
    Epoch epoch;
    bool active = true;
    std::size_t calls = 0;
    Promise<void> drained;
    bool draining = false;
    std::function<Result<void>()> businessGuard;
    std::function<void()> invalidated;
};

/** @brief 退役绑定没有保留与调用时才完成提供者清理前置屏障。 */
void checkUnused(const std::shared_ptr<BindingControl>& binding)
{
    if (binding->state == BindingState::Retiring && binding->committed == 0 && binding->calls == 0) binding->unused.resolve();
}

/** @brief 索引通知只排队，投递受阻时调用宿主诊断入口。 */
void notifyRegistry(const std::shared_ptr<ServiceRegistryControl>& registry)
{
    std::vector<std::shared_ptr<ScheduledJob>> jobs;
    for (auto it = registry->watchers.begin(); it != registry->watchers.end();) {
        if (auto job = it->lock()) { jobs.push_back(std::move(job)); ++it; }
        else it = registry->watchers.erase(it);
    }
    for (const auto& job : jobs) {
        try { if (registry->scheduler->post(*job)) continue; } catch (...) {}
        if (job->discarded) job->discarded();
    }
}

/** @brief 对消费者到提供者的实际租约图做可达性检查，拒绝新增强依赖环。 */
bool reaches(const ServiceRegistryControl& registry, InstanceId from, InstanceId target)
{
    std::set<InstanceId> visited;
    std::vector<InstanceId> pending{from};
    while (!pending.empty()) {
        auto current = pending.back(); pending.pop_back();
        if (current == target) return true;
        if (!visited.insert(current).second) continue;
        for (const auto& entry : registry.bindings) {
            if (!entry.second->provider.valid()) continue;
            for (const auto& weak : entry.second->consumers) if (auto lease = weak.lock()) {
                if (lease->active && lease->instance == current) pending.push_back(entry.second->provider);
            }
        }
    }
    return false;
}

/** @brief 租约复用仅按精确键与接口类型匹配。 */
bool matchesCommitted(const std::shared_ptr<CommittedLease>& lease, const ServiceKey& key, std::type_index type,
    const std::shared_ptr<ServiceRegistryControl>& registry)
{
    return lease->active && lease->binding->owner.lock() == registry && lease->binding->type == type && !(lease->binding->key < key) && !(key < lease->binding->key);
}

/** @brief 已通过门禁的同步调用共用异常隔离及在途计数。 */
Result<void> invokeBinding(const std::shared_ptr<BindingControl>& binding, const std::function<void(void*)>& callback)
{
    if (binding->calls == std::numeric_limits<std::size_t>::max()) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "call count exhausted"));
    ++binding->calls;
    try { callback(binding->object.get()); }
    catch (const std::exception& error) {
        --binding->calls;
        checkUnused(binding);
        return Result<void>::failure(makeError(ErrorCode::CallbackFailed, error.what()));
    } catch (...) {
        --binding->calls;
        checkUnused(binding);
        return Result<void>::failure(makeError(ErrorCode::CallbackFailed, "service callback threw"));
    }
    --binding->calls;
    checkUnused(binding);
    return Result<void>::success();
}

/** @brief 清理租约必须匹配实例代次；普通调用还需通过实例能力与 Published 门禁。 */
Result<void> callCommitted(const std::weak_ptr<CommittedLease>& weak, std::type_index type,
    const std::function<void(void*)>& callback, bool cleanup, InstanceId instance, Epoch epoch)
{
    auto lease = weak.lock();
    if (!lease || !lease->binding) return Result<void>::failure(makeError(ErrorCode::StaleContext, "committed lease expired"));
    auto binding = lease->binding;
    if (!binding->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "service requires owner thread"));
    if (!lease->active) return Result<void>::failure(makeError(ErrorCode::StaleContext, "committed lease released"));
    if (!binding->scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (cleanup) {
        if (lease->instance != instance || lease->epoch != epoch)
            return Result<void>::failure(makeError(ErrorCode::StaleContext, "cleanup lease belongs to another instance or epoch"));
    } else {
        auto allowed = lease->businessGuard();
        if (!allowed.hasValue()) return allowed;
    }
    if (binding->state != BindingState::Published && !(cleanup && binding->state == BindingState::Retiring))
        return Result<void>::failure(makeError(ErrorCode::ServiceRetiring, "binding not available for this call"));
    if (binding->type != type) return Result<void>::failure(makeError(ErrorCode::DependencyVersion, "service type mismatch"));
    if (lease->calls == std::numeric_limits<std::size_t>::max()) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "lease call count exhausted"));
    ++lease->calls;
    auto result = invokeBinding(binding, callback);
    --lease->calls;
    if (lease->draining && lease->calls == 0) lease->drained.resolve();
    return result;
}

/** @brief 所有者关闭业务入口后，等待已进入本代租约的同步/异步调用退出。 */
Task<void> drainCommitted(const std::vector<std::shared_ptr<CommittedLease>>& leases)
{
    if (leases.empty()) return completedTask();
    auto pending = std::make_shared<std::size_t>(leases.size());
    Promise<void> finished;
    for (const auto& lease : leases) {
        lease->draining = true;
        if (lease->calls == 0) lease->drained.resolve();
        TaskAccess::observe(lease->drained.task(), [pending, finished](Task<void>::Snapshot) {
            if (--*pending == 0) finished.resolve();
        });
    }
    return finished.task();
}

/** @brief 只在实例所有清理确认静止之后由 owner 线程撤销保留。 */
void releaseCommitted(const std::shared_ptr<CommittedLease>& lease)
{
    if (!lease->active) return;
    lease->active = false;
    --lease->binding->committed;
    checkUnused(lease->binding);
}

/** @brief 分配不复用编号；耗尽时返回无效零值。 */
std::uint64_t nextIdentity(std::atomic<std::uint64_t>& counter)
{
    auto value = counter.load();
    do {
        if (value == std::numeric_limits<std::uint64_t>::max()) return 0;
    } while (!counter.compare_exchange_weak(value, value + 1));
    return value + 1;
}

/** @brief 持有调用屏障并隔离插件回调异常，不向调用者交出服务所有权。 */
Result<void> callService(const std::weak_ptr<BindingControl>& weak, std::type_index type,
                         const std::function<void(void*)>& callback)
{
    auto binding = weak.lock();
    if (!binding) return Result<void>::failure(makeError(ErrorCode::StaleContext, "binding expired"));
    if (!binding->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "service requires owner thread"));
    if (!binding->scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (binding->state != BindingState::Published) return Result<void>::failure(makeError(ErrorCode::ServiceRetiring, "binding not published"));
    if (binding->type != type) return Result<void>::failure(makeError(ErrorCode::DependencyVersion, "service type mismatch"));
    return invokeBinding(binding, callback);
}

/** @brief 宿主异步调用控制块，投递中断时保留资源而非伪报任务完成。 */
struct AsyncServiceCall : std::enable_shared_from_this<AsyncServiceCall> {
    std::shared_ptr<BindingControl> binding;
    std::shared_ptr<CommittedLease> lease;
    std::shared_ptr<AsyncServiceCall> pin;
    Promise<void> completed;

    /** @brief owner 处理真实完成后才撤销绑定与实例调用计数。 */
    void finish(const Result<void>& result)
    {
        --binding->calls;
        if (lease) {
            --lease->calls;
            if (lease->draining && lease->calls == 0) lease->drained.resolve();
        }
        checkUnused(binding);
        if (result.hasValue()) completed.resolve();
        else completed.reject(result.error());
        pin.reset();
    }

    /** @brief 源任务可跨线程完成，资源账目的修改统一投递回 owner。 */
    void observe(const Task<void>& source)
    {
        auto self = shared_from_this();
        auto receipt = source.thenOn(binding->scheduler, [self](const Result<void>& result) { self->finish(result); });
        TaskAccess::observe(receipt, [weak = std::weak_ptr<AsyncServiceCall>(self)](Task<void>::Snapshot result) {
            if (!result->hasValue()) if (auto call = weak.lock()) call->binding->deliveryBlocked.store(true);
        });
    }
};

/** @brief 在调用插件前固定异步 CallLease，异常同样通过异步完成路径收敛。 */
Task<void> invokeService(const std::weak_ptr<BindingControl>& weak, const std::weak_ptr<CommittedLease>& committedLease,
    bool committed, std::type_index type, std::function<Task<void>(void*)> callback)
{
    auto lease = committed ? committedLease.lock() : std::shared_ptr<CommittedLease>{};
    auto binding = committed ? (lease ? lease->binding : nullptr) : weak.lock();
    if (!binding) return failedTask(makeError(ErrorCode::StaleContext, "service reference expired"));
    if (!binding->scheduler->isCurrentThread()) return failedTask(makeError(ErrorCode::WrongThread, "invoke requires owner thread"));
    if (!binding->scheduler->isOpen()) return failedTask(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (lease) {
        if (!lease->active) return failedTask(makeError(ErrorCode::StaleContext, "lease expired"));
        auto allowed = lease->businessGuard();
        if (!allowed.hasValue()) return failedTask(allowed.error());
    }
    if (binding->state != BindingState::Published) return failedTask(makeError(ErrorCode::ServiceRetiring, "binding retiring"));
    if (binding->type != type) return failedTask(makeError(ErrorCode::DependencyVersion, "service type mismatch"));
    if (binding->calls == std::numeric_limits<std::size_t>::max() || (lease && lease->calls == std::numeric_limits<std::size_t>::max()))
        return failedTask(makeError(ErrorCode::InvalidArgument, "call count exhausted"));
    auto call = std::make_shared<AsyncServiceCall>();
    call->binding = binding;
    call->lease = lease;
    call->pin = call;
    ++binding->calls;
    if (lease) ++lease->calls;
    try { call->observe(callback(binding->object.get())); }
    catch (const std::exception& error) { call->observe(failedTask(makeError(ErrorCode::CallbackFailed, error.what()))); }
    catch (...) { call->observe(failedTask(makeError(ErrorCode::CallbackFailed, "async service callback threw"))); }
    return call->completed.task();
}
} // namespace cordis::detail

namespace cordis {
/** @brief 调度器身份在索引创建后不可变，允许挂载前安全检查。 */
bool ServiceRegistry::usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const { return control_->scheduler == scheduler; }
/** @brief 域归属由 owner 线程的索引创建记录确定。 */
bool ServiceRegistry::containsRealm(RealmId realm) const { return control_->realms.count(realm) != 0; }
/** @brief 在索引与类型校验后增加绑定保留，不允许跨 scheduler 连接消费者。 */
Result<std::shared_ptr<detail::CommittedLease>> ServiceRegistry::commitErased(const ServiceKey& key, std::type_index type,
    const std::shared_ptr<IScheduler>& scheduler, InstanceId instance, Epoch epoch,
    std::function<Result<void>()> businessGuard, std::function<void()> invalidated) const
{
    using Output = Result<std::shared_ptr<detail::CommittedLease>>;
    if (scheduler != control_->scheduler) return Output::failure(makeError(ErrorCode::InvalidArgument, "consumer requires same scheduler"));
    auto found = findErased(key, type);
    if (!found.hasValue()) return Output::failure(found.error());
    auto binding = found.value();
    if (binding->provider.valid() && detail::reaches(*control_, binding->provider, instance))
        return Output::failure(makeError(ErrorCode::DependencyCycle, "committed service dependency cycle"));
    if (binding->committed == std::numeric_limits<std::size_t>::max()) return Output::failure(makeError(ErrorCode::InvalidArgument, "committed count exhausted"));
    auto lease = std::make_shared<detail::CommittedLease>();
    lease->binding = binding;
    lease->instance = instance;
    lease->epoch = epoch;
    lease->businessGuard = std::move(businessGuard);
    lease->invalidated = std::move(invalidated);
    binding->consumers.push_back(lease);
    ++binding->committed;
    return Output::success(std::move(lease));
}

/** @brief 保存弱通知记录，实例释放注册令牌后不会再接收通知。 */
std::shared_ptr<ScheduledJob> ServiceRegistry::watch(ScheduledJob job) const
{
    auto slot = std::make_shared<ScheduledJob>(std::move(job));
    control_->watchers.push_back(slot);
    return slot;
}
/** @brief 校验 scheduler，不创建默认服务或执行插件回调。 */
ServiceRegistry::ServiceRegistry(std::shared_ptr<IScheduler> scheduler)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen()) throw std::invalid_argument("ServiceRegistry requires open owner-thread scheduler");
    control_ = std::make_shared<detail::ServiceRegistryControl>();
    control_->scheduler = std::move(scheduler);
}

/** @brief 根域之外的隔离域在进程中单调分配，并限制到所属注册表。 */
Result<RealmId> ServiceRegistry::createRealm() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<RealmId>::failure(makeError(ErrorCode::WrongThread, "realm requires owner thread"));
    if (!control_->scheduler->isOpen()) return Result<RealmId>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    static std::atomic<std::uint64_t> next{1};
    const RealmId id(detail::nextIdentity(next));
    if (!id.valid()) return Result<RealmId>::failure(makeError(ErrorCode::InvalidArgument, "realm ids exhausted"));
    control_->realms.insert(id);
    return Result<RealmId>::success(id);
}

/** @brief 检查参数和冲突后才将对象纳入资源账目。 */
Result<ProviderHandle> ServiceRegistry::stageErased(ServiceKey key, std::shared_ptr<void> object, std::type_index type, ServiceMetadata metadata) const
{
    if (!control_->scheduler->isCurrentThread()) return Result<ProviderHandle>::failure(makeError(ErrorCode::WrongThread, "stage requires owner thread"));
    if (!control_->scheduler->isOpen()) return Result<ProviderHandle>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (!object || key.name.empty() || key.interfaceMajor == 0 || control_->realms.count(key.realm) == 0)
        return Result<ProviderHandle>::failure(makeError(ErrorCode::InvalidArgument, "invalid service or realm"));
    std::set<std::string> capabilities;
    for (const auto& capability : metadata.capabilities)
        if (capability.empty() || !capabilities.insert(capability).second) return Result<ProviderHandle>::failure(makeError(ErrorCode::InvalidArgument, "invalid service capabilities"));
    if (auto found = control_->bindings.find(key); found != control_->bindings.end()) {
        auto error = makeError(ErrorCode::ServiceConflict, "service key reserved: " + key.name + "/v" + std::to_string(key.interfaceMajor) + " realm=" + std::to_string(key.realm.value()));
        if (found->second->provider.valid()) error.instance = found->second->provider;
        return Result<ProviderHandle>::failure(std::move(error));
    }
    static std::atomic<std::uint64_t> next{0};
    const BindingId id(detail::nextIdentity(next));
    if (!id.valid()) return Result<ProviderHandle>::failure(makeError(ErrorCode::InvalidArgument, "binding ids exhausted"));
    auto binding = std::make_shared<detail::BindingControl>();
    binding->scheduler = control_->scheduler;
    binding->owner = control_;
    binding->id = id;
    binding->key = std::move(key);
    binding->object = std::move(object);
    binding->type = type;
    binding->metadata = std::move(metadata);
    control_->bindings.emplace(binding->key, binding);
    control_->pin = control_;
    return Result<ProviderHandle>::success(ProviderHandle(std::move(binding)));
}

/** @brief 不暴露 Staged 或 Retiring 输出，也不跨 realm 回退。 */
Result<std::shared_ptr<detail::BindingControl>> ServiceRegistry::findErased(const ServiceKey& key, std::type_index type,
    std::uint32_t minimumMinor, const std::vector<std::string>& capabilities) const
{
    using Output = Result<std::shared_ptr<detail::BindingControl>>;
    if (!control_->scheduler->isCurrentThread()) return Output::failure(makeError(ErrorCode::WrongThread, "find requires owner thread"));
    if (!control_->scheduler->isOpen()) return Output::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (key.name.empty() || key.interfaceMajor == 0 || !containsRealm(key.realm))
        return Output::failure(makeError(ErrorCode::InvalidArgument, "invalid service key or foreign realm"));
    auto found = control_->bindings.find(key);
    if (found == control_->bindings.end() || found->second->state != BindingState::Published)
        return Output::failure(makeError(ErrorCode::DependencyMissing, "published service missing"));
    if (found->second->type != type) return Output::failure(makeError(ErrorCode::DependencyVersion, "service type mismatch"));
    if (found->second->metadata.minor < minimumMinor) return Output::failure(makeError(ErrorCode::DependencyVersion, "service minor too low: " + key.name));
    for (const auto& capability : capabilities) {
        const auto& available = found->second->metadata.capabilities;
        if (std::find(available.begin(), available.end(), capability) == available.end())
            return Output::failure(makeError(ErrorCode::DependencyVersion, "service capability missing: " + key.name + ":" + capability));
    }
    return Output::success(found->second);
}

/** @brief 诊断所有未释放绑定，不改变资源状态。 */
Result<std::vector<BindingSnapshot>> ServiceRegistry::snapshot() const
{
    if (!control_->scheduler->isCurrentThread()) return Result<std::vector<BindingSnapshot>>::failure(makeError(ErrorCode::WrongThread, "snapshot requires owner thread"));
    std::vector<BindingSnapshot> bindings;
    for (const auto& entry : control_->bindings) bindings.push_back(entry.second->snapshot());
    return Result<std::vector<BindingSnapshot>>::success(std::move(bindings));
}

/** @brief 发布时验证线程和状态，禁止复活 Retiring 或 Released。 */
Result<void> ProviderHandle::publish() const
{
    if (!binding_->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "publish requires owner thread"));
    if (!binding_->scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (binding_->state != BindingState::Staged && binding_->state != BindingState::Published)
        return Result<void>::failure(makeError(ErrorCode::ServiceRetiring, "cannot republish retired binding"));
    binding_->state = BindingState::Published;
    if (auto owner = binding_->owner.lock()) detail::notifyRegistry(owner);
    return Result<void>::success();
}

/** @brief 关闭业务入口；即使 scheduler 已关闭仍允许 owner 执行同步撤回。 */
Result<void> ProviderHandle::retire() const
{
    if (!binding_->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "retire requires owner thread"));
    if (binding_->state == BindingState::Released || binding_->state == BindingState::Retiring) return Result<void>::success();
    binding_->state = BindingState::Retiring;
    std::vector<std::shared_ptr<detail::CommittedLease>> consumers;
    for (const auto& weak : binding_->consumers) if (auto lease = weak.lock()) if (lease->active) consumers.push_back(lease);
    for (const auto& lease : consumers) if (lease->invalidated) lease->invalidated();
    detail::checkUnused(binding_);
    if (auto owner = binding_->owner.lock()) detail::notifyRegistry(owner);
    return Result<void>::success();
}

/** @brief 托管绑定只记录非零实例标识，不暴露给插件操纵。 */
void ProviderHandle::setOwner(InstanceId owner) const { binding_->provider = owner; }

/** @brief 等待退役绑定的消费者保留和同步调用全部退出。 */
Task<void> ProviderHandle::whenUnused() const
{
    if (!binding_->scheduler->isCurrentThread() || binding_->state != BindingState::Retiring) {
        Promise<void> failed;
        failed.reject(makeError(binding_->scheduler->isCurrentThread() ? ErrorCode::InvalidArgument : ErrorCode::WrongThread, "whenUnused requires retired owner-thread binding"));
        return failed.task();
    }
    detail::checkUnused(binding_);
    return binding_->unused.task();
}

/** @brief 先撤回索引与对象账目，再在容器外析构对象，允许析构重入。 */
Result<void> ProviderHandle::release() const
{
    auto binding = binding_;
    if (!binding->scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "release requires owner thread"));
    if (binding->state == BindingState::Released) return Result<void>::success();
    if (binding->state != BindingState::Retiring || binding->calls != 0 || binding->committed != 0)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "binding not retired or calls in flight"));
    binding->state = BindingState::Released;
    auto object = std::move(binding->object);
    if (auto owner = binding->owner.lock()) {
        owner->bindings.erase(binding->key);
        detail::notifyRegistry(owner);
        if (owner->bindings.empty()) owner->pin.reset();
    }
    object.reset();
    return Result<void>::success();
}

/** @brief 读取包括已释放状态的绑定快照。 */
Result<BindingSnapshot> ProviderHandle::snapshot() const
{
    if (!binding_->scheduler->isCurrentThread()) return Result<BindingSnapshot>::failure(makeError(ErrorCode::WrongThread, "snapshot requires owner thread"));
    return Result<BindingSnapshot>::success(binding_->snapshot());
}
} // namespace cordis
