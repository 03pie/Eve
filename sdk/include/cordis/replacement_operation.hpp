/** @file replacement_operation.hpp
 * @brief 宿主整体替换：拥有真实 Core/模块预约，历史结果不保留工厂或回调。
 */
#pragma once
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/replacement_builder.hpp>
#include <map>
namespace cordis {
enum class ReplacementPhase {
    Prepared,
    Stopping,
    Building,
    Discarding,
    Recovering,
    Committed,
    Restored,
    Failed,
    Cancelled
};
enum class ReplacementOutcome { Applied, Restored, Cancelled, Failed };
struct ReplacementInstanceMapping {
    InstanceId previous;
    InstanceId current;
};
struct ReplacementReport {
    OperationId operation;
    GenerationId previous;
    GenerationId candidate;
    ReplacementOutcome outcome = ReplacementOutcome::Failed;
    bool committed = false;
    bool recovered = false;
    bool resourcesRetained = false;
    /** @brief 只包含整体提交或旧代恢复已发布的映射，不暴露待提交/已丢弃实例。 */
    std::vector<ReplacementInstanceMapping> instances;
    std::vector<ErrorInfo> errors;
};
/** @brief 纯未来契约；依赖和输出在平台接纳后均为精确解析域。 */
struct ReplacementServiceContract {
    std::vector<ServiceRequirement> requirements;
    std::vector<ServiceOutput> outputs;
};
struct ReplacementConstruction {
    std::size_t key;
    std::optional<std::size_t> parent;
    std::optional<ReplacementInput> previous;
    /** @brief 仅平台可独立创建的根/显式配置项有工厂；动态子实例交由拥有者创建。 */
    std::optional<Runtime> factory;
    std::string config;
    RealmMap realms;
    std::vector<ServiceRequirement> additionalDependencies;
    std::optional<ReplacementServiceContract> contract = {};
    /** @brief 空值为内置工厂；不透明成员在拥有者预检前取得实际默认来源。 */
    std::optional<module::ModuleFactorySource> source = {};
};
struct ReplacementBuildPlan {
    bool recovery;
    std::vector<ReplacementConstruction> members;
};
struct ReplacementRebuiltInstance {
    std::size_t key;
    Instance instance;
};
struct ReplacementRebuiltTree {
    std::vector<ReplacementRebuiltInstance> members;
};
/** @brief 仅描述动态成员及新增子节点；新 key 由平台规范化后随 rebuild 计划交还。 */
struct ReplacementDynamicMember {
    std::size_t key;
    std::optional<std::size_t> parent;
    std::string config;
    RealmMap realms;
    ReplacementServiceContract contract;
    /** @brief 明确的未来来源；空值表示内置，已有成员可复制预检输入的默认来源。 */
    std::optional<module::ModuleFactorySource> source = {};
};
struct ReplacementOwnerPreflight {
    std::vector<ReplacementDynamicMember> candidate;
    std::vector<ReplacementDynamicMember> recovery;
};
/** @brief 宿主拥有单元的重建协议。preflight 不执行业务，rebuild 只在旧单元静止后调用。 */
class IReplacementOwner {
public:
    virtual ~IReplacementOwner() = default;
    virtual Result<ReplacementOwnerPreflight> preflight(const ReplacementBuildPlan& candidate,
                                                        const ReplacementBuildPlan& recovery) = 0;
    virtual Task<ReplacementRebuiltTree> rebuild(ReplacementBuilder, ReplacementBuildPlan) = 0;
};
struct ReplacementConfiguration {
    std::shared_ptr<loader::ConfigurationSession> session;
    std::shared_ptr<loader::KernelReconcileDriver> driver;
    std::shared_ptr<const loader::ConfigurationDefinitions> definitions;
    std::shared_ptr<const loader::ValidatedCandidate> target;
    std::uint64_t expectedRevision;
};
struct ReplacementRequest {
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<Kernel> kernel;
    std::shared_ptr<module::ModuleManager> modules;
    GenerationId previous;
    GenerationId candidate;
    /** @brief 配置定位文本解析所用的已装载插件根；手动装配可为空。 */
    std::string pluginRoot;
    std::vector<RuntimeReplacement> definitions;
    std::vector<ReplacementConfiguration> configurations;
    /** @brief 键必须是实际闭包根，不允许用任意实例编号授权另一拥有单元。 */
    std::map<InstanceId, std::shared_ptr<IReplacementOwner>> owners;
};
struct ReplacementSnapshot {
    ReplacementPhase phase;
    bool cancelled;
    bool deliveryBlocked;
    ReplacementReport report;
};
namespace detail {
struct ReplacementOperationControl;
}
/** @brief 仅 owner 线程操作；丢弃 facade 不撤销已开始的清理或解除失败资源的预约。 */
class ReplacementOperation {
public:
    static Result<ReplacementOperation> begin(ReplacementRequest request);
    ReplacementOperation(const ReplacementOperation&) = delete;
    ReplacementOperation& operator=(const ReplacementOperation&) = delete;
    ReplacementOperation(ReplacementOperation&&) noexcept = default;
    ReplacementOperation& operator=(ReplacementOperation&&) noexcept = default;
    Task<ReplacementReport> settled() const;
    Result<ReplacementSnapshot> snapshot() const;
    Result<void> cancel();

private:
    explicit ReplacementOperation(std::shared_ptr<detail::ReplacementOperationControl> control)
        : control_(std::move(control))
    {
    }
    std::shared_ptr<detail::ReplacementOperationControl> control_;
};
} // namespace cordis
