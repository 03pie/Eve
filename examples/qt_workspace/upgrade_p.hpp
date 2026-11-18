/** @file upgrade_p.hpp
 * @brief 一个实际执行拥有者；原事务仍独占冻结、状态迁移及共同提交。
 */
#pragma once
#include "source_p.hpp"
#include <cordis/module/install.hpp>

namespace notebook::detail {
struct WorkspaceUpgradeExecution : std::enable_shared_from_this<WorkspaceUpgradeExecution> {
    std::shared_ptr<Workspace> owner;
    WorkspaceUpgradeReport report;
    std::shared_ptr<native_services::CurrentModule> previousNative, candidateNative;
    std::shared_ptr<NotebookModule> previousNotebook, candidateNotebook;
    std::optional<cordis::module::InstalledArtifact> installed;
    std::optional<cordis::ReplacementOperation> operation;
    cordis::RuntimeId previousRuntime, candidateRuntime;
    std::string previousLocator, candidateLocator;
    cordis::Promise<WorkspaceUpgradeReport> completed;
    std::shared_ptr<std::atomic_bool> blocked = std::make_shared<std::atomic_bool>(false);
    bool cancelRequested = false, retiring = false;

    WorkspaceUpgradeExecution(std::shared_ptr<Workspace> workspace, const UpgradePreview& preview);
    void start(const UpgradePreview&);
    void preparationFailed(cordis::ErrorInfo);
    cordis::Result<void> project();
    cordis::Result<void> cancel();
    void settled(const cordis::Result<cordis::ReplacementReport>&);
    void retain(cordis::ErrorInfo);
    void retire();
    void barrier();
    void reclaim();
    void finish();
    template <class T, class F> void watch(const cordis::Task<T>& task, F callback)
    {
        const auto delivery = blocked;
        const auto done = completed;
        const auto receipt = task.thenOn(owner->scheduler, std::move(callback));
        // 丢弃可发生在线程外；只改原子诊断/Promise，原执行和两代仍原地保留。
        cordis::detail::TaskAccess::observe(receipt, [delivery, done](cordis::Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                delivery->store(true);
                done.reject(result->error());
            }
        });
    }
};
bool sameArtifact(const cordis::module::PreflightArtifact&, const cordis::module::PreflightArtifact&);
bool sameManifest(const cordis::module::ModuleManifest&, const cordis::module::ModuleManifest&);
bool supportsBusinessUpgrade(const std::string& alias, const cordis::Runtime* runtime);
} // namespace notebook::detail
