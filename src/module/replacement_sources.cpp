/** @file replacement_sources.cpp
 * @brief 完整宿主计划的创建来源封存；与原实际代次读持有一并发布。
 */
#include "catalog_registry_p.hpp"
#include "replacement_commit_p.hpp"
#include <algorithm>
#include <type_traits>
namespace cordis::module::detail {
namespace {
ErrorInfo sourceError(const char* message, OperationId operation)
{
    auto error = makeError(ErrorCode::ConfigConflict, message);
    error.phase = "module.replacement.sources";
    error.operation = operation;
    return error;
}
} // namespace
bool requiresGeneration(const ReplacementReservationControl& held, GenerationId generation, bool recovery)
{
    if (generation == held.current)
        return true;
    if (generation == held.candidate)
        return !recovery;
    for (const auto& usage : held.borrowed)
        for (const auto& entry : usage.second)
            if (entry.generation == generation && (recovery ? entry.original : entry.required))
                return true;
    return false;
}
Result<std::shared_ptr<ReplacementSourceEvents>>
ReplacementAccess::watchSources(const ReplacementReservation& reservation)
{
    using Output = Result<std::shared_ptr<ReplacementSourceEvents>>;
    const auto held = reservation.control_;
    auto events = std::make_shared<ReplacementSourceEvents>();
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    const auto owner = held ? catalog.replacements.find(held->moduleId) : catalog.replacements.end();
    if (!held || owner == catalog.replacements.end() || owner->second != held || held->released.load() ||
        !held->factorySources || !held->sourceEvents.expired())
        return Output::failure(sourceError("source observation requires actual sealed reservation", {}));
    held->sourceEvents = events;
    return Output::success(std::move(events));
}
Result<void> ReplacementAccess::checkSources(const ReplacementReservation& reservation, bool recovery)
{
    const auto held = reservation.control_;
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    const auto owner = held ? catalog.replacements.find(held->moduleId) : catalog.replacements.end();
    if (!held || owner == catalog.replacements.end() || owner->second != held || held->released.load())
        return Result<void>::failure(sourceError("source reservation released", {}));
    std::set<GenerationId> sources{held->current, held->candidate};
    for (const auto& usage : held->borrowed)
        for (const auto& entry : usage.second)
            sources.insert(entry.generation);
    for (const auto id : sources) {
        if (!requiresGeneration(*held, id, recovery))
            continue;
        const auto generation = catalogGenerationLocked(catalog, id);
        if (!generation)
            return Result<void>::failure(sourceError("source generation missing", held->operation));
        std::lock_guard<std::mutex> member(generation->mutex);
        auto healthy = generationReusable(*generation, ReusePurpose::Staging);
        if (!healthy.hasValue())
            return healthy;
        if (!generation->ledger.snapshot().accepting)
            return Result<void>::failure(sourceError("source generation draining", held->operation));
    }
    return Result<void>::success();
}
Result<void> ReplacementAccess::sealSources(const ReplacementReservation& reservation, const ReplacementScope& scope,
                                            const std::vector<ModuleFactorySource>& candidate,
                                            const std::vector<ModuleFactorySource>& recovery)
{
    auto view = scope.snapshot();
    if (!view.hasValue())
        return Result<void>::failure(view.error());
    const auto held = reservation.control_;
    if (!held || view.value().state != ReplacementScopeState::Reserved || view.value().cancelled ||
        view.value().deliveryBlocked || !held->operation.valid() || held->operation != view.value().operation)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "sources require actual reserved operation"));
    auto prepared = std::make_unique<FactorySourcePlan>();
    for (const auto* phase : {&candidate, &recovery})
        for (const auto& source : *phase)
            (phase == &candidate ? prepared->candidate : prepared->recovery)[source.generation].insert(source.pluginId);
    std::unique_ptr<const FactorySourcePlan> next = std::move(prepared);
    static_assert(std::is_nothrow_swappable_v<decltype(next)>);
    auto& catalog = moduleCatalog();
    std::lock_guard<std::mutex> directory(catalog.mutex);
    const auto owner = catalog.replacements.find(held->moduleId);
    if (owner == catalog.replacements.end() || owner->second != held || held->released.load() || held->factorySources)
        return Result<void>::failure(sourceError("factory source plan already sealed or released", held->operation));
    auto usage = held->borrowed;
    auto readers = catalog.readers;
    for (const bool restoring : {false, true})
        for (const auto& source : (restoring ? next->recovery : next->candidate)) {
            const auto generation = catalogGenerationLocked(catalog, source.first);
            if (!generation)
                return Result<void>::failure(sourceError("unknown planned factory generation", held->operation));
            std::lock_guard<std::mutex> member(generation->mutex);
            const auto& data = generation->data;
            const bool own = data.manifest.moduleId == held->moduleId;
            if (own ? source.first != (restoring ? held->current : held->candidate)
                    : data.role != GenerationRole::Current)
                return Result<void>::failure(sourceError("planned factory generation has wrong role", held->operation));
            if (!data.businessAccepted ||
                std::any_of(source.second.begin(), source.second.end(), [&](const auto& plugin) {
                    return plugin.empty() || std::find(data.manifest.plugins.begin(), data.manifest.plugins.end(),
                                                       plugin) == data.manifest.plugins.end();
                }))
                return Result<void>::failure(sourceError("planned factory plugin is not accepted", held->operation));
            auto ready = generationReusable(*generation, ReusePurpose::Staging);
            if (!ready.hasValue())
                return ready;
            if (generation->admissions || !generation->ledger.snapshot().accepting)
                return Result<void>::failure(
                    sourceError("planned factory generation is entering or draining", held->operation));
            if (own)
                continue;
            const auto exclusive = catalog.replacements.find(data.manifest.moduleId);
            if (exclusive != catalog.replacements.end())
                return Result<void>::failure(
                    sourceError("planned factory module is reserved", exclusive->second->operation));
            auto& entries = usage[data.manifest.moduleId];
            auto found = std::find_if(entries.begin(), entries.end(),
                                      [&](const auto& entry) { return entry.generation == source.first; });
            if (found == entries.end())
                entries.push_back({source.first, restoring, !restoring});
            else if (restoring)
                found->original = true;
            else
                found->required = true;
            readers[data.manifest.moduleId].insert(held->operation);
        }
    held->borrowed.swap(usage);
    catalog.readers.swap(readers);
    held->factorySources.swap(next);
    return Result<void>::success();
}
} // namespace cordis::module::detail
