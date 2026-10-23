/** @file service.hpp
 * @brief 声明同工具链服务绑定、隔离域和同步调用门禁。
 */
#pragma once
#include <cordis/scheduler.hpp>
#include <cordis/task.hpp>
#include <cordis/state.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace cordis {
class Context;
class Instance;
class CleanupContext;
struct ServiceKey;
namespace detail {
struct BindingControl;
struct CommittedLease;
struct ServiceRegistryControl;
struct InstanceControl;
/** @brief 通过已提交租约调用，清理调用额外核对实例和代次。 */
Result<void> callCommitted(const std::weak_ptr<CommittedLease>& lease, std::type_index type,
    const std::function<void(void*)>& callback, bool cleanup, InstanceId instance = {}, Epoch epoch = 0);
/** @brief 清理确认静止后释放一次 committed 保留，不由句柄析构推断。 */
void releaseCommitted(const std::shared_ptr<CommittedLease>& lease);
/** @brief 等待本代所有已进入的同步/异步调用退出，不提前释放实例租约。 */
Task<void> drainCommitted(const std::vector<std::shared_ptr<CommittedLease>>& leases);
/** @brief 检查本代已提交租约是否精确匹配声明键和类型。 */
bool matchesCommitted(const std::shared_ptr<CommittedLease>& lease, const ServiceKey& key, std::type_index type,
    const std::shared_ptr<ServiceRegistryControl>& registry);
/** @brief 检查绑定、线程和类型，持有同步调用计数直到回调退出。 */
Result<void> callService(const std::weak_ptr<BindingControl>& binding, std::type_index type,
                         const std::function<void(void*)>& callback);
/** @brief 异步调用将绑定与消费者调用保留到源任务完成且 owner 已处理其结果。 */
Task<void> invokeService(const std::weak_ptr<BindingControl>& binding, const std::weak_ptr<CommittedLease>& lease,
    bool committed, std::type_index type, std::function<Task<void>(void*)> callback);
}

/** @brief 服务名称、接口主版本和精确隔离域构成的键。 */
struct ServiceKey {
    std::string name;
    std::uint32_t interfaceMajor = 1;
    RealmId realm{1};
    /** @brief 供宿主索引按完整键排序，不进行跨域回退。 */
    friend bool operator<(const ServiceKey& a, const ServiceKey& b)
    {
        if (a.name != b.name) return a.name < b.name;
        if (a.interfaceMajor != b.interfaceMajor) return a.interfaceMajor < b.interfaceMajor;
        return a.realm < b.realm;
    }
};

/** @brief 不可变的按服务名称覆盖的 realm 视图；派生副本继承未覆盖项。 */
class RealmMap {
public:
    /** @brief 返回派生视图，不修改当前视图；名称和 realm 必须有效。 */
    Result<RealmMap> isolate(std::string name, RealmId realm) const
    {
        if (name.empty() || !realm.valid()) return Result<RealmMap>::failure(makeError(ErrorCode::InvalidArgument, "invalid realm override"));
        auto derived = *this;
        derived.overrides_[std::move(name)] = realm;
        return Result<RealmMap>::success(std::move(derived));
    }
    /** @brief 名称覆盖优先于传入键的 realm，未覆盖项保留原键。 */
    ServiceKey resolve(ServiceKey key) const
    {
        auto found = overrides_.find(key.name);
        if (found != overrides_.end()) key.realm = found->second;
        return key;
    }
    /** @brief 只读访问覆盖项，供挂载校验域归属。 */
    const std::map<std::string, RealmId>& overrides() const noexcept { return overrides_; }
private:
    std::map<std::string, RealmId> overrides_;
};

/** @brief 同一主版本下的实现 minor 与能力名称集合。 */
struct ServiceMetadata {
    std::uint32_t minor = 0;
    std::vector<std::string> capabilities;
};

/** @brief 不授予调用能力的绑定诊断。 */
struct BindingSnapshot {
    BindingId id;
    ServiceKey key;
    BindingState state;
    std::size_t activeCalls;
    std::size_t committedLeases;
    ServiceMetadata metadata;
    bool deliveryBlocked;
};

/** @brief 非拥有的业务引用，每次同步调用重新检查绑定门禁。 */
template<class T> class ServiceRef {
public:
    /** @brief 发起返回 Task<void> 的异步调用；返回任务完成前服务所有权保持有效。 */
    template<class F> Task<void> invoke(F&& function) const
    {
        static_assert(std::is_same_v<std::invoke_result_t<F&, T&>, Task<void>>, "async service callback must return Task<void>");
        std::function<Task<void>(T&)> callback(std::forward<F>(function));
        if (!callback) return failedTask(makeError(ErrorCode::InvalidArgument, "empty async service callback"));
        return detail::invokeService(binding_, lease_, committed_, typeid(T), [callback = std::move(callback)](void* object) {
            return callback(*static_cast<T*>(object));
        });
    }
    /** @brief 执行同步服务调用，不允许保存传入引用或用于异步业务。 */
    template<class F> Result<void> withService(F&& function) const
    {
        static_assert(std::is_same_v<std::invoke_result_t<F&, T&>, void>, "service callback must return void; async calls need an explicit lease");
        std::function<void(T&)> callback(std::forward<F>(function));
        if (!callback) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "empty service callback"));
        std::function<void(void*)> erased = [callback = std::move(callback)](void* object) {
            callback(*static_cast<T*>(object));
        };
        return committed_ ? detail::callCommitted(lease_, typeid(T), erased, false)
                          : detail::callService(binding_, typeid(T), erased);
    }
private:
    friend class ServiceRegistry;
    friend class Context;
    friend class CleanupContext;
    /** @brief 保存弱绑定，引用本身不延长服务对象生命周期。 */
    explicit ServiceRef(std::weak_ptr<detail::BindingControl> binding) : binding_(std::move(binding)) {}
    /** @brief 保存实例提交租约的弱引用，资源保留由实例账目承担。 */
    explicit ServiceRef(std::weak_ptr<detail::CommittedLease> lease) : lease_(std::move(lease)), committed_(true) {}
    std::weak_ptr<detail::BindingControl> binding_;
    std::weak_ptr<detail::CommittedLease> lease_;
    bool committed_ = false;
};

/** @brief 提供者持有的显式绑定生命周期句柄，析构不发布或释放服务。 */
class ProviderHandle {
public:
    /** @brief 退役后等待消费者保留和所有在途调用归零，不释放对象。 */
    Task<void> whenUnused() const;
    /** @brief 从 Staged 发布服务；重复发布 Published 绑定是幂等的。 */
    Result<void> publish() const;
    /** @brief 立即拒绝新业务调用；Staged 回滚也使用该入口。 */
    Result<void> retire() const;
    /** @brief 仅在 Retiring 且无同步调用及 committed 保留时释放对象所有权及键；不自动重试。 */
    Result<void> release() const;
    /** @brief owner 线程读取状态，即使 Released 也可诊断。 */
    Result<BindingSnapshot> snapshot() const;
private:
    friend class ServiceRegistry;
    friend struct detail::InstanceControl;
    /** @brief 为托管输出标记实例所有者，用于依赖环诊断。 */
    void setOwner(InstanceId owner) const;
    /** @brief 保存绑定控制块，不绕过注册表持有的资源所有权。 */
    explicit ProviderHandle(std::shared_ptr<detail::BindingControl> binding) : binding_(std::move(binding)) {}
    std::shared_ptr<detail::BindingControl> binding_;
};

/** @brief 服务索引与实例 committed 保留底座，为托管实例排队通知服务变化。 */
class ServiceRegistry {
public:
    /** @brief 创建 owner 线程注册表，要求开放调度器；根域为 RealmId{1}。 */
    explicit ServiceRegistry(std::shared_ptr<IScheduler> scheduler);
    /** @brief 分配本注册表可用的进程唯一隔离域。 */
    Result<RealmId> createRealm() const;
    /** @brief 保留完整键并登记 Staged 服务；同键所有未释放状态均冲突。 */
    template<class T> Result<ProviderHandle> stage(ServiceKey key, std::shared_ptr<T> object, ServiceMetadata metadata = {}) const
    { return stageErased(std::move(key), std::move(object), typeid(T), std::move(metadata)); }
    /** @brief 只查找当前 Published 的同类型、同完整键绑定。 */
    template<class T> Result<ServiceRef<T>> find(const ServiceKey& key, std::uint32_t minimumMinor = 0, const std::vector<std::string>& capabilities = {}) const
    {
        auto binding = findErased(key, typeid(T), minimumMinor, capabilities);
        if (!binding.hasValue()) return Result<ServiceRef<T>>::failure(binding.error());
        return Result<ServiceRef<T>>::success(ServiceRef<T>(binding.value()));
    }
    /** @brief 读取所有尚未释放的绑定，不隐式清理资源。 */
    Result<std::vector<BindingSnapshot>> snapshot() const;
private:
    friend class Context;
    friend struct detail::InstanceControl;
    friend class Instance;
    /** @brief 登记子实例到父实例的所有权边，供联合等待环检查。 */
    void attachParent(InstanceId child, InstanceId parent) const;
    /** @brief 实例确认终结后撤销其所有权记录。 */
    void forgetInstance(InstanceId instance) const;
    /** @brief 登记已按域视图解析的强依赖与必供输出，用于激活前环诊断。 */
    void declareInstance(InstanceId instance, std::vector<ServiceKey> dependencies, std::vector<ServiceKey> outputs) const;
    /** @brief 对确定提供者的声明边和所有权边预检，缺失或歧义提供者不臆测连边。 */
    Result<void> checkDeclarations(InstanceId instance) const;
    /** @brief 检查托管实例与索引是否归属同一调度器。 */
    bool usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const;
    /** @brief 检查隔离域是否属于本注册表，不创建新域。 */
    bool containsRealm(RealmId realm) const;
    /** @brief 弱登记索引变化通知，回调始终经 scheduler 排队。 */
    std::shared_ptr<ScheduledJob> watch(ScheduledJob job) const;
    /** @brief 为指定实例代次固定当前 Published 绑定；要求相同 scheduler。 */
    Result<std::shared_ptr<detail::CommittedLease>> commitErased(const ServiceKey& key, std::type_index type,
        const std::shared_ptr<IScheduler>& scheduler, InstanceId instance, Epoch epoch,
        std::function<Result<void>()> businessGuard, std::function<void()> invalidated = {}) const;
    /** @brief 擦除对象类型后登记，检查参数、完整键冲突与归属域。 */
    Result<ProviderHandle> stageErased(ServiceKey key, std::shared_ptr<void> object, std::type_index type, ServiceMetadata metadata = {}) const;
    /** @brief 在生成弱业务引用之前检查发布状态和 C++ 类型。 */
    Result<std::shared_ptr<detail::BindingControl>> findErased(const ServiceKey& key, std::type_index type,
        std::uint32_t minimumMinor = 0, const std::vector<std::string>& capabilities = {}) const;
    std::shared_ptr<detail::ServiceRegistryControl> control_;
};
} // namespace cordis
