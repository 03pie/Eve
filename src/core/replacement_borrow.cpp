/** @file replacement_borrow.cpp
 * @brief Scope 持有未换代规范工厂的共享读意图，不另建运行索引。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <algorithm>
namespace cordis::detail {
bool ReplacementScopeControl::readsDefinition(RuntimeId runtime) const noexcept
{
    return borrowedRuntimes.count(runtime) ||
           std::any_of(selection.closure.members.begin(), selection.closure.members.end(),
                       [&](const auto& member) { return member.runtime == runtime; });
}
Result<void> ReplacementScopeControl::borrow(const std::vector<Runtime>& factories)
{
    auto admitted = check();
    if (!admitted.hasValue())
        return admitted;
    const auto owner = kernel;
    const auto registry = owner->services_->control_;
    std::set<RuntimeId> additions;
    for (const auto& runtime : factories)
        if (!ownsDefinition(runtime.id()) &&
            std::find(selection.closure.runtimes.begin(), selection.closure.runtimes.end(), runtime.id()) ==
                selection.closure.runtimes.end())
            additions.insert(runtime.id());
    const auto validate = [&]() -> Result<void> {
        if (kernel != owner || state != ReplacementScopeState::Reserved || cancelled || blocked.load() ||
            owner->closing_)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "borrow requires open reserved scope"));
        for (const auto runtime : additions) {
            if (std::none_of(owner->runtimes_.begin(), owner->runtimes_.end(),
                             [&](const auto& entry) { return entry.second.id() == runtime; }))
                return Result<void>::failure(
                    makeError(ErrorCode::ConfigConflict, "borrowed factory is not canonical here"));
            const auto holder = registry->replacementRuntimes.find(runtime);
            if (holder != registry->replacementRuntimes.end() && holder->second != operation) {
                auto error = makeError(ErrorCode::ConfigConflict, "borrowed factory is being replaced");
                error.operation = holder->second;
                return Result<void>::failure(std::move(error));
            }
        }
        return Result<void>::success();
    };
    auto current = validate();
    if (!current.hasValue())
        return current;
    for (auto it = additions.begin(); it != additions.end();)
        if (readsDefinition(*it))
            it = additions.erase(it);
        else
            ++it;
    borrowedRuntimes.merge(additions);
    return Result<void>::success();
}
} // namespace cordis::detail
