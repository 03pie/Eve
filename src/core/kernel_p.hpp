/** @file kernel_p.hpp
 * @brief Kernel 的唯一执行账目；门面和异步操作共享寿命，不借用门面地址。
 */
#pragma once
#include <atomic>
#include <cordis/kernel.hpp>
#include <map>

namespace cordis::detail {
struct ReplacementSelection {
    ReplacementClosure closure;
    std::vector<ServiceKey> outputs;
    std::map<InstanceId, Instance> instances;
};
struct KernelControl : std::enable_shared_from_this<KernelControl> {
    explicit KernelControl(std::shared_ptr<IScheduler> scheduler);
    Result<Runtime> registerPlugin(std::string name, Runtime::Activate activate,
                                   std::vector<ServiceRequirement> dependencies, std::vector<ServiceOutput> outputs);
    Result<Runtime> registerRuntime(std::string name, const Runtime& runtime);
    Result<Instance> mount(const Runtime& runtime, std::string config, RealmMap realms,
                           std::vector<ServiceRequirement> additionalDependencies);
    Result<PublicationGroup> holdPublication() const;
    Result<Instance> mountHeld(const PublicationGroup& group, const Runtime& runtime, std::string config,
                               RealmMap realms, std::vector<ServiceRequirement> additionalDependencies);
    Result<Instance> mountWithRegistry(const Runtime& runtime, std::string config, RealmMap realms,
                                       std::vector<ServiceRequirement> additionalDependencies,
                                       std::shared_ptr<ServiceRegistry> services);
    Result<Instance> remount(const Instance& previous);
    Task<CleanupReport> close();
    Result<KernelSnapshot> snapshot();
    Result<Instance> findInstance(InstanceId id) const;
    Result<ReplacementClosure> replacementClosure(const std::vector<RuntimeId>& requested) const;
    Result<ReplacementSelection> selectReplacement(const std::vector<RuntimeId>& requested) const;
    Result<ReplacementScope> reserveReplacement(const std::vector<RuntimeId>& requested);
    Result<std::vector<ReplacementScopeSnapshot>> replacements() const;
    Task<void> whenReplacementReleased(OperationId operation) const;
    Result<void> checkMount(const Runtime& runtime, const RealmMap& realms,
                            const std::vector<ServiceRequirement>& additionalDependencies) const;
    std::shared_ptr<IScheduler> scheduler_;
    std::shared_ptr<ServiceRegistry> services_;
    std::map<std::string, Runtime> runtimes_;
    std::map<InstanceId, Instance> instances_;
    bool closing_ = false;
    Promise<CleanupReport> closed_;
    std::shared_ptr<std::atomic_bool> blocked_ = std::make_shared<std::atomic_bool>(false);
};
} // namespace cordis::detail
