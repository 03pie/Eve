/** @file operation.cpp
 * @brief 操作门面、owner 串行推进及失败保留，不执行模块锁内回调。
 */
#include "operation_p.hpp"
namespace cordis::detail {
Result<void> ReplacementOperationControl::releaseClaims()
{
    if (!execution)
        return Result<void>::success();
    const auto unlock = [this]() noexcept {
        execution->reservation.reset();
        execution->driverClaims.clear();
    };
    if (execution->scope)
        return CoreReplacementAccess::release(*execution->scope, unlock);
    unlock();
    return Result<void>::success();
}
ErrorInfo ReplacementOperationControl::abortPreparation(ErrorInfo error)
{
    auto released = releaseClaims();
    if (!released.hasValue()) {
        error.causes.push_back(released.error());
        if (!error.operation.valid())
            error.operation = released.error().operation;
        report.errors.push_back(error);
        report.resourcesRetained = true;
        CoreReplacementAccess::retainFailure(*execution->scope);
        const bool observed = static_cast<bool>(pin);
        pin = shared_from_this();
        execution->sessions.clear();
        if (!observed)
            observeKernelClose();
    } else {
        // 解除实际替换权限后才能让最后一份会话持有触发 Driver/Store 关闭。
        if (execution)
            execution->sessions.clear();
        execution.reset();
        pin.reset();
    }
    return error;
}
void ReplacementOperationControl::observeKernelClose()
{
    watchInterruption(CoreReplacementAccess::whenKernelClosed(*execution->request.kernel),
                      [](ReplacementOperationControl& self, const Result<CleanupReport>& result) {
                          self.cancelled = true;
                          ++self.sequence;
                          if (!result.hasValue())
                              self.report.errors.push_back(result.error());
                          else
                              self.report.errors.insert(self.report.errors.end(), result.value().errors.begin(),
                                                        result.value().errors.end());
                          const bool quiet = result.hasValue() && result.value().quiescent;
                          self.settle(quiet ? ReplacementOutcome::Cancelled : ReplacementOutcome::Failed, !quiet);
                      });
}
void ReplacementOperationControl::observeInputs()
{
    for (const bool restoring : {false, true}) {
        auto signal = CoreReplacementAccess::whenInputsLost(*execution->scope, *execution->request.kernel, restoring);
        const auto interrupted = [restoring](ReplacementOperationControl& self, const Result<void>& result) {
            if (self.recovery == restoring &&
                (self.phase == ReplacementPhase::Building || self.phase == ReplacementPhase::Recovering) &&
                !result.hasValue())
                self.fail(result.error());
        };
        watchInterruption(signal, interrupted);
        watchInterruption((restoring ? execution->sourceEvents->recovery : execution->sourceEvents->candidate).task(),
                          interrupted);
    }
}
void ReplacementOperationControl::observeSessionClose()
{
    std::vector<Task<void>> signals;
    for (const auto& session : execution->sessions)
        signals.push_back(session.closing());
    for (const auto& signal : signals) {
        if (delivery->terminal.load() || delivery->blocked.load())
            return;
        watchInterruption(signal, [](ReplacementOperationControl& self, const Result<void>&) { self.cancel(); });
    }
}
bool ReplacementOperationControl::sessionClosing() const
{
    for (const auto& session : execution->sessions)
        if (session.closing().isReady())
            return true;
    return false;
}
void ReplacementOperationControl::cancel()
{
    if (delivery->terminal.load() || delivery->blocked.load() || report.committed || report.recovered)
        return;
    cancelled = true;
    const auto self = shared_from_this();
    dispatch([self] {
        if (CoreReplacementAccess::kernelClosing(*self->execution->request.kernel))
            return;
        if (self->phase == ReplacementPhase::Prepared)
            self->settle(ReplacementOutcome::Cancelled, false);
        else if (self->phase == ReplacementPhase::Building)
            self->fail(makeError(ErrorCode::OperationCancelled, "replacement cancelled"));
    });
}
void ReplacementOperationControl::dispatch(std::function<void()> job)
{
    if (std::this_thread::get_id() != owner) {
        delivery->fail(replacementError(ErrorCode::WrongThread, "replacement continuation requires owner"));
        return;
    }
    if (delivery->terminal.load() || delivery->blocked.load())
        return;
    jobs.push_back(std::move(job));
    if (draining)
        return;
    const auto self = shared_from_this();
    draining = true;
    while (!jobs.empty() && !delivery->terminal.load() && !delivery->blocked.load()) {
        auto current = std::move(jobs.front());
        jobs.pop_front();
        try {
            current();
        } catch (const std::exception& error) {
            fail(makeError(ErrorCode::CallbackFailed, error.what()));
        } catch (...) {
            fail(replacementError(ErrorCode::CallbackFailed, "replacement callback threw"));
        }
    }
    draining = false;
}
void ReplacementOperationControl::start()
{
    if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
        return;
    cancelled = cancelled || sessionClosing();
    if (cancelled) {
        settle(ReplacementOutcome::Cancelled, false);
        return;
    }
    for (std::size_t index = 0; index != execution->configurations.size(); ++index) {
        const auto& participant = execution->request.configurations[index];
        auto latest = loader::detail::SessionCommitAccess::inspect(*participant.session, *participant.driver);
        if (!latest.hasValue()) {
            report.errors.push_back(latest.error());
            settle(ReplacementOutcome::Failed, false);
            return;
        }
        const auto& frozen = execution->configurations[index];
        auto merged = loader::detail::mergeReplacementTarget(frozen.original->source(), participant.target->source(),
                                                             latest.value().desired->source(), frozen.paths);
        auto definitions = loader::detail::DriverCommitAccess::rebaseDefinitions(*participant.driver, frozen.change,
                                                                                 ReplacementCommitMode::Recovery);
        if (!merged.hasValue() || !definitions.hasValue()) {
            report.errors.push_back(!merged.hasValue() ? merged.error() : definitions.error());
            settle(ReplacementOutcome::Failed, false);
            return;
        }
    }
    for (const bool restoring : {false, true}) {
        auto graph = planGraph(restoring);
        if (!graph.hasValue()) {
            report.errors.push_back(graph.error());
            settle(ReplacementOutcome::Failed, false);
            return;
        }
        auto sources = module::detail::ReplacementAccess::checkSources(execution->reservation, restoring);
        if (!sources.hasValue()) {
            report.errors.push_back(sources.error());
            settle(ReplacementOutcome::Failed, false);
            return;
        }
    }
    auto frozen = execution->scope->freeze();
    if (!frozen.hasValue()) {
        report.errors.push_back(frozen.error());
        settle(ReplacementOutcome::Failed, false);
        return;
    }
    phase = ReplacementPhase::Stopping;
    ++sequence;
    auto stopped = execution->scope->stop();
    watch(stopped, [self = shared_from_this()](const Result<CleanupReport>& result) {
        if (!result.hasValue()) {
            self->fail(result.error());
            return;
        }
        if (!result.value().quiescent) {
            self->report.errors.insert(self->report.errors.end(), result.value().errors.begin(),
                                       result.value().errors.end());
            self->settle(ReplacementOutcome::Failed, true);
            return;
        }
        if (self->cancelled) {
            self->recover();
        } else
            self->build(false);
    });
}
void ReplacementOperationControl::recover()
{
    recovery = true;
    auto allowed = execution->reservation.authorizeRecovery(*execution->scope);
    if (!allowed.hasValue()) {
        fail(allowed.error());
        return;
    }
    build(true);
}
void ReplacementOperationControl::fail(ErrorInfo error)
{
    if (delivery->terminal.load())
        return;
    if (CoreReplacementAccess::kernelClosing(*execution->request.kernel))
        return;
    cancelled = cancelled || sessionClosing();
    error.operation = report.operation;
    report.errors.push_back(std::move(error));
    if (phase == ReplacementPhase::Stopping || phase == ReplacementPhase::Discarding || report.committed) {
        settle(ReplacementOutcome::Failed, true);
        return;
    }
    const bool failedRecovery = recovery;
    if (failedRecovery) {
        auto failure = replacementError(ErrorCode::RecoveryFailed, "old generation reconstruction failed");
        failure.operation = report.operation;
        failure.causes.push_back(report.errors.back());
        report.errors.push_back(std::move(failure));
    }
    phase = ReplacementPhase::Discarding;
    ++sequence;
    auto state = execution->scope->snapshot();
    if (!state.hasValue()) {
        report.errors.push_back(state.error());
        settle(ReplacementOutcome::Failed, true);
        return;
    }
    if (state.value().state == ReplacementScopeState::Stopped) {
        if (failedRecovery)
            settle(ReplacementOutcome::Failed, true);
        else
            recover();
        return;
    }
    auto discarded = execution->scope->discard();
    watch(discarded, [self = shared_from_this(), failedRecovery](const Result<CleanupReport>& result) {
        if (!result.hasValue() || !result.value().quiescent) {
            if (!result.hasValue())
                self->report.errors.push_back(result.error());
            else
                self->report.errors.insert(self->report.errors.end(), result.value().errors.begin(),
                                           result.value().errors.end());
            self->settle(ReplacementOutcome::Failed, true);
            return;
        }
        if (failedRecovery)
            self->settle(ReplacementOutcome::Failed, true);
        else
            self->recover();
    });
}
void ReplacementOperationControl::settle(ReplacementOutcome outcome, bool retained)
{
    if (delivery->terminal.load())
        return;
    if (!retained && execution) {
        auto released = releaseClaims();
        if (!released.hasValue()) {
            report.errors.push_back(released.error());
            retained = true;
            outcome = ReplacementOutcome::Failed;
        }
    }
    report.outcome = outcome;
    report.resourcesRetained = retained;
    if (retained && execution)
        CoreReplacementAccess::retainFailure(*execution->scope);
    phase = outcome == ReplacementOutcome::Applied ? ReplacementPhase::Committed
            : outcome == ReplacementOutcome::Restored || (outcome == ReplacementOutcome::Cancelled && report.recovered)
                ? ReplacementPhase::Restored
            : outcome == ReplacementOutcome::Cancelled ? ReplacementPhase::Cancelled
                                                       : ReplacementPhase::Failed;
    ++sequence;
    if (delivery->blocked.load())
        return; // 跨线程投递失败已经终结 Promise；继续保留实际资源。
    delivery->terminal.store(true);
    jobs.clear();
    execution->sessions.clear();
    if (!retained) {
        execution.reset();
        pin.reset();
    }
    delivery->done.resolve(report);
}
} // namespace cordis::detail
namespace cordis {
Result<ReplacementOperation> ReplacementOperation::begin(ReplacementRequest request)
{
    using Output = Result<ReplacementOperation>;
    const auto control = std::make_shared<detail::ReplacementOperationControl>();
    auto prepared = [&]() -> Result<void> {
        try {
            return control->prepare(std::move(request));
        } catch (const std::exception& error) {
            return Result<void>::failure(makeError(ErrorCode::CallbackFailed, error.what()));
        } catch (...) {
            return Result<void>::failure(makeError(ErrorCode::CallbackFailed, "replacement preflight threw"));
        }
    }();
    if (!prepared.hasValue())
        return Output::failure(control->abortPreparation(prepared.error()));
    control->pin = control;
    const auto gate = control->delivery;
    const auto scheduler = control->execution->request.scheduler;
    control->observeKernelClose();
    if (!gate->terminal.load() && !gate->blocked.load())
        control->observeSessionClose();
    if (!gate->terminal.load() && !gate->blocked.load())
        control->observeInputs();
    if (gate->terminal.load() || gate->blocked.load())
        return Output::success(ReplacementOperation(control));
    bool posted = false;
    try {
        posted = scheduler->post(
            {[control] { control->dispatch([control] { control->start(); }); },
             [gate] { gate->fail(makeError(ErrorCode::SchedulerClosed, "replacement start discarded")); }});
    } catch (...) {
    }
    if (!posted) {
        return Output::failure(
            control->abortPreparation(makeError(ErrorCode::SchedulerClosed, "replacement start rejected")));
    }
    return Output::success(ReplacementOperation(control));
}
Task<ReplacementReport> ReplacementOperation::settled() const
{
    if (control_)
        return control_->delivery->done.task();
    Promise<ReplacementReport> failed;
    failed.reject(makeError(ErrorCode::InvalidArgument, "replacement moved from"));
    return failed.task();
}
Result<ReplacementSnapshot> ReplacementOperation::snapshot() const
{
    if (!control_)
        return Result<ReplacementSnapshot>::failure(makeError(ErrorCode::InvalidArgument, "replacement moved from"));
    if (control_->owner != std::this_thread::get_id())
        return Result<ReplacementSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "replacement snapshot requires owner"));
    auto report = control_->report;
    if (control_->delivery->blocked.load())
        report.resourcesRetained = true;
    return Result<ReplacementSnapshot>::success(
        {control_->phase, control_->cancelled, control_->delivery->blocked.load(), std::move(report)});
}
Result<void> ReplacementOperation::cancel()
{
    const auto control = control_;
    if (!control || control->owner != std::this_thread::get_id())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "replacement cancellation requires owner"));
    if (control->delivery->terminal.load() || control->delivery->blocked.load() || control->report.committed ||
        control->report.recovered)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "replacement already settled or blocked"));
    control->cancel();
    return Result<void>::success();
}
} // namespace cordis
