/** @file state_p.hpp
 * @brief 本操作的原始纯值存储；不保存另一套实例对象或候选转换副本。
 */
#pragma once
#include "../core/state_budget_p.hpp"
#include <eve/identity.hpp>
#include <map>

namespace eve::detail {
struct MigrationState {
    std::shared_ptr<StateBudget> budget = StateBudget::create();
    std::map<InstanceId, StateSchema> declared;
    std::map<InstanceId, std::shared_ptr<const OwnedStateSnapshot>> original;
};
} // namespace eve::detail
