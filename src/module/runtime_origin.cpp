/** @file runtime_origin.cpp
 * @brief 在唯一模块目录登记实际工厂身份，查询不延长执行引用的寿命。
 */
#include "catalog_p.hpp"
#include "runtime_origin_p.hpp"
#include <algorithm>

namespace cordis::module {
namespace {
ErrorInfo originError(ErrorCode code, const char* message)
{
    auto error = makeError(code, message);
    error.phase = "module.runtime.origin";
    return error;
}
} // namespace
namespace detail {
RuntimeOriginControl::RuntimeOriginControl(std::shared_ptr<Generation> generation, std::string plugin,
                                           ModuleLease lease)
    : generation_(std::move(generation)), plugin_(std::move(plugin)), lease_(std::move(lease))
{
}
RuntimeOriginControl::~RuntimeOriginControl()
{
    if (runtime_.valid()) {
        std::lock_guard<std::mutex> lock(generation_->mutex);
        generation_->runtimeOrigins.erase(runtime_);
    }
    // 先退出目录锁再释放资源租约，不将未来的排空观察/所有者析构带进目录临界区。
    lease_.reset();
}
Result<void> RuntimeOriginControl::bind(RuntimeId runtime)
{
    if (!runtime.valid() || runtime_.valid())
        return Result<void>::failure(
            originError(ErrorCode::InvalidArgument, "runtime origin is already bound or invalid"));
    std::lock_guard<std::mutex> lock(generation_->mutex);
    if (!generation_->runtimeOrigins.emplace(runtime, plugin_).second)
        return Result<void>::failure(originError(ErrorCode::ConfigConflict, "runtime identity already has an origin"));
    runtime_ = runtime;
    return Result<void>::success();
}
Result<std::shared_ptr<RuntimeOriginControl>> retainRuntimeOrigin(GenerationId id, std::string plugin)
{
    using Output = Result<std::shared_ptr<RuntimeOriginControl>>;
    const auto generation = findGeneration(id);
    if (!generation)
        return Output::failure(originError(ErrorCode::InvalidArgument, "unknown runtime generation"));
    const auto& plugins = generation->data.manifest.plugins;
    if (std::find(plugins.begin(), plugins.end(), plugin) == plugins.end())
        return Output::failure(originError(ErrorCode::InvalidArgument, "unknown runtime plugin"));
    auto lease = generation->ledger.acquire(ResourceKind::ExternalHandle, "runtime.definition." + plugin);
    if (!lease.hasValue())
        return Output::failure(lease.error());
    return Output::success(
        std::make_shared<RuntimeOriginControl>(generation, std::move(plugin), std::move(lease.value())));
}
} // namespace detail
Result<std::optional<ModuleRuntimeOrigin>> ModuleManager::runtimeOrigin(RuntimeId runtime) const
{
    using Output = Result<std::optional<ModuleRuntimeOrigin>>;
    if (std::this_thread::get_id() != owner_)
        return Output::failure(originError(ErrorCode::WrongThread, "runtime origin requires manager owner"));
    if (!runtime.valid())
        return Output::failure(originError(ErrorCode::InvalidArgument, "invalid runtime identity"));
    for (const auto& generation : detail::allGenerations()) {
        std::lock_guard<std::mutex> lock(generation->mutex);
        const auto at = generation->runtimeOrigins.find(runtime);
        if (at != generation->runtimeOrigins.end())
            return Output::success(ModuleRuntimeOrigin{runtime, generation->data.generation,
                                                       generation->data.manifest.moduleId, at->second});
    }
    return Output::success(std::nullopt);
}
} // namespace cordis::module
