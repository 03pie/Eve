/** @file unload.cpp
 * @brief 原子关闭真实代次准入，锁外退役缓存、消费拥有引用并验证映像离开。
 */
#include "catalog_registry_p.hpp"
#include "unload_p.hpp"
#include <algorithm>
#include <utility>

namespace cordis::module::detail {
namespace {
ErrorInfo failure(ErrorCode code, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = "module.unload";
    return value;
}
Result<std::shared_ptr<Generation>> prepare(GenerationId id)
{
    using Output = Result<std::shared_ptr<Generation>>;
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    auto generation = catalogGenerationLocked(catalog, id);
    if (!generation)
        return Output::failure(failure(ErrorCode::InvalidArgument, "unknown generation ID"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (generation->ownerThread != std::this_thread::get_id() || generation->ownerIdentity != moduleThreadIdentity())
        return Output::failure(failure(ErrorCode::WrongThread, "physical unload requires the first image owner"));
    if (generation->data.state == ModuleState::Unloaded)
        return Output::success(std::move(generation));
    if (generation->policy != ModuleLoadPolicy::ApprovedNative ||
        generation->data.manifest.loader != ModuleLoader::CAbi)
        return Output::failure(failure(ErrorCode::ModulePinned, "generation is permanently resident by policy"));
    const auto& module = generation->data.manifest.moduleId;
    const auto readers = catalog.readers.find(module);
    if (catalog.replacements.count(module) || (readers != catalog.readers.end() && !readers->second.empty()))
        return Output::failure(failure(ErrorCode::ConfigConflict, "generation is retained by a replacement"));
    if (generation->loading || generation->admissions || generation->data.state == ModuleState::Unloading ||
        !generation->runtimeOrigins.empty() || !generation->instanceOrigins.empty())
        return Output::failure(failure(ErrorCode::ContextClosing, "generation has active admissions or sources"));
    if (!generation->data.descriptorAccepted || generation->data.failure || generation->data.businessFailure ||
        !generation->image || !generation->artifactFile || !generation->data.nativeMapped ||
        generation->observationImage)
        return Output::failure(failure(ErrorCode::ModulePinned, "generation has no healthy owned native image"));
    {
        std::lock_guard<std::mutex> signal(generation->businessSignal->mutex);
        if (generation->businessSignal->error)
            return Output::failure(*generation->businessSignal->error);
    }
    auto sealed = generation->sealEmpty(true);
    if (!sealed.hasValue())
        return Output::failure(sealed.error());
    generation->data.state = ModuleState::Unloading;
    generation->data.unloadFailure.reset();
    return Output::success(std::move(generation));
}
Result<void> retainFailure(const std::shared_ptr<Generation>& generation, ErrorInfo error, const char* pin = nullptr,
                           HMODULE image = nullptr, HMODULE observation = nullptr)
{
    std::lock_guard<std::mutex> lock(generation->mutex);
    if (pin)
        (void)generation->ledger.pin(pin);
    if (image) {
        generation->image = image;
        generation->data.nativeMapped = true;
    }
    generation->observationImage = observation;
    generation->data.unloadFailure = error;
    generation->data.state = ModuleState::Resident;
    return Result<void>::failure(std::move(error));
}
void publishUnloaded(const std::shared_ptr<Generation>& generation)
{
    std::shared_ptr<BackendResidency> backend;
    std::unique_ptr<OpenedPath> artifact;
    {
        std::lock_guard<std::mutex> member(generation->mutex);
        backend = std::move(generation->backend);
        artifact = std::move(generation->artifactFile);
    }
    // 真实析构/文件关闭期间仍为 Unloading；禁止在内部锁中执行后端析构或
    // CloseHandle。
    backend.reset();
    artifact.reset();
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    {
        std::lock_guard<std::mutex> member(generation->mutex);
        generation->data.state = ModuleState::Unloaded;
        generation->data.role = GenerationRole::Retired;
        generation->data.unloadFailure.reset();
        generation->replacement.reset();
    }
    // 单一容器、单调 ID；仅淘汰最旧纯历史，不触碰失败/不确定代次。
    auto history = std::count_if(catalog.generations.begin(), catalog.generations.end(), [](const auto& item) {
        std::lock_guard<std::mutex> member(item->mutex);
        return item->data.state == ModuleState::Unloaded;
    });
    for (auto at = catalog.generations.begin(); history > 128 && at != catalog.generations.end();) {
        bool unloaded;
        {
            std::lock_guard<std::mutex> member((*at)->mutex);
            unloaded = (*at)->data.state == ModuleState::Unloaded;
        }
        if (unloaded) {
            at = catalog.generations.erase(at);
            --history;
        } else
            ++at;
    }
}
} // namespace

Result<void> unloadGeneration(GenerationId id, const NativeUnloadSystem& system)
{
    if (!system.release || !system.probe || !system.query)
        return Result<void>::failure(failure(ErrorCode::InvalidArgument, "missing native unload system boundary"));
    auto prepared = prepare(id);
    if (!prepared.hasValue())
        return Result<void>::failure(prepared.error());
    const auto generation = std::move(prepared).value();
    HMODULE original;
    std::shared_ptr<BackendResidency> backend;
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        if (generation->data.state == ModuleState::Unloaded)
            return Result<void>::success();
        original = generation->image;
        backend = generation->backend;
    }
    if (backend) {
        auto retired = backend->retire();
        if (!retired.hasValue())
            return retainFailure(generation, retired.error(), "nativeRetirementRejected");
        backend.reset();
    }
    if (!system.release(original)) {
        const auto code = GetLastError();
        return retainFailure(generation, failure(ErrorCode::ModulePinned,
                                                 "FreeLibrary failed; original reference retained, Windows error " +
                                                     std::to_string(code)));
    }
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->image = nullptr;
        generation->data.nativeMapped = false;
    }
    HMODULE probe = nullptr;
    if (system.probe(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(original), &probe)) {
        const OpenedPath actual(mappedModulePath(probe), false);
        if (!actual.sameFile(actual))
            return retainFailure(generation, failure(ErrorCode::ModulePinned, "owned probe identity is uncertain"),
                                 "unknownExternalMapping", nullptr, probe);
        if (generation->artifactFile->sameFile(actual))
            return retainFailure(generation, failure(ErrorCode::ModulePinned, "target image remains externally mapped"),
                                 "unknownExternalMapping", probe);
        // 地址被另一映像复用；仅归还本次 probe，不消费其他装载器的原引用。
        if (!system.release(probe))
            return retainFailure(generation, failure(ErrorCode::ModulePinned, "foreign image probe release failed"),
                                 "unknownExternalMapping", nullptr, probe);
    } else {
        MEMORY_BASIC_INFORMATION region{};
        if (system.query(original, &region, sizeof(region)) != sizeof(region) ||
            (region.Type == MEM_IMAGE && region.AllocationBase == original))
            return retainFailure(generation,
                                 failure(ErrorCode::ModulePinned, "cannot prove target image left its allocation"),
                                 "unknownExternalMapping");
    }
    publishUnloaded(generation);
    return Result<void>::success();
}
} // namespace cordis::module::detail
