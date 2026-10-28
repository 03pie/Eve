/** @file manager.cpp
 * @brief 固定产物、受限搜索、只读 C 握手及进程级默认驻留；不创建业务实例。
 */
#include "native_file_p.hpp"
#include "native_source_p.hpp"
#include <QFileInfo>
#include <algorithm>
#include <cordis/module/manager.hpp>
#include <cordis_abi/module_query.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

namespace cordis::module {
namespace {
using detail::OpenedPath;
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
bool sameManifest(const ModuleManifest& left, const ModuleManifest& right)
{
    auto a = left.plugins, b = right.plugins;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return left.moduleId == right.moduleId && left.moduleVersion == right.moduleVersion &&
           left.sha256 == right.sha256 && a == b;
}
/** @brief 只含宿主对象；注册后驻留至进程退出，不在管理器析构中销毁原生映射。 */
struct Generation {
    Generation(const PreflightArtifact& artifact, std::unique_ptr<OpenedPath> file)
        : data{ledger.snapshot().generation,
               ModuleState::Validated,
               artifact.manifest(),
               artifact.manifestPath(),
               artifact.libraryPath(),
               false,
               false,
               false,
               {},
               ledger.snapshot()},
          artifactFile(std::move(file))
    {
        (void)ledger.pin("residentPolicy");
    }
    ModuleGenerationSnapshot copy()
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto resources = ledger.snapshot();
        if (data.state == ModuleState::Draining && resources.drained())
            data.state = ModuleState::Resident;
        auto value = data;
        value.resources = std::move(resources);
        {
            std::lock_guard<std::mutex> signalLock(businessSignal->mutex);
            if (businessSignal->error) {
                value.state = ModuleState::Failed;
                value.businessFailure = businessSignal->error;
            }
        }
        return value;
    }
    GenerationLedger ledger;
    std::shared_ptr<detail::NativeFailureSignal> businessSignal = std::make_shared<detail::NativeFailureSignal>();
    std::mutex mutex;
    ModuleGenerationSnapshot data;
    const std::unique_ptr<OpenedPath> artifactFile;
    bool loading = true;
    HMODULE image = nullptr;
};
struct Registry {
    std::mutex mutex;
    std::vector<std::shared_ptr<Generation>> generations;
};
/** @brief 有界的进程驻留目录；不在静态析构期关闭插件引用或文件保护。 */
Registry& registry()
{
    static Registry* value = new Registry;
    return *value;
}
Result<ModuleLoadReport> failed(const std::shared_ptr<Generation>& generation, ErrorInfo failure)
{
    (void)generation->ledger.pin("loadRejected");
    generation->ledger.beginDraining();
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.state = ModuleState::Failed;
        generation->data.failure = failure;
        generation->loading = false;
    }
    return Result<ModuleLoadReport>::failure(std::move(failure));
}
/** @brief 从卷根向下固定每个祖先目录，只在装载期间持有，禁止路径解析时替换祖先。 */
bool protectParents(const QString& file, std::vector<std::unique_ptr<OpenedPath>>& guards)
{
    std::vector<QString> paths;
    auto current = QFileInfo(file).absolutePath();
    for (;;) {
        if (paths.size() >= 128)
            return false;
        paths.push_back(current);
        QDir parent(current);
        if (parent.isRoot())
            break;
        if (!parent.cdUp() || parent.absolutePath() == current)
            return false;
        current = parent.absolutePath();
    }
    for (auto at = paths.rbegin(); at != paths.rend(); ++at) {
        auto guard = std::make_unique<OpenedPath>(*at, true);
        if (!guard->valid() || guard->finalPath() != *at)
            return false;
        guards.push_back(std::move(guard));
    }
    return true;
}
QString imagePath(HMODULE image)
{
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(image, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), static_cast<int>(length));
}
/** @brief 不把改名为 .dll 的 EXE 当作库查询，先验证 AMD64/PE32+ 与 DLL 特征。 */
bool compatibleImage(const OpenedPath& artifact)
{
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return artifact.readAt(0, reinterpret_cast<char*>(&dos), sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
           dos.e_lfanew >= static_cast<LONG>(sizeof(dos)) &&
           artifact.readAt(static_cast<std::uint64_t>(dos.e_lfanew), reinterpret_cast<char*>(&nt), sizeof(nt)) &&
           nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0 &&
           nt.FileHeader.SizeOfOptionalHeader >= sizeof(nt.OptionalHeader) &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
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
    auto first = preflightModule(pluginRoot, manifestPath, limits);
    if (!first.hasValue())
        return Result<ModuleLoadReport>::failure(first.error());
    const auto& initial = first.value();
    auto artifact = std::make_unique<OpenedPath>(QString::fromUtf8(initial.libraryPath().c_str()), false);
    const OpenedPath manifestGuard(QString::fromUtf8(initial.manifestPath().c_str()), false);
    std::vector<std::unique_ptr<OpenedPath>> directories;
    if (!artifact->valid() || !manifestGuard.valid() ||
        !protectParents(QString::fromUtf8(initial.libraryPath().c_str()), directories) ||
        !protectParents(QString::fromUtf8(initial.manifestPath().c_str()), directories))
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::ModuleLoad, "module.source", "cannot protect module source or ancestor directories"));
    auto checked = preflightModule(pluginRoot, manifestPath, limits);
    if (!checked.hasValue())
        return Result<ModuleLoadReport>::failure(checked.error());
    const auto& verified = checked.value();
    if (initial.manifestPath() != verified.manifestPath() || initial.libraryPath() != verified.libraryPath() ||
        !sameManifest(initial.manifest(), verified.manifest()) ||
        artifact->finalPath().toUtf8().toStdString() != verified.libraryPath() ||
        manifestGuard.finalPath().toUtf8().toStdString() != verified.manifestPath())
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::ModuleLoad, "module.source", "source changed while acquiring load guards"));
    if (!artifact->sameFile(*artifact))
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::ModuleLoad, "module.source", "stable 128-bit file identity is unavailable"));
    if (!compatibleImage(*artifact))
        return Result<ModuleLoadReport>::failure(
            error(ErrorCode::ModuleLoad, "module.image", "artifact is not a supported AMD64 PE32+ DLL"));
    std::shared_ptr<Generation> generation;
    bool created = false;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& item : catalog.generations) {
            if (item->artifactFile->sameFile(*artifact) && item->data.manifest.moduleId != verified.manifest().moduleId)
                return Result<ModuleLoadReport>::failure(error(ErrorCode::AbiMismatch, "module.identity",
                                                               "artifact already belongs to another module identity"));
            if (item->data.manifest.moduleId == verified.manifest().moduleId)
                generation = item;
        }
        if (!generation) {
            if (catalog.generations.size() >= 64)
                return Result<ModuleLoadReport>::failure(
                    error(ErrorCode::ModulePinned, "module.catalog", "resident module catalog budget exhausted"));
            generation = std::make_shared<Generation>(verified, std::move(artifact));
            catalog.generations.push_back(generation);
            created = true;
        }
    }
    if (!created) {
        std::lock_guard<std::mutex> lock(generation->mutex);
        if (!sameManifest(generation->data.manifest, verified.manifest()))
            return Result<ModuleLoadReport>::failure(
                error(ErrorCode::ModulePinned, "module.identity",
                      "different content/version/catalog already owns this module ID"));
        if (generation->loading)
            return Result<ModuleLoadReport>::failure(
                error(ErrorCode::ModuleLoad, "module.load", "generation load is already in progress"));
        if (generation->data.failure)
            return Result<ModuleLoadReport>::failure(*generation->data.failure);
        {
            std::lock_guard<std::mutex> signalLock(generation->businessSignal->mutex);
            if (generation->businessSignal->error)
                return Result<ModuleLoadReport>::failure(*generation->businessSignal->error);
        }
        if (generation->data.state != ModuleState::Loaded && generation->data.state != ModuleState::Serving)
            return Result<ModuleLoadReport>::failure(
                error(ErrorCode::ModulePinned, "module.load", "generation no longer accepts activation references"));
        return Result<ModuleLoadReport>::success({generation->data.generation, true});
    }
    // 外部加载器、静态初始化及模块 query 都在宿主目录锁之外运行。
    const auto nativePath = QDir::toNativeSeparators(QString::fromUtf8(verified.libraryPath().c_str())).toStdWString();
    if (GetModuleHandleW(nativePath.c_str()))
        return failed(generation, error(ErrorCode::ModulePinned, "module.existing",
                                        "untracked native image is already mapped; adoption refused"));
    const auto image =
        LoadLibraryExW(nativePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!image)
        return failed(generation, error(ErrorCode::ModuleLoad, "module.load",
                                        "LoadLibraryExW failed, Windows error " + std::to_string(GetLastError())));
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->image = image;
        generation->data.nativeMapped = true;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(image), &pinned) ||
        pinned != image)
        return failed(generation, error(ErrorCode::ModulePinned, "module.pin",
                                        "native process pin failed; original loader reference retained"));
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.nativePinned = true;
    }
    (void)generation->ledger.pin("nativeProcessPin");
    const OpenedPath actual(imagePath(image), false);
    if (!generation->artifactFile->sameFile(actual) ||
        actual.finalPath().toUtf8().toStdString() != verified.libraryPath())
        return failed(generation, error(ErrorCode::ModuleLoad, "module.image", "loader mapped a different artifact"));
    auto lease = generation->ledger.acquire(ResourceKind::Call, "module.discovery");
    if (!lease.hasValue())
        return failed(generation, lease.error());
    auto described = describe(image, verified.manifest());
    lease.value().reset();
    if (!described.hasValue())
        return failed(generation, described.error());
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
    std::vector<std::shared_ptr<Generation>> generations;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        generations = catalog.generations;
    }
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
    std::shared_ptr<Generation> generation;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& item : catalog.generations)
            if (item->data.generation == id) {
                generation = item;
                break;
            }
    }
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
    std::shared_ptr<Generation> generation;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& value : catalog.generations)
            if (value->data.generation == id) {
                generation = value;
                break;
            }
    }
    if (!generation)
        return Result<NativeSource>::failure(
            error(ErrorCode::InvalidArgument, "module.business", "unknown generation"));
    std::lock_guard<std::mutex> lock(generation->mutex);
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
    std::shared_ptr<Generation> generation;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& value : catalog.generations)
            if (value->data.generation == id) {
                generation = value;
                break;
            }
    }
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
    std::shared_ptr<Generation> generation;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& value : catalog.generations)
            if (value->data.generation == id) {
                generation = value;
                break;
            }
    }
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
