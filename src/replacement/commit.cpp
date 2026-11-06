/** @file commit.cpp
 * @brief 基于最新接受目标准备全部账目，退出模块锁后才解除预约和通知。
 */
#include "operation_p.hpp"
namespace cordis::detail {
void ReplacementOperationControl::commit()
{
    using namespace loader::detail;
    auto& work = *execution;
    const auto mode = recovery ? ReplacementCommitMode::Recovery : ReplacementCommitMode::Candidate;
    std::vector<SessionCommitAccess::View> views;
    for (std::size_t index = 0; index != work.request.configurations.size(); ++index) {
        const auto& participant = work.request.configurations[index];
        auto view = SessionCommitAccess::inspect(*participant.session, *participant.driver, &work.sessions[index]);
        if (!view.hasValue()) {
            fail(view.error());
            return;
        }
        if (!view.value().idle) {
            if (view.value().settled.isReady()) {
                fail(replacementError(ErrorCode::ConfigConflict,
                                      "configuration coordinator is still inside a callback"));
                return;
            }
            watch(view.value().settled,
                  [self = shared_from_this()](const Result<loader::PlanReport>&) { self->commit(); });
            return;
        }
        views.push_back(std::move(view).value());
    }
    std::vector<PreparedConfigurationCommit> metadata;
    for (std::size_t index = 0; index != work.configurations.size(); ++index) {
        const auto& participant = work.request.configurations[index];
        const auto& frozen = work.configurations[index];
        auto definitions = DriverCommitAccess::rebaseDefinitions(*participant.driver, frozen.change, mode);
        if (!definitions.hasValue()) {
            fail(definitions.error());
            return;
        }
        std::shared_ptr<const loader::ValidatedCandidate> target = views[index].desired;
        if (!recovery) {
            auto source = mergeReplacementTarget(frozen.original->source(), participant.target->source(),
                                                 views[index].desired->source(), frozen.paths);
            if (!source.hasValue()) {
                fail(source.error());
                return;
            }
            auto valid = definitions.value()->validate(std::move(source).value());
            if (!valid.hasValue()) {
                fail(valid.error());
                return;
            }
            target = std::make_shared<const loader::ValidatedCandidate>(std::move(valid).value());
        }
        std::vector<ReplacementEntry> mappings;
        for (std::size_t key = 0; key != work.configured.size(); ++key)
            if (work.configured[key] && work.configured[key]->participant == index) {
                const auto& source = *work.configured[key];
                mappings.push_back({source.path, source.previous, work.built.at(key).snapshot().value().instance});
            }
        auto prepared = ConfigurationCommitAccess::prepare(*participant.session, *participant.driver, *work.scope,
                                                           definitions.value(), target, mappings, views[index].revision,
                                                           &work.sessions[index], &work.driverClaims[index]);
        if (!prepared.hasValue()) {
            metadata.clear();
            auto latest =
                SessionCommitAccess::inspect(*participant.session, *participant.driver, &work.sessions[index]);
            if (latest.hasValue() && latest.value().revision != views[index].revision) {
                watch(latest.value().settled,
                      [self = shared_from_this()](const Result<loader::PlanReport>&) { self->commit(); });
                return;
            }
            fail(prepared.error());
            return;
        }
        metadata.push_back(std::move(prepared).value());
    }
    auto published = [&]() -> Result<void> {
        auto modules = module::detail::ReplacementAccess::prepare(work.reservation, *work.scope);
        if (!modules.hasValue())
            return Result<void>::failure(modules.error());
        cancelled = cancelled || sessionClosing();
        bool ready = !delivery->blocked.load() && (!cancelled || recovery);
        for (const auto& item : metadata)
            ready = ready && item.ready();
        if (!ready)
            return Result<void>::failure(
                replacementError(cancelled && !recovery ? ErrorCode::OperationCancelled : ErrorCode::ConfigConflict,
                                 "metadata invalidated or cancelled before global commit"));
        auto core = modules.value().publish();
        if (!core.hasValue())
            return core;
        for (auto& item : metadata)
            item.publish();
        report.instances.swap(work.preparedMappings);
        report.committed = !recovery;
        report.recovered = recovery;
        return Result<void>::success();
    }();
    if (!published.hasValue()) {
        metadata.clear();
        fail(published.error());
        return;
    }
    auto notifications = releaseClaims();
    bool completed = notifications.hasValue();
    for (auto& item : metadata) {
        auto finished = item.finish(notifications);
        if (!finished.hasValue()) {
            report.errors.push_back(finished.error());
            completed = false;
        }
    }
    metadata.clear();
    if (!completed) {
        if (!notifications.hasValue() && work.configurations.empty())
            report.errors.push_back(notifications.error());
        settle(ReplacementOutcome::Failed, true);
        return;
    }
    settle(cancelled  ? ReplacementOutcome::Cancelled
           : recovery ? ReplacementOutcome::Restored
                      : ReplacementOutcome::Applied,
           false);
}
} // namespace cordis::detail
