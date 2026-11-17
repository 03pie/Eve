/** @file state_value_p.hpp
 * @brief 纯值结构/预算检查；不包含 epoch、参与者或模块策略。
 */
#pragma once
#include <cordis/migration_state.hpp>

namespace cordis::detail {
bool validStateSchema(const StateSchema&) noexcept;
Result<void> checkStateSnapshot(const StateSnapshot&);
} // namespace cordis::detail
