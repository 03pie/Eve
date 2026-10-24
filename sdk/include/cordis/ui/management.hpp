/** @file management.hpp
 * @brief 区分宿主管理能力与 diagnostics/v1 只读快照，不暴露 Qt 控件或插件对象。
 */
#pragma once
#include <cordis/kernel.hpp>
#include <cordis/ui/pages.hpp>
#include <cordis/ui/commands.hpp>
#include <cordis/ui/notifications.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cordis::ui {
namespace detail { struct ManagementControl; }
class ElaShell;
/** @brief 宿主支持的操作；停止终结 InstanceId，重试只用于清理完成的失败实例。 */
enum class ManagementAction { Stop, Retry };
/** @brief 不含 Task 或闭包的操作记录；request 是管理请求号，operation 是内核真实操作号。 */
struct ManagementOperation {
    std::uint64_t request = 0;
    InstanceId instance;
    ManagementAction action = ManagementAction::Stop;
    OperationId operation;
    bool pending = true;
    bool deliveryBlocked = false;
    std::optional<StableReport> stable;
    std::optional<CleanupReport> cleanup;
    std::optional<ErrorInfo> error;
};
/** @brief 当前顶层实例的只读详情，包含当前操作历史结果和实时依赖查找状态。 */
struct ManagedInstanceSnapshot {
    InstanceSnapshot instance;
    std::string name;
    std::vector<DependencySnapshot> dependencies;
    OperationId operation;
    std::optional<StableReport> stable;
    bool busy = false;
    bool canStop = false;
    bool canRetry = false;
};
/** @brief 全部为宿主值数据；子实例当前只显示数量，配置计划/持久化/模块账目另接入。 */
struct DiagnosticsSnapshot {
    bool managementOpen;
    KernelSnapshot kernel;
    std::vector<ManagedInstanceSnapshot> instances;
    std::vector<BindingSnapshot> bindings;
    std::vector<PageSnapshot> pages;
    std::vector<CommandSnapshot> commands;
    std::vector<NotificationSnapshot> notifications;
    std::vector<ManagementOperation> operations;
};
/** @brief diagnostics/v1 只读服务；不提供 Kernel、Instance、停止或重试能力。 */
class DiagnosticsService {
public:
    /** @brief owner 复制当前快照；绑定的 Kernel 过期时报错，不读取悬空宿主。 */
    Result<DiagnosticsSnapshot> snapshot() const;
private:
    friend class ElaShell;
    /** @brief 外壳拥有后端，服务只借用弱控制块。 */
    explicit DiagnosticsService(std::weak_ptr<detail::ManagementControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ManagementControl> control_;
};
/** @brief 仅宿主持有的管理入口；每个实例最多一个管理请求，所有动作排队重新校验。 */
class ManagementController {
public:
    static constexpr std::size_t historyLimit = 100;
    /** @brief owner 绑定同 scheduler 的 Kernel，只持弱引用；相同 Kernel 重复绑定幂等。 */
    Result<void> attach(const std::shared_ptr<Kernel>& kernel) const;
    /** @brief owner 按真实快照刷新管理页，刷新不执行插件回调或服务调用。 */
    Result<void> refresh() const;
    /** @brief 接受后立即锁定该实例并排队停止，返回管理请求号；双击不重复接受。 */
    Result<std::uint64_t> stop(InstanceId instance) const;
    /** @brief 接受后排队重试，记录新 OperationId 和最终 StableReport，不改配置修订。 */
    Result<std::uint64_t> retry(InstanceId instance) const;
private:
    friend class ElaShell;
    /** @brief 控制器不强持 Kernel 或原生窗口，宿主关闭后不得发起新操作。 */
    explicit ManagementController(std::weak_ptr<detail::ManagementControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ManagementControl> control_;
};
} // namespace cordis::ui
