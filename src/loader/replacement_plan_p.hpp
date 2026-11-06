/** @file replacement_plan_p.hpp
 * @brief 冻结前的真实重建输入和拥有者义务；快照本身不授予冻结或提交权限。
 */
#pragma once
#include "../core/replacement_inspect_p.hpp"
#include <cordis/loader/kernel_driver.hpp>
#include <thread>
namespace cordis::loader::detail {
struct ConfiguredReconstruction {
    std::string path;
    InstanceId previous;
    Runtime runtime;
    std::string config;
    RealmMap realms;
    std::vector<ServiceRequirement> additionalDependencies;
};
enum class ReconstructionKind { ConfiguredEntry, ManualRoot, OwnerManagedChild, ExternalOwner };
struct ReconstructionOwnership {
    InstanceId instance;
    ReconstructionKind kind;
    std::string path;
};
struct DriverDefinitionChange {
    std::shared_ptr<const ConfigurationDefinitions> baseline, candidate;
    std::vector<std::string> plugins;
};
struct DriverReconstructionPlan {
    std::thread::id thread = std::this_thread::get_id();
    cordis::detail::OriginalReplacementView original;
    DriverDefinitionChange change;
    std::shared_ptr<const ValidatedCandidate> previousTarget, target;
    std::vector<ConfiguredReconstruction> entries;
    std::vector<ReconstructionOwnership> ownership;
    // OwnerManagedChild 必须由真实拥有者重建；不能从捕获的临时子工厂单独重放。
};
struct DriverReconstructionRequest {
    std::shared_ptr<KernelReconcileDriver> driver;
    std::shared_ptr<const ConfigurationDefinitions> definitions;
    std::shared_ptr<const ValidatedCandidate> target;
};
struct ConfigurationReconstructionPlan {
    std::vector<DriverReconstructionRequest> participants;
    std::vector<DriverReconstructionPlan> drivers;
};
struct ConfigurationReconstructionAccess {
    static Result<ConfigurationReconstructionPlan> inspect(const ReplacementScope&,
                                                           std::vector<DriverReconstructionRequest>);
    static bool current(const ConfigurationReconstructionPlan&) noexcept;
};
} // namespace cordis::loader::detail
