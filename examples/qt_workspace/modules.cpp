/** @file modules.cpp
 * @brief 模块视图仅复制原目录/会话值；同步写栈与破坏性关闭共享退出屏障。
 */
#include "workspace.hpp"

namespace notebook {
using namespace cordis;
Result<WorkspaceModulesSnapshot> Workspace::modulesSnapshot()
{
    using Output = Result<WorkspaceModulesSnapshot>;
    const auto synced = synchronizeUpgrade();
    if (!synced.hasValue())
        return Output::failure(synced.error());
    auto generations = manager->snapshot();
    if (!generations.hasValue())
        return Output::failure(generations.error());
    WorkspaceModulesSnapshot value;
    value.open =
        moduleViewsOpen_ && !closeState_->closing && !closeState_->closed && scheduler->isOpen() && !deliveryBlocked();
    value.generations = std::move(generations).value();
    const auto appendCurrent = [&](const char* alias, GenerationId generation, RuntimeId runtime) {
        value.current.push_back({alias, std::string("example.") + alias, generation, runtime, {}});
    };
    if (counter_)
        appendCurrent("counter", counter_->generation, counter_->runtime ? counter_->runtime->id() : RuntimeId{});
    if (reporter_)
        appendCurrent("reporter", reporter_->generation, reporter_->runtime ? reporter_->runtime->id() : RuntimeId{});
    if (notebook_)
        appendCurrent("notebook", notebook_->generation, notebook_->runtime ? notebook_->runtime->id() : RuntimeId{});
    if (session) {
        const auto state = session->snapshot();
        if (!state.hasValue()) {
            value.open = false;
            value.configurationErrors.push_back(state.error());
        } else {
            const auto& source = state.value();
            const auto appendError = [&](const std::optional<ErrorInfo>& error) {
                if (error)
                    value.configurationErrors.push_back(*error);
            };
            appendError(source.executionError);
            appendError(source.coordinator.runningError);
            appendError(source.store.lastError);
            for (const auto& entry : source.coordinator.running)
                appendError(entry.failure);
            if (source.latestPlan)
                value.configurationErrors.insert(value.configurationErrors.end(), source.latestPlan->errors.begin(),
                                                 source.latestPlan->errors.end());
            value.open = value.open && !source.closing && !source.deliveryBlocked &&
                         !source.coordinator.deliveryBlocked && !source.coordinator.unsafe &&
                         !source.coordinator.runningError && !source.store.deliveryBlocked;
            if (source.coordinator.desired)
                for (auto& current : value.current)
                    for (const auto& entry : source.coordinator.desired->source().structure().entries())
                        if (entry.second.plugin == current.plugin)
                            current.paths.push_back(entry.first);
        }
    }
    // 原关闭结果保留完整原因链；关闭后不再借用 Session/Driver 或运行工厂。
    const auto stopped = stopped_.task().tryResult();
    if (stopped && !stopped->hasValue())
        value.configurationErrors.push_back(stopped->error());
    return Output::success(std::move(value));
}
Result<void> Workspace::withModuleView(std::function<void()> action)
{
    if (!scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "module view requires owner"));
    if (!action)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "module view action is empty"));
    if (!moduleViewsOpen_ || closeState_->closed || !scheduler->isOpen())
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "module view closed"));
    if (!moduleViewDepth_)
        moduleViewsIdle_ = Promise<void>{};
    ++moduleViewDepth_;
    struct Exit {
        std::shared_ptr<Workspace> owner;
        std::size_t& depth;
        Promise<void> idle;
        ~Exit()
        {
            if (--depth == 0)
                idle.resolve();
        }
    } exit{shared_from_this(), moduleViewDepth_, moduleViewsIdle_};
    try {
        action();
        return Result<void>::success();
    } catch (const std::exception& failure) {
        return Result<void>::failure(makeError(ErrorCode::CallbackFailed, failure.what()));
    } catch (...) {
        return Result<void>::failure(makeError(ErrorCode::CallbackFailed, "module view action threw"));
    }
}
} // namespace notebook
