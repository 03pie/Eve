/** @file replacement_commit.cpp
 * @brief 在唯一目录锁内共同封印角色和借用版本，Core 与宿主元数据交换前完成所有验证。
 */
#include "catalog_registry_p.hpp"
#include "replacement_commit_p.hpp"
#include <algorithm>
#include <thread>

namespace cordis::module::detail {
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "module.replacement.commit";
    return value;
}
bool ownsBusiness(const Generation& generation, OperationId operation)
{
    const auto ledger = generation.ledger.snapshot();
    const auto instances = std::count_if(ledger.records.begin(), ledger.records.end(),
                                         [](const auto& record) { return record.kind == ResourceKind::Instance; });
    return static_cast<std::size_t>(instances) == generation.instanceOrigins.size() &&
           std::all_of(generation.instanceOrigins.begin(), generation.instanceOrigins.end(),
                       [&](const auto& entry) { return entry.second.managed && entry.second.operation == operation; });
}
} // namespace
struct PreparedModuleCommitControl {
    const ReplacementScope* scope;
    ReplacementPreparation preparation;
    std::shared_ptr<ReplacementReservationControl> reservation;
    std::shared_ptr<Generation> current, candidate;
    std::vector<std::shared_ptr<Generation>> generations;
    // 逆序先释放信号/代次/目录锁，最后释放强引用，避免在目录锁内析构所有者。
    std::unique_lock<std::mutex> directory;
    std::vector<std::unique_lock<std::mutex>> members, signals;
    std::thread::id owner = std::this_thread::get_id();
    bool committed = false;
};
PreparedModuleCommit::PreparedModuleCommit(std::unique_ptr<PreparedModuleCommitControl> control)
    : control_(std::move(control))
{
}
PreparedModuleCommit::PreparedModuleCommit(PreparedModuleCommit&&) noexcept = default;
PreparedModuleCommit::~PreparedModuleCommit() = default;
bool PreparedModuleCommit::committed() const noexcept
{
    return control_ && control_->committed;
}
Result<void> PreparedModuleCommit::publish()
{
    const auto& control = control_;
    if (!control || control->owner != std::this_thread::get_id())
        return Result<void>::failure(error(ErrorCode::WrongThread, "module commit requires prepared owner"));
    if (control->committed)
        return Result<void>::success();
    auto published = control->scope->commitPrepared();
    if (!published.hasValue())
        return published;
    if (control->preparation.mode == ReplacementCommitMode::Candidate) {
        control->current->data.role = GenerationRole::Retired;
        control->candidate->data.role = GenerationRole::Current;
    }
    control->committed = true;
    return Result<void>::success();
}
Result<PreparedModuleCommit> ReplacementAccess::prepare(const ReplacementReservation& reservation,
                                                        const ReplacementScope& scope)
{
    using Output = Result<PreparedModuleCommit>;
    auto preparation = scope.preparation();
    if (!preparation.hasValue())
        return Output::failure(preparation.error());
    const auto held = reservation.control_;
    if (!held || !held->operation.valid() || held->operation != preparation.value().operation)
        return Output::failure(error(ErrorCode::ConfigConflict, "module reservation does not own Core preparation"));
    auto value = std::make_unique<PreparedModuleCommitControl>();
    value->scope = &scope;
    value->preparation = std::move(preparation).value();
    value->reservation = held;
    auto& catalog = moduleCatalog();
    value->directory = std::unique_lock<std::mutex>(catalog.mutex);
    const auto at = catalog.replacements.find(held->moduleId);
    if (at == catalog.replacements.end() || at->second != held || held->released.load())
        return Output::failure(error(ErrorCode::ConfigConflict, "module reservation has been released"));
    value->current = catalogGenerationLocked(catalog, held->current);
    value->candidate = catalogGenerationLocked(catalog, held->candidate);
    if (!value->current || !value->candidate)
        return Output::failure(error(ErrorCode::ConfigConflict, "reserved generation disappeared"));
    value->generations = {value->current, value->candidate};
    for (const auto& usage : held->borrowed) {
        const auto readers = catalog.readers.find(usage.first);
        if (readers == catalog.readers.end() || !readers->second.count(held->operation))
            return Output::failure(error(ErrorCode::ConfigConflict, "module usage reservation changed"));
        for (const auto& entry : usage.second) {
            auto generation = catalogGenerationLocked(catalog, entry.generation);
            if (!generation || generation->data.manifest.moduleId != usage.first)
                return Output::failure(error(ErrorCode::ConfigConflict, "borrowed generation changed"));
            value->generations.push_back(std::move(generation));
        }
    }
    std::sort(value->generations.begin(), value->generations.end(),
              [](const auto& a, const auto& b) { return a->data.generation < b->data.generation; });
    value->generations.erase(std::unique(value->generations.begin(), value->generations.end()),
                             value->generations.end());
    value->members.reserve(value->generations.size());
    value->signals.reserve(value->generations.size());
    for (const auto& generation : value->generations)
        value->members.emplace_back(generation->mutex);
    for (const auto& generation : value->generations)
        value->signals.emplace_back(generation->businessSignal->mutex);
    const auto& current = *value->current;
    const auto& candidate = *value->candidate;
    const bool recovery = value->preparation.mode == ReplacementCommitMode::Recovery;
    if (current.data.role != GenerationRole::Current || candidate.data.role != GenerationRole::Candidate ||
        candidate.data.predecessor != current.data.generation || current.admissions || candidate.admissions ||
        held->authorized != (recovery ? held->current : held->candidate))
        return Output::failure(error(ErrorCode::ConfigConflict, "module lineage or creation mode changed"));
    for (const auto& generation : value->generations) {
        if (!requiresGeneration(*held, generation->data.generation, recovery))
            continue;
        auto healthy = generationReusableLockedSignal(*generation, ReusePurpose::Staging);
        if (!healthy.hasValue())
            return Output::failure(healthy.error());
        if (!generation->ledger.snapshot().accepting)
            return Output::failure(error(ErrorCode::ModulePinned, "prepared generation is draining"));
    }
    const auto& retired = recovery ? candidate : current;
    if (!retired.instanceOrigins.empty() || !ownsBusiness(retired, held->operation) ||
        !ownsBusiness(recovery ? current : candidate, held->operation))
        return Output::failure(error(ErrorCode::ConfigConflict, "business ownership changed before commit"));
    for (const auto& definition : value->preparation.definitions) {
        const auto previous = current.runtimeOrigins.find(definition.previous);
        if (previous == current.runtimeOrigins.end())
            return Output::failure(error(ErrorCode::ConfigConflict, "old factory origin changed"));
        if (!recovery) {
            const auto next = candidate.runtimeOrigins.find(definition.candidate);
            if (next == candidate.runtimeOrigins.end() || previous->second != next->second)
                return Output::failure(error(ErrorCode::AbiMismatch, "candidate factory origin or plugin differs"));
        }
    }
    return Output::success(PreparedModuleCommit(std::move(value)));
}
} // namespace cordis::module::detail
