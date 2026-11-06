/** @file graph.cpp
 * @brief 全部拥有单元与唯一 Core 外部服务共同求解，不维护另一份运行图。
 */
#include "../core/service_plan_p.hpp"
#include "operation_p.hpp"
namespace cordis::detail {
Result<std::vector<std::size_t>>
ReplacementOperationControl::planGraph(bool restoring, CoreReplacementAccess::InputBindings* inputs) const
{
    using Output = Result<std::vector<std::size_t>>;
    const auto& work = *execution;
    auto context = CoreReplacementAccess::planningContext(*work.scope, *work.request.kernel, restoring);
    if (!context.hasValue())
        return Output::failure(context.error());
    std::vector<ServicePlanNode> nodes;
    std::map<std::string, std::size_t> roots, members;
    for (std::size_t unit = 0; unit != work.units.size(); ++unit)
        for (const auto& input : (restoring ? work.units[unit].recovery : work.units[unit].candidate).members) {
            if (!input.contract)
                return Output::failure(replacementError(ErrorCode::ConfigConflict, "future contract is missing"));
            bool realms = true;
            for (const auto& realm : input.realms.overrides())
                realms = realms && context.value().realms.count(realm.second);
            for (const auto& requirement : input.contract->requirements)
                realms = realms && context.value().realms.count(requirement.key.realm);
            for (const auto& output : input.contract->outputs)
                realms = realms && context.value().realms.count(output.key.realm);
            if (!realms)
                return Output::failure(replacementError(ErrorCode::ConfigInvalid, "future plan uses an unknown realm"));
            auto key = std::to_string(input.key);
            ServicePlanNode node{key, {}, input.contract->requirements, input.contract->outputs};
            if (input.parent)
                node.parent = std::to_string(*input.parent);
            else
                roots.emplace(key, unit);
            members.emplace(key, input.key);
            nodes.push_back(std::move(node));
        }
    auto ordered = planServices(nodes, context.value().providers, MissingServicePolicy::Reject);
    if (!ordered.hasValue()) {
        auto error = ordered.error();
        error.operation = report.operation;
        if (error.entryPath) {
            const auto found = members.find(*error.entryPath);
            error.entryPath.reset();
            if (found != members.end() && found->second < work.configured.size() && work.configured[found->second])
                error.entryPath = work.configured[found->second]->path;
        }
        return Output::failure(std::move(error));
    }
    if (inputs)
        for (const auto& node : nodes)
            for (const auto& requirement : node.requirements) {
                const auto binding = context.value().bindings.find(requirement.key);
                if (binding != context.value().bindings.end())
                    inputs->emplace(*binding);
            }
    std::vector<std::size_t> units;
    for (const auto& key : ordered.value())
        if (roots.count(key))
            units.push_back(roots.at(key));
    return Output::success(std::move(units));
}
} // namespace cordis::detail
