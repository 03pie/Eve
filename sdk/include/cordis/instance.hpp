/** @file instance.hpp
 * @brief 声明内置插件的多代实例、配置操作及弱生命周期上下文。
 */
#pragma once
#include <cordis/effect.hpp>
#include <cordis/service.hpp>
#include <optional>

namespace cordis {
class Runtime;
class Instance;
struct ServiceRequirement;
template<class T> class EventBus;
namespace detail { struct InstanceControl; }
namespace detail { struct CleanupAccess; }

/** @brief 清理回调所属旧代的不可变信息，不包含新业务能力。 */
struct CleanupSnapshot {
    InstanceId instance;
    RuntimeId runtime;
    Epoch epoch;
    ConfigRevision revision;
    std::string config;
};

/** @brief 仅在一次清理回调及其异步任务期间有效的受限上下文。 */
class CleanupContext {
public:
    /** @brief owner 线程读取旧代信息；清理已完成或上下文过期时返回 StaleContext。 */
    Result<CleanupSnapshot> snapshot() const;
    /** @brief 清理窗口内异步访问本实例本代旧绑定，调用屏障持续至源任务实际完成。 */
    template <class T, class F> Task<void> invokeService(const ServiceRef<T>& service, F&& function) const
    {
        static_assert(std::is_same_v<std::invoke_result_t<F&, T&>, Task<void>>,
                      "cleanup async service callback must return Task<void>");
        auto current = snapshot();
        if (!current.hasValue())
            return failedTask(current.error());
        std::function<Task<void>(T&)> callback(std::forward<F>(function));
        if (!callback)
            return failedTask(makeError(ErrorCode::InvalidArgument, "empty cleanup async service callback"));
        return detail::invokeService(
            {}, service.lease_, service.committed_, typeid(T),
            [callback = std::move(callback)](void* object) { return callback(*static_cast<T*>(object)); }, true,
            current.value().instance, current.value().epoch);
    }
    /** @brief 仅在有效清理窗口内访问本实例本代已提交的旧绑定，允许 Retiring。 */
    template<class T, class F> Result<void> withService(const ServiceRef<T>& service, F&& function) const
    {
        static_assert(std::is_same_v<std::invoke_result_t<F&, T&>, void>, "cleanup service callback must return void");
        auto current = snapshot();
        if (!current.hasValue()) return Result<void>::failure(current.error());
        std::function<void(T&)> callback(std::forward<F>(function));
        if (!callback) return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "empty cleanup service callback"));
        return detail::callCommitted(service.lease_, typeid(T), [callback = std::move(callback)](void* object) {
            callback(*static_cast<T*>(object));
        }, true, current.value().instance, current.value().epoch);
    }
private:
    friend class Context;
    /** @brief 只持有清理访问控制的弱引用，不延长资源生命周期。 */
    explicit CleanupContext(std::weak_ptr<detail::CleanupAccess> access) : access_(std::move(access)) {}
    std::weak_ptr<detail::CleanupAccess> access_;
};

/** @brief 当前代业务能力的归属信息，仅用于资源命名与诊断。 */
struct ContextIdentity {
    InstanceId instance;
    Epoch epoch;
};
namespace ui { class UiPagesService; class CommandService; class NotificationService; }
namespace qt {
class QtConnection;
class QtWorker;
}
namespace module {
class NativeModule;
}

/** @brief 使用受限上下文执行清理，返回真实释放完成任务。 */
using InstanceCleanup = std::function<Task<void>(CleanupContext)>;

/** @brief 弱引用实例的业务上下文；仅允许在 owner 线程注册资源。 */
class Context {
public:
    /** @brief 在业务仍有效时读取实例与代次；不会授予新的业务或清理能力。 */
    Result<ContextIdentity> identity() const;
    /** @brief 本代创建子实例，继承域视图/索引，可追加类型化依赖；父退役等待整个子树。 */
    Result<Instance> inject(const Runtime& runtime, std::string config = {},
                            std::vector<ServiceRequirement> additionalDependencies = {}) const;
    /** @brief 派生只覆盖指定服务的 realm 视图，不修改父上下文，旧 epoch 不得派生能力。 */
    Result<Context> isolate(std::string name, RealmId realm) const;
    /** @brief 在本代登记托管 Staged 输出，激活完成后才对其他实例发布。 */
    template<class T> Result<BindingId> provide(ServiceKey key, std::shared_ptr<T> object) const
    { return provideErased(std::move(key), std::move(object), typeid(T)); }
    /** @brief 撤回本代自有输出并等待消费者/调用排空；对象及键仍由实例最终清理释放。 */
    Task<void> withdraw(BindingId binding) const;
    /** @brief 从托管注册表消费服务，优先使用激活前固定的声明依赖；最低要求失败不登记新保留。 */
    template <class T>
    Result<ServiceRef<T>> consume(const ServiceKey& key, std::uint32_t minimumMinor = 0,
                                  const std::vector<std::string>& capabilities = {}) const
    {
        auto lease = consumeManaged(key, typeid(T), minimumMinor, capabilities);
        if (!lease.hasValue()) return Result<ServiceRef<T>>::failure(lease.error());
        return Result<ServiceRef<T>>::success(ServiceRef<T>(std::weak_ptr<detail::CommittedLease>(lease.value())));
    }
    /** @brief 获取当前上下文捕获的配置副本，不读取新代配置。 */
    const std::string& config() const noexcept { return config_; }
    /** @brief 在当前激活仍有效时登记 effect；停止后拒绝新资源。 */
    Result<EffectToken> effect(EffectSetup setup, std::string label = "anonymous") const;
    /** @brief 登记独立清理组；清理中只能使用 CleanupContext，不获得激活上下文。 */
    Result<EffectToken> onCleanup(InstanceCleanup cleanup, std::string label = "instance.cleanup") const;
    /** @brief 固定当前 Published 服务到本代 committed 账目；缺服务返回 DependencyMissing。 */
    template <class T>
    Result<ServiceRef<T>> consume(const ServiceRegistry& registry, const ServiceKey& key,
                                  std::uint32_t minimumMinor = 0,
                                  const std::vector<std::string>& capabilities = {}) const
    {
        auto lease = consumeErased(registry, key, typeid(T), minimumMinor, capabilities);
        if (!lease.hasValue()) return Result<ServiceRef<T>>::failure(lease.error());
        return Result<ServiceRef<T>>::success(ServiceRef<T>(std::weak_ptr<detail::CommittedLease>(lease.value())));
    }
private:
    friend class ui::UiPagesService;
    friend class ui::CommandService;
    friend class ui::NotificationService;
    friend class qt::QtConnection;
    friend class qt::QtWorker;
    friend class module::NativeModule;
    template<class T> friend class EventBus;
    /** @brief 为事件/UI/Qt 适配器提供同 scheduler、本代业务门禁，不授予实例所有权。 */
    Result<std::function<Result<void>()>> eventGuard(const std::shared_ptr<IScheduler>& scheduler) const;
    /** @brief 校验本代能力后登记并保存托管输出。 */
    Result<BindingId> provideErased(ServiceKey key, std::shared_ptr<void> object, std::type_index type) const;
    /** @brief 解析本实例托管注册表，不向插件暴露发布/释放控制。 */
    Result<std::shared_ptr<detail::CommittedLease>> consumeManaged(const ServiceKey& key, std::type_index type,
                                                                   std::uint32_t minimumMinor,
                                                                   const std::vector<std::string>& capabilities) const;
    /** @brief 经过实例能力校验后固定服务，并纳入本代清理屏障。 */
    Result<std::shared_ptr<detail::CommittedLease>> consumeErased(const ServiceRegistry& registry,
                                                                  const ServiceKey& key, std::type_index type,
                                                                  std::uint32_t minimumMinor,
                                                                  const std::vector<std::string>& capabilities) const;
    friend struct detail::InstanceControl;
    friend class Instance;
    /** @brief 捕获实例的弱引用，不延长实例生命周期。 */
    explicit Context(std::weak_ptr<detail::InstanceControl> control, Epoch epoch, std::string config, RealmMap realms = {})
        : control_(std::move(control)), epoch_(epoch), config_(std::move(config)), realms_(std::move(realms)) {}
    std::weak_ptr<detail::InstanceControl> control_;
    Epoch epoch_;
    std::string config_;
    RealmMap realms_;
};

/** @brief 激活前必须固定的强依赖，接口类型仅用于同工具链检查。 */
struct ServiceRequirement {
    ServiceKey key;
    std::type_index type;
    std::uint32_t minimumMinor = 0;
    std::vector<std::string> capabilities;
    /** @brief 创建类型化强依赖声明。 */
    template<class T> static ServiceRequirement of(ServiceKey key, std::uint32_t minimumMinor = 0, std::vector<std::string> capabilities = {})
    { return {std::move(key), typeid(T), minimumMinor, std::move(capabilities)}; }
};

/** @brief 声明托管输出的类型、必供约束和实现版本；optional 输出仍须满足声明类型。 */
struct ServiceOutput {
    ServiceKey key;
    std::type_index type;
    bool required = true;
    ServiceMetadata metadata;
    /** @brief 创建输出描述，required=false 表示允许该输出缺席。 */
    template<class T> static ServiceOutput of(ServiceKey key, bool required = true, ServiceMetadata metadata = {})
    { return {std::move(key), typeid(T), required, std::move(metadata)}; }
};

/** @brief 同一内置插件定义；可用于创建多个独立实例。 */
class Runtime {
public:
    /** @brief 获取进程内唯一标识，副本共享标识；移动后原句柄返回无效标识。 */
    RuntimeId id() const noexcept { return activate_ ? id_ : RuntimeId{}; }
    /** @brief 宿主读取不可变的类型化依赖契约，不授予激活回调或服务调用能力。 */
    const std::vector<ServiceRequirement>& requirements() const noexcept { return dependencies_; }
    /** @brief 宿主读取不可变的输出契约，用于运行计划排序与域匹配。 */
    const std::vector<ServiceOutput>& outputs() const noexcept { return outputs_; }
    using Activate = std::function<Task<void>(Context)>;
    /** @brief 创建插件定义；全部输出须声明，空输出表禁止 provide；无效描述抛 invalid_argument。 */
    explicit Runtime(Activate activate, std::vector<ServiceRequirement> dependencies = {}, std::vector<ServiceOutput> outputs = {});
private:
    friend class Instance;
    std::shared_ptr<const Activate> activate_;
    RuntimeId id_;
    std::vector<ServiceRequirement> dependencies_;
    std::vector<ServiceOutput> outputs_;
};

/** @brief 实例当前代次、目标修订及资源的诊断视图。 */
struct InstanceSnapshot {
    InstanceState state;
    bool deliveryBlocked;
    ScopeSnapshot resources;
    Epoch epoch;
    ConfigRevision revision;
    InstanceId instance;
    RuntimeId runtime;
    std::size_t committedLeases;
    std::optional<ErrorInfo> failure;
    std::size_t children;
};

/** @brief 声明强依赖的当前解析状态；key 已应用实例 realm，error 为空表示可发现。 */
struct DependencySnapshot {
    ServiceKey key;
    std::uint32_t minimumMinor;
    std::vector<std::string> capabilities;
    std::optional<ErrorInfo> error;
};

/** @brief 一次配置操作的稳定结果；epoch 为实际启动代，未启动时为零。 */
struct StableReport {
    StableOutcome outcome;
    ConfigRevision revision;
    Epoch epoch;
};

/** @brief 配置操作的可共享观察句柄。 */
struct OperationHandle {
    ConfigRevision revision;
    Task<StableReport> target;
    OperationId id;
    InstanceId instance;
};

/** @brief 指定代次曾成功进入 Active 的历史结果，不授予当前业务能力。 */
struct ActiveReport {
    InstanceId instance;
    Epoch epoch;
    ConfigRevision revision;
};

/** @brief 可更新的内置插件实例；必须在关闭调度器前显式 stop。 */
class Instance {
public:
    /** @brief 排队激活插件；要求开放的 owner-thread scheduler，否则抛 invalid_argument。 */
    static Instance mount(const Runtime& runtime, std::shared_ptr<IScheduler> scheduler, std::string config = {},
                          std::shared_ptr<ServiceRegistry> services = {}, RealmMap realms = {},
                          std::vector<ServiceRequirement> additionalDependencies = {});
    /** @brief 在期望修订匹配时替换目标配置；返回独立稳定结果观察句柄。 */
    Result<OperationHandle> update(std::string config, ConfigRevision expectedRevision) const;
    /** @brief 对失败且已静止的实例重新激活，保留配置修订并分配新操作。 */
    Result<OperationHandle> retry() const;
    /** @brief 获取当前操作，包括 mount 的初始操作；仅 owner 线程使用。 */
    Result<OperationHandle> operation() const;
    /** @brief 查询本实例操作的稳定结果；拒绝其他实例或未知操作。 */
    Task<StableReport> awaitTarget(const OperationHandle& operation) const;
    /** @brief 查询已经启动的代次，未知/未来代次报错；退役前未激活的代次报取消。 */
    Task<ActiveReport> awaitActive(Epoch epoch) const;
    /** @brief owner 观察当前或后续 afterEpoch 后的弱上下文；不回放退役代次，失败可等重试，停止取消。 */
    Task<Context> whenActiveAfter(Epoch afterEpoch = 0) const;
    /** @brief 兼容的首次有效激活观察；失败后不重置，后续代次通过操作 target 观察。 */
    Task<void> ready() const;
    /** @brief 幂等终结实例，等待激活和所有 effect 清理；错误线程返回失败任务。 */
    Task<CleanupReport> stop() const;
    /** @brief 在 owner 线程读取状态及尚未释放的资源。 */
    Result<InstanceSnapshot> snapshot() const;
    /** @brief owner 只读检查依赖的精确域、类型、minor 和 capability；不提交新租约。 */
    Result<std::vector<DependencySnapshot>> dependencies() const;
private:
    friend class Context;
    friend class Kernel;
    /** @brief 仅 Kernel 以同一注册表和 Runtime 复制已终结实例的输入，旧实例保持终结。 */
    Result<Instance> remount(const Runtime& runtime, const std::shared_ptr<IScheduler>& scheduler,
                             const std::shared_ptr<ServiceRegistry>& services) const;
    /** @brief 接受配置替换或重试，区分配置修订与操作标识。 */
    Result<OperationHandle> change(std::string config, ConfigRevision expectedRevision, bool revise) const;
    /** @brief 持有实例共享控制块，句柄析构不执行异步停止。 */
    explicit Instance(std::shared_ptr<detail::InstanceControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::InstanceControl> control_;
};
} // namespace cordis
