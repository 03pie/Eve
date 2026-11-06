/** @file service_plan.cpp
 * @brief 精确键提供者匹配与父级/服务依赖的确定性拓扑排序。
 */
#include "service_plan_p.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace cordis::detail {
Result<std::vector<std::string>> planServices(const std::vector<ServicePlanNode>& nodes,
                                              const std::vector<ServiceOutput>& external, MissingServicePolicy missing,
                                              bool validateProviders)
{
    using Output = Result<std::vector<std::string>>;
    const auto fail = [](ErrorCode code, const char* message, const std::string& key = {}) {
        auto error = makeError(code, message);
        error.phase = "service.plan";
        if (!key.empty())
            error.entryPath = key;
        return Output::failure(std::move(error));
    };
    struct Provider {
        std::optional<std::string> node;
        ServiceOutput output;
    };
    std::map<ServiceKey, Provider> providers;
    std::map<std::string, std::set<std::string>> edges;
    std::map<std::string, std::size_t> degrees;
    for (const auto& output : external)
        if (!providers.emplace(output.key, Provider{{}, output}).second)
            return fail(ErrorCode::ConfigInvalid, "duplicate external service provider");
    for (const auto& node : nodes) {
        if (!degrees.emplace(node.key, 0).second)
            return fail(ErrorCode::ConfigInvalid, "duplicate service plan node", node.key);
        for (const auto& output : node.outputs)
            if (!providers.emplace(output.key, Provider{node.key, output}).second)
                return fail(ErrorCode::ConfigInvalid, "multiple providers for one service key", node.key);
    }
    const auto edge = [&](const std::string& provider, const std::string& consumer) {
        if (edges[provider].insert(consumer).second)
            ++degrees.at(consumer);
    };
    for (const auto& node : nodes) {
        if (node.parent) {
            if (degrees.count(*node.parent))
                edge(*node.parent, node.key);
            else if (missing == MissingServicePolicy::Reject)
                return fail(ErrorCode::ConfigInvalid, "plan parent is missing", node.key);
        }
        for (const auto& requirement : node.requirements) {
            const auto found = providers.find(requirement.key);
            if (found == providers.end()) {
                if (missing == MissingServicePolicy::Reject)
                    return fail(ErrorCode::DependencyMissing, "planned dependency has no provider", node.key);
                continue;
            }
            const auto& output = found->second.output;
            if (validateProviders &&
                (output.type != requirement.type || output.metadata.minor < requirement.minimumMinor ||
                 std::any_of(
                     requirement.capabilities.begin(), requirement.capabilities.end(),
                     [&](const auto& name) {
                         return std::find(output.metadata.capabilities.begin(), output.metadata.capabilities.end(),
                                          name) == output.metadata.capabilities.end();
                     })))
                return fail(ErrorCode::ConfigInvalid, "provider does not satisfy planned dependency", node.key);
            if (found->second.node)
                edge(*found->second.node, node.key);
        }
    }
    std::set<std::string> ready;
    for (const auto& degree : degrees)
        if (degree.second == 0)
            ready.insert(degree.first);
    std::vector<std::string> order;
    while (!ready.empty()) {
        auto key = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(key);
        for (const auto& consumer : edges[key])
            if (--degrees.at(consumer) == 0)
                ready.insert(consumer);
    }
    if (order.size() != nodes.size())
        return fail(ErrorCode::DependencyCycle, "service plan dependency cycle");
    return Output::success(std::move(order));
}
} // namespace cordis::detail
