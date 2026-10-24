/** @file coordinator.hpp
 * @brief 以 CAS 接受内置配置目标，串行运行驱动动作并在安全检查点收敛到最新目标。
 */
#pragma once
#include <cordis/loader/yaml.hpp>
#include <cordis/effect.hpp>
#include <memory>
#include <optional>

namespace cordis::loader {
/** @brief 动作由运行驱动基于实际资源规划，不能直接使用目标之间的 PlanPreview。 */
struct EntryOperation {
    std::string path;
    ChangeKind kind = ChangeKind::NoOp;
};
/** @brief 运行驱动的一次稳定动作结果；清理不确定时必须停止后续执行。 */
enum class StepOutcome { Applied, Pending, Failed };
/** @brief Failed 必须附带错误；safeToContinue=false 表示不能在该资源状态上继续协调。 */
struct StepReport {
    StepOutcome outcome = StepOutcome::Applied;
    bool safeToContinue = true;
    std::optional<ErrorInfo> error;
};
/** @brief 对外动作状态，区分未开始、在途、实际结果及被旧计划放弃的动作。 */
enum class EntryStatus { NotStarted, Running, Applied, Pending, Failed, Skipped, Superseded };
/** @brief 终结报告仅含宿主值，不保留候选、运行驱动或插件函数。 */
struct EntryExecution {
    EntryOperation operation;
    EntryStatus status = EntryStatus::NotStarted;
    std::optional<ErrorInfo> error;
};
/** @brief Pending 是历史稳定结果，关闭或被替代也不能伪装成全部应用成功。 */
enum class PlanOutcome { Applied, Pending, Failed, Superseded, Cancelled };
/** @brief 一个已接受修订的历史执行报告，已应用部分在失败或替代后继续保留。 */
struct PlanReport {
    std::uint64_t revision = 0;
    PlanOutcome outcome = PlanOutcome::Applied;
    std::vector<EntryExecution> entries;
    std::vector<ErrorInfo> errors;
};
/** @brief 实际资源的值快照，由运行驱动提供，协调器不以目标推断运行成功。 */
struct RunningEntry {
    std::string path;
    EntryValue configuration;
    InstanceState state = InstanceState::Pending;
    InstanceId instance;
    Epoch epoch = 0;
};
/** @brief 宿主运行适配协议；所有方法只在 owner 线程调用，方法可同步重入协调器。 */
class IReconcileDriver {
public:
    /** @brief 只有 close 确认静止后才允许释放驱动拥有的资源。 */
    virtual ~IReconcileDriver() = default;
    /** @brief 无副作用检查驱动是否支持整个目标，拒绝未实现的子树、域或依赖能力。 */
    virtual Result<void> preflight(const ValidatedCandidate& target) = 0;
    /** @brief 从实际运行资源规划完整有序动作，负责消费者先移除/提供者先添加及父子归属。 */
    virtual Result<std::vector<EntryOperation>> plan(const ValidatedCandidate& target) = 0;
    /** @brief 执行单个原子协调动作并返回稳定结果；异步期间自行保留目标，关闭必须终结在途动作。 */
    virtual Task<StepReport> apply(EntryOperation operation, std::shared_ptr<const ValidatedCandidate> target,
                                   std::uint64_t revision) = 0;
    /** @brief 返回实际值快照，不转移运行能力。 */
    virtual Result<std::vector<RunningEntry>> snapshot() const = 0;
    /** @brief 立即广播停止并异步排空；可与尚未完成的 apply 共存，不得等待协调器先结束该动作。 */
    virtual Task<CleanupReport> close() = 0;
};
/** @brief 目标接受与执行结果分离，执行失败不回滚已接受的目标修订。 */
struct AcceptedTarget {
    std::uint64_t revision;
    Task<PlanReport> settled;
};
/** @brief 期望状态与实际运行状态分开读取，尚无持久化状态。 */
struct CoordinatorSnapshot {
    std::uint64_t desiredRevision = 0;
    std::shared_ptr<const ValidatedCandidate> desired;
    std::optional<std::uint64_t> activeRevision;
    bool closing = false;
    bool unsafe = false;
    bool deliveryBlocked = false;
    std::vector<RunningEntry> running;
    std::optional<ErrorInfo> runningError;
};
namespace detail { struct CoordinatorControl; }
/** @brief 同时最多一个在途动作和一个最新待执行目标；须在关闭 scheduler 前显式 close。 */
class ConfigurationCoordinator {
public:
    /** @brief 要求开放 owner scheduler 和运行驱动，构造不调用驱动或执行资源动作。 */
    ConfigurationCoordinator(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<IReconcileDriver> driver);
    /** @brief facade 销毁不替代显式异步清理。 */
    ~ConfigurationCoordinator();
    /** @brief 协调器只有一个 facade 所有者，不复制执行队列。 */
    ConfigurationCoordinator(const ConfigurationCoordinator&) = delete;
    /** @brief 不允许覆盖尚未完成的驱动和目标账目。 */
    ConfigurationCoordinator& operator=(const ConfigurationCoordinator&) = delete;
    /** @brief 接受完整目标；检查 expectedDesiredRevision，预检重入后再次检查 CAS 和关闭门禁。 */
    Result<AcceptedTarget> accept(ValidatedCandidate target, std::uint64_t expectedDesiredRevision);
    /** @brief owner 读取目标及实际快照；运行查询失败通过 runningError 报告，期望目标仍可读取。 */
    Result<CoordinatorSnapshot> snapshot() const;
    /** @brief 同步关门并优先请求驱动停止，同时等待在途回执和真实关闭证据。 */
    Task<CleanupReport> close();
private:
    std::shared_ptr<detail::CoordinatorControl> control_;
};
} // namespace cordis::loader
