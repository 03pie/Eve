/** @file replacement_scope.hpp
 * @brief Core 闭包的唯一排他所有者；不表示模块角色或业务升级已切换。
 */
#pragma once
#include <cordis/effect.hpp>
#include <cordis/instance.hpp>
#include <cordis/replacement.hpp>
#include <cordis/service.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace cordis {
namespace detail {
struct ReplacementScopeControl;
struct CoreReplacementAccess;
}
enum class ReplacementScopeState {
    Reserved,
    Frozen,
    Stopping,
    Stopped,
    Failed,
    Released,
    Staging,
    Discarding,
    Prepared,
    Committed
};
enum class ReplacementCommitMode { Candidate, Recovery };
/** @brief 定义身份换代，不修改旧 Runtime 的工厂；名称到新身份的绑定仅在提交时交换。 */
struct RuntimeReplacement {
    RuntimeId previous;
    Runtime candidate;
};
/** @brief 已准备定义的纯身份，不保留工厂、实例或迁移状态。 */
struct RuntimeReplacementIdentity {
    RuntimeId previous;
    RuntimeId candidate;
};
struct ReplacementPreparation {
    OperationId operation;
    ReplacementCommitMode mode;
    std::vector<RuntimeReplacementIdentity> definitions;
};
/** @brief 占用时捕获的宿主重建输入，含工厂引用；不是纯诊断、迁移状态或序列化载荷。 */
struct ReplacementInput {
    InstanceId instance;
    InstanceId parent;
    RuntimeId runtime;
    /** @brief 仅保留 Kernel 已登记的原始定义；未登记子实例必须由拥有者重建。 */
    std::optional<Runtime> factory;
    std::string config;
    RealmMap realms;
    /** @brief 旧实例实际合并依赖，用于核对；新工厂不能将其误当作宿主附加契约。 */
    std::vector<ServiceRequirement> dependencies;
    /** @brief 原始宿主附加依赖；重建时与选中的原/新工厂契约及 realm 重新合并。 */
    std::vector<ServiceRequirement> additionalDependencies;
    Epoch epoch;
    ConfigRevision revision;
    /** @brief 实际实例的纯输出声明；不暴露临时激活工厂或授予重放权限。 */
    std::vector<ServiceOutput> outputs = {};
};
struct ReplacementScopeSnapshot {
    OperationId operation;
    ReplacementScopeState state;
    /** @brief 取得占用时的版本快照；不回写为最新实例状态。 */
    ReplacementClosure closure;
    std::vector<ServiceKey> reservedOutputs;
    bool abandoned;
    bool deliveryBlocked;
    bool cancelled;
    /** @brief 旧闭包 stop 的历史回执；候选清理分别观察 discard 回执及 failure。 */
    std::optional<CleanupReport> cleanup;
    std::optional<ErrorInfo> failure;
};
/** @brief owner 线程使用；停止开始后丢弃不会自动解除占用或伪报恢复完成。 */
class ReplacementScope {
public:
    ReplacementScope() noexcept = default;
    ReplacementScope(const ReplacementScope&) = delete;
    ReplacementScope& operator=(const ReplacementScope&) = delete;
    ReplacementScope(ReplacementScope&&) noexcept = default;
    ReplacementScope& operator=(ReplacementScope&& other) noexcept;
    ~ReplacementScope();
    Result<ReplacementScopeSnapshot> snapshot() const;
    /** @brief owner 线程读取占用时的输入副本；释放或取消后拒绝，不授予挂载绕过能力。 */
    Result<std::vector<ReplacementInput>> inputs() const;
    /** @brief 冻结前一次暂存完整目标定义；不登记规范名称或执行工厂。 */
    Result<void> prepareDefinitions(std::vector<RuntimeReplacement> definitions) const;
    /** @brief 重验已占用闭包并关闭旧业务；清理能力继续有效。 */
    Result<void> freeze() const;
    /** @brief 冻结后逐项等待真实 stop；观察共享回执，不创建/切换任何工厂。 */
    Task<CleanupReport> stop() const;
    /** @brief 仅旧闭包静止后在本作用域的隔离发布视图创建候选，普通挂载不能复用此权限。 */
    Result<Instance> stage(const Runtime& runtime, std::string config = {}, RealmMap realms = {},
                           std::vector<ServiceRequirement> additionalDependencies = {}) const;
    /** @brief 放弃候选发布组并等待真实清理；失败保留全部占用，不表示旧代已经恢复。 */
    Task<CleanupReport> discard() const;
    /** @brief 全部候选就绪后准备 Core 提交；冻结候选业务，不运行外部通知。 */
    Result<void> prepareCommit(ReplacementCommitMode mode = ReplacementCommitMode::Candidate) const;
    /** @brief owner 线程读取真实 Prepared 模式和定义身份，供宿主跨层提交核对；其他阶段拒绝。 */
    Result<ReplacementPreparation> preparation() const;
    /** @brief 只交换已准备的 Core 名称绑定和输出状态，不分配或通知；不表示模块/配置已提交。 */
    Result<void> commitPrepared() const;
    /** @brief 撤销静止作用域，或整体元数据提交后解除 Core 占用并通知；失败保留真实状态。 */
    Result<void> release() const;

private:
    friend class Kernel;
    friend struct detail::KernelControl;
    friend struct detail::CoreReplacementAccess;
    explicit ReplacementScope(std::shared_ptr<detail::ReplacementScopeControl> control) : control_(std::move(control))
    {
    }
    std::shared_ptr<detail::ReplacementScopeControl> control_;
};
} // namespace cordis
