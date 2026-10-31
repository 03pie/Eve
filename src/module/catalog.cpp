/** @file catalog.cpp
 * @brief 共享目录、代次保留及同一 Windows 映像的受限映射；不调用后端握手。
 */
#include "admission_p.hpp"
#include "catalog_p.hpp"
#include <algorithm>
#include <map>
#include <utility>

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
    std::map<std::string, std::shared_ptr<ReplacementReservationControl>> replacements;
};
/** @brief 有界的进程驻留目录；不在静态析构期关闭插件引用或文件保护。 */
Registry& registry()
{
    static Registry* value = new Registry;
    return *value;
}
std::shared_ptr<Generation> findLocked(Registry& catalog, GenerationId id)
{
    for (const auto& generation : catalog.generations)
        if (generation->data.generation == id)
            return generation;
    return {};
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
    if (const auto reserved = replacement.lock(); reserved && !reserved->released.load())
        value.replacement = reserved->snapshot();
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
    if (predecessor.valid() && catalog.replacements.count(verified.manifest().moduleId))
        return Output::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "module is reserved for replacement"));
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
Result<std::shared_ptr<ReplacementReservationControl>> reserveReplacementPair(GenerationId currentId,
                                                                              GenerationId candidateId)
{
    using Output = Result<std::shared_ptr<ReplacementReservationControl>>;
    if (!currentId.valid() || !candidateId.valid() || currentId == candidateId)
        return Output::failure(
            error(ErrorCode::InvalidArgument, "module.replacement", "replacement requires distinct generations"));
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto current = findLocked(catalog, currentId), candidate = findLocked(catalog, candidateId);
    if (!current || !candidate)
        return Output::failure(error(ErrorCode::InvalidArgument, "module.replacement", "unknown generation"));
    std::scoped_lock generations(current->mutex, candidate->mutex);
    if (current->data.role != GenerationRole::Current || candidate->data.role != GenerationRole::Candidate ||
        candidate->data.predecessor != currentId || !sameCatalog(current->data.manifest, candidate->data.manifest))
        return Output::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "pair is not a current/candidate lineage"));
    if (catalog.replacements.count(current->data.manifest.moduleId) || current->admissions || candidate->admissions)
        return Output::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "module is reserved or creation is in progress"));
    for (const auto& generation : {current, candidate}) {
        auto ready = reusable(*generation, ReusePurpose::Staging);
        if (!ready.hasValue())
            return Output::failure(ready.error());
        const auto resources = generation->ledger.snapshot();
        if (!resources.accepting)
            return Output::failure(error(ErrorCode::ModulePinned, "module.replacement", "generation is draining"));
        if (generation == candidate &&
            std::any_of(resources.records.begin(), resources.records.end(),
                        [](const auto& record) { return record.kind == ResourceKind::Instance; }))
            return Output::failure(
                error(ErrorCode::ConfigConflict, "module.replacement", "candidate already owns business instances"));
    }
    for (const auto& generation : catalog.generations)
        if (generation != current && generation != candidate &&
            generation->data.manifest.moduleId == current->data.manifest.moduleId) {
            std::lock_guard<std::mutex> member(generation->mutex);
            if (generation->loading || generation->admissions)
                return Output::failure(error(ErrorCode::ConfigConflict, "module.replacement",
                                             "another generation of this module is entering"));
        }
    auto control = std::make_shared<ReplacementReservationControl>();
    control->moduleId = current->data.manifest.moduleId;
    control->current = currentId;
    control->candidate = candidateId;
    catalog.replacements.emplace(control->moduleId, control);
    for (const auto& generation : catalog.generations)
        if (generation->data.manifest.moduleId == control->moduleId) {
            // 当前、候选已持锁；所有兄弟代也参加同模块的准入与诊断。
            if (generation == current || generation == candidate)
                generation->replacement = control;
            else {
                std::lock_guard<std::mutex> member(generation->mutex);
                generation->replacement = control;
            }
        }
    return Output::success(std::move(control));
}
void releaseReplacementPair(const std::shared_ptr<ReplacementReservationControl>& control) noexcept
{
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto found = catalog.replacements.find(control->moduleId);
    if (found == catalog.replacements.end() || found->second != control)
        return;
    control->released.store(true);
    for (const auto& generation : catalog.generations)
        if (generation->data.manifest.moduleId == control->moduleId) {
            std::lock_guard<std::mutex> member(generation->mutex);
            if (generation->replacement.lock() == control)
                generation->replacement.reset();
        }
    catalog.replacements.erase(found);
}
Result<GenerationAdmission> admitGeneration(GenerationId id)
{
    using Output = Result<GenerationAdmission>;
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto generation = findLocked(catalog, id);
    if (!generation)
        return Output::failure(error(ErrorCode::InvalidArgument, "module.admission", "unknown generation"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (catalog.replacements.count(generation->data.manifest.moduleId))
        return Output::failure(error(ErrorCode::ConfigConflict, "module.replacement", "module creation is reserved"));
    auto ready = reusable(*generation, ReusePurpose::Staging);
    if (!ready.hasValue())
        return Output::failure(ready.error());
    if (!generation->ledger.snapshot().accepting)
        return Output::failure(error(ErrorCode::ModulePinned, "module.admission", "generation is draining"));
    ++generation->admissions;
    return Output::success(GenerationAdmission(generation));
}
void GenerationAdmission::reset() noexcept
{
    if (!generation_)
        return;
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    std::lock_guard<std::mutex> member(generation_->mutex);
    --generation_->admissions;
    generation_.reset();
}
GenerationAdmission& GenerationAdmission::operator=(GenerationAdmission&& other) noexcept
{
    if (this != &other) {
        reset();
        generation_ = std::move(other.generation_);
    }
    return *this;
}
GenerationAdmission::~GenerationAdmission()
{
    reset();
}
Result<void> drainGeneration(GenerationId id)
{
    auto& catalog = registry();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto generation = findLocked(catalog, id);
    if (!generation)
        return Result<void>::failure(error(ErrorCode::InvalidArgument, "module.drain", "unknown generation ID"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (catalog.replacements.count(generation->data.manifest.moduleId))
        return Result<void>::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "reserved generation cannot drain"));
    if (generation->loading || generation->admissions)
        return Result<void>::failure(
            error(ErrorCode::ContextClosing, "module.drain", "module is negotiating or creating"));
    if (!generation->ledger.snapshot().records.empty())
        return Result<void>::failure(
            error(ErrorCode::ContextClosing, "module.drain", "close sessions and resources before draining"));
    if (generation->data.state == ModuleState::Loaded || generation->data.state == ModuleState::Serving)
        generation->data.state = ModuleState::Draining;
    generation->ledger.beginDraining();
    if (generation->data.state == ModuleState::Draining && generation->ledger.snapshot().drained())
        generation->data.state = ModuleState::Resident;
    return Result<void>::success();
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
