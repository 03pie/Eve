/** @file configuration_limits_p.hpp
 * @brief 配置结构校验与 YAML 入口共用的预算约束。
 */
#pragma once
#include <eve/loader/configuration.hpp>

namespace eve::loader::detail {
/** @brief 结构条目与 YAML 节点各自计深度，两种预算的可配置范围均为 1 到 128。 */
constexpr bool validDepthLimit(std::size_t depth) noexcept
{
    return depth > 0 && depth <= 128;
}

/** @brief 条目、深度和字节预算必须有效，模块、注入和能力个数允许设为零。 */
constexpr bool validStructureLimits(const ConfigurationLimits& limits) noexcept
{
    return limits.maximumEntries > 0 && validDepthLimit(limits.maximumDepth) && limits.maximumBytes > 0;
}
} // namespace eve::loader::detail
