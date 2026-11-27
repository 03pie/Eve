/** @file replacement_state.cpp
 * @brief 本阶段成员创建与导入证明，复用原 Scope/发布组及实际 InstanceControl。
 */
#include "instance_state_p.hpp"
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include "state_value_p.hpp"

namespace eve::detail {
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "kernel.replacement.state";
    return value;
}
} // namespace
Result<void> ReplacementScopeControl::configureStatePlan(std::vector<StateConstruction> members,
                                                         std::shared_ptr<StateBudget> budget, bool required)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if (!staging || !stageOpen(staging->control_) || !staging->control_->members.empty() || statePlan || !budget)
        return Result<void>::failure(error(ErrorCode::ConfigConflict, "state plan needs a fresh construction group"));
    ReplacementStatePlan pending{required, std::move(budget), {}};
    for (auto& member : members) {
        if (member.input) {
            if (member.input->bytes.budget() != pending.budget || !checkStateSnapshot(member.input->value).hasValue())
                return Result<void>::failure(
                    error(ErrorCode::InvalidArgument, "member input needs the operation budget"));
            pending.required = true;
        }
        const auto key = member.key;
        auto permit = std::make_shared<StateAdmission>(
            StateAdmission{std::move(member), shared_from_this(), staging->control_, false});
        if (!pending.members.emplace(key, std::move(permit)).second)
            return Result<void>::failure(error(ErrorCode::InvalidArgument, "duplicate construction state key"));
    }
    for (const auto& entry : pending.members) {
        auto parent = entry.second->member.parent;
        std::size_t depth = 0;
        while (parent) {
            const auto at = pending.members.find(*parent);
            if (at == pending.members.end() || ++depth > pending.members.size())
                return Result<void>::failure(
                    error(ErrorCode::InvalidArgument, "state plan has a missing parent or cycle"));
            parent = at->second->member.parent;
        }
    }
    statePlan.emplace(std::move(pending));
    return Result<void>::success();
}
Result<std::shared_ptr<StateAdmission>>
ReplacementScopeControl::stateAdmission(std::size_t key, const std::shared_ptr<PublicationControl>& permission,
                                        bool child)
{
    using Output = Result<std::shared_ptr<StateAdmission>>;
    auto allowed = check();
    if (!allowed.hasValue())
        return Output::failure(allowed.error());
    if (!stageOpen(permission))
        return Output::failure(error(ErrorCode::ContextClosing, "state construction permission expired"));
    if (!statePlan)
        return Output::failure(error(ErrorCode::ConfigConflict, "state construction plan is not installed"));
    const auto at = statePlan->members.find(key);
    if (at == statePlan->members.end() || at->second->claimed || at->second->member.parent.has_value() != child)
        return Output::failure(error(ErrorCode::ConfigConflict, "unknown, used or misplaced state member"));
    return Output::success(at->second);
}
Result<void> claimReplacementState(const std::shared_ptr<PublicationControl>& publication,
                                   const std::shared_ptr<StateAdmission>& admission)
{
    const auto owner = publication ? publication->replacement.lock() : nullptr;
    if (!owner)
        return admission ? Result<void>::failure(error(ErrorCode::ContextClosing, "foreign state permission"))
                         : Result<void>::success();
    if (std::this_thread::get_id() != owner->ownerThread)
        return Result<void>::failure(error(ErrorCode::WrongThread, "state construction requires owner"));
    if (owner->state == ReplacementScopeState::Released)
        return admission ? Result<void>::failure(error(ErrorCode::ContextClosing, "state construction has ended"))
                         : Result<void>::success();
    auto allowed = owner->check();
    if (!allowed.hasValue())
        return allowed;
    if (!admission) {
        if (owner->statePlan && owner->statePlan->required)
            return Result<void>::failure(
                error(ErrorCode::ConfigConflict, "stateful construction requires a planned key"));
        return Result<void>::success();
    }
    if (!owner->stageOpen(publication) || !owner->statePlan || admission->owner.lock() != owner ||
        admission->publication.lock() != publication || admission->claimed)
        return Result<void>::failure(error(ErrorCode::ContextClosing, "state permission is not current"));
    const auto at = owner->statePlan->members.find(admission->member.key);
    if (at == owner->statePlan->members.end() || at->second != admission)
        return Result<void>::failure(error(ErrorCode::ConfigConflict, "state permission is not this planned member"));
    admission->claimed = true;
    return Result<void>::success();
}
Result<void> ReplacementScopeControl::validateStatePlan() const
{
    if (!statePlan || !statePlan->required)
        return Result<void>::success();
    if (!staging)
        return Result<void>::failure(error(ErrorCode::ConfigConflict, "state construction group is missing"));
    std::set<std::size_t> actual;
    for (const auto& entry : staging->control_->members) {
        const auto instance = staged.find(entry.first);
        if (instance == staged.end())
            return Result<void>::failure(error(ErrorCode::ConfigConflict, "state member is not owned by this scope"));
        auto receipt = InstanceStateAccess::receipt(instance->second);
        if (!receipt.hasValue())
            return Result<void>::failure(receipt.error());
        const auto key = receipt.value().key;
        const auto expected = statePlan->members.find(key);
        if (expected == statePlan->members.end() || receipt.value().operation != operation ||
            !actual.insert(key).second)
            return Result<void>::failure(error(ErrorCode::ConfigConflict, "actual state members differ from plan"));
    }
    if (actual.size() != statePlan->members.size())
        return Result<void>::failure(
            error(ErrorCode::ProvisionIncomplete, "state construction omitted planned members"));
    return Result<void>::success();
}
Result<void> CoreReplacementAccess::statePlan(const ReplacementScope& scope, const Kernel& kernel,
                                              std::vector<StateConstruction> members,
                                              std::shared_ptr<StateBudget> budget, bool required)
{
    if (!scope.control_ || scope.control_->kernel != kernel.control_)
        return Result<void>::failure(error(ErrorCode::ConfigConflict, "state plan requires actual scope Kernel"));
    return scope.control_->configureStatePlan(std::move(members), std::move(budget), required);
}
} // namespace eve::detail
namespace eve {
Result<Instance> ReplacementBuilder::stageMember(std::size_t key, const Runtime& runtime, std::string config,
                                                 RealmMap realms, std::vector<ServiceRequirement> dependencies) const
{
    const auto owner = owner_.lock();
    const auto permission = publication_.lock();
    if (!owner || !permission)
        return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "construction permission expired"));
    auto admission = owner->stateAdmission(key, permission, false);
    if (!admission.hasValue())
        return Result<Instance>::failure(admission.error());
    return owner->stage(runtime, std::move(config), std::move(realms), std::move(dependencies), permission, {},
                        std::move(admission.value()));
}
Result<Instance> ReplacementBuilder::injectMember(std::size_t key, const Context& parent, const Runtime& runtime,
                                                  std::string config,
                                                  std::vector<ServiceRequirement> dependencies) const
{
    const auto owner = owner_.lock();
    const auto permission = publication_.lock();
    if (!owner || !permission)
        return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "construction permission expired"));
    auto admission = owner->stateAdmission(key, permission, true);
    if (!admission.hasValue())
        return Result<Instance>::failure(admission.error());
    return parent.injectWithState(runtime, std::move(config), std::move(dependencies), {},
                                  std::move(admission.value()));
}
} // namespace eve
