/** @file replacement_subtree.hpp
 * @brief 宿主拥有动态子树：临时工厂留在拥有者内，平台只核对实际成员对应。
 */
#pragma once
#include <algorithm>
#include <cordis/replacement_operation.hpp>
#include <map>
#include <stdexcept>

class SubtreeReplacementOwner final : public cordis::IReplacementOwner {
    struct Build : std::enable_shared_from_this<Build> {
        std::shared_ptr<cordis::IScheduler> scheduler;
        cordis::Runtime childFactory;
        std::optional<std::pair<cordis::Runtime, cordis::Runtime>> business;
        std::map<std::string, cordis::Runtime> candidateFactories;
        cordis::ReplacementBuildPlan plan;
        cordis::ReplacementBuilder builder;
        cordis::ReplacementRebuiltTree tree;
        std::shared_ptr<cordis::ReplacementRebuiltTree> observed;
        cordis::Promise<cordis::ReplacementRebuiltTree> done;
        std::optional<cordis::Context> rootContext;
        bool omit = false, wrongParent = false;

        Build(std::shared_ptr<cordis::IScheduler> queue, cordis::Runtime child, cordis::ReplacementBuildPlan input,
              cordis::ReplacementBuilder permission)
            : scheduler(std::move(queue)), childFactory(std::move(child)), plan(std::move(input)),
              builder(std::move(permission))
        {
        }

        void create(std::size_t key, std::optional<cordis::Context> parent)
        {
            using namespace cordis;
            const auto input = std::find_if(plan.members.begin(), plan.members.end(),
                                            [&](const auto& item) { return item.key == key; });
            CHECK(input != plan.members.end());
            if (parent)
                for (const auto& realm : input->realms.overrides())
                    parent = parent->isolate(realm.first, realm.second).value();
            const auto custom = plan.recovery ? candidateFactories.end() : candidateFactories.find(input->config);
            const auto& child = custom != candidateFactories.end() ? custom->second
                                : business && input->previous && input->previous->runtime == business->first.id()
                                    ? (plan.recovery ? business->first : business->second)
                                    : childFactory;
            auto created =
                parent ? parent->inject(child, input->config, input->additionalDependencies)
                       : builder.stage(*input->factory, input->config, input->realms, input->additionalDependencies);
            if (!created.hasValue()) {
                done.reject(created.error());
                return;
            }
            const auto instance = created.value();
            tree.members.push_back({key, instance});
            auto receipt = instance.ready().thenOn(scheduler, [self = shared_from_this(), key,
                                                               instance](const Result<void>& ready) {
                if (!ready.hasValue()) {
                    self->done.reject(ready.error());
                    return;
                }
                auto context = instance.whenActiveAfter().tryResult();
                CHECK(context && context->hasValue());
                if (!self->rootContext)
                    self->rootContext = context->value();
                const auto child = std::find_if(self->plan.members.begin(), self->plan.members.end(),
                                                [&](const auto& item) { return item.parent && *item.parent == key; });
                if (child != self->plan.members.end()) {
                    self->create(child->key, self->wrongParent ? *self->rootContext : context->value());
                    return;
                }
                if (self->omit)
                    self->tree.members.pop_back();
                *self->observed = self->tree;
                self->done.resolve(std::move(self->tree));
            });
            cordis::detail::TaskAccess::observe(receipt, [done = done](Task<void>::Snapshot result) {
                if (!result->hasValue())
                    done.reject(result->error());
            });
        }
    };

public:
    enum class FutureIssue { None, MissingService, Cycle, UnknownRealm, Version };
    enum class RecoveryIssue { None, Config, Realm, Requirements, Outputs };
    std::shared_ptr<cordis::IScheduler> scheduler;
    cordis::Runtime childFactory;
    std::optional<std::pair<cordis::Runtime, cordis::Runtime>> business;
    std::map<std::string, cordis::Runtime> candidateFactories;
    std::map<std::string, std::string> candidateConfigs;
    using Sources = std::map<std::string, std::optional<cordis::module::ModuleFactorySource>>;
    Sources candidateSources, recoverySources;
    bool omitCandidate = false, wrongCandidateParent = false;
    bool addCandidateChild = false, lieCandidateContract = false, omitFuture = false;
    FutureIssue futureIssue = FutureIssue::None;
    RecoveryIssue recoveryIssue = RecoveryIssue::None;
    bool equivalentRecovery = false;
    bool suspendCandidate = false;
    cordis::Promise<void> entered;
    cordis::Promise<cordis::ReplacementRebuiltTree> pending;
    bool probeStale = false, staleRejected = false;
    std::optional<cordis::ReplacementBuilder> previousBuilder;
    std::optional<cordis::Runtime> previousFactory;
    std::size_t checked = 0, rebuilt = 0;
    std::shared_ptr<cordis::ReplacementRebuiltTree> observed;
    SubtreeReplacementOwner(std::shared_ptr<cordis::IScheduler> queue, cordis::Runtime child)
        : scheduler(std::move(queue)), childFactory(std::move(child))
    {
    }

    cordis::Result<cordis::ReplacementOwnerPreflight> preflight(const cordis::ReplacementBuildPlan& candidate,
                                                                const cordis::ReplacementBuildPlan& recovery) override
    {
        CHECK(candidate.members.size() == 3 && recovery.members.size() == 3);
        for (const auto* plan : {&candidate, &recovery}) {
            std::size_t roots = 0;
            for (const auto& item : plan->members) {
                CHECK(item.previous);
                if (!item.parent) {
                    CHECK(item.factory);
                    ++roots;
                } else {
                    CHECK(!item.factory); // 临时工厂不能由平台捕获并单独重放。
                    CHECK(item.previous->runtime == childFactory.id() ||
                          (business && item.previous->runtime == business->first.id()));
                }
            }
            CHECK(roots == 1);
        }
        ++checked;
        const auto describe = [&](const cordis::ReplacementBuildPlan& plan) {
            std::vector<cordis::ReplacementDynamicMember> result;
            for (const auto& item : plan.members) {
                if (item.factory)
                    continue;
                const auto custom = plan.recovery ? candidateFactories.end() : candidateFactories.find(item.config);
                const auto& runtime = custom != candidateFactories.end() ? custom->second
                                      : business && item.previous->runtime == business->first.id()
                                          ? (plan.recovery ? business->first : business->second)
                                          : childFactory;
                auto requirements = runtime.requirements();
                requirements.insert(requirements.end(), item.additionalDependencies.begin(),
                                    item.additionalDependencies.end());
                result.push_back({item.key,
                                  item.parent,
                                  item.config,
                                  item.realms,
                                  {std::move(requirements), runtime.outputs()},
                                  item.source});
                const auto& sources = plan.recovery ? recoverySources : candidateSources;
                const auto source = sources.find(item.config);
                if (source != sources.end())
                    result.back().source = source->second;
                const auto config = candidateConfigs.find(item.config);
                if (!plan.recovery && config != candidateConfigs.end())
                    result.back().config = config->second;
            }
            if (!plan.recovery && lieCandidateContract)
                result.back().contract.outputs.push_back(cordis::ServiceOutput::of<int>({"future.claim"}));
            if (plan.recovery) {
                switch (recoveryIssue) {
                case RecoveryIssue::Config:
                    result.back().config = "changed-recovery";
                    break;
                case RecoveryIssue::Realm:
                    result.back().realms = {};
                    break;
                case RecoveryIssue::Requirements:
                    result.back().contract.requirements.clear();
                    break;
                case RecoveryIssue::Outputs:
                    result.back().contract.outputs.push_back(cordis::ServiceOutput::of<int>({"changed.recovery"}));
                    break;
                case RecoveryIssue::None:
                    break;
                }
                if (equivalentRecovery) {
                    CHECK(!result.back().contract.requirements.empty());
                    const auto duplicate = result.back().contract.requirements.front();
                    result.back().contract.requirements.push_back(duplicate);
                }
            }
            if (!plan.recovery) {
                switch (futureIssue) {
                case FutureIssue::MissingService:
                    result.back().contract.requirements.push_back(
                        cordis::ServiceRequirement::of<int>({"future.absent"}));
                    break;
                case FutureIssue::Cycle:
                    result.back().contract.requirements.push_back(
                        cordis::ServiceRequirement::of<int>({"future.cycle"}));
                    result.back().contract.outputs.push_back(cordis::ServiceOutput::of<int>({"future.cycle"}));
                    break;
                case FutureIssue::UnknownRealm:
                    result.back().realms = result.back().realms.isolate("owner.child", cordis::RealmId{999999}).value();
                    break;
                case FutureIssue::Version:
                    result.back().contract.requirements.push_back(
                        cordis::ServiceRequirement::of<int>({"owner.child"}, 1));
                    break;
                case FutureIssue::None:
                    break;
                }
            }
            if (!plan.recovery && omitFuture)
                result.pop_back();
            if (!plan.recovery && addCandidateChild) {
                const auto leaf = std::find_if(plan.members.begin(), plan.members.end(), [&](const auto& item) {
                    return std::none_of(plan.members.begin(), plan.members.end(),
                                        [&](const auto& child) { return child.parent == item.key; });
                });
                CHECK(leaf != plan.members.end());
                result.push_back(
                    {100, leaf->key, "new-child", leaf->realms, {childFactory.requirements(), childFactory.outputs()}});
            }
            return result;
        };
        return cordis::Result<cordis::ReplacementOwnerPreflight>::success({describe(candidate), describe(recovery)});
    }
    cordis::Task<cordis::ReplacementRebuiltTree> rebuild(cordis::ReplacementBuilder builder,
                                                         cordis::ReplacementBuildPlan plan) override
    {
        ++rebuilt;
        if (!plan.recovery && suspendCandidate) {
            const auto root =
                std::find_if(plan.members.begin(), plan.members.end(), [](const auto& item) { return !item.parent; });
            CHECK(root != plan.members.end() && root->factory);
            previousBuilder = builder;
            previousFactory = root->factory;
            entered.resolve();
            return pending.task();
        }
        if (probeStale) {
            const auto root =
                std::find_if(plan.members.begin(), plan.members.end(), [](const auto& item) { return !item.parent; });
            CHECK(root != plan.members.end() && root->factory);
            if (!plan.recovery) {
                previousBuilder = builder;
                previousFactory = root->factory;
                cordis::Promise<cordis::ReplacementRebuiltTree> rejected;
                rejected.reject(
                    cordis::makeError(cordis::ErrorCode::ActivationFailed, "candidate rejected before create"));
                return rejected.task();
            }
            staleRejected =
                !previousBuilder->stage(*previousFactory, root->config, root->realms, root->additionalDependencies)
                     .hasValue();
        }
        auto build = std::make_shared<Build>(scheduler, childFactory, std::move(plan), std::move(builder));
        build->business = business;
        build->candidateFactories = candidateFactories;
        observed = std::make_shared<cordis::ReplacementRebuiltTree>();
        build->observed = observed;
        build->omit = !build->plan.recovery && omitCandidate;
        build->wrongParent = !build->plan.recovery && wrongCandidateParent;
        const auto root = std::find_if(build->plan.members.begin(), build->plan.members.end(),
                                       [](const auto& item) { return !item.parent; });
        CHECK(root != build->plan.members.end());
        build->create(root->key, {});
        return build->done.task();
    }
};

template <class SceneType, class Wait>
void operationSubtreeCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                          const std::shared_ptr<cordis::IScheduler>& scheduler,
                          const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    const auto take = [](auto result, const char* step) {
        if (!result.hasValue())
            throw std::runtime_error(std::string(step) + ": " + result.error().message);
        return std::move(result).value();
    };
    struct Counts {
        std::size_t starts = 0, cleanups = 0;
    };
    const auto counts = std::make_shared<Counts>();
    const bool suspendInput = mode == "operation_subtree_input_suspend";
    std::optional<ProviderHandle> candidateUpstream;
    if (suspendInput) {
        candidateUpstream =
            take(kernel->services().stage<int>({"candidate.upstream"}, std::make_shared<int>(1)), "candidate upstream");
        candidateUpstream->publish().value();
        const auto source =
            take(kernel->registerPlugin("candidate.source",
                                        [](Context context) {
                                            context.provide<int>({"candidate.only"}, std::make_shared<int>(2)).value();
                                            return completedTask();
                                        },
                                        {ServiceRequirement::of<int>({"candidate.upstream"})},
                                        {ServiceOutput::of<int>({"candidate.only"})}),
                 "candidate source");
        const auto instance = take(kernel->mount(source), "candidate source instance");
        CHECK(waitFor(instance.ready())->hasValue());
    }
    const auto realm = take(kernel->services().createRealm(), "child realm");
    std::optional<ProviderHandle> upstream;
    if (mode == "operation_subtree_provider_loss") {
        upstream = take(kernel->services().stage<int>({"source.upstream"}, std::make_shared<int>(1)), "upstream");
        upstream->publish().value();
    }
    const auto provider =
        take(kernel->registerPlugin(
                 "subtree.input",
                 [](Context context) {
                     context.provide<int>({"owner.child"}, std::make_shared<int>(7)).value();
                     return completedTask();
                 },
                 upstream ? std::vector<ServiceRequirement>{ServiceRequirement::of<int>({"source.upstream"})}
                          : std::vector<ServiceRequirement>{},
                 {ServiceOutput::of<int>({"owner.child"})}),
             "provider definition");
    const auto provided =
        take(kernel->mount(provider, {}, RealmMap{}.isolate("owner.child", realm).value()), "private provider");
    CHECK(waitFor(provided.ready())->hasValue());
    Runtime transient(
        [counts](Context context) {
            ++counts->starts;
            context
                .onCleanup([counts](CleanupContext) {
                    ++counts->cleanups;
                    return completedTask();
                })
                .value();
            return completedTask();
        },
        {ServiceRequirement::of<int>({"owner.child"})});
    const bool nested = mode.startsWith("operation_subtree_nested");
    const auto hostRoot =
        nested ? kernel->registerPlugin("subtree.owner", [](Context) { return completedTask(); }).value() : old;
    const auto root = take(kernel->mount(hostRoot, scene.qt ? "normal" : "\"sync\""), "root");
    CHECK(waitFor(root.ready())->hasValue());
    const auto rootContext = take(*root.whenActiveAfter().tryResult(), "root context");
    const auto isolated = take(rootContext.isolate("owner.child", realm), "child context");
    const auto first =
        take(isolated.inject(nested ? old : transient, nested ? (scene.qt ? "normal" : "\"sync\"") : "first"),
             "first child");
    CHECK(waitFor(first.ready())->hasValue());
    const auto second =
        take(take(*first.whenActiveAfter().tryResult(), "first context").inject(transient, "second"), "second child");
    CHECK(waitFor(second.ready())->hasValue());
    auto owner = std::make_shared<SubtreeReplacementOwner>(scheduler, transient);
    if (suspendInput) {
        owner->suspendCandidate = true;
        owner->candidateFactories.emplace("second", Runtime([](Context) { return completedTask(); },
                                                            {ServiceRequirement::of<int>({"owner.child"}),
                                                             ServiceRequirement::of<int>({"candidate.only"})}));
    }
    if (nested)
        owner->business.emplace(old, next);
    owner->omitCandidate = mode == "operation_subtree_omit" || mode == "operation_subtree_nested_omit";
    owner->wrongCandidateParent = mode == "operation_subtree_parent";
    owner->addCandidateChild = mode == "operation_subtree_future_child";
    owner->lieCandidateContract = mode == "operation_subtree_future_contract";
    owner->omitFuture = mode == "operation_subtree_future_missing";
    using RecoveryIssue = SubtreeReplacementOwner::RecoveryIssue;
    if (mode == "operation_subtree_recovery_config")
        owner->recoveryIssue = RecoveryIssue::Config;
    else if (mode == "operation_subtree_recovery_realm")
        owner->recoveryIssue = RecoveryIssue::Realm;
    else if (mode == "operation_subtree_recovery_requirements")
        owner->recoveryIssue = RecoveryIssue::Requirements;
    else if (mode == "operation_subtree_recovery_outputs")
        owner->recoveryIssue = RecoveryIssue::Outputs;
    owner->equivalentRecovery = mode == "operation_subtree_recovery_equivalent";
    if (owner->equivalentRecovery)
        owner->omitCandidate = true;
    using FutureIssue = SubtreeReplacementOwner::FutureIssue;
    if (mode == "operation_subtree_future_missing_service")
        owner->futureIssue = FutureIssue::MissingService;
    else if (mode == "operation_subtree_future_cycle")
        owner->futureIssue = FutureIssue::Cycle;
    else if (mode == "operation_subtree_future_realm")
        owner->futureIssue = FutureIssue::UnknownRealm;
    else if (mode == "operation_subtree_future_version")
        owner->futureIssue = FutureIssue::Version;
    owner->probeStale = mode == "operation_subtree_nested_stale";
    const auto request = [&] {
        ReplacementRequest value;
        value.scheduler = scheduler;
        value.kernel = kernel;
        value.modules = scene.managerOwner;
        value.previous = scene.current;
        value.candidate = scene.candidate;
        value.definitions.push_back({old.id(), next});
        value.owners.emplace(root.snapshot().value().instance, owner);
        return value;
    };
    auto started = ReplacementOperation::begin(request());
    if (owner->recoveryIssue != RecoveryIssue::None) {
        CHECK(!started.hasValue());
        CHECK(started.error().code == ErrorCode::ConfigConflict &&
              started.error().message == "recovery dynamic member differs from original input");
        CHECK(owner->checked == 1 && owner->rebuilt == 0 && counts->starts == 2 && counts->cleanups == 0);
        CHECK(root.snapshot().value().state == InstanceState::Active &&
              first.snapshot().value().state == InstanceState::Active &&
              second.snapshot().value().state == InstanceState::Active);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(scene.state(scene.current).role == module::GenerationRole::Current);
        owner->recoveryIssue = RecoveryIssue::None;
        auto retry = take(ReplacementOperation::begin(request()), "retry corrected recovery plan");
        CHECK(retry.cancel().hasValue());
        const auto cancelled = waitFor(retry.settled());
        CHECK(cancelled && cancelled->hasValue() && cancelled->value().outcome == ReplacementOutcome::Cancelled);
        CHECK(!cancelled->value().resourcesRetained && owner->checked == 2 && owner->rebuilt == 0);
        CHECK(root.snapshot().value().state == InstanceState::Active &&
              first.snapshot().value().state == InstanceState::Active &&
              second.snapshot().value().state == InstanceState::Active);
        CHECK(waitFor(kernel->close())->value().quiescent && counts->cleanups == counts->starts);
        return;
    }
    if (owner->futureIssue != FutureIssue::None) {
        CHECK(!started.hasValue());
        const auto expected = owner->futureIssue == FutureIssue::Cycle            ? ErrorCode::DependencyCycle
                              : owner->futureIssue == FutureIssue::MissingService ? ErrorCode::DependencyMissing
                                                                                  : ErrorCode::ConfigInvalid;
        CHECK(started.error().code == expected);
        CHECK(owner->rebuilt == 0 && root.snapshot().value().state == InstanceState::Active);
        CHECK(first.snapshot().value().state == InstanceState::Active &&
              second.snapshot().value().state == InstanceState::Active);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(waitFor(kernel->close())->value().quiescent && counts->cleanups == counts->starts);
        return;
    }
    if (owner->omitFuture) {
        CHECK(!started.hasValue() && started.error().message == "owner omitted future dynamic contract" &&
              owner->rebuilt == 0);
        CHECK(root.snapshot().value().state == InstanceState::Active);
        CHECK(first.snapshot().value().state == InstanceState::Active &&
              second.snapshot().value().state == InstanceState::Active);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(waitFor(kernel->close())->value().quiescent && counts->cleanups == counts->starts);
        return;
    }
    if (!started.hasValue())
        throw std::runtime_error(started.error().message);
    auto operation = std::move(started).value();
    if (suspendInput) {
        CHECK(waitFor(owner->entered.task())->hasValue());
        CHECK(candidateUpstream->retire().hasValue());
    }
    if (mode == "operation_subtree_provider_guard") {
        const auto operationId = operation.snapshot().value().report.operation;
        const auto stopped = provided.stop().tryResult();
        CHECK(stopped && !stopped->hasValue() && stopped->error().code == ErrorCode::ConfigConflict &&
              stopped->error().operation == operationId);
        const auto updated = provided.update("changed", provided.snapshot().value().revision);
        CHECK(!updated.hasValue() && updated.error().code == ErrorCode::ConfigConflict &&
              updated.error().operation == operationId);
        CHECK(provided.snapshot().value().state == InstanceState::Active);
        CHECK(operation.cancel().hasValue());
        const auto cancelled = waitFor(operation.settled());
        CHECK(cancelled && cancelled->hasValue() && cancelled->value().outcome == ReplacementOutcome::Cancelled &&
              !cancelled->value().resourcesRetained && owner->rebuilt == 0);
        CHECK(waitFor(provided.stop())->value().quiescent);
        CHECK(waitFor(kernel->close())->value().quiescent && counts->cleanups == counts->starts);
        return;
    }
    if (mode == "operation_subtree_provider_loss") {
        CHECK(upstream && upstream->retire().hasValue());
        auto result = waitFor(operation.settled());
        CHECK(result && result->hasValue() && result->value().outcome == ReplacementOutcome::Failed);
        CHECK(!result->value().committed && !result->value().recovered && !result->value().resourcesRetained);
        CHECK(result->value().instances.empty() && owner->rebuilt == 0);
        CHECK(std::any_of(result->value().errors.begin(), result->value().errors.end(),
                          [](const auto& error) { return error.code == ErrorCode::DependencyMissing; }));
        CHECK(root.snapshot().value().state == InstanceState::Active);
        CHECK(waitFor(upstream->whenUnused())->hasValue());
        CHECK(upstream->release().hasValue());
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
        CHECK(waitFor(kernel->close())->value().quiescent && counts->cleanups == counts->starts);
        return;
    }
    auto completed = waitFor(operation.settled());
    CHECK(completed && completed->hasValue());
    const auto report = completed->value();
    const bool restored = owner->omitCandidate || owner->wrongCandidateParent || owner->probeStale ||
                          owner->lieCandidateContract || suspendInput;
    if (owner->probeStale)
        CHECK(owner->staleRejected);
    if (report.outcome != (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied))
        for (const auto& error : report.errors)
            std::cerr << error.message << " [" << error.phase << "]\n";
    CHECK(report.outcome == (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied));
    CHECK(report.committed == !restored && report.recovered == restored && !report.resourcesRetained);
    if (suspendInput) {
        CHECK(std::any_of(report.errors.begin(), report.errors.end(),
                          [](const auto& error) { return error.code == ErrorCode::DependencyMissing; }));
        CHECK(!owner->previousBuilder->stage(*owner->previousFactory).hasValue());
        owner->pending.resolve({}); // 晚到拥有者不能改变已经完成的真实恢复。
        CHECK(operation.snapshot().value().report.recovered && !operation.snapshot().value().report.committed);
    }
    if (owner->lieCandidateContract)
        CHECK(std::any_of(report.errors.begin(), report.errors.end(), [](const auto& error) {
            return error.message == "actual service contract differs from preflight";
        }));
    CHECK(report.instances.size() == 3 && owner->checked == 1 && owner->rebuilt == (restored ? 2U : 1U));
    const auto current = [&](const Instance& previous) {
        const auto id = previous.snapshot().value().instance;
        const auto found = std::find_if(report.instances.begin(), report.instances.end(),
                                        [&](const auto& item) { return item.previous == id; });
        CHECK(found != report.instances.end() && found->current != id);
        const auto owned =
            std::find_if(owner->observed->members.begin(), owner->observed->members.end(),
                         [&](const auto& item) { return item.instance.snapshot().value().instance == found->current; });
        CHECK(owned != owner->observed->members.end());
        auto instance = owned->instance;
        CHECK(instance.snapshot().value().state == InstanceState::Active);
        CHECK(previous.snapshot().value().state == InstanceState::Disposed);
        return instance;
    };
    CHECK(current(root).snapshot().value().children == 1);
    CHECK(current(first).snapshot().value().children == 1);
    CHECK(current(first).snapshot().value().runtime == (nested ? (restored ? old.id() : next.id()) : transient.id()));
    CHECK(current(second).whenActiveAfter().tryResult()->value().config() == "second");
    const auto dependencies = current(second).dependencies().value();
    CHECK(dependencies.size() == 1 && dependencies[0].key.realm == realm && !dependencies[0].error);
    const auto dynamicCount = nested ? 1U : 2U;
    const auto extra = owner->addCandidateChild ? 1U : 0U;
    CHECK(current(second).snapshot().value().children == extra);
    CHECK(counts->starts == dynamicCount * (restored && !owner->probeStale && !suspendInput ? 3U : 2U) + extra &&
          counts->cleanups == counts->starts - dynamicCount - extra);
    CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    CHECK(waitFor(kernel->close())->value().quiescent);
    if (candidateUpstream) {
        CHECK(waitFor(candidateUpstream->whenUnused())->hasValue());
        CHECK(candidateUpstream->release().hasValue());
    }
    CHECK(counts->cleanups == counts->starts);
}
