/** @file catalog.cpp
 * @brief 共享目录、代次保留及同一 Windows 映像的受限映射；不调用后端握手。
 */
#include "catalog_p.hpp"
#include <algorithm>

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
enum class ReusePurpose { Discovery, Staging };
/** @brief 只读内部状态检查，调用方持 generation.mutex，不调用模块代码。 */
Result<void> reusable(const Generation& generation, ReusePurpose purpose)
{
    if (generation.loading)
        return Result<void>::failure(
            error(ErrorCode::ModuleLoad, "module.load", "generation load is already in progress"));
    if (generation.data.failure)
        return Result<void>::failure(*generation.data.failure);
    if (purpose == ReusePurpose::Staging && generation.data.businessFailure)
        return Result<void>::failure(*generation.data.businessFailure);
    {
        std::lock_guard<std::mutex> signalLock(generation.businessSignal->mutex);
        if (generation.businessSignal->error)
            return Result<void>::failure(*generation.businessSignal->error);
    }
    if (generation.data.state != ModuleState::Loaded && generation.data.state != ModuleState::Serving)
        return Result<void>::failure(
            error(ErrorCode::ModulePinned, "module.load", "generation no longer accepts activation references"));
    return Result<void>::success();
}
bool sameCatalog(const ModuleManifest& left, const ModuleManifest& right)
{
    auto a = left.plugins, b = right.plugins;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return left.moduleId == right.moduleId && left.loader == right.loader && a == b;
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
Result<GenerationReservation> reserveGeneration(VerifiedSource& source, GenerationId predecessor)
{
    using Output = Result<GenerationReservation>;
    const auto& verified = source.artifact;
    auto& artifact = source.library;
    auto& catalog = registry();
    // One lock orders reservations across both backends and every manager. No external code runs here.
    std::lock_guard<std::mutex> lock(catalog.mutex);
    std::shared_ptr<Generation> current, matching;
    for (const auto& item : catalog.generations) {
        std::lock_guard<std::mutex> itemLock(item->mutex);
        if (item->artifactFile->sameFile(*artifact)) {
            if (item->data.manifest.moduleId != verified.manifest().moduleId)
                return Output::failure(error(ErrorCode::AbiMismatch, "module.identity",
                                             "artifact already belongs to another module identity"));
            if (!sameManifest(item->data.manifest, verified.manifest()))
                return Output::failure(error(ErrorCode::ModulePinned, "module.identity",
                                             "one physical artifact cannot represent different generations"));
        }
        if (item->data.manifest.moduleId != verified.manifest().moduleId)
            continue;
        if (item->data.role == GenerationRole::Current)
            current = item;
        if (sameManifest(item->data.manifest, verified.manifest()))
            matching = item;
    }
    if (predecessor.valid()) {
        if (!current || current->data.generation != predecessor)
            return Output::failure(error(ErrorCode::ConfigConflict, "module.stage",
                                         "candidate predecessor is not the current generation of this module"));
        std::lock_guard<std::mutex> currentLock(current->mutex);
        auto available = reusable(*current, ReusePurpose::Staging);
        if (!available.hasValue())
            return Output::failure(available.error());
        if (!sameCatalog(current->data.manifest, verified.manifest()))
            return Output::failure(error(ErrorCode::AbiMismatch, "module.stage",
                                         "candidate backend or plugin catalog differs from current generation"));
        if (sameManifest(current->data.manifest, verified.manifest()))
            return Output::failure(
                error(ErrorCode::InvalidArgument, "module.stage", "candidate must differ from current generation"));
    } else if (current) {
        // Ordinary load continues to name only the current generation, even after candidates exist.
        std::lock_guard<std::mutex> currentLock(current->mutex);
        if (!sameManifest(current->data.manifest, verified.manifest()))
            return Output::failure(error(ErrorCode::ModulePinned, "module.identity",
                                         "different content/version/catalog already owns this module ID"));
        auto available = reusable(*current, ReusePurpose::Discovery);
        return available.hasValue() ? Output::success({current, false}) : Output::failure(available.error());
    }
    if (matching) {
        std::lock_guard<std::mutex> matchLock(matching->mutex);
        if (matching->data.role != GenerationRole::Candidate || matching->data.predecessor != predecessor)
            return Output::failure(error(ErrorCode::ConfigConflict, "module.stage",
                                         "existing content belongs to another generation lineage"));
        auto available = reusable(*matching, ReusePurpose::Staging);
        return available.hasValue() ? Output::success({matching, false}) : Output::failure(available.error());
    }
    if (catalog.generations.size() >= 64)
        return Output::failure(
            error(ErrorCode::ModulePinned, "module.catalog", "resident module catalog budget exhausted"));
    auto generation = std::make_shared<Generation>(verified, std::move(artifact));
    if (predecessor.valid()) {
        generation->data.role = GenerationRole::Candidate;
        generation->data.predecessor = predecessor;
    }
    catalog.generations.push_back(generation);
    return Output::success({std::move(generation), true});
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
