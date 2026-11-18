/** @file upgrade_retirement.cpp
 * @brief 事务终结后的目标代次回收；物理拒绝不改原业务提交结果。
 */
#include "upgrade_p.hpp"
#include <algorithm>

namespace notebook::detail {
using namespace cordis;
void WorkspaceUpgradeExecution::retire()
{
    if (retiring)
        return;
    retiring = true;
    const bool committed = report.replacement && report.replacement->committed;
    report.retired = committed ? report.previous : report.candidate;
    const auto native = committed ? previousNative : candidateNative;
    const auto notebook = committed ? previousNotebook : candidateNotebook;
    if (native && native->session) {
        const auto self = shared_from_this();
        watch(native->session->close(), [self, native](const Result<CleanupReport>& result) {
            self->report.nativeClose.emplace(result);
            if (!result.hasValue() || !result.value().quiescent) {
                self->retain(!result.hasValue()
                                 ? result.error()
                                 : makeError(ErrorCode::CleanupFailed, "retired native session did not quiesce"));
                return;
            }
            native->runtime.reset();
            self->installed.reset();
            self->barrier();
        });
    } else {
        if (native)
            native->runtime.reset();
        if (notebook)
            notebook->runtime.reset();
        installed.reset();
        barrier();
    }
}
void WorkspaceUpgradeExecution::barrier()
{
    Promise<void> reached;
    const bool accepted = owner->scheduler->post(
        {[reached] { reached.resolve(); },
         [reached] { reached.reject(makeError(ErrorCode::SchedulerClosed, "upgrade retirement barrier discarded")); }});
    if (!accepted) {
        blocked->store(true);
        retain(makeError(ErrorCode::SchedulerClosed, "upgrade retirement barrier rejected"));
        return;
    }
    const auto self = shared_from_this();
    watch(reached.task(), [self](const Result<void>& result) {
        if (!result.hasValue()) {
            self->blocked->store(true);
            self->retain(result.error());
        } else
            self->reclaim();
    });
}
void WorkspaceUpgradeExecution::reclaim()
{
    const bool committed = report.replacement && report.replacement->committed;
    if (report.retired.valid()) {
        const auto runtime = committed ? previousRuntime : candidateRuntime;
        const auto origin = runtime.valid() ? owner->manager->runtimeOrigin(runtime)
                                            : Result<std::optional<module::ModuleRuntimeOrigin>>::success({});
        const auto rows = owner->manager->snapshot();
        if (!origin.hasValue() || !rows.hasValue()) {
            retain(!origin.hasValue() ? origin.error() : rows.error());
            return;
        }
        const auto actual = std::find_if(rows.value().begin(), rows.value().end(),
                                         [&](const auto& row) { return row.generation == report.retired; });
        if (actual == rows.value().end()) {
            retain(makeError(ErrorCode::ConfigConflict, "retired generation disappeared"));
            return;
        }
        // 原目录同时核对真实 source、ledger、模块预约与读者，不代替其他拥有者释放。
        report.drain.emplace(owner->manager->drain(report.retired));
        if (report.drain->hasValue() && actual->manifest.loader == module::ModuleLoader::CAbi)
            report.unload.emplace(owner->manager->tryUnload(report.retired));
    }
    finish();
}
void WorkspaceUpgradeExecution::finish()
{
    const auto workspace = owner;
    // 历史只有纯值。先解除当前执行，再通知；同步新请求不能被旧完成栈覆盖。
    workspace->upgradeHistory_.push_back(report);
    if (workspace->upgradeHistory_.size() > 128)
        workspace->upgradeHistory_.pop_front();
    if (workspace->upgrade_.get() == this)
        workspace->upgrade_.reset();
    previousNative.reset();
    candidateNative.reset();
    previousNotebook.reset();
    candidateNotebook.reset();
    installed.reset();
    operation.reset();
    owner.reset();
    completed.resolve(report);
}
} // namespace notebook::detail

namespace notebook {
using namespace cordis;
void Workspace::closeAfterUpgrade()
{
    if (!upgrade_) {
        flushBeforeClose();
        return;
    }
    const auto execution = upgrade_;
    (void)execution->cancel();
    const auto self = shared_from_this();
    watch(execution->completed.task(), [self, execution](const Result<WorkspaceUpgradeReport>& result) {
        if (!result.hasValue() || result.value().retained || execution->blocked->load()) {
            self->retainCloseFailure(!result.hasValue()
                                         ? result.error()
                                         : makeError(ErrorCode::CleanupFailed, "workspace upgrade remains retained"));
            return;
        }
        self->flushBeforeClose();
    });
}
} // namespace notebook
