/** @file close.cpp
 * @brief 逐段等待真实清理回执；确认前保留原拥有者，drain 另行报告。
 */
#include "workspace.hpp"

namespace notebook {
using namespace cordis;
namespace {
ErrorInfo cleanupFailure(const WorkspaceCloseSnapshot& state)
{
    auto failure = makeError(ErrorCode::CleanupFailed, "workspace close retained unconfirmed resources");
    failure.phase = "workspace.close";
    const auto append = [&](const auto& result) {
        if (result && !result->hasValue())
            failure.causes.push_back(result->error());
    };
    append(state.session);
    append(state.kernel);
    append(state.counter);
    append(state.reporter);
    append(state.shell);
    append(state.providers);
    for (const auto* result : {&state.kernel, &state.counter, &state.reporter})
        if (*result && (*result)->hasValue())
            failure.causes.insert(failure.causes.end(), (*result)->value().errors.begin(),
                                  (*result)->value().errors.end());
    return failure;
}
bool quiet(const std::optional<Result<CleanupReport>>& result)
{
    return !result || (result->hasValue() && result->value().quiescent);
}
} // namespace
Task<void> Workspace::close()
{
    if (!scheduler->isCurrentThread())
        return failedTask(makeError(ErrorCode::WrongThread, "workspace close requires owner"));
    if (closeState_->closing || closeState_->closed)
        return stopped_.task();
    stopped_ = Promise<void>{};
    closeState_.emplace();
    closeState_->closing = true;
    pin_ = shared_from_this();
    closeAfterUpgrade();
    return stopped_.task();
}
void Workspace::retryableCloseFailure(ErrorInfo failure)
{
    const auto done = stopped_;
    auto keepAlive = std::move(pin_);
    closeState_->closing = false;
    done.reject(std::move(failure));
    keepAlive.reset();
}
void Workspace::retainCloseFailure(ErrorInfo failure)
{
    closeState_->retained = true;
    stopped_.reject(std::move(failure));
}
void Workspace::flushBeforeClose()
{
    if (!session) {
        closeKernel();
        return;
    }
    const auto state = session->snapshot();
    if (!state.hasValue()) {
        retryableCloseFailure(state.error());
        return;
    }
    if (!state.value().store.dirty) {
        closeSession();
        return;
    }
    const auto saved = session->save(state.value().coordinator.desiredRevision);
    if (!saved.hasValue()) {
        closeState_->save.emplace(Result<loader::SaveReport>::failure(saved.error()));
        retryableCloseFailure(saved.error());
        return;
    }
    const auto self = shared_from_this();
    watch(saved.value(), [self](const Result<loader::SaveReport>& result) {
        self->closeState_->save.emplace(result);
        if (result.hasValue() && (result.value().outcome == loader::SaveOutcome::Written ||
                                  result.value().outcome == loader::SaveOutcome::Unchanged))
            self->flushBeforeClose();
        else
            self->retryableCloseFailure(
                !result.hasValue() ? result.error()
                                   : result.value().error.value_or(makeError(
                                         ErrorCode::ConfigConflict,
                                         "configuration was not saved; resolve disk conflict before closing")));
    });
}
void Workspace::closeSession()
{
    const auto self = shared_from_this();
    watch(session->close(), [self](const Result<void>& result) {
        self->closeState_->session.emplace(result);
        self->closeKernel();
    });
}
void Workspace::closeKernel()
{
    const auto self = shared_from_this();
    watch(kernel->close(), [self](const Result<CleanupReport>& result) {
        self->closeState_->kernel.emplace(result);
        self->closeCounter();
    });
}
void Workspace::closeCounter()
{
    if (!counter_ || !counter_->session) {
        closeReporter();
        return;
    }
    const auto self = shared_from_this();
    watch(counter_->session->close(), [self](const Result<CleanupReport>& result) {
        self->closeState_->counter.emplace(result);
        self->closeReporter();
    });
}
void Workspace::closeReporter()
{
    if (!reporter_ || !reporter_->session) {
        closeShell();
        return;
    }
    const auto self = shared_from_this();
    watch(reporter_->session->close(), [self](const Result<CleanupReport>& result) {
        self->closeState_->reporter.emplace(result);
        self->closeShell();
    });
}
void Workspace::closeShell()
{
    const auto& state = *closeState_;
    if ((state.session && !state.session->hasValue()) || !quiet(state.kernel) || !quiet(state.counter) ||
        !quiet(state.reporter)) {
        retainCloseFailure(cleanupFailure(state));
        return;
    }
    const auto self = shared_from_this();
    watch(shell.close(), [self](const Result<void>& result) {
        self->closeState_->shell.emplace(result);
        if (!result.hasValue())
            self->retainCloseFailure(result.error());
        else
            self->releaseOwners();
    });
}
void Workspace::releaseOwners()
{
    std::optional<ErrorInfo> error;
    for (const auto& provider : providers_) {
        const auto retired = provider.retire();
        if (!retired.hasValue()) {
            if (!error)
                error.emplace(retired.error());
            else
                error->causes.push_back(retired.error());
        }
        const auto released = provider.release();
        if (!released.hasValue()) {
            if (!error)
                error.emplace(released.error());
            else
                error->causes.push_back(released.error());
        }
    }
    closeState_->providers.emplace(error ? Result<void>::failure(*error) : Result<void>::success());
    if (error) {
        retainCloseFailure(*error);
        return;
    }
    providers_.clear();
    host_.reset();
    configurationPage.reset();
    modulesPage.reset();
    configurationManagement.reset();
    session.reset();
    driver.reset();
    definitions_.reset();
    if (notebook_)
        notebook_->runtime.reset();
    if (counter_)
        counter_->runtime.reset();
    if (reporter_)
        reporter_->runtime.reset();
    kernel.reset();
    // 真正拥有者已交还；等原 FIFO 回执退出后再读取工厂来源和 drain。
    Promise<void> barrier;
    const auto blocked = deliveryBlocked_;
    const bool accepted = scheduler->post(
        {[barrier] { barrier.resolve(); },
         [barrier] { barrier.reject(makeError(ErrorCode::SchedulerClosed, "workspace close barrier discarded")); }});
    if (!accepted) {
        blocked->store(true);
        retainCloseFailure(makeError(ErrorCode::SchedulerClosed, "workspace close barrier rejected"));
        return;
    }
    const auto self = shared_from_this();
    watch(barrier.task(), [self](const Result<void>& result) {
        if (!result.hasValue()) {
            self->deliveryBlocked_->store(true);
            self->retainCloseFailure(result.error());
        } else
            self->finishClose();
    });
}
void Workspace::finishClose()
{
    if (counter_)
        closeState_->counterDrain.emplace(manager->drain(counter_->generation));
    if (reporter_)
        closeState_->reporterDrain.emplace(manager->drain(reporter_->generation));
    // 外部 Runtime 拒绝 drain 是独立证据，不撤销本装配已确认的关闭。
    closeState_->closed = true;
    stopped_.resolve();
    pin_.reset();
}
} // namespace notebook
