/** @file service_contract_p.hpp
 * @brief Core 入场与宿主纯计划共用实际描述校验和精确域依赖合并。
 */
#pragma once
#include <cordis/instance.hpp>
namespace cordis::detail {
bool validServiceDescriptor(const ServiceKey&, std::type_index, const std::vector<std::string>&);
std::vector<ServiceRequirement> mergeDependencies(const std::vector<ServiceRequirement>&,
                                                  const std::vector<ServiceRequirement>&, const RealmMap&);
} // namespace cordis::detail
