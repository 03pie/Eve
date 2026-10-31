/** @file definitions_p.hpp
 * @brief 目录只保存一份完整定义，模块映射为其不可变派生索引。
 */
#pragma once
#include "definition_validation_p.hpp"
#include <cordis/loader/definitions.hpp>
#include <map>

namespace cordis::loader::detail {
struct DefinitionIdentity {};
struct DefinitionsData {
    std::map<std::string, ConfiguredPlugin> entries;
    std::map<std::string, PluginModule> modules;
    std::shared_ptr<const DefinitionIdentity> identity;
};
} // namespace cordis::loader::detail
