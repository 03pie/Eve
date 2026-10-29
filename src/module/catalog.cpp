/** @file catalog.cpp
 * @brief 共享目录、代次保留及同一 Windows 映像的受限映射；不调用后端握手。
 */
#include "catalog_p.hpp"

namespace cordis::module::detail {
namespace {
ErrorInfo error(ErrorCode code, std::string phase, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = std::move(phase);
    return value;
}
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
QString imagePath(HMODULE image)
{
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(image, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), static_cast<int>(length));
}
} // namespace
Generation::Generation(const PreflightArtifact& artifact, std::unique_ptr<OpenedPath> file)
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
ModuleGenerationSnapshot Generation::copy()
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
Result<ModuleLoadReport> failGeneration(const std::shared_ptr<Generation>& generation, ErrorInfo failure)
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
std::shared_ptr<Generation> findGeneration(GenerationId id)
{
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    for (const auto& value : catalog.generations)
        if (value->data.generation == id)
            return value;
    return {};
}
std::vector<std::shared_ptr<Generation>> allGenerations()
{
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    return catalog.generations;
}
Result<GenerationReservation> reserveGeneration(VerifiedSource& source)
{
    const auto& verified = source.artifact;
    auto& artifact = source.library;
    std::shared_ptr<Generation> generation;
    bool created = false;
    {
        auto& catalog = registry();
        std::lock_guard<std::mutex> lock(catalog.mutex);
        for (const auto& item : catalog.generations) {
            if (item->artifactFile->sameFile(*artifact) && item->data.manifest.moduleId != verified.manifest().moduleId)
                return Result<GenerationReservation>::failure(error(
                    ErrorCode::AbiMismatch, "module.identity", "artifact already belongs to another module identity"));
            if (item->data.manifest.moduleId == verified.manifest().moduleId)
                generation = item;
        }
        if (!generation) {
            if (catalog.generations.size() >= 64)
                return Result<GenerationReservation>::failure(
                    error(ErrorCode::ModulePinned, "module.catalog", "resident module catalog budget exhausted"));
            generation = std::make_shared<Generation>(verified, std::move(artifact));
            catalog.generations.push_back(generation);
            created = true;
        }
    }
    if (!created) {
        std::lock_guard<std::mutex> lock(generation->mutex);
        if (!sameManifest(generation->data.manifest, verified.manifest()))
            return Result<GenerationReservation>::failure(
                error(ErrorCode::ModulePinned, "module.identity",
                      "different content/version/catalog already owns this module ID"));
        if (generation->loading)
            return Result<GenerationReservation>::failure(
                error(ErrorCode::ModuleLoad, "module.load", "generation load is already in progress"));
        if (generation->data.failure)
            return Result<GenerationReservation>::failure(*generation->data.failure);
        {
            std::lock_guard<std::mutex> signalLock(generation->businessSignal->mutex);
            if (generation->businessSignal->error)
                return Result<GenerationReservation>::failure(*generation->businessSignal->error);
        }
        if (generation->data.state != ModuleState::Loaded && generation->data.state != ModuleState::Serving)
            return Result<GenerationReservation>::failure(
                error(ErrorCode::ModulePinned, "module.load", "generation no longer accepts activation references"));
        return Result<GenerationReservation>::success({generation, false});
    }
    return Result<GenerationReservation>::success({generation, true});
}
Result<void> mapGenerationImage(const std::shared_ptr<Generation>& generation)
{
    const auto& verified = generation->data;
    const auto nativePath = QDir::toNativeSeparators(QString::fromUtf8(verified.libraryPath.c_str())).toStdWString();
    if (GetModuleHandleW(nativePath.c_str()))
        return Result<void>::failure(error(ErrorCode::ModulePinned, "module.existing",
                                           "untracked native image is already mapped; adoption refused"));
    const auto image =
        LoadLibraryExW(nativePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!image)
        return Result<void>::failure(error(ErrorCode::ModuleLoad, "module.load",
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
        return Result<void>::failure(error(ErrorCode::ModulePinned, "module.pin",
                                           "native process pin failed; original loader reference retained"));
    {
        std::lock_guard<std::mutex> lock(generation->mutex);
        generation->data.nativePinned = true;
    }
    (void)generation->ledger.pin("nativeProcessPin");
    const OpenedPath actual(imagePath(image), false);
    if (!generation->artifactFile->sameFile(actual) ||
        actual.finalPath().toUtf8().toStdString() != verified.libraryPath)
        return Result<void>::failure(
            error(ErrorCode::ModuleLoad, "module.image", "loader mapped a different artifact"));
    return Result<void>::success();
}
} // namespace cordis::module::detail
