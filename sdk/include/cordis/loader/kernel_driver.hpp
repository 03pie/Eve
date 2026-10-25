/** @file kernel_driver.hpp
 * @brief 将顶层内置配置及宿主类型化的 inject 映射到真实 Kernel 实例。
 */
#pragma once
#include <cordis/kernel.hpp>
#include <cordis/loader/coordinator.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cordis::loader {
/** @brief 配置插件标识映射到已经登记在同一 Kernel 的不可变 Runtime。 */
struct KernelPlugin {
    std::string id;
    Runtime runtime;
    /** @brief 可按需追加的依赖类型/原始域及最低要求；未列入的新增接口在接受前拒绝。 */
    std::vector<ServiceRequirement> configurableInject{};
};
namespace detail {
struct KernelDriverControl;
}
/** @brief 顶层内置条目支持固定依赖收紧、类型化附加依赖和宿主命名的 realm。 */
class KernelReconcileDriver final : public IReconcileDriver {
public:
    /** @brief 校验 scheduler/Kernel/Runtime 归属；构造不挂载实例，不执行插件。 */
    KernelReconcileDriver(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<Kernel> kernel,
                          std::vector<KernelPlugin> plugins, std::map<std::string, RealmId> realms = {});
    /** @brief 析构不替代 close；有资源或投递阻断时控制块保守自保留。 */
    ~KernelReconcileDriver() override;
    /** @brief 禁止复制驱动执行账目。 */
    KernelReconcileDriver(const KernelReconcileDriver&) = delete;
    /** @brief 禁止覆盖仍有实例的驱动。 */
    KernelReconcileDriver& operator=(const KernelReconcileDriver&) = delete;
    /** @brief 检查整个目标的能力、域及依赖图，无实例副作用。 */
    Result<void> preflight(const ValidatedCandidate& target) override;
    /** @brief 按实际记录规划：消费者先删除，提供者先添加/更新，动作路径唯一。 */
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate& target) override;
    /** @brief 最多一个动作在途，使用真实稳定操作及完整 stop 回执。 */
    Task<StepReport> apply(EntryOperation operation, std::shared_ptr<const ValidatedCandidate> target,
                           std::uint64_t revision) override;
    /** @brief 返回真实状态；内部投递阻断报告错误，不能用空列表冒充排空。 */
    Result<std::vector<RunningEntry>> snapshot() const override;
    /** @brief 先广播停止所有本驱动拥有的实例，再等清理与在途动作；不关闭共享 Kernel。 */
    Task<CleanupReport> close() override;

private:
    std::shared_ptr<detail::KernelDriverControl> control_;
};
} // namespace cordis::loader
