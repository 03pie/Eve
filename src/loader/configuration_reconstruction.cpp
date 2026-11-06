/** @file configuration_reconstruction.cpp
 * @brief 组合实际驱动的拥有关系；准备输入不改变运行/目标账目。
 */
#include "configuration_commit_p.hpp"
#include <map>
#include <set>

namespace cordis::loader::detail {
Result<ConfigurationReconstructionPlan>
ConfigurationReconstructionAccess::inspect(const ReplacementScope& scope,
                                           std::vector<DriverReconstructionRequest> participants)
{
    using Output = Result<ConfigurationReconstructionPlan>;
    if (participants.empty())
        return Output::failure(
            makeError(ErrorCode::InvalidArgument, "configuration reconstruction needs participants"));
    ConfigurationReconstructionPlan plan{std::move(participants), {}};
    std::set<const KernelReconcileDriver*> seen;
    for (const auto& participant : plan.participants) {
        if (!participant.driver || !seen.insert(participant.driver.get()).second)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "duplicate or missing reconstruction driver"));
        auto inspected = DriverCommitAccess::inspectReconstruction(*participant.driver, scope, participant.definitions,
                                                                   participant.target);
        if (!inspected.hasValue())
            return Output::failure(inspected.error());
        plan.drivers.push_back(std::move(inspected).value());
    }
    std::map<InstanceId, std::size_t> coverage;
    for (const auto& driver : plan.drivers)
        for (const auto& owner : driver.ownership)
            if (owner.kind != ReconstructionKind::ExternalOwner) {
                auto& count = coverage[owner.instance];
                if (owner.kind == ReconstructionKind::ConfiguredEntry)
                    ++count;
            }
    for (const auto& member : plan.drivers.front().original.members) {
        const auto found = coverage.find(member.input.instance);
        if (found == coverage.end() || found->second > 1)
            return Output::failure(makeError(ErrorCode::ConfigConflict, "closure reconstruction owner is unresolved"));
    }
    if (!current(plan))
        return Output::failure(makeError(ErrorCode::ConfigConflict, "configuration reconstruction invalidated"));
    return Output::success(std::move(plan));
}
bool ConfigurationReconstructionAccess::current(const ConfigurationReconstructionPlan& plan) noexcept
{
    if (plan.drivers.empty() || plan.drivers.size() != plan.participants.size())
        return false;
    for (std::size_t index = 0; index != plan.drivers.size(); ++index)
        if (!plan.participants[index].driver ||
            !DriverCommitAccess::current(*plan.participants[index].driver, plan.drivers[index]))
            return false;
    return true;
}
} // namespace cordis::loader::detail
