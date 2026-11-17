/** @file replacement_state_p.hpp
 * @brief 未来成员的纯值创建准入；实际实例仍只在原 staged 账目中。
 */
#pragma once
#include "state_budget_p.hpp"
#include <map>
#include <optional>

namespace cordis::detail {
struct PublicationControl;
struct ReplacementScopeControl;
struct StateConstruction {
    std::size_t key;
    std::optional<std::size_t> parent;
    std::shared_ptr<const OwnedStateSnapshot> input;
};
struct StateAdmission {
    StateConstruction member;
    std::weak_ptr<ReplacementScopeControl> owner;
    std::weak_ptr<PublicationControl> publication;
    bool claimed = false;
};
struct ReplacementStatePlan {
    bool required;
    std::shared_ptr<StateBudget> budget;
    std::map<std::size_t, std::shared_ptr<StateAdmission>> members;
};
Result<void> claimReplacementState(const std::shared_ptr<PublicationControl>&, const std::shared_ptr<StateAdmission>&);
} // namespace cordis::detail
