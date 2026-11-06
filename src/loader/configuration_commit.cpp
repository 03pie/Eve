/** @file configuration_commit.cpp
 * @brief 单一宿主准备窗口组合实际 Driver 与泛型 Session，低层不反向依赖模块。
 */
#include "configuration_commit_p.hpp"
#include <thread>

namespace cordis::loader::detail {
struct ConfigurationCommitControl {
    // 逆序先解除 Driver，再解除 Session 的目标槽与业务访问。
    std::optional<PreparedSessionCommit> session;
    std::optional<PreparedDriverCommit> driver;
    std::thread::id thread = std::this_thread::get_id();
    bool published = false;
};
Result<DriverReplacementClaim>
ConfigurationCommitAccess::reserve(ConfigurationSession& session, KernelReconcileDriver& driver,
                                   const ReplacementScope& scope,
                                   std::shared_ptr<const ConfigurationDefinitions> definitions,
                                   std::shared_ptr<const ValidatedCandidate> target, std::uint64_t expected)
{
    using Output = Result<DriverReplacementClaim>;
    auto original = SessionCommitAccess::inspect(session, driver);
    if (!original.hasValue())
        return Output::failure(original.error());
    if (original.value().revision != expected || !original.value().idle)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "session changed before driver reservation"));
    auto reserved =
        DriverCommitAccess::reserve(driver, scope, std::move(definitions), std::move(target), original.value().desired);
    if (!reserved.hasValue())
        return Output::failure(reserved.error());
    auto current = SessionCommitAccess::inspect(session, driver);
    if (!current.hasValue())
        return Output::failure(current.error());
    if (current.value().revision != expected || !current.value().idle ||
        current.value().desired != original.value().desired)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "session changed during driver reservation"));
    return reserved;
}
Result<PreparedConfigurationCommit> ConfigurationCommitAccess::prepare(
    ConfigurationSession& session, KernelReconcileDriver& driver, const ReplacementScope& scope,
    std::shared_ptr<const ConfigurationDefinitions> definitions, std::shared_ptr<const ValidatedCandidate> target,
    const std::vector<ReplacementEntry>& mappings, std::uint64_t expected, const SessionReplacementHold* hold,
    const DriverReplacementClaim* claim)
{
    using Output = Result<PreparedConfigurationCommit>;
    auto value = std::make_shared<ConfigurationCommitControl>();
    auto core = scope.preparation();
    if (!core.hasValue())
        return Output::failure(core.error());
    std::vector<EntryOperation> applied;
    applied.reserve(mappings.size());
    for (const auto& entry : mappings)
        applied.push_back({entry.path, ChangeKind::Recreate});
    auto metadata = SessionCommitAccess::prepare(
        session, driver, target, expected,
        core.value().mode == ReplacementCommitMode::Recovery ? TargetCommitMode::Preserve : TargetCommitMode::Advance,
        applied, hold);
    if (!metadata.hasValue())
        return Output::failure(metadata.error());
    value->session.emplace(std::move(metadata).value());
    auto runtime = DriverCommitAccess::prepare(driver, scope, std::move(definitions), target, mappings, claim);
    if (!runtime.hasValue())
        return Output::failure(runtime.error());
    value->driver.emplace(std::move(runtime).value());
    if (value->driver->mode() != core.value().mode)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "Core preparation mode changed"));
    PreparedConfigurationCommit result(std::move(value));
    if (!result.ready())
        return Output::failure(makeError(ErrorCode::ConfigConflict, "configuration commit preparation invalidated"));
    return Output::success(std::move(result));
}
bool PreparedConfigurationCommit::ready() const noexcept
{
    return control_ && control_->thread == std::this_thread::get_id() && !control_->published && control_->driver &&
           control_->driver->ready() && control_->session && control_->session->ready();
}
void PreparedConfigurationCommit::publish() noexcept
{
    if (control_->published)
        return;
    control_->driver->publish();
    control_->session->publish();
    control_->published = true;
}
Result<void> PreparedConfigurationCommit::finish(Result<void> notifications)
{
    const auto value = control_;
    if (!value || value->thread != std::this_thread::get_id())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "configuration completion requires owner"));
    if (!value->published)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "configuration commit is not published"));
    value->driver.reset();
    return value->session->finish(std::move(notifications));
}
std::optional<AcceptedTarget> PreparedConfigurationCommit::accepted() const
{
    return control_ && control_->session ? control_->session->accepted() : std::nullopt;
}
} // namespace cordis::loader::detail
