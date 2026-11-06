/** @file preflight.cpp
 * @brief 在任何停止之前绑定真实来源、会话、完整拥有单元和候选/恢复输入。
 */
#include "operation_p.hpp"
#include <algorithm>
namespace cordis::detail {
namespace {
Result<void> compatible(const Runtime& old, const Runtime& next)
{
    for (const auto& output : old.outputs()) {
        const auto found = std::find_if(next.outputs().begin(), next.outputs().end(), [&](const auto& item) {
            return item.key.name == output.key.name && item.key.interfaceMajor == output.key.interfaceMajor &&
                   item.key.realm == output.key.realm;
        });
        if (found == next.outputs().end() || found->type != output.type ||
            found->metadata.minor < output.metadata.minor || (output.required && !found->required) ||
            std::any_of(
                output.metadata.capabilities.begin(), output.metadata.capabilities.end(),
                [&](const auto& capability) {
                    return std::find(found->metadata.capabilities.begin(), found->metadata.capabilities.end(),
                                     capability) == found->metadata.capabilities.end();
                }))
            return Result<void>::failure(
                replacementError(ErrorCode::AbiMismatch, "candidate weakens published service contract"));
    }
    return Result<void>::success();
}
Result<void> binding(const ReplacementRequest& request, const loader::ConfigurationDefinitions& definitions,
                     const std::string& plugin, const module::ModuleGenerationSnapshot& generation)
{
    auto source = definitions.module(plugin);
    if (!source.hasValue())
        return Result<void>::failure(source.error());
    if (source.value().alias == "builtin" || request.pluginRoot.empty())
        return Result<void>::failure(replacementError(
            ErrorCode::ConfigInvalid, "module definition needs verified module locator and plugin root"));
    auto artifact = module::preflightModule(request.pluginRoot, source.value().manifest);
    if (!artifact.hasValue())
        return Result<void>::failure(artifact.error());
    if (artifact.value().manifestPath() != generation.manifestPath ||
        artifact.value().libraryPath() != generation.libraryPath ||
        artifact.value().manifest().sha256 != generation.manifest.sha256 ||
        artifact.value().manifest().moduleId != generation.manifest.moduleId)
        return Result<void>::failure(
            replacementError(ErrorCode::ConfigConflict, "configuration locator is not the actual factory artifact"));
    return Result<void>::success();
}
} // namespace
Result<void> ReplacementOperationControl::prepare(ReplacementRequest request)
{
    using loader::detail::DriverCommitAccess;
    using loader::detail::SessionCommitAccess;
    if (!request.scheduler || !request.kernel || !request.modules || !request.scheduler->isCurrentThread() ||
        !request.scheduler->isOpen() || !request.kernel->usesScheduler(request.scheduler) ||
        request.definitions.empty())
        return Result<void>::failure(
            replacementError(ErrorCode::InvalidArgument, "replacement requires open owner host and definitions"));
    execution = std::make_unique<Execution>();
    auto& work = *execution;
    work.request = std::move(request);
    auto& host = work.request;
    std::vector<RuntimeId> requested;
    for (const auto& item : host.definitions)
        requested.push_back(item.previous);
    std::vector<InstanceId> anchors;
    std::set<const loader::ConfigurationSession*> seenSessions;
    std::optional<Task<void>> settled;
    if (!host.configurations.empty()) {
        settled.emplace(delivery->done.task().thenOn(host.scheduler, [](const Result<ReplacementReport>&) {}));
        work.sessions.reserve(host.configurations.size());
    }
    for (const auto& config : host.configurations) {
        if (!config.session || !config.driver || !config.definitions || !config.target ||
            !seenSessions.insert(config.session.get()).second || !config.session->usesScheduler(host.scheduler))
            return Result<void>::failure(
                replacementError(ErrorCode::ConfigConflict, "invalid configuration participant"));
        auto session = SessionCommitAccess::inspect(*config.session, *config.driver);
        if (!session.hasValue())
            return Result<void>::failure(session.error());
        if (session.value().revision != config.expectedRevision || !session.value().idle)
            return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "configuration is not settled"));
        auto held = SessionCommitAccess::hold(*config.session, *config.driver, *settled);
        if (!held.hasValue())
            return Result<void>::failure(held.error());
        work.sessions.push_back(std::move(held).value());
        auto owners = DriverCommitAccess::reconstructionOwners(*config.driver, requested);
        if (!owners.hasValue())
            return Result<void>::failure(owners.error());
        anchors.insert(anchors.end(), owners.value().begin(), owners.value().end());
    }
    auto reserved = CoreReplacementAccess::reserve(*host.kernel, requested, anchors);
    if (!reserved.hasValue())
        return Result<void>::failure(reserved.error());
    work.scope = std::make_shared<ReplacementScope>(std::move(reserved).value());
    auto prepared = work.scope->prepareDefinitions(host.definitions);
    if (!prepared.hasValue())
        return prepared;
    auto original = CoreReplacementAccess::original(*work.scope, *host.kernel);
    if (!original.hasValue())
        return Result<void>::failure(original.error());
    work.original = std::move(original).value();
    delivery->scopeFailure = CoreReplacementAccess::failureSignal(*work.scope, *host.kernel);
    if (!delivery->scopeFailure)
        return Result<void>::failure(
            replacementError(ErrorCode::ConfigConflict, "replacement failure signal is unavailable"));
    report.operation = work.original.operation;
    report.previous = host.previous;
    report.candidate = host.candidate;
    for (const auto& definition : work.original.factories) {
        auto old = host.modules->runtimeOrigin(definition.first.id()),
             next = host.modules->runtimeOrigin(definition.second.id());
        if (!old.hasValue())
            return Result<void>::failure(old.error());
        if (!next.hasValue())
            return Result<void>::failure(next.error());
        if (!old.value() || !next.value() || old.value()->generation != host.previous ||
            next.value()->generation != host.candidate || old.value()->moduleId != next.value()->moduleId ||
            old.value()->pluginId != next.value()->pluginId)
            return Result<void>::failure(
                replacementError(ErrorCode::AbiMismatch, "replacement factory origins or plugin identities differ"));
        auto contract = compatible(definition.first, definition.second);
        if (!contract.hasValue())
            return contract;
    }
    auto moduleReservation = host.modules->reserveReplacement(host.previous, host.candidate, *work.scope);
    if (!moduleReservation.hasValue())
        return Result<void>::failure(moduleReservation.error());
    work.reservation = std::move(moduleReservation).value();
    auto generations = host.modules->snapshot();
    if (!generations.hasValue())
        return Result<void>::failure(generations.error());
    const auto oldGeneration = std::find_if(generations.value().begin(), generations.value().end(),
                                            [&](const auto& item) { return item.generation == host.previous; });
    const auto newGeneration = std::find_if(generations.value().begin(), generations.value().end(),
                                            [&](const auto& item) { return item.generation == host.candidate; });
    if (oldGeneration == generations.value().end() || newGeneration == generations.value().end())
        return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "replacement generation disappeared"));
    std::vector<loader::detail::DriverReconstructionRequest> participants;
    std::vector<SessionCommitAccess::View> sessions;
    for (const auto& config : host.configurations) {
        auto view = SessionCommitAccess::inspect(*config.session, *config.driver);
        if (!view.hasValue())
            return Result<void>::failure(view.error());
        if (view.value().revision != config.expectedRevision || !view.value().idle)
            return Result<void>::failure(
                replacementError(ErrorCode::ConfigConflict, "configuration is not settled at expected revision"));
        sessions.push_back(std::move(view).value());
        participants.push_back({config.driver, config.definitions, config.target});
    }
    std::optional<loader::detail::ConfigurationReconstructionPlan> configuration;
    if (!participants.empty()) {
        auto checked = loader::detail::ConfigurationReconstructionAccess::inspect(*work.scope, std::move(participants));
        if (!checked.hasValue())
            return Result<void>::failure(checked.error());
        configuration.emplace(std::move(checked).value());
    }
    std::vector<ReplacementConstruction> candidate, restoredMembers;
    std::map<InstanceId, std::size_t> keys;
    for (const auto& member : work.original.members) {
        const auto key = candidate.size();
        keys.emplace(member.input.instance, key);
        auto factory = member.input.parent.valid() ? std::optional<Runtime>{} : member.input.factory;
        restoredMembers.push_back({key,
                                   {},
                                   member.input,
                                   factory,
                                   member.input.config,
                                   member.input.realms,
                                   member.input.additionalDependencies});
        if (factory)
            for (const auto& definition : work.original.factories)
                if (definition.first.id() == factory->id()) {
                    factory = definition.second;
                    break;
                }
        candidate.push_back({key,
                             {},
                             member.input,
                             factory,
                             member.input.config,
                             member.input.realms,
                             member.input.additionalDependencies});
        work.configured.push_back({});
    }
    for (std::size_t key = 0; key != work.original.members.size(); ++key) {
        const auto parent = work.original.members[key].input.parent;
        if (!parent.valid())
            continue;
        const auto found = keys.find(parent);
        if (found == keys.end())
            return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, "closure parent is not captured"));
        candidate[key].parent = found->second;
        restoredMembers[key].parent = found->second;
    }
    for (std::size_t index = 0; index != host.configurations.size(); ++index) {
        const auto& config = host.configurations[index];
        const auto& next = configuration->drivers[index];
        auto old = DriverCommitAccess::inspectReconstruction(*config.driver, *work.scope, next.change.baseline,
                                                             sessions[index].desired, ReplacementCommitMode::Recovery);
        if (!old.hasValue())
            return Result<void>::failure(old.error());
        ConfigurationWork frozen{next.change, sessions[index].desired, {}};
        for (const auto& entry : next.entries)
            frozen.paths.insert(entry.path);
        for (const auto& entry : frozen.original->source().structure().entries())
            if (std::find(frozen.change.plugins.begin(), frozen.change.plugins.end(), entry.second.plugin) !=
                frozen.change.plugins.end())
                frozen.paths.insert(entry.first);
        for (const auto& id : frozen.change.plugins) {
            auto before = binding(host, *frozen.change.baseline, id, *oldGeneration);
            if (!before.hasValue())
                return before;
            auto after = binding(host, *frozen.change.candidate, id, *newGeneration);
            if (!after.hasValue())
                return after;
        }
        auto merged = loader::detail::mergeReplacementTarget(frozen.original->source(), config.target->source(),
                                                             frozen.original->source(), frozen.paths);
        if (!merged.hasValue())
            return Result<void>::failure(merged.error());
        std::map<std::string, std::size_t> paths;
        for (const auto& entry : next.entries) {
            const auto oldEntry = std::find_if(old.value().entries.begin(), old.value().entries.end(),
                                               [&](const auto& item) { return item.path == entry.path; });
            if (oldEntry == old.value().entries.end())
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "old configuration input is missing"));
            std::size_t key;
            const auto found = keys.find(entry.previous);
            if (found != keys.end()) {
                key = found->second;
            } else {
                key = candidate.size();
                candidate.push_back(
                    {key, {}, {}, entry.runtime, entry.config, entry.realms, entry.additionalDependencies});
                restoredMembers.push_back({key,
                                           {},
                                           {},
                                           oldEntry->runtime,
                                           oldEntry->config,
                                           oldEntry->realms,
                                           oldEntry->additionalDependencies});
                work.configured.push_back({});
            }
            candidate[key].factory = entry.runtime;
            candidate[key].config = entry.config;
            candidate[key].realms = entry.realms;
            candidate[key].additionalDependencies = entry.additionalDependencies;
            restoredMembers[key].factory = oldEntry->runtime;
            restoredMembers[key].config = oldEntry->config;
            restoredMembers[key].realms = oldEntry->realms;
            restoredMembers[key].additionalDependencies = oldEntry->additionalDependencies;
            work.configured[key] = ConfiguredSource{index, entry.path, entry.previous};
            paths.emplace(entry.path, key);
        }
        for (const auto& path : paths) {
            const auto slash = path.first.rfind('/');
            if (slash == std::string::npos)
                continue;
            const auto parent = paths.find(path.first.substr(0, slash));
            if (parent == paths.end())
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "configuration parent requires closure expansion"));
            candidate[path.second].parent = parent->second;
            restoredMembers[path.second].parent = parent->second;
        }
        work.configurations.push_back(std::move(frozen));
    }
    if (host.configurations.empty()) {
        for (const auto& member : work.original.members) {
            auto identity = InstanceOwnershipAccess::owner(member.instance);
            if (!identity.hasValue())
                return Result<void>::failure(identity.error());
            if (identity.value())
                return Result<void>::failure(
                    replacementError(ErrorCode::ConfigConflict, "configuration owner was omitted"));
        }
    }
    std::vector<Runtime> borrowed;
    for (const auto* inputs : {&candidate, &restoredMembers})
        for (const auto& item : *inputs)
            if (item.factory)
                borrowed.push_back(*item.factory);
    auto protectedFactories = CoreReplacementAccess::borrow(*work.scope, *host.kernel, borrowed);
    if (!protectedFactories.hasValue())
        return protectedFactories;
    std::vector<Runtime> candidateFactories, recoveryFactories;
    for (const auto& item : candidate)
        if (item.factory)
            candidateFactories.push_back(*item.factory);
    for (const auto& item : restoredMembers)
        if (item.factory)
            recoveryFactories.push_back(*item.factory);
    auto protectedModules =
        module::detail::ReplacementAccess::borrow(work.reservation, *work.scope, candidateFactories, recoveryFactories);
    if (!protectedModules.hasValue())
        return protectedModules;
    work.driverClaims.reserve(host.configurations.size());
    for (const auto& participant : host.configurations) {
        auto claim = loader::detail::ConfigurationCommitAccess::reserve(
            *participant.session, *participant.driver, *work.scope, participant.definitions, participant.target,
            participant.expectedRevision);
        if (!claim.hasValue())
            return Result<void>::failure(claim.error());
        work.driverClaims.push_back(std::move(claim).value());
    }
    std::set<InstanceId> usedOwners;
    std::size_t nextKey = candidate.size();
    for (const auto& root : candidate) {
        if (root.parent)
            continue;
        const auto id = root.previous ? root.previous->instance : InstanceId{};
        BuildUnit unit{id, {false, {}}, {true, {}}, {}};
        const auto provided = host.owners.find(id);
        if (provided != host.owners.end()) {
            if (!provided->second)
                return Result<void>::failure(replacementError(ErrorCode::InvalidArgument, "empty owner rebuilder"));
            unit.owner = provided->second;
            usedOwners.insert(id);
        }
        for (const auto& item : candidate) {
            auto ancestor = item.key;
            while (candidate[ancestor].parent)
                ancestor = *candidate[ancestor].parent;
            if (ancestor == root.key) {
                unit.candidate.members.push_back(item);
                unit.recovery.members.push_back(restoredMembers[item.key]);
            }
        }
        if (!unit.owner && std::any_of(unit.candidate.members.begin(), unit.candidate.members.end(),
                                       [](const auto& item) { return !item.factory; }))
            return Result<void>::failure(replacementError(
                ErrorCode::ConfigConflict, "dynamic subtree requires its owner reconstruction protocol"));
        for (auto* plan : {&unit.candidate, &unit.recovery}) {
            auto described = describeSources(*plan);
            if (!described.hasValue())
                return described;
        }
        ReplacementOwnerPreflight future;
        if (unit.owner) {
            auto accepted = unit.owner->preflight(unit.candidate, unit.recovery);
            if (!accepted.hasValue())
                return Result<void>::failure(accepted.error());
            future = std::move(accepted).value();
        }
        auto planned = planConstruction(unit.candidate, future.candidate, nextKey);
        if (!planned.hasValue())
            return planned;
        auto recoveryPlan = planConstruction(unit.recovery, future.recovery, nextKey);
        if (!recoveryPlan.hasValue())
            return recoveryPlan;
        work.units.push_back(std::move(unit));
    }
    if (usedOwners.size() != host.owners.size())
        return Result<void>::failure(
            replacementError(ErrorCode::ConfigConflict, "owner key is not an actual closure root"));
    if (!CoreReplacementAccess::current(work.original))
        return Result<void>::failure(
            replacementError(ErrorCode::ConfigConflict, "replacement invalidated during owner preflight"));
    CoreReplacementAccess::InputBindings candidateInputs, recoveryInputs;
    for (const bool restoring : {false, true}) {
        auto graph = planGraph(restoring, restoring ? &recoveryInputs : &candidateInputs);
        if (!graph.hasValue())
            return Result<void>::failure(graph.error());
    }
    for (std::size_t i = 0; i != work.configurations.size(); ++i) {
        auto view = SessionCommitAccess::inspect(*host.configurations[i].session, *host.configurations[i].driver);
        if (!view.hasValue())
            return Result<void>::failure(view.error());
        if (view.value().revision != host.configurations[i].expectedRevision || !view.value().idle ||
            !DriverCommitAccess::current(*host.configurations[i].driver, configuration->drivers[i]))
            return Result<void>::failure(
                replacementError(ErrorCode::ConfigConflict, "configuration changed during owner preflight"));
    }
    if (!CoreReplacementAccess::current(work.original))
        return Result<void>::failure(
            replacementError(ErrorCode::ConfigConflict, "replacement invalidated while holding sessions"));
    std::vector<ServiceKey> outputs;
    for (const auto& unit : work.units)
        for (const auto* plan : {&unit.candidate, &unit.recovery})
            for (const auto& member : plan->members)
                for (const auto& output : member.contract->outputs)
                    outputs.push_back(output.key);
    auto sealed = CoreReplacementAccess::sealPlan(*work.scope, *host.kernel, outputs, candidateInputs, recoveryInputs);
    if (!sealed.hasValue())
        return sealed;
    auto inputsHeld = borrowInputs();
    if (!inputsHeld.hasValue())
        return inputsHeld;
    return sealSources();
}
} // namespace cordis::detail
