/** @file definitions_p.hpp
 * @brief 目录只保存一份完整定义，模块映射为其不可变派生索引。
 */
#pragma once
#include "definition_validation_p.hpp"
#include <eve/loader/definitions.hpp>
#include <map>

namespace eve::loader::detail {
struct DefinitionIdentity {
    std::map<std::string, PluginModule> unavailable;
};
struct DefinitionsData {
    std::map<std::string, std::shared_ptr<const ConfiguredPlugin>> entries;
    std::map<std::string, PluginModule> modules;
    std::shared_ptr<const DefinitionIdentity> identity;
};
} // namespace eve::loader::detail
