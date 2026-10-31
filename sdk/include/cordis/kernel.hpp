/** @file kernel.hpp
 * @brief 提供 owner 线程的内置插件注册、实例所有权和关闭入口。
 */
#pragma once
#include <atomic>
#include <cordis/instance.hpp>
#include <cordis/replacement.hpp>
#include <map>

namespace cordis {
/** @brief 插件名称映射只含宿主值，不包含激活回调或 Runtime 能力。 */
struct RuntimeNameSnapshot {
    RuntimeId id;
    std::string name;
};
/** @brief 内核登记的实例诊断集合；Disposed 实例在读取时回收。 */
struct KernelSnapshot {
    bool closing;
    bool deliveryBlocked;
    std::size_t runtimes;
    std::vector<InstanceSnapshot> instances;
    std::vector<RuntimeNameSnapshot> names;
};

/** @brief 强持有已挂载实例；宿主须在 scheduler 关闭前显式 close。 */
class Kernel {
public:
    /** @brief 创建 owner 线程内核，要求开放调度器。 */
    explicit Kernel(std::shared_ptr<IScheduler> scheduler);
    /** @brief 禁止复制内核，避免注册表和关闭协议出现多个所有者。 */
    Kernel(const Kernel&) = delete;
    /** @brief 禁止替换存活内核的资源账目。 */
    Kernel& operator=(const Kernel&) = delete;
    /** @brief 按名称登记插件；重复名称、关闭或错误线程返回错误。 */
    Result<Runtime> registerPlugin(std::string name, Runtime::Activate activate, std::vector<ServiceRequirement> dependencies = {}, std::vector<ServiceOutput> outputs = {});
    /** @brief 登记宿主已创建的定义并保留 RuntimeId；拒绝重复名称或身份。 */
    Result<Runtime> registerRuntime(std::string name, const Runtime& runtime);
    /** @brief 获取共享索引的宿主句柄，用于隔离域配置和诊断。 */
    ServiceRegistry services() const { return *services_; }
    /** @brief 挂载已登记 Runtime；附加类型化依赖只能增加或收紧本实例的要求。 */
    Result<Instance> mount(const Runtime& runtime, std::string config = {}, RealmMap realms = {},
                           std::vector<ServiceRequirement> additionalDependencies = {});
    /** @brief 创建属于本内核索引的发布组；创建本身不激活实例。 */
    Result<PublicationGroup> holdPublication() const;
    /** @brief 以同一服务索引的组内视图挂载；正常依赖仍可用，组外不可见候选输出。 */
    Result<Instance> mountHeld(const PublicationGroup& group, const Runtime& runtime, std::string config = {},
                               RealmMap realms = {}, std::vector<ServiceRequirement> additionalDependencies = {});
    /** @brief 用本内核已静止实例的最新配置及 realm 创建新 InstanceId，不复活旧实例。 */
    Result<Instance> remount(const Instance& previous);
    /** @brief 请求停止全部实例，共享关闭报告；非静止实例继续保留。 */
    Task<CleanupReport> close();
    /** @brief owner 线程读取诊断并回收已经 Disposed 的实例记录。 */
    Result<KernelSnapshot> snapshot();
    /** @brief 宿主按本内核持有的顶层实例编号查找；未知/已回收编号不返回其他实例。 */
    Result<Instance> findInstance(InstanceId id) const;
    /** @brief 只读计算替换闭包；包含全部目标 Runtime 实例、子树、实际及等待消费者。
     * 拒绝未登记 Runtime、外部消费者和开放发布组；不停止、锁定、创建或调用插件。
     */
    Result<ReplacementClosure> replacementClosure(const std::vector<RuntimeId>& runtimes) const;
    /** @brief 查询不可变的调度器归属，供宿主适配层拒绝跨 scheduler 绑定。 */
    bool usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const;
private:
    Result<Instance> mountWithRegistry(const Runtime& runtime, std::string config, RealmMap realms,
                                       std::vector<ServiceRequirement> additionalDependencies,
                                       std::shared_ptr<ServiceRegistry> services);
    std::shared_ptr<IScheduler> scheduler_;
    std::shared_ptr<ServiceRegistry> services_;
    std::map<std::string, Runtime> runtimes_;
    std::map<InstanceId, Instance> instances_;
    bool closing_ = false;
    Promise<CleanupReport> closed_;
    std::shared_ptr<std::atomic_bool> blocked_ = std::make_shared<std::atomic_bool>(false);
};
} // namespace cordis
