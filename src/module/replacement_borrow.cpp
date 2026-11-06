/** @file replacement_borrow.cpp
 * @brief 构造计划复用唯一模块来源和读者账目，不建立额外代次索引。
 */
#include "catalog_registry_p.hpp"
#include "replacement_commit_p.hpp"
#include <algorithm>

namespace cordis::module::detail {
namespace {
ErrorInfo conflict(const char* message, OperationId operation = {})
{
    auto error = makeError(ErrorCode::ConfigConflict, message);
    error.phase = "module.replacement.borrow";
    error.operation = operation;
    return error;
}
} // namespace
Result<void> ReplacementAccess::borrow(const ReplacementReservation& reservation, const ReplacementScope& scope,
                                       const std::vector<Runtime>& candidate, const std::vector<Runtime>& recovery)
{
    return borrowSources(reservation, scope, candidate, recovery, {});
}
Result<void> ReplacementAccess::borrowInputs(const ReplacementReservation& reservation, const ReplacementScope& scope,
                                             const std::vector<ModuleInputSource>& sources)
{
    return borrowSources(reservation, scope, {}, {}, sources);
}
Result<void> ReplacementAccess::borrowSources(const ReplacementReservation& reservation, const ReplacementScope& scope,
                                              const std::vector<Runtime>& candidate,
                                              const std::vector<Runtime>& recovery,
                                              const std::vector<ModuleInputSource>& sources)
{
    auto snapshot = scope.snapshot();
    if (!snapshot.hasValue())
        return Result<void>::failure(snapshot.error());
    const auto& source = snapshot.value();
    const auto held = reservation.control_;
    if (!held || source.state != ReplacementScopeState::Reserved || source.cancelled || source.deliveryBlocked ||
        !held->operation.valid() || held->operation != source.operation)
        return Result<void>::failure(conflict("module reads require the reserved Core operation"));
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    const auto owner = catalog.replacements.find(held->moduleId);
    if (owner == catalog.replacements.end() || owner->second != held || held->released.load())
        return Result<void>::failure(conflict("module reservation has been released"));
    auto usage = held->borrowed;
    auto readers = catalog.readers;
    for (const auto& input : sources)
        if (!catalogGenerationLocked(catalog, input.generation))
            return Result<void>::failure(conflict("input generation is missing"));
    for (const auto& generation : catalog.generations) {
        if (generation->data.manifest.moduleId == held->moduleId)
            continue;
        std::lock_guard<std::mutex> member(generation->mutex);
        const auto uses = [&](const auto& factories, bool restoring) -> Result<bool> {
            bool used = std::any_of(factories.begin(), factories.end(), [&](const auto& factory) {
                return generation->runtimeOrigins.count(factory.id()) != 0;
            });
            for (const auto& input : sources) {
                if (!(restoring ? input.recovery : input.candidate) || input.generation != generation->data.generation)
                    continue;
                const auto origins = generation->instanceOrigins.equal_range({input.instance, input.epoch});
                if (std::none_of(origins.first, origins.second,
                                 [&](const auto& row) {
                                     return row.second.managed == input.runtime.has_value() &&
                                            (!input.runtime || row.second.runtime == *input.runtime);
                                 }) ||
                    generation->data.role != GenerationRole::Current)
                    return Result<bool>::failure(conflict("input source is not actual current module business"));
                used = true;
            }
            return Result<bool>::success(used);
        };
        auto needed = uses(candidate, false), restored = uses(recovery, true);
        if (!needed.hasValue() || !restored.hasValue())
            return Result<void>::failure(!needed.hasValue() ? needed.error() : restored.error());
        const bool required = needed.value(), original = restored.value();
        if (!required && !original)
            continue;
        const auto exclusive = catalog.replacements.find(generation->data.manifest.moduleId);
        if (exclusive != catalog.replacements.end())
            return Result<void>::failure(conflict("planned module is reserved", exclusive->second->operation));
        auto ready = generationReusable(*generation, ReusePurpose::Staging);
        if (!ready.hasValue())
            return ready;
        if (generation->admissions || !generation->ledger.snapshot().accepting)
            return Result<void>::failure(conflict("planned generation is entering or draining"));
        auto& entries = usage[generation->data.manifest.moduleId];
        auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto& entry) { return entry.generation == generation->data.generation; });
        if (found == entries.end())
            entries.push_back({generation->data.generation, original, required});
        else {
            found->original = found->original || original;
            found->required = found->required || required;
        }
        readers[generation->data.manifest.moduleId].insert(held->operation);
    }
    held->borrowed.swap(usage);
    catalog.readers.swap(readers);
    return Result<void>::success();
}
} // namespace cordis::module::detail
