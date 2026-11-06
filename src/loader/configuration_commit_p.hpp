/** @file configuration_commit_p.hpp
 * @brief 宿主整体替换的配置提交准备，不向插件开放独立切换入口。
 */
#pragma once
#include "replacement_claim_p.hpp"
#include "replacement_plan_p.hpp"
#include "session_commit_p.hpp"
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <memory>

namespace cordis::loader::detail {
struct ReplacementEntry {
    std::string path;
    InstanceId previous;
    InstanceId candidate;
};
struct DriverCommitControl;
class PreparedDriverCommit {
public:
    PreparedDriverCommit(const PreparedDriverCommit&) = delete;
    PreparedDriverCommit& operator=(const PreparedDriverCommit&) = delete;
    PreparedDriverCommit(PreparedDriverCommit&&) noexcept;
    PreparedDriverCommit& operator=(PreparedDriverCommit&&) = delete;
    ~PreparedDriverCommit();
    bool ready() const noexcept;
    ReplacementCommitMode mode() const noexcept;
    /** @brief 仅在 ready、真实 Core 提交成功且中间无回调后调用。 */
    void publish() noexcept;

private:
    friend struct DriverCommitAccess;
    explicit PreparedDriverCommit(std::unique_ptr<DriverCommitControl> control);
    std::unique_ptr<DriverCommitControl> control_;
};
struct DriverCommitAccess {
    static Result<DriverReplacementClaim> reserve(KernelReconcileDriver&, const ReplacementScope&,
                                                  std::shared_ptr<const ConfigurationDefinitions>,
                                                  std::shared_ptr<const ValidatedCandidate>,
                                                  std::shared_ptr<const ValidatedCandidate> baseline);
    static Result<std::vector<InstanceId>> reconstructionOwners(KernelReconcileDriver&, const std::vector<RuntimeId>&);
    static bool current(const KernelReconcileDriver&, const DriverReconstructionPlan&) noexcept;
    static Result<DriverReconstructionPlan>
    inspectReconstruction(KernelReconcileDriver&, const ReplacementScope&,
                          std::shared_ptr<const ConfigurationDefinitions>, std::shared_ptr<const ValidatedCandidate>,
                          ReplacementCommitMode mode = ReplacementCommitMode::Candidate);
    static Result<std::shared_ptr<const ConfigurationDefinitions>>
    rebaseDefinitions(KernelReconcileDriver&, const DriverDefinitionChange&, ReplacementCommitMode);
    static Result<bool> owns(const KernelReconcileDriver&, const Instance&);
    static Result<PreparedDriverCommit> prepare(KernelReconcileDriver&, const ReplacementScope&,
                                                std::shared_ptr<const ConfigurationDefinitions>,
                                                std::shared_ptr<const ValidatedCandidate>,
                                                const std::vector<ReplacementEntry>&,
                                                const DriverReplacementClaim* = nullptr);
};
struct ConfigurationCommitControl;
class PreparedConfigurationCommit {
public:
    PreparedConfigurationCommit(const PreparedConfigurationCommit&) = delete;
    PreparedConfigurationCommit& operator=(const PreparedConfigurationCommit&) = delete;
    PreparedConfigurationCommit(PreparedConfigurationCommit&&) noexcept = default;
    PreparedConfigurationCommit& operator=(PreparedConfigurationCommit&&) = delete;
    bool ready() const noexcept;
    /** @brief Core/Module 提交成功后无回调交换 Driver/目标/Session 元数据。 */
    void publish() noexcept;
    /** @brief 退出模块锁、释放预约及完成 Core 通知后调用；解除门禁后才发布计划结果。 */
    Result<void> finish(Result<void> notifications);
    std::optional<AcceptedTarget> accepted() const;

private:
    friend struct ConfigurationCommitAccess;
    explicit PreparedConfigurationCommit(std::shared_ptr<ConfigurationCommitControl> control)
        : control_(std::move(control))
    {
    }
    std::shared_ptr<ConfigurationCommitControl> control_;
};
struct ConfigurationCommitAccess {
    static Result<DriverReplacementClaim> reserve(ConfigurationSession&, KernelReconcileDriver&,
                                                  const ReplacementScope&,
                                                  std::shared_ptr<const ConfigurationDefinitions>,
                                                  std::shared_ptr<const ValidatedCandidate>, std::uint64_t expected);
    // 宿主操作须在 owner 线程保留 Kernel/Scope 到 finish；准备凭据不能排队等待。
    static Result<PreparedConfigurationCommit>
    prepare(ConfigurationSession&, KernelReconcileDriver&, const ReplacementScope&,
            std::shared_ptr<const ConfigurationDefinitions>, std::shared_ptr<const ValidatedCandidate>,
            const std::vector<ReplacementEntry>&, std::uint64_t expected, const SessionReplacementHold* = nullptr,
            const DriverReplacementClaim* = nullptr);
};
} // namespace cordis::loader::detail
