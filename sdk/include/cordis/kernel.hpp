/** @file kernel.hpp
 * @brief 提供 owner 线程的内置插件注册、实例所有权和关闭入口。
 */
#pragma once
#include <cordis/instance.hpp>
#include <map>
#include <atomic>

namespace cordis {
/** @brief 内核登记的实例诊断集合；Disposed 实例在读取时回收。 */
struct KernelSnapshot {
    bool closing;
    bool deliveryBlocked;
    std::size_t runtimes;
    std::vector<InstanceSnapshot> instances;
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
    Result<Runtime> registerPlugin(std::string name, Runtime::Activate activate);
    /** @brief 挂载本内核登记的 Runtime，异步激活由实例调度。 */
    Result<Instance> mount(const Runtime& runtime, std::string config = {});
    /** @brief 请求停止全部实例，共享关闭报告；非静止实例继续保留。 */
    Task<CleanupReport> close();
    /** @brief owner 线程读取诊断并回收已经 Disposed 的实例记录。 */
    Result<KernelSnapshot> snapshot();
private:
    std::shared_ptr<IScheduler> scheduler_;
    std::map<std::string, Runtime> runtimes_;
    std::map<InstanceId, Instance> instances_;
    bool closing_ = false;
    Promise<CleanupReport> closed_;
    std::shared_ptr<std::atomic_bool> blocked_ = std::make_shared<std::atomic_bool>(false);
};
} // namespace cordis
