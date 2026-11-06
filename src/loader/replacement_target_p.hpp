/** @file replacement_target_p.hpp
 * @brief 从最新目标合并本次替换的受影响属性，不覆盖并行接受的无关目标。
 */
#pragma once
#include <cordis/loader/yaml.hpp>
#include <set>
namespace cordis::loader::detail {
Result<ParsedConfiguration> mergeReplacementTarget(const ParsedConfiguration& original,
                                                   const ParsedConfiguration& proposed,
                                                   const ParsedConfiguration& latest,
                                                   const std::set<std::string>& paths, YamlLimits limits = {});
}
