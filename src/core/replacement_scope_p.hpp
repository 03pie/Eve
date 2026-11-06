/** @file replacement_scope_p.hpp
 * @brief 一个真实闭包的占用、冻结及停止账目，不保存模块策略或用户回调。
 */
#pragma once
#include "kernel_p.hpp"
#include "replacement_inspect_p.hpp"
#include <atomic>
#include <optional>
#include <set>
#include <thread>

namespace cordis::detail {
struct PreparedReplacementStamp {
    std::weak_ptr<ReplacementScopeControl> owner;
};
/** @brief 宿主投递故障可跨线程封闭同一作用域；信号本身不延长 Kernel/工厂寿命。 */
struct ReplacementFailureFlag {
    const std::shared_ptr<std::atomic_bool> signal = std::make_shared<std::atomic_bool>(false);
    bool load() const noexcept { return signal->load(); }
    void store(bool value) noexcept { signal->store(value); }
};
OperationId nextOperationId();
struct ReplacementScopeControl : std::enable_shared_from_this<ReplacementScopeControl> {
    OperationId operation;
    std::shared_ptr<KernelControl> kernel;
    std::shared_ptr<IScheduler> scheduler;
    ReplacementSelection selection;
    ReplacementScopeState state = ReplacementScopeState::Reserved;
    std::atomic_bool abandoned{false};
    ReplacementFailureFlag blocked;
    bool cancelled = false;
    bool releasing = false;
    std::size_t next = 0;
    CleanupReport report{true, {}};
    Promise<CleanupReport> stopped;
    Promise<void> released;
    std::optional<PublicationGroup> staging;
    std::map<InstanceId, Instance> staged;
    std::set<RuntimeId> stagedRuntimes;
    std::set<RuntimeId> borrowedRuntimes;
    std::set<ServiceKey> stagedOutputs;
    /** @brief 完整宿主计划已固定键权限；未封存的低层 Scope 保持原动态入场。 */
    bool planSealed = false;
    struct BindingRead {
        std::shared_ptr<BindingControl> binding;
        bool candidate = false, recovery = false;
    };
    std::map<ServiceKey, BindingRead> readBindings;
    bool recoveringInputs = false;
    Promise<void> candidateInputsLost, recoveryInputsLost;
    bool inputsAvailable(bool recovery) const noexcept;
    Result<void> checkInputs(bool recovery) const;
    Promise<CleanupReport> discarded;
    CleanupReport stageReport{true, {}};
    std::size_t pendingStage = 0;
    std::map<RuntimeId, Runtime> candidateDefinitions;
    std::map<std::string, Runtime> preparedRuntimes;
    std::map<std::string, RuntimeId> preparedBase;
    std::vector<Instance> preparedMembers;
    std::vector<std::shared_ptr<BindingControl>> preparedBindings;
    ReplacementCommitMode preparedMode = ReplacementCommitMode::Candidate;
    std::shared_ptr<const PreparedReplacementStamp> preparedStamp;
    const std::thread::id ownerThread = std::this_thread::get_id();
    Result<void> check() const;
    bool deliveryBlocked() const;
    Result<ReplacementScopeSnapshot> snapshot() const;
    Result<void> prepareDefinitions(std::vector<RuntimeReplacement> definitions);
    bool ownsDefinition(RuntimeId runtime) const;
    Result<void> borrow(const std::vector<Runtime>&);
    bool readsDefinition(RuntimeId) const noexcept;
    Result<void> sealPlan(const std::vector<ServiceKey>&, const CoreReplacementAccess::InputBindings&,
                          const CoreReplacementAccess::InputBindings&);
    Result<void> checkOutputs(const std::set<ServiceKey>&, const std::shared_ptr<PublicationControl>&) const;
    Result<void> prepareCommit(ReplacementCommitMode mode);
    Result<void> commitPrepared();
    Result<PreparedReplacementView> inspectPrepared() const;
    Result<void> freeze();
    Task<CleanupReport> stop();
    void advanceStop();
    Result<void> beginStage();
    Result<ReplacementBuilder> builder();
    Result<Instance> stage(const Runtime& runtime, std::string config, RealmMap realms,
                           std::vector<ServiceRequirement> additionalDependencies,
                           const std::shared_ptr<PublicationControl>& permission = {});
    Result<void> claimStage(const std::shared_ptr<PublicationControl>& permission, const Runtime& runtime,
                            const RealmMap& realms, const std::vector<ServiceRequirement>& additionalDependencies,
                            bool root = false);
    bool ownsStageView(const std::shared_ptr<PublicationControl>& view) const
    {
        return staging && staging->control_ == view;
    }
    /** @brief 仅在已核对 owner 后使用的纯状态检查，不调用调度器或插件。 */
    bool stageOpen(const std::shared_ptr<PublicationControl>& view) const noexcept;
    void registerStage(const Instance& instance);
    Task<CleanupReport> discard();
    void finishStage(const Result<CleanupReport>& result);
    Result<void> release(const std::function<void()>& beforeNotifications = {});
    Result<CoreReplacementAccess::PlanningContext> planningContext(bool recovery) const;
    void removeClaims(bool resume);
    void shutdown(bool finished, const CleanupReport& outcome);
};
bool replacementViewAllowsStage(const std::shared_ptr<PublicationControl>& publication);
bool replacementOwnsDefinition(const std::shared_ptr<PublicationControl>& publication, RuntimeId runtime);
void registerReplacementStageInstance(const std::shared_ptr<PublicationControl>& publication, const Instance& instance);
bool replacementAllowsActivation(const std::weak_ptr<ReplacementScopeControl>& claim,
                                 const std::shared_ptr<PublicationControl>& publication = {});
bool replacementAllowsBusiness(const std::weak_ptr<ReplacementScopeControl>& claim,
                               const std::shared_ptr<PublicationControl>& publication = {});
Result<void> replacementMutationAccess(const std::weak_ptr<ReplacementScopeControl>& claim);
Result<void> replacementOutputAccess(const std::shared_ptr<ServiceRegistryControl>& registry, const ServiceKey& key,
                                     const std::shared_ptr<PublicationControl>& publication = {});
Result<void> replacementProviderAccess(const std::shared_ptr<ServiceRegistryControl>&, InstanceId);
void invalidateReplacementInputs(const std::shared_ptr<BindingControl>&);
void shutdownReplacementScopes(const std::shared_ptr<ServiceRegistryControl>& registry, bool finished,
                               const CleanupReport& outcome = {false, {}});
} // namespace cordis::detail
