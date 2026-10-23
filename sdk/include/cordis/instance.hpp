/** @file instance.hpp
 * @brief 声明内置插件的多代实例、配置操作及弱生命周期上下文。
 */
#pragma once
#include <cordis/effect.hpp>

namespace cordis {
namespace detail { struct InstanceControl; }

/** @brief 弱引用实例的业务上下文；仅允许在 owner 线程注册资源。 */
class Context {
public:
    /** @brief 获取当前上下文捕获的配置副本，不读取新代配置。 */
    const std::string& config() const noexcept { return config_; }
    /** @brief 在当前激活仍有效时登记 effect；停止后拒绝新资源。 */
    Result<EffectToken> effect(EffectSetup setup, std::string label = "anonymous") const;
private:
    friend struct detail::InstanceControl;
    /** @brief 捕获实例的弱引用，不延长实例生命周期。 */
    explicit Context(std::weak_ptr<detail::InstanceControl> control, Epoch epoch, std::string config)
        : control_(std::move(control)), epoch_(epoch), config_(std::move(config)) {}
    std::weak_ptr<detail::InstanceControl> control_;
    Epoch epoch_;
    std::string config_;
};

/** @brief 同一内置插件定义；可用于创建多个独立实例。 */
class Runtime {
public:
    /** @brief 获取进程内唯一标识，副本共享标识；移动后原句柄返回无效标识。 */
    RuntimeId id() const noexcept { return activate_ ? id_ : RuntimeId{}; }
    using Activate = std::function<Task<void>(Context)>;
    /** @brief 创建插件定义；空回调抛出 invalid_argument。 */
    explicit Runtime(Activate activate);
private:
    friend class Instance;
    std::shared_ptr<const Activate> activate_;
    RuntimeId id_;
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
    static Instance mount(const Runtime& runtime, std::shared_ptr<IScheduler> scheduler, std::string config = {});
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
    /** @brief 兼容的首次有效激活观察；失败后不重置，后续代次通过操作 target 观察。 */
    Task<void> ready() const;
    /** @brief 幂等终结实例，等待激活和所有 effect 清理；错误线程返回失败任务。 */
    Task<CleanupReport> stop() const;
    /** @brief 在 owner 线程读取状态及尚未释放的资源。 */
    Result<InstanceSnapshot> snapshot() const;
private:
    /** @brief 接受配置替换或重试，区分配置修订与操作标识。 */
    Result<OperationHandle> change(std::string config, ConfigRevision expectedRevision, bool revise) const;
    /** @brief 持有实例共享控制块，句柄析构不执行异步停止。 */
    explicit Instance(std::shared_ptr<detail::InstanceControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::InstanceControl> control_;
};
} // namespace cordis
