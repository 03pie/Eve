/** @file definition_validation_p.hpp
 * @brief 复用定义规范化，不依赖 Runtime、内核或任何装载后端。
 */
#pragma once
#include <cordis/loader/yaml.hpp>
namespace cordis::loader::detail {
Result<std::map<std::string, PluginDefinition>> normalizeDefinitions(std::vector<PluginDefinition> definitions);
}
