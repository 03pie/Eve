/** @file manager.cpp
 * @brief 固定产物、受限搜索、只读 C 握手及进程级默认驻留；不创建业务实例。
 */
#include "catalog_p.hpp"
#include "native_source_p.hpp"
#include <algorithm>
#include <cordis/module/manager.hpp>
#include <cordis_abi/module_query.h>
#include <cstring>
#include <set>

namespace cordis::module {
namespace {
using namespace detail;
ErrorInfo error(ErrorCode code, std::string phase, std::string message)
{
    auto result = makeError(code, std::move(message));
    result.phase = std::move(phase);
    return result;
}
ErrorInfo ownerError()
{
    return error(ErrorCode::WrongThread, "module.owner", "module manager called outside its owner thread");
}
/** @brief 仅复制有界借用值，不保留 DLL 字符串或数组地址。模块属于同进程可信代码。 */
bool matches(CordisQueryBytesV1 bytes, const std::string& expected, std::uint64_t maximum)
{
    return bytes.data && bytes.size > 0 && bytes.size <= maximum && bytes.size == expected.size() &&
           std::memcmp(bytes.data, expected.data(), expected.size()) == 0;
}
Result<void> describe(HMODULE image, const ModuleManifest& manifest)
{
    const auto address = GetProcAddress(image, "cordis_module_query_v1");
    if (!address)
        return Result<void>::failure(
            error(ErrorCode::AbiMismatch, "module.query", "missing cordis_module_query_v1 export"));
    CordisModuleQueryFnV1 query = nullptr;
    static_assert(sizeof(query) == sizeof(address), "Windows function pointer layout");
    std::memcpy(&query, &address, sizeof(query));
    CordisModuleQueryV1 request{{sizeof(request), CORDIS_MODULE_QUERY_MAJOR, CORDIS_MODULE_QUERY_MINOR, 0}, 1, 0, 0};
    CordisModuleDescriptorV1 descriptor{};
    descriptor.header.struct_size = sizeof(descriptor);
    CordisQueryStatus status;
    try {
        status = query(&request, &descriptor);
    } catch (...) {
        return Result<void>::failure(
            error(ErrorCode::AbiProtocol, "module.query", "exception escaped module discovery boundary"));
    }
    if (status != CORDIS_QUERY_OK)
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "module.query",
                                           "module query rejected discovery, status " + std::to_string(status)));
    if (descriptor.header.struct_size < sizeof(descriptor) ||
        descriptor.header.query_major != CORDIS_MODULE_QUERY_MAJOR ||
        descriptor.header.query_minor != CORDIS_MODULE_QUERY_MINOR || descriptor.header.reserved != 0 ||
        descriptor.runtime_abi_major != 1 || descriptor.runtime_abi_minor != 0 || descriptor.reserved != 0 ||
        descriptor.flags != CORDIS_MODULE_RESIDENT)
        return Result<void>::failure(
            error(ErrorCode::AbiMismatch, "module.descriptor", "incompatible discovery descriptor header or policy"));
    if (!matches(descriptor.module_id, manifest.moduleId, 128) ||
        !matches(descriptor.module_version, manifest.moduleVersion, 64) ||
        descriptor.plugin_count != manifest.plugins.size() || !descriptor.plugins)
        return Result<void>::failure(error(ErrorCode::AbiMismatch, "module.descriptor",
                                           "module identity/version/catalog differs from manifest"));
    std::set<std::string> remaining(manifest.plugins.begin(), manifest.plugins.end());
    for (std::uint32_t i = 0; i < descriptor.plugin_count; ++i) {
        const auto bytes = descriptor.plugins[i];
        if (!bytes.data || bytes.size == 0 || bytes.size > 128)
            return Result<void>::failure(
                error(ErrorCode::AbiMismatch, "module.descriptor", "invalid plugin identifier span"));
        if (remaining.erase(
                std::string(reinterpret_cast<const char*>(bytes.data), static_cast<std::size_t>(bytes.size))) != 1)
            return Result<void>::failure(
                error(ErrorCode::AbiMismatch, "module.descriptor", "unknown or duplicate plugin identifier"));
    }
    return Result<void>::success();
}
} // namespace

ModuleManager::ModuleManager() : owner_(std::this_thread::get_id()) {}
Result<ModuleLoadReport> ModuleManager::load(const std::string& pluginRoot, const std::string& manifestPath,
                                             PreflightLimits limits)
{
    if (std::this_thread::get_id() != owner_)
        return Result<ModuleLoadReport>::failure(ownerError());
    auto source = detail::verifyModuleSource(pluginRoot, manifestPath, limits);
    if (!source.hasValue())
        return Result<ModuleLoadReport>::failure(source.error());
    const auto& verified = source.value()->artifact;
    if (verified.manifest().loader != ModuleLoader::CAbi)
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::AbiMismatch, "module.loader", "C ABI backend requires c-abi loader"));
    auto reserved = reserveGeneration(*source.value());
    if (!reserved.hasValue())
        return Result<ModuleLoadReport>::failure(reserved.error());
    const auto generation = reserved.value().generation;
    if (!reserved.value().created)
        return Result<ModuleLoadReport>::success({generation->data.generation, true});
    auto mapped = mapGenerationImage(generation);
    if (!mapped.hasValue())
        return failGeneration(generation, mapped.error());
    auto lease = generation->ledger.acquire(ResourceKind::Call, "module.discovery");
    if (!lease.hasValue())
        return failGeneration(generation, lease.error());
    auto described = describe(generation->image, verified.manifest());
    lease.value().reset();
    if (!described.hasValue())
        return failGeneration(generation, described.error());
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.state = ModuleState::Loaded;
        generation->data.descriptorAccepted = true;
        generation->loading = false;
    }
    return Result<ModuleLoadReport>::success({generation->data.generation, false});
}
Result<std::vector<ModuleGenerationSnapshot>> ModuleManager::snapshot() const
{
    if (std::this_thread::get_id() != owner_)
        return Result<std::vector<ModuleGenerationSnapshot>>::failure(ownerError());
    const auto generations = allGenerations();
    std::vector<ModuleGenerationSnapshot> values;
    values.reserve(generations.size());
    for (const auto& generation : generations)
        values.push_back(generation->copy());
    return Result<std::vector<ModuleGenerationSnapshot>>::success(std::move(values));
}
Result<void> ModuleManager::drain(GenerationId id)
{
    if (std::this_thread::get_id() != owner_)
        return Result<void>::failure(ownerError());
    const auto generation = findGeneration(id);
    if (!generation)
        return Result<void>::failure(error(ErrorCode::InvalidArgument, "module.drain", "unknown generation ID"));
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        if (generation->loading)
            return Result<void>::failure(
                error(ErrorCode::ContextClosing, "module.drain", "module is still negotiating"));
        if (!generation->ledger.snapshot().records.empty())
            return Result<void>::failure(error(ErrorCode::ContextClosing, "module.drain",
                                               "close native sessions and resources before draining generation"));
        if (generation->data.state == ModuleState::Loaded || generation->data.state == ModuleState::Serving)
            generation->data.state = ModuleState::Draining;
    }
    generation->ledger.beginDraining();
    (void)generation->copy();
    return Result<void>::success();
}
} // namespace cordis::module

namespace cordis::module::detail {
Result<NativeSource> acquireNativeSource(GenerationId id)
{
    const auto generation = findGeneration(id);
    if (!generation)
        return Result<NativeSource>::failure(
            error(ErrorCode::InvalidArgument, "module.business", "unknown generation"));
    std::lock_guard<std::mutex> lock(generation->mutex);
    if (generation->data.manifest.loader != ModuleLoader::CAbi)
        return Result<NativeSource>::failure(
            error(ErrorCode::AbiMismatch, "module.business", "native business backend requires c-abi loader"));
    if (generation->loading ||
        (generation->data.state != ModuleState::Loaded && generation->data.state != ModuleState::Serving) ||
        generation->data.failure)
        return Result<NativeSource>::failure(
            error(ErrorCode::ModulePinned, "module.business", "generation is not available"));
    if (generation->data.businessFailure)
        return Result<NativeSource>::failure(*generation->data.businessFailure);
    {
        std::lock_guard<std::mutex> signalLock(generation->businessSignal->mutex);
        if (generation->businessSignal->error)
            return Result<NativeSource>::failure(*generation->businessSignal->error);
    }
    auto session = generation->ledger.acquire(ResourceKind::ExternalHandle, "native.host");
    if (!session.hasValue())
        return Result<NativeSource>::failure(session.error());
    const auto address = GetProcAddress(generation->image, "cordis_query_v1");
    CordisQueryFnV1 query = nullptr;
    static_assert(sizeof(query) == sizeof(address), "business entry pointer");
    std::memcpy(&query, &address, sizeof(query));
    return Result<NativeSource>::success({id, generation->data.manifest, generation->ledger, generation->businessSignal,
                                          query, std::move(session.value())});
}
void recordNativeBusiness(GenerationId id, const std::optional<ErrorInfo>& failure)
{
    const auto generation = findGeneration(id);
    if (!generation)
        return;
    std::lock_guard<std::mutex> lock(generation->mutex);
    generation->data.businessAccepted = !failure;
    generation->data.businessFailure = failure;
    if (failure)
        (void)generation->ledger.pin("businessRejected");
}
void recordNativePhase(GenerationId id, ModuleState phase, const std::optional<ErrorInfo>& failure)
{
    const auto generation = findGeneration(id);
    if (!generation)
        return;
    std::lock_guard<std::mutex> lock(generation->mutex);
    if (generation->data.state == ModuleState::Resident || generation->data.state == ModuleState::Failed)
        return;
    {
        std::lock_guard<std::mutex> signalLock(generation->businessSignal->mutex);
        if (generation->businessSignal->error) {
            generation->data.state = ModuleState::Failed;
            generation->data.businessFailure = generation->businessSignal->error;
            return;
        }
    }
    generation->data.state = phase;
    if (failure)
        generation->data.businessFailure = failure;
}
} // namespace cordis::module::detail
