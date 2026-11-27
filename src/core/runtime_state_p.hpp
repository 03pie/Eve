/** @file runtime_state_p.hpp
 * @brief 宿主提前验证的只读契约入口；仍使用与实际 epoch 相同的纯值检查。
 */
#pragma once
#include <eve/instance.hpp>

namespace eve::detail {
struct RuntimeStateAccess {
    static Result<StateSnapshot> validate(const Runtime&, const StateSnapshot&);
};
} // namespace eve::detail
