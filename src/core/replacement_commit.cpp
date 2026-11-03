/** @file replacement_commit.cpp
 * @brief 候选定义只由作用域拥有，Core 提交切换规范名称与输出，通知在整体元数据提交后解除占用时执行。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <algorithm>
#include <type_traits>

namespace cordis::detail {
static_assert(std::is_nothrow_swappable_v<Runtime>);
namespace {
ErrorInfo commitError(ErrorCode code, const char* message, OperationId operation)
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.commit";
    error.operation = operation;
    return error;
}
} // namespace
bool ReplacementScopeControl::ownsDefinition(RuntimeId runtime) const
{
    return std::any_of(candidateDefinitions.begin(), candidateDefinitions.end(),
                       [&](const auto& entry) { return entry.second.id() == runtime; });
}
bool replacementOwnsDefinition(const std::shared_ptr<PublicationControl>& publication, RuntimeId runtime)
{
    return replacementViewAllowsStage(publication) && publication->replacement.lock()->ownsDefinition(runtime);
}
Result<void> ReplacementScopeControl::prepareDefinitions(std::vector<RuntimeReplacement> definitions)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if (state != ReplacementScopeState::Reserved || !candidateDefinitions.empty() ||
        definitions.size() != selection.closure.runtimes.size())
        return Result<void>::failure(
            commitError(ErrorCode::ConfigConflict, "definitions require a fresh complete reservation", operation));
    const auto registry = kernel->services_->control_;
    std::map<RuntimeId, Runtime> pending;
    std::set<RuntimeId> identities;
    auto claims = registry->replacementRuntimes;
    auto owned = stagedRuntimes;
    for (const auto& definition : definitions) {
        const auto id = definition.candidate.id();
        if (!id.valid() ||
            std::find(selection.closure.runtimes.begin(), selection.closure.runtimes.end(), definition.previous) ==
                selection.closure.runtimes.end() ||
            pending.count(definition.previous) || !identities.insert(id).second ||
            std::any_of(kernel->runtimes_.begin(), kernel->runtimes_.end(),
                        [&](const auto& entry) { return entry.second.id() == id; }))
            return Result<void>::failure(commitError(
                ErrorCode::InvalidArgument, "invalid, duplicate or already registered candidate identity", operation));
        if (const auto at = claims.find(id); at != claims.end() && at->second != operation)
            return Result<void>::failure(
                commitError(ErrorCode::ConfigConflict, "candidate identity belongs to another operation", operation));
        pending.emplace(definition.previous, definition.candidate);
        claims.emplace(id, operation);
        owned.insert(id);
    }
    candidateDefinitions.swap(pending);
    registry->replacementRuntimes.swap(claims);
    stagedRuntimes.swap(owned);
    return Result<void>::success();
}
Result<void> ReplacementScopeControl::prepareCommit(ReplacementCommitMode mode)
{
    auto allowed = check();
    if (!allowed.hasValue())
        return allowed;
    if ((state != ReplacementScopeState::Staging && state != ReplacementScopeState::Stopped) || deliveryBlocked())
        return Result<void>::failure(
            commitError(ErrorCode::ContextClosing, "replacement cannot prepare commit in this phase", operation));
    if (mode != ReplacementCommitMode::Candidate && mode != ReplacementCommitMode::Recovery)
        return Result<void>::failure(
            commitError(ErrorCode::InvalidArgument, "unknown replacement commit mode", operation));
    if (mode == ReplacementCommitMode::Candidate && candidateDefinitions.size() != selection.closure.runtimes.size())
        return Result<void>::failure(
            commitError(ErrorCode::ConfigConflict, "candidate definitions are not complete", operation));
    const auto owner = kernel;
    std::vector<Instance> members;
    std::vector<std::shared_ptr<BindingControl>> bindings;
    const bool openGroup = staging && staging->control_->state == PublicationState::Open;
    if (!openGroup && !selection.closure.members.empty())
        return Result<void>::failure(commitError(ErrorCode::ProvisionIncomplete,
                                                 "stopped closure has no recovered candidate members", operation));
    if (openGroup) {
        auto prepared = preparePublication(staging->control_);
        if (!prepared.hasValue())
            return Result<void>::failure(prepared.error());
        for (const auto& entry : staging->control_->members) {
            const auto at = staged.find(entry.first);
            if (at == staged.end())
                return Result<void>::failure(commitError(
                    ErrorCode::ConfigConflict, "publication member is not owned by this operation", operation));
            const auto actual = at->second.snapshot().value().runtime;
            if ((mode == ReplacementCommitMode::Candidate && candidateDefinitions.count(actual)) ||
                (mode == ReplacementCommitMode::Recovery && ownsDefinition(actual)))
                return Result<void>::failure(commitError(
                    ErrorCode::ConfigConflict, "publication member uses the wrong runtime generation", operation));
            members.push_back(at->second);
        }
        bindings = std::move(prepared).value().bindings;
    }
    std::map<std::string, Runtime> names;
    std::map<std::string, RuntimeId> baseline;
    for (const auto& definition : candidateDefinitions) {
        auto at = std::find_if(owner->runtimes_.begin(), owner->runtimes_.end(),
                               [&](const auto& entry) { return entry.second.id() == definition.first; });
        if (at == owner->runtimes_.end())
            return Result<void>::failure(
                commitError(ErrorCode::ConfigConflict, "canonical definition changed before preparation", operation));
        baseline.emplace(at->first, definition.first);
        names.emplace(at->first, mode == ReplacementCommitMode::Candidate ? definition.second : at->second);
    }
    if (cancelled || kernel != owner || owner->closing_ ||
        (state != ReplacementScopeState::Staging && state != ReplacementScopeState::Stopped))
        return Result<void>::failure(
            commitError(ErrorCode::ContextClosing, "replacement closed during preparation", operation));
    preparedRuntimes.swap(names);
    preparedBase.swap(baseline);
    preparedMembers.swap(members);
    preparedBindings.swap(bindings);
    preparedMode = mode;
    state = ReplacementScopeState::Prepared;
    return Result<void>::success();
}
Result<void> ReplacementScopeControl::commitPrepared()
{
    // 成功路径不调用调度器、用户或插件；上层可在模块元数据锁内组合多层提交。
    if (std::this_thread::get_id() != ownerThread)
        return Result<void>::failure(
            commitError(ErrorCode::WrongThread, "prepared commit requires its owner", operation));
    if (state != ReplacementScopeState::Prepared || cancelled || blocked.load() || !kernel || kernel->closing_)
        return Result<void>::failure(
            commitError(ErrorCode::ContextClosing, "prepared commit is no longer usable", operation));
    for (const auto& entry : preparedBase) {
        const auto at = kernel->runtimes_.find(entry.first);
        if (at == kernel->runtimes_.end() || at->second.id() != entry.second)
            return Result<void>::failure(
                commitError(ErrorCode::ConfigConflict, "kernel definitions changed after preparation", operation));
    }
    for (const auto& member : preparedMembers)
        if (!member.preparedForPublication())
            return Result<void>::failure(
                commitError(ErrorCode::ProvisionIncomplete, "prepared member is no longer active", operation));
    for (const auto& binding : preparedBindings)
        if (binding->state != BindingState::Held || binding->deliveryBlocked.load())
            return Result<void>::failure(
                commitError(ErrorCode::ProvisionIncomplete, "prepared output changed", operation));
    if (!preparedMembers.empty() && (!staging || staging->control_->state != PublicationState::Open))
        return Result<void>::failure(
            commitError(ErrorCode::ContextClosing, "prepared publication is no longer open", operation));
    for (auto& entry : preparedRuntimes)
        std::swap(kernel->runtimes_.at(entry.first), entry.second);
    if (!preparedMembers.empty()) {
        // preparedRuntimes 留存旧工厂，整个宿主切换完成前不在锁内释放它们。
        applyPublication(staging->control_, preparedBindings);
    }
    state = ReplacementScopeState::Committed;
    return Result<void>::success();
}
} // namespace cordis::detail
namespace cordis {
Result<void> ReplacementScope::prepareDefinitions(std::vector<RuntimeReplacement> definitions) const
{
    const auto control = control_;
    return control ? control->prepareDefinitions(std::move(definitions))
                   : Result<void>::failure(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Result<void> ReplacementScope::prepareCommit(ReplacementCommitMode mode) const
{
    const auto control = control_;
    return control ? control->prepareCommit(mode)
                   : Result<void>::failure(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Result<void> ReplacementScope::commitPrepared() const
{
    const auto control = control_;
    return control ? control->commitPrepared()
                   : Result<void>::failure(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
}
Result<ReplacementPreparation> ReplacementScope::preparation() const
{
    using Output = Result<ReplacementPreparation>;
    const auto control = control_;
    if (!control)
        return Output::failure(makeError(ErrorCode::InvalidArgument, "replacement scope moved from"));
    auto admitted = control->check();
    if (!admitted.hasValue())
        return Output::failure(admitted.error());
    if (control->state != ReplacementScopeState::Prepared)
        return Output::failure(makeError(ErrorCode::ContextClosing, "Core replacement is not prepared"));
    ReplacementPreparation value{control->operation, control->preparedMode, {}};
    value.definitions.reserve(control->candidateDefinitions.size());
    for (const auto& definition : control->candidateDefinitions)
        value.definitions.push_back({definition.first, definition.second.id()});
    return Output::success(std::move(value));
}
} // namespace cordis
