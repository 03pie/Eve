/** @file replacement_scope_p.hpp
 * @brief 一个真实闭包的占用、冻结及停止账目，不保存模块策略或用户回调。
 */
#pragma once
#include "kernel_p.hpp"
#include <atomic>
#include <optional>
#include <set>
#include <thread>

namespace cordis::detail {
OperationId nextOperationId();
struct ReplacementScopeControl : std::enable_shared_from_this<ReplacementScopeControl> {
    OperationId operation;
    std::shared_ptr<KernelControl> kernel;
    std::shared_ptr<IScheduler> scheduler;
    ReplacementSelection selection;
    ReplacementScopeState state = ReplacementScopeState::Reserved;
    std::atomic_bool abandoned{false}, blocked{false};
    bool cancelled = false;
    bool releasing = false;
    std::size_t next = 0;
    CleanupReport report{true, {}};
    Promise<CleanupReport> stopped;
    Promise<void> released;
    std::optional<PublicationGroup> staging;
    std::map<InstanceId, Instance> staged;
    std::set<RuntimeId> stagedRuntimes;
    std::set<ServiceKey> stagedOutputs;
    Promise<CleanupReport> discarded;
    CleanupReport stageReport{true, {}};
    std::size_t pendingStage = 0;
    std::map<RuntimeId, Runtime> candidateDefinitions;
    std::map<std::string, Runtime> preparedRuntimes;
    std::map<std::string, RuntimeId> preparedBase;
    std::vector<Instance> preparedMembers;
    std::vector<std::shared_ptr<BindingControl>> preparedBindings;
    ReplacementCommitMode preparedMode = ReplacementCommitMode::Candidate;
    const std::thread::id ownerThread = std::this_thread::get_id();
    Result<void> check() const;
    bool deliveryBlocked() const;
    Result<ReplacementScopeSnapshot> snapshot() const;
    Result<void> prepareDefinitions(std::vector<RuntimeReplacement> definitions);
    bool ownsDefinition(RuntimeId runtime) const;
    Result<void> prepareCommit(ReplacementCommitMode mode);
    Result<void> commitPrepared();
    Result<void> freeze();
    Task<CleanupReport> stop();
    void advanceStop();
    Result<Instance> stage(const Runtime& runtime, std::string config, RealmMap realms,
                           std::vector<ServiceRequirement> additionalDependencies);
    Result<void> claimStage(const Runtime& runtime, const RealmMap& realms,
                            const std::vector<ServiceRequirement>& additionalDependencies, bool root = false);
    bool ownsStageView(const std::shared_ptr<PublicationControl>& view) const
    {
        return staging && staging->control_ == view;
    }
    /** @brief 仅在已核对 owner 后使用的纯状态检查，不调用调度器或插件。 */
    bool stageOpen(const std::shared_ptr<PublicationControl>& view) const noexcept;
    void registerStage(const Instance& instance);
    Task<CleanupReport> discard();
    void finishStage(const Result<CleanupReport>& result);
    Result<void> release();
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
void shutdownReplacementScopes(const std::shared_ptr<ServiceRegistryControl>& registry, bool finished,
                               const CleanupReport& outcome = {false, {}});
} // namespace cordis::detail
