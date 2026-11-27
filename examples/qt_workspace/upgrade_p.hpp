/** @file upgrade_p.hpp
 * @brief 一个实际执行拥有者；原事务仍独占冻结、状态迁移及共同提交。
 */
#pragma once
#include "source_p.hpp"
#include <eve/module/install.hpp>

namespace notebook::detail {
struct WorkspaceUpgradeExecution : std::enable_shared_from_this<WorkspaceUpgradeExecution> {
    std::shared_ptr<Workspace> owner;
    WorkspaceUpgradeReport report;
    std::shared_ptr<native_services::CurrentModule> previousNative, candidateNative;
    std::shared_ptr<NotebookModule> previousNotebook, candidateNotebook;
    std::optional<eve::module::InstalledArtifact> installed;
    std::optional<eve::ReplacementOperation> operation;
    eve::RuntimeId previousRuntime, candidateRuntime;
    std::string previousLocator, candidateLocator;
    eve::Promise<WorkspaceUpgradeReport> completed;
    std::shared_ptr<std::atomic_bool> blocked = std::make_shared<std::atomic_bool>(false);
    bool cancelRequested = false, retiring = false;

    WorkspaceUpgradeExecution(std::shared_ptr<Workspace> workspace, const UpgradePreview& preview);
    void start(const UpgradePreview&);
    void preparationFailed(eve::ErrorInfo);
    eve::Result<void> project();
    eve::Result<void> cancel();
    void settled(const eve::Result<eve::ReplacementReport>&);
    void retain(eve::ErrorInfo);
    void retire();
    void barrier();
    void reclaim();
    void finish();
    template <class T, class F> void watch(const eve::Task<T>& task, F callback)
    {
        const auto delivery = blocked;
        const auto done = completed;
        const auto receipt = task.thenOn(owner->scheduler, std::move(callback));
        // 丢弃可发生在线程外；只改原子诊断/Promise，原执行和两代仍原地保留。
        eve::detail::TaskAccess::observe(receipt, [delivery, done](eve::Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                delivery->store(true);
                done.reject(result->error());
            }
        });
    }
};
bool sameArtifact(const eve::module::PreflightArtifact&, const eve::module::PreflightArtifact&);
bool sameManifest(const eve::module::ModuleManifest&, const eve::module::ModuleManifest&);
bool supportsBusinessUpgrade(const std::string& alias, const eve::Runtime* runtime);
} // namespace notebook::detail
