/** @file service_plan_p.hpp
 * @brief 配置与整体替换共用的纯服务求解；节点不保留运行权限。
 */
#pragma once
#include <cordis/instance.hpp>
#include <optional>
namespace cordis::detail {
struct ServicePlanNode {
    std::string key;
    std::optional<std::string> parent;
    std::vector<ServiceRequirement> requirements;
    std::vector<ServiceOutput> outputs;
};
enum class MissingServicePolicy { Pending, Reject };
Result<std::vector<std::string>> planServices(const std::vector<ServicePlanNode>&,
                                              const std::vector<ServiceOutput>& external, MissingServicePolicy,
                                              bool validateProviders = true);
} // namespace cordis::detail
