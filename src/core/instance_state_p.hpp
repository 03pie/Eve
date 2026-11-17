/** @file instance_state_p.hpp
 * @brief 状态读取由真实 Frozen Scope 授权；纯编号或 Context 不能绕过冻结。
 */
#pragma once
#include <cordis/replacement_scope.hpp>

namespace cordis::detail {
struct InstanceStateAccess {
    static Result<std::optional<StateSchema>> schema(const Instance&);
    static Result<std::optional<StateSnapshot>> capture(const ReplacementScope&, const Instance&);
};
} // namespace cordis::detail
