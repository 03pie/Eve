/** @file catalog_registry.cpp
 * @brief 进程目录只有一个状态源；模块事务与装载共用同一锁和健康检查。
 */
#include "catalog_registry_p.hpp"

namespace cordis::module::detail {
ModuleCatalog& moduleCatalog()
{
    // 默认驻留，不在静态析构期关闭插件引用或文件保护。
    static auto* value = new ModuleCatalog;
    return *value;
}
std::shared_ptr<Generation> catalogGenerationLocked(ModuleCatalog& catalog, GenerationId id)
{
    for (const auto& generation : catalog.generations)
        if (generation->data.generation == id)
            return generation;
    return {};
}
Result<void> generationReusable(const Generation& generation, ReusePurpose purpose)
{
    std::lock_guard<std::mutex> lock(generation.businessSignal->mutex);
    return generationReusableLockedSignal(generation, purpose);
}
Result<void> generationReusableLockedSignal(const Generation& generation, ReusePurpose purpose)
{
    auto failure = [](ErrorCode code, const char* message) {
        auto error = makeError(code, message);
        error.phase = "module.load";
        return Result<void>::failure(std::move(error));
    };
    if (generation.loading)
        return failure(ErrorCode::ModuleLoad, "generation load is already in progress");
    if (generation.data.role == GenerationRole::Retired)
        return failure(ErrorCode::ModulePinned, "retired generation no longer accepts activation references");
    if (generation.data.failure)
        return Result<void>::failure(*generation.data.failure);
    if (purpose == ReusePurpose::Staging && generation.data.businessFailure)
        return Result<void>::failure(*generation.data.businessFailure);
    if (generation.businessSignal->error)
        return Result<void>::failure(*generation.businessSignal->error);
    if (generation.data.state != ModuleState::Loaded && generation.data.state != ModuleState::Serving)
        return failure(ErrorCode::ModulePinned, "generation no longer accepts activation references");
    return Result<void>::success();
}
} // namespace cordis::module::detail
