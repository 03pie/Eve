/** @file instance_state_p.hpp
 * @brief 状态读取由真实 Frozen Scope 授权；纯编号或 Context 不能绕过冻结。
 */
#pragma once
#include <cordis/replacement_scope.hpp>

namespace cordis::detail {
struct StateImportReceipt {
    OperationId operation;
    std::size_t key;
    Epoch epoch;
};
struct InstanceStateAccess {
    static Result<std::optional<StateSchema>> schema(const Instance&);
    static Result<std::optional<StateSnapshot>> capture(const ReplacementScope&, const Instance&);
    static Result<StateImportReceipt> receipt(const Instance&);
};
} // namespace cordis::detail
