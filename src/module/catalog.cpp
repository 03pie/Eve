/** @file catalog.cpp
 * @brief 共享目录、代次保留及同一 Windows 映像的受限映射；不调用后端握手。
 */
#include "admission_p.hpp"
#include "catalog_p.hpp"
#include "catalog_registry_p.hpp"
#include <algorithm>
#include <cordis/instance.hpp>
#include <map>
#include <set>
#include <utility>

namespace cordis::module::detail {
namespace {
ErrorInfo error(ErrorCode code, std::string phase, std::string message)
{
    auto value = makeError(code, std::move(message));
    value.phase = std::move(phase);
    return value;
}
QString imagePath(HMODULE image)
{
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(image, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return QString::fromWCharArray(buffer.data(), static_cast<int>(length));
}
bool sameCatalog(const ModuleManifest& left, const ModuleManifest& right)
{
    auto a = left.plugins, b = right.plugins;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return left.moduleId == right.moduleId && left.loader == right.loader && a == b;
}
/** @brief 调用方持目录锁及目标两代锁；只复制实际闭包工厂的来源，不调用 Core。 */
Result<BorrowedModules> collectBorrowed(ModuleCatalog& catalog, const ReplacementClosure& closure,
                                        const std::string& target)
{
    using Output = Result<BorrowedModules>;
    BorrowedModules borrowed;
    for (const auto& generation : catalog.generations) {
        // manifest 是已固定的宿主值，不在状态/角色切换中修改。
        if (generation->data.manifest.moduleId == target)
            continue;
        std::lock_guard<std::mutex> lock(generation->mutex);
        if (std::none_of(closure.members.begin(), closure.members.end(),
                         [&](const auto& member) { return generation->runtimeOrigins.count(member.runtime) != 0; }))
            continue;
        if (catalog.replacements.count(generation->data.manifest.moduleId))
            return Output::failure(
                error(ErrorCode::ConfigConflict, "module.replacement", "consumer module has an exclusive reservation"));
        auto ready = generationReusable(*generation, ReusePurpose::Staging);
        if (!ready.hasValue())
            return Output::failure(ready.error());
        if (!generation->ledger.snapshot().accepting)
            return Output::failure(
                error(ErrorCode::ModulePinned, "module.replacement", "consumer generation is draining"));
        borrowed[generation->data.manifest.moduleId].push_back({generation->data.generation, true, true});
    }
    return Output::success(std::move(borrowed));
}
bool borrowed(const ModuleCatalog& catalog, const std::string& module)
{
    const auto at = catalog.readers.find(module);
    return at != catalog.readers.end() && !at->second.empty();
}
/** @brief 目录锁和本代锁已持有；实际候选创建可以扩大同一操作的共享使用集合。 */
void retainBorrowed(ModuleCatalog& catalog, const Generation& generation, const ContextSource& source)
{
    if (!source.replacement)
        return;
    const auto owner = std::find_if(catalog.replacements.begin(), catalog.replacements.end(),
                                    [&](const auto& entry) { return entry.second->operation == *source.replacement; });
    if (owner == catalog.replacements.end() || owner->first == generation.data.manifest.moduleId)
        return;
    const auto& control = owner->second;
    if (control->factorySources)
        return; // 完整宿主计划的持有已取得，禁止在实际创建时扩张。
    const auto existing = control->borrowed.find(generation.data.manifest.moduleId);
    if (existing != control->borrowed.end() &&
        std::any_of(existing->second.begin(), existing->second.end(), [&](const auto& entry) {
            return entry.generation == generation.data.generation && entry.required;
        }))
        return;
    auto usage = control->borrowed;
    auto readers = catalog.readers;
    auto& entries = usage[generation.data.manifest.moduleId];
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&](const auto& entry) { return entry.generation == generation.data.generation; });
    if (found == entries.end())
        entries.push_back({generation.data.generation, false, true});
    else
        found->required = true;
    readers[generation.data.manifest.moduleId].insert(control->operation);
    control->borrowed.swap(usage);
    catalog.readers.swap(readers);
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
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    for (const auto& value : catalog.generations)
        if (value->data.generation == id)
            return value;
    return {};
}
std::vector<std::shared_ptr<Generation>> allGenerations()
{
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    return catalog.generations;
}
Result<GenerationReservation> reserveGeneration(VerifiedSource& source, GenerationId predecessor)
{
    using Output = Result<GenerationReservation>;
    const auto& verified = source.artifact;
    auto& artifact = source.library;
    auto& catalog = moduleCatalog();
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
        auto available = generationReusable(*current, ReusePurpose::Staging);
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
        auto available = generationReusable(*current, ReusePurpose::Discovery);
        return available.hasValue() ? Output::success({current, false}) : Output::failure(available.error());
    }
    if (matching) {
        std::lock_guard<std::mutex> matchLock(matching->mutex);
        if (matching->data.role != GenerationRole::Candidate || matching->data.predecessor != predecessor)
            return Output::failure(error(ErrorCode::ConfigConflict, "module.stage",
                                         "existing content belongs to another generation lineage"));
        auto available = generationReusable(*matching, ReusePurpose::Staging);
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
                                                                              GenerationId candidateId,
                                                                              OperationId operation,
                                                                              const ReplacementClosure* closure)
{
    using Output = Result<std::shared_ptr<ReplacementReservationControl>>;
    if (!currentId.valid() || !candidateId.valid() || currentId == candidateId)
        return Output::failure(
            error(ErrorCode::InvalidArgument, "module.replacement", "replacement requires distinct generations"));
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto current = catalogGenerationLocked(catalog, currentId),
               candidate = catalogGenerationLocked(catalog, candidateId);
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
    if (borrowed(catalog, current->data.manifest.moduleId)) {
        auto failure = error(ErrorCode::ConfigConflict, "module.replacement", "module is used by another replacement");
        failure.operation = *catalog.readers.at(current->data.manifest.moduleId).begin();
        return Output::failure(std::move(failure));
    }
    if (operation.valid()) {
        if (!closure)
            return Output::failure(
                error(ErrorCode::InvalidArgument, "module.replacement", "Core ownership closure is required"));
        for (const auto& row : current->instanceOrigins) {
            const auto& origin = row.second;
            if (!origin.managed ||
                std::none_of(closure->members.begin(), closure->members.end(), [&](const auto& member) {
                    return member.instance == origin.instance && member.epoch == origin.epoch &&
                           member.runtime == origin.runtime;
                }))
                return Output::failure(error(ErrorCode::ConfigConflict, "module.replacement",
                                             "current business is outside Core ownership closure"));
        }
    }
    for (const auto& generation : {current, candidate}) {
        auto ready = generationReusable(*generation, ReusePurpose::Staging);
        if (!ready.hasValue())
            return Output::failure(ready.error());
        const auto resources = generation->ledger.snapshot();
        if (operation.valid() && static_cast<std::size_t>(std::count_if(
                                     resources.records.begin(), resources.records.end(), [](const auto& record) {
                                         return record.kind == ResourceKind::Instance;
                                     })) != generation->instanceOrigins.size())
            return Output::failure(
                error(ErrorCode::ConfigConflict, "module.replacement", "business instance provenance is incomplete"));
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
            if (operation.valid()) {
                const auto resources = generation->ledger.snapshot();
                if (!generation->instanceOrigins.empty() ||
                    std::any_of(resources.records.begin(), resources.records.end(),
                                [](const auto& record) { return record.kind == ResourceKind::Instance; }))
                    return Output::failure(error(ErrorCode::ConfigConflict, "module.replacement",
                                                 "another generation still owns business instances"));
            }
        }
    auto control = std::make_shared<ReplacementReservationControl>();
    control->moduleId = current->data.manifest.moduleId;
    control->current = currentId;
    control->candidate = candidateId;
    control->operation = operation;
    if (operation.valid())
        control->authorized = candidateId;
    if (operation.valid()) {
        auto usage = collectBorrowed(catalog, *closure, control->moduleId);
        if (!usage.hasValue())
            return Output::failure(usage.error());
        control->borrowed = std::move(usage).value();
    }
    // 整组写占用/读占用预分配后一起发布；失败不留下半组预约，也不等待其他事务。
    auto readers = catalog.readers;
    auto replacements = catalog.replacements;
    for (const auto& usage : control->borrowed)
        readers[usage.first].insert(operation);
    replacements.emplace(control->moduleId, control);
    catalog.readers.swap(readers);
    catalog.replacements.swap(replacements);
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
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto found = catalog.replacements.find(control->moduleId);
    if (found == catalog.replacements.end() || found->second != control)
        return;
    control->released.store(true);
    for (const auto& usage : control->borrowed) {
        const auto at = catalog.readers.find(usage.first);
        if (at != catalog.readers.end()) {
            at->second.erase(control->operation);
            if (at->second.empty())
                catalog.readers.erase(at);
        }
    }
    for (const auto& generation : catalog.generations)
        if (generation->data.manifest.moduleId == control->moduleId) {
            std::lock_guard<std::mutex> member(generation->mutex);
            if (generation->replacement.lock() == control)
                generation->replacement.reset();
        }
    catalog.replacements.erase(found);
}
Result<void> authorizeRecoveryPair(const std::shared_ptr<ReplacementReservationControl>& control, OperationId operation)
{
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto at = catalog.replacements.find(control->moduleId);
    if (at == catalog.replacements.end() || at->second != control || !operation.valid() ||
        control->operation != operation)
        return Result<void>::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "foreign or released recovery reservation"));
    const auto current = catalogGenerationLocked(catalog, control->current),
               candidate = catalogGenerationLocked(catalog, control->candidate);
    std::scoped_lock generations(current->mutex, candidate->mutex);
    if (current->admissions || candidate->admissions)
        return Result<void>::failure(error(ErrorCode::ConfigConflict, "module.replacement", "creation is in progress"));
    auto ready = generationReusable(*current, ReusePurpose::Staging);
    if (!ready.hasValue())
        return ready;
    for (auto& usage : control->borrowed)
        for (auto& generation : usage.second)
            generation.required = generation.original;
    control->authorized = control->current;
    return Result<void>::success();
}
Result<GenerationAdmission> admitGeneration(GenerationId id, const Context* context)
{
    using Output = Result<GenerationAdmission>;
    std::optional<ContextSource> source;
    if (context) {
        auto actual = context->source();
        if (!actual.hasValue())
            return Output::failure(actual.error());
        source = std::move(actual).value();
    }
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto generation = catalogGenerationLocked(catalog, id);
    if (!generation)
        return Output::failure(error(ErrorCode::InvalidArgument, "module.admission", "unknown generation"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (source && !generation->runtimeOrigins.count(source->runtime))
        return Output::failure(
            error(ErrorCode::InvalidArgument, "module.admission", "context factory belongs to another generation"));
    if (const auto at = catalog.replacements.find(generation->data.manifest.moduleId);
        at != catalog.replacements.end()) {
        const auto& reservation = *at->second;
        if (!source || !reservation.operation.valid() || source->replacement != reservation.operation ||
            reservation.authorized != id)
            return Output::failure(
                error(ErrorCode::ConfigConflict, "module.replacement", "module creation is reserved"));
    }
    if (source && source->replacement) {
        const auto owner =
            std::find_if(catalog.replacements.begin(), catalog.replacements.end(),
                         [&](const auto& entry) { return entry.second->operation == *source->replacement; });
        if (owner != catalog.replacements.end() && owner->second->factorySources) {
            const auto& control = *owner->second;
            const auto& planned = control.authorized == control.current ? control.factorySources->recovery
                                                                        : control.factorySources->candidate;
            const auto allowed = planned.find(id);
            const auto& plugin = generation->runtimeOrigins.at(source->runtime);
            bool retained = generation->data.manifest.moduleId == control.moduleId;
            if (!retained) {
                const auto held = control.borrowed.find(generation->data.manifest.moduleId);
                retained = held != control.borrowed.end() &&
                           std::any_of(held->second.begin(), held->second.end(),
                                       [&](const auto& entry) { return entry.generation == id && entry.required; });
            }
            if (allowed == planned.end() || !allowed->second.count(plugin) || !retained) {
                auto rejected = error(ErrorCode::ConfigConflict, "module.replacement.sources",
                                      "factory source is outside sealed plan");
                rejected.operation = control.operation;
                return Output::failure(std::move(rejected));
            }
        }
    }
    auto ready = generationReusable(*generation, ReusePurpose::Staging);
    if (!ready.hasValue())
        return Output::failure(ready.error());
    if (!generation->ledger.snapshot().accepting)
        return Output::failure(error(ErrorCode::ModulePinned, "module.admission", "generation is draining"));
    if (source)
        retainBorrowed(catalog, *generation, *source);
    ++generation->admissions;
    return Output::success(GenerationAdmission(generation));
}
Result<GenerationAdmission> admitGenerationLifecycle(GenerationId id)
{
    using Output = Result<GenerationAdmission>;
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto generation = catalogGenerationLocked(catalog, id);
    if (!generation)
        return Output::failure(error(ErrorCode::InvalidArgument, "module.lifecycle", "unknown generation"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (catalog.replacements.count(generation->data.manifest.moduleId))
        return Output::failure(error(ErrorCode::ConfigConflict, "module.replacement", "reserved host cannot close"));
    if (borrowed(catalog, generation->data.manifest.moduleId))
        return Output::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "consumer host is used by replacement"));
    if (generation->loading || generation->admissions)
        return Output::failure(error(ErrorCode::ContextClosing, "module.lifecycle", "module creation is in progress"));
    // 清理不要求健康或 ledger 接受新资源；否则故障代将失去关闭路径。
    ++generation->admissions;
    return Output::success(GenerationAdmission(generation));
}
void GenerationAdmission::reset() noexcept
{
    if (!generation_)
        return;
    auto& catalog = moduleCatalog();
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
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> lock(catalog.mutex);
    const auto generation = catalogGenerationLocked(catalog, id);
    if (!generation)
        return Result<void>::failure(error(ErrorCode::InvalidArgument, "module.drain", "unknown generation ID"));
    std::lock_guard<std::mutex> member(generation->mutex);
    if (catalog.replacements.count(generation->data.manifest.moduleId))
        return Result<void>::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "reserved generation cannot drain"));
    if (borrowed(catalog, generation->data.manifest.moduleId))
        return Result<void>::failure(
            error(ErrorCode::ConfigConflict, "module.replacement", "consumer generation is used by replacement"));
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
