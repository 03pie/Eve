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
/** @brief 手动停止/重试/加载操作实例；配置启停/协调操作目标路径。 */
enum class ManagementAction { Stop, Retry, Load, SetEnabled, Reconcile };
/** @brief 配置动作的历史值结果，不包含 Loader 类型或运行句柄。 */
enum class ConfigurationOutcome { Applied, Pending, Failed, Superseded, Cancelled };
/** @brief 管理器只观察此值回执，实际运行及保存由宿主会话负责。 */
struct ConfigurationActionReport {
    std::uint64_t revision = 0;
    ConfigurationOutcome outcome = ConfigurationOutcome::Failed;
    std::optional<ErrorInfo> error{};
};
/** @brief 配置路径是稳定身份；无实例和父禁用状态不伪造 Core 快照。 */
struct ManagedConfigurationEntry {
    std::string path, plugin;
    InstanceId instance;
    Epoch epoch = 0;
    InstanceState state = InstanceState::Pending;
    bool hasActual = false, enabled = true, effectiveEnabled = true;
    bool waitingForParent = false, busy = false, canSetEnabled = false, canReconcile = false;
    std::optional<ErrorInfo> error{};
};
/** @brief 配置期望、真实条目和保存状态的宿主值快照。 */
struct ConfigurationManagementSnapshot {
    std::uint64_t revision = 0;
    std::optional<std::uint64_t> persistedRevision{};
    bool open = false, dirty = false, writing = false;
    std::vector<ManagedConfigurationEntry> entries;
    std::optional<ErrorInfo> error{};
};
/** @brief 宿主适配能力，不注册为普通插件可消费服务；快照无业务副作用。 */
class IConfigurationManagement {
public:
    /** @brief facade 释放不替代所属会话的关闭。 */
    virtual ~IConfigurationManagement() = default;
    /** @brief owner 读取值，可同步重入宿主；失败时管理器禁止配置动作。 */
    virtual Result<ConfigurationManagementSnapshot> snapshot() const = 0;
    /** @brief 按捕获的目标修订修改路径期望，不允许用实例停止代替。 */
    virtual Result<Task<ConfigurationActionReport>> setEnabled(const std::string& path, bool enabled,
                                                               std::uint64_t expectedRevision) = 0;
    /** @brief 显式重新协调完整配置目标；path 用于权限及失败选择校验。 */
    virtual Result<Task<ConfigurationActionReport>> reconcile(const std::string& path,
                                                              std::uint64_t expectedRevision) = 0;
};
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
    /** @brief Load 的源实例；instance 在执行挂载后改为新身份，其他操作保持无效值。 */
    InstanceId previousInstance;
    /** @brief 配置请求保留路径及提交时基准修订，实例重建不改变动作归属。 */
    std::string configurationPath{};
    std::uint64_t expectedDesiredRevision = 0;
    std::optional<bool> desiredEnabled{};
    std::optional<ConfigurationActionReport> configurationResult{};
};
/** @brief 顶层实例及有界停止缓存的只读详情，不返回 Instance 或 Runtime 能力。 */
struct ManagedInstanceSnapshot {
    InstanceSnapshot instance;
    std::string name;
    std::vector<DependencySnapshot> dependencies;
    OperationId operation;
    std::optional<StableReport> stable;
    bool busy = false;
    bool canStop = false;
    bool canRetry = false;
    bool canLoad = false;
};
/** @brief 全部为宿主值数据；配置条目可包含子路径，模块账目仍待接入。 */
struct DiagnosticsSnapshot {
    bool managementOpen;
    KernelSnapshot kernel;
    std::vector<ManagedInstanceSnapshot> instances;
    std::vector<BindingSnapshot> bindings;
    std::vector<PageSnapshot> pages;
    std::vector<CommandSnapshot> commands;
    std::vector<NotificationSnapshot> notifications;
    std::vector<ManagementOperation> operations;
    std::optional<ConfigurationManagementSnapshot> configuration{};
    std::optional<ErrorInfo> configurationError{};
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
/** @brief 仅宿主持有的管理入口；每个手动实例或配置路径最多一个在途请求，派发时重校验。 */
class ManagementController {
public:
    /** @brief 管理历史及停止后可加载缓存各自最多保留 100 条。 */
    static constexpr std::size_t historyLimit = 100;
    /** @brief owner 绑定同 scheduler 的 Kernel，只持弱引用；相同 Kernel 重复绑定幂等。 */
    Result<void> attach(const std::shared_ptr<Kernel>& kernel) const;
    /** @brief 一次绑定弱配置适配器；宿主须保活并独占保护定义，其所有实例禁止手动操作。 */
    Result<void> attachConfiguration(const std::shared_ptr<IConfigurationManagement>& provider,
                                     std::vector<RuntimeId> protectedRuntimes) const;
    /** @brief 排队修改路径期望；接受和执行都核对修订、权限及路径忙状态。 */
    Result<std::uint64_t> setConfigurationEnabled(const std::string& path, bool enabled,
                                                  std::uint64_t expectedRevision) const;
    /** @brief 显式重试配置收敛，不向配置实例调用手动 retry。 */
    Result<std::uint64_t> reconcileConfiguration(const std::string& path, std::uint64_t expectedRevision) const;
    /** @brief owner 按真实快照刷新管理页，刷新不执行插件回调或服务调用。 */
    Result<void> refresh() const;
    /** @brief 接受后立即锁定该实例并排队停止，返回管理请求号；双击不重复接受。 */
    Result<std::uint64_t> stop(InstanceId instance) const;
    /** @brief 接受后排队重试，记录新 OperationId 和最终 StableReport，不改配置修订。 */
    Result<std::uint64_t> retry(InstanceId instance) const;
    /** @brief 为本控制器停止并保留的实例创建新身份；复用配置/realm，不恢复旧页签内容。 */
    Result<std::uint64_t> load(InstanceId instance) const;

private:
    friend class ElaShell;
    /** @brief 控制器不强持 Kernel 或原生窗口，宿主关闭后不得发起新操作。 */
    explicit ManagementController(std::weak_ptr<detail::ManagementControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::ManagementControl> control_;
};
} // namespace cordis::ui
