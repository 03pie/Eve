/** @file service_contract.cpp
 * @brief 实际实例入场与未来计划共用同一描述校验及依赖合并。
 */
#include "service_contract_p.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
namespace cordis::detail {
/** @brief 插件固定契约与宿主附加契约共用键、类型及能力名称校验。 */
bool validServiceDescriptor(const ServiceKey& key, std::type_index type, const std::vector<std::string>& capabilities)
{
    if (key.name.empty() || key.interfaceMajor == 0 || !key.realm.valid() || type == typeid(void))
        return false;
    std::set<std::string> names;
    for (const auto& name : capabilities)
        if (name.empty() || !names.insert(name).second)
            return false;
    return true;
}
/** @brief 按精确解析域联合依赖，类型必须一致，minor 取最大值、能力取并集。 */
std::vector<ServiceRequirement> mergeDependencies(const std::vector<ServiceRequirement>& required,
                                                  const std::vector<ServiceRequirement>& additional,
                                                  const RealmMap& realms)
{
    std::map<ServiceKey, ServiceRequirement> merged;
    for (const auto* list : {&required, &additional})
        for (const auto& dependency : *list) {
            if (!validServiceDescriptor(dependency.key, dependency.type, dependency.capabilities))
                throw std::invalid_argument("invalid additional service requirement");
            auto value = dependency;
            value.key = realms.resolve(value.key);
            auto found = merged.find(value.key);
            if (found == merged.end()) {
                const auto key = value.key;
                merged.emplace(key, std::move(value));
            } else {
                if (found->second.type != value.type)
                    throw std::invalid_argument("conflicting dependency types for resolved service key");
                found->second.minimumMinor = std::max(found->second.minimumMinor, value.minimumMinor);
                auto& capabilities = found->second.capabilities;
                capabilities.insert(capabilities.end(), value.capabilities.begin(), value.capabilities.end());
            }
        }
    std::vector<ServiceRequirement> result;
    for (auto& entry : merged) {
        auto& capabilities = entry.second.capabilities;
        std::sort(capabilities.begin(), capabilities.end());
        capabilities.erase(std::unique(capabilities.begin(), capabilities.end()), capabilities.end());
        result.push_back(std::move(entry.second));
    }
    return result;
}
} // namespace cordis::detail
