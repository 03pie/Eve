/** @file retirement.cpp
 * @brief 高级回收只重用原目录授权/账目；尝试结果为纯值，不拥有 Native 生命周期。
 */
#include "retirement_p.hpp"
#include <algorithm>

namespace notebook::detail {
using namespace cordis;
using namespace cordis::module;
Result<ModuleGenerationSnapshot> retirementTarget(const WorkspaceModulesSnapshot& snapshot, GenerationId generation)
{
    using Output = Result<ModuleGenerationSnapshot>;
    if (!generation.valid())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "retirement generation identity is invalid"));
    const auto target = std::find_if(snapshot.generations.begin(), snapshot.generations.end(),
                                     [generation](const auto& row) { return row.generation == generation; });
    if (target == snapshot.generations.end())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "retirement generation is unavailable"));
    if (target->role == GenerationRole::Current)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "current generation cannot be retired manually"));
    if (target->manifest.loader != ModuleLoader::CAbi)
        return Output::failure(makeError(ErrorCode::ModulePinned, "Qt generation remains resident"));
    const bool business = std::any_of(snapshot.current.begin(), snapshot.current.end(), [&](const auto& current) {
        const auto actual = std::find_if(snapshot.generations.begin(), snapshot.generations.end(),
                                         [&](const auto& row) { return row.generation == current.generation; });
        return actual != snapshot.generations.end() && actual->manifest.loader == ModuleLoader::CAbi &&
               actual->manifest.moduleId == target->manifest.moduleId;
    });
    if (!business)
        return Output::failure(
            makeError(ErrorCode::InvalidArgument, "generation is outside workspace business modules"));
    return Output::success(*target);
}
WorkspaceRetirementReport attemptRetirement(ModuleManager& manager, const ModuleGenerationSnapshot& target)
{
    WorkspaceRetirementReport result{target.generation, {}, {}};
    if (target.manifest.loader == ModuleLoader::CAbi && target.state == ModuleState::Unloaded) {
        result.unload.emplace(manager.tryUnload(target.generation));
        return result;
    }
    result.drain.emplace(manager.drain(target.generation));
    if (result.drain->hasValue() && target.manifest.loader == ModuleLoader::CAbi)
        result.unload.emplace(manager.tryUnload(target.generation));
    return result;
}
} // namespace notebook::detail

namespace notebook {
using namespace cordis;
Result<WorkspaceRetirementReport> Workspace::retryRetirement(GenerationId expectedGeneration)
{
    using Output = Result<WorkspaceRetirementReport>;
    if (!scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "retirement retry requires owner"));
    if (closeState_->closing || closeState_->closed || !moduleViewsOpen_ || !scheduler->isOpen() || deliveryBlocked())
        return Output::failure(makeError(ErrorCode::ContextClosing, "workspace retirement retry closed"));
    // 原执行可能已解除 claims 但仍保留两代，不能用目录空预约推断可以物理清理。
    if (upgrade_)
        return Output::failure(makeError(ErrorCode::ConfigConflict, "workspace upgrade still owns its execution"));
    const auto snapshot = modulesSnapshot();
    if (!snapshot.hasValue())
        return Output::failure(snapshot.error());
    if (!snapshot.value().open) {
        auto error = makeError(ErrorCode::ContextClosing, "workspace is not accepting retirement attempts");
        error.causes = snapshot.value().configurationErrors;
        return Output::failure(std::move(error));
    }
    const auto target = detail::retirementTarget(snapshot.value(), expectedGeneration);
    if (!target.hasValue())
        return Output::failure(target.error());
    return Output::success(detail::attemptRetirement(*manager, target.value()));
}
} // namespace notebook
