/** @file target_commit.cpp
 * @brief 合并 Coordinator/Store 的准备槽，发布阶段不分配或回调。
 */
#include "target_commit_p.hpp"

namespace cordis::loader::detail {
Result<PreparedTargetCommit> TargetCommitAccess::prepare(ConfigurationCoordinator& coordinator,
                                                         ConfigurationStore& store,
                                                         std::shared_ptr<const ValidatedCandidate> target,
                                                         std::uint64_t expected)
{
    using Output = Result<PreparedTargetCommit>;
    auto runtime = prepareCoordinator(coordinator, target, expected);
    if (!runtime.hasValue())
        return Output::failure(runtime.error());
    auto persistence = prepareStore(store, std::move(target), expected);
    if (!persistence.hasValue())
        return Output::failure(persistence.error());
    if (runtime.value()->scheduler != persistence.value()->scheduler ||
        runtime.value()->baseline != persistence.value()->baseline)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "target owners do not share accepted state"));
    PreparedTargetCommit result(std::move(runtime).value(), std::move(persistence).value());
    if (!result.ready())
        return Output::failure(makeError(ErrorCode::ConfigConflict, "target preparation invalidated"));
    return Output::success(std::move(result));
}
bool PreparedTargetCommit::ready() const noexcept
{
    return coordinator_ && store_ && coordinator_->ready() && store_->ready();
}
void PreparedTargetCommit::publish() noexcept
{
    coordinator_->publish();
    store_->publish();
}
std::uint64_t PreparedTargetCommit::revision() const noexcept
{
    return coordinator_ ? coordinator_->previous + 1 : 0;
}
} // namespace cordis::loader::detail
