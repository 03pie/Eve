/** @file replacement_state_tree.hpp
 * @brief 真实 DLL 拥有树上的状态兑现、服务消费者、预算和完整旧值恢复。
 */
#pragma once
#include "../../src/module/catalog_registry_p.hpp"
#include "../../src/module/reservation_p.hpp"
#include "replacement_owner.hpp"
#include "replacement_peer.hpp"
#include <cordis/loader/qt_store.hpp>
#include <iostream>
#include <limits>

namespace replacement_state_tree {
using namespace cordis;

struct Value {
    std::string name, json = "0", observed, dependency;
    ContextIdentity identity;
    std::uint32_t version = 0;
    std::size_t restores = 0;
    bool ready = false;
};
struct Observations {
    // 仅保存测试值和实际激活身份，不持有 Context、工厂或另一套运行实例目录。
    std::vector<std::shared_ptr<Value>> values;
    std::size_t failConsumers = 0;
    std::function<void()> validateHook;
    std::shared_ptr<Value> latest(const std::string& name) const
    {
        const auto at =
            std::find_if(values.rbegin(), values.rend(), [&](const auto& value) { return value->name == name; });
        CHECK(at != values.rend());
        return *at;
    }
};
inline Runtime counter(const std::shared_ptr<Observations>& seen, std::uint32_t version, bool provider = false,
                       bool consumer = false, bool wide = false, std::vector<ServiceRequirement> requirements = {})
{
    if (consumer)
        requirements.push_back(ServiceRequirement::of<Value>({"state.tree"}));
    return Runtime::stateful(
        [seen, version, provider, consumer](Context context) {
            auto value = std::make_shared<Value>();
            value->name = context.config();
            value->identity = context.identity().value();
            value->version = version;
            seen->values.push_back(value);
            auto imported = context.state({[value] { return Result<std::string>::success(value->json); },
                                           [value](std::string_view json) {
                                               value->json = json;
                                               ++value->restores;
                                               return Result<void>::success();
                                           }});
            if (!imported.hasValue())
                return failedTask(imported.error());
            value->observed = value->json; // 必须在实际激活入口看到导入值。
            if (provider)
                context.provide<Value>({"state.tree"}, value).value();
            if (consumer) {
                context.consume<Value>({"state.tree"})
                    .value()
                    .withService([&](Value& source) { value->dependency = source.json; })
                    .value();
                if (seen->failConsumers) {
                    --seen->failConsumers;
                    return failedTask(makeError(ErrorCode::ActivationFailed, "state consumer activation rejected"));
                }
            }
            return completedTask();
        },
        {{"tree.counter", version},
         [seen, version, consumer, wide](const StateSnapshot& source) {
             if (consumer && seen->validateHook) {
                 auto hook = std::move(seen->validateHook);
                 seen->validateHook = {};
                 hook();
             }
             if (source.schema.id != "tree.counter" || source.schema.version != 1)
                 return Result<std::string>::failure(makeError(ErrorCode::ConfigInvalid, "unexpected original state"));
             return Result<std::string>::success(version == 2 && !wide ? source.json + '0' : source.json);
         }},
        std::move(requirements),
        provider ? std::vector<ServiceOutput>{ServiceOutput::of<Value>({"state.tree"})} : std::vector<ServiceOutput>{});
}

class Owner final : public IReplacementOwner {
    struct Build : std::enable_shared_from_this<Build> {
        std::shared_ptr<IScheduler> scheduler;
        std::map<std::string, Runtime> factories;
        ReplacementBuilder builder;
        ReplacementBuildPlan plan;
        ReplacementRebuiltTree tree;
        Promise<ReplacementRebuiltTree> done;
        std::shared_ptr<Observations> observations;
        std::size_t remaining;
        Build(std::shared_ptr<IScheduler> queue, std::map<std::string, Runtime> inputs, ReplacementBuilder permission,
              ReplacementBuildPlan future, std::shared_ptr<Observations> seen)
            : scheduler(std::move(queue)), factories(std::move(inputs)), builder(std::move(permission)),
              plan(std::move(future)), observations(std::move(seen)), remaining(plan.members.size())
        {
        }
        void create(std::size_t key, std::optional<Context> parent)
        {
            if (done.task().isReady())
                return;
            const auto at = std::find_if(plan.members.begin(), plan.members.end(),
                                         [key](const auto& item) { return item.key == key; });
            CHECK(at != plan.members.end());
            if (parent)
                for (const auto& realm : at->realms.overrides())
                    parent = parent->isolate(realm.first, realm.second).value();
            const auto& factory = at->factory ? *at->factory : factories.at(at->config);
            auto made = parent ? builder.injectMember(key, *parent, factory, at->config, at->additionalDependencies)
                               : builder.stageMember(key, factory, at->config, at->realms, at->additionalDependencies);
            if (!made.hasValue()) {
                done.reject(made.error());
                return;
            }
            const auto instance = made.value();
            tree.members.push_back({key, instance});
            auto receipt = instance.ready().thenOn(
                scheduler, [self = shared_from_this(), key, instance](const Result<void>& ready) {
                    if (self->done.task().isReady())
                        return;
                    if (!ready.hasValue()) {
                        self->done.reject(ready.error());
                        return;
                    }
                    const auto context = instance.whenActiveAfter().tryResult();
                    CHECK(context && context->hasValue());
                    if (self->observations)
                        for (const auto& value : self->observations->values)
                            if (value->identity.instance == instance.snapshot().value().instance)
                                value->ready = true;
                    for (const auto& child : self->plan.members)
                        if (child.parent && *child.parent == key)
                            self->create(child.key, context->value());
                    if (--self->remaining == 0)
                        self->done.resolve(std::move(self->tree));
                });
            cordis::detail::TaskAccess::observe(receipt, [promise = done](Task<void>::Snapshot result) {
                if (!result->hasValue())
                    promise.reject(result->error());
            });
        }
    };

public:
    enum class Change { Keep, ReplaceBeta, ParentOnly, ChildOfRemovedParent, ConfiguredChild };
    std::shared_ptr<IScheduler> scheduler;
    std::map<std::string, Runtime> candidate, recovery;
    std::shared_ptr<Observations> observations;
    Change change = Change::Keep;
    std::size_t checked = 0, rebuilt = 0;
    explicit Owner(std::shared_ptr<IScheduler> queue) : scheduler(std::move(queue)) {}
    Result<ReplacementOwnerPreflight> preflight(const ReplacementBuildPlan& next,
                                                const ReplacementBuildPlan& previous) override
    {
        ++checked;
        ReplacementOwnerPreflight result;
        result.stateAware = true;
        const auto root =
            std::find_if(next.members.begin(), next.members.end(), [](const auto& item) { return !item.parent; });
        CHECK(root != next.members.end());
        std::optional<std::size_t> removedParent;
        for (const auto& item : next.members) {
            const bool erase = change == Change::ReplaceBeta  ? item.config == "beta"
                               : change == Change::ParentOnly ? item.config == "alpha"
                               : change == Change::ChildOfRemovedParent
                                   ? item.config == "alpha" || item.config == "beta"
                               : change == Change::ConfiguredChild ? item.parent && item.factory.has_value()
                                                                   : false;
            if (erase) {
                result.removed.push_back(item.key);
                if (item.config == "alpha")
                    removedParent = item.key;
            }
        }
        for (const auto* plan : {&next, &previous}) {
            auto& descriptions = plan->recovery ? result.recovery : result.candidate;
            const auto& factories = plan->recovery ? recovery : candidate;
            for (const auto& item : plan->members) {
                if (item.factory || (!plan->recovery && std::find(result.removed.begin(), result.removed.end(),
                                                                  item.key) != result.removed.end()))
                    continue;
                const auto& factory = factories.at(item.config);
                auto requirements = factory.requirements();
                requirements.insert(requirements.end(), item.additionalDependencies.begin(),
                                    item.additionalDependencies.end());
                descriptions.push_back({item.key,
                                        item.parent,
                                        item.config,
                                        item.realms,
                                        {std::move(requirements), factory.outputs()},
                                        item.source});
                // 本夹具所有动态候选均为内置工厂，恢复仍使用实际旧成员来源。
                if (!plan->recovery)
                    descriptions.back().source.reset();
            }
        }
        if (change == Change::ReplaceBeta || change == Change::ChildOfRemovedParent) {
            const auto& factory = candidate.at("gamma");
            result.candidate.push_back({std::numeric_limits<std::size_t>::max() - 1,
                                        removedParent ? *removedParent : root->key,
                                        "gamma",
                                        {},
                                        {factory.requirements(), factory.outputs()},
                                        {}});
        }
        return Result<ReplacementOwnerPreflight>::success(std::move(result));
    }
    Task<ReplacementRebuiltTree> rebuild(ReplacementBuilder builder, ReplacementBuildPlan plan) override
    {
        ++rebuilt;
        auto build = std::make_shared<Build>(scheduler, plan.recovery ? recovery : candidate, std::move(builder),
                                             std::move(plan), observations);
        const auto root = std::find_if(build->plan.members.begin(), build->plan.members.end(),
                                       [](const auto& item) { return !item.parent; });
        CHECK(root != build->plan.members.end());
        build->create(root->key, {});
        return build->done.task();
    }
};
inline bool hasError(const ErrorInfo& error, ErrorCode code)
{
    return error.code == code || std::any_of(error.causes.begin(), error.causes.end(),
                                             [code](const auto& cause) { return hasError(cause, code); });
}
inline bool hasError(const ReplacementReport& report, ErrorCode code)
{
    return std::any_of(report.errors.begin(), report.errors.end(),
                       [code](const auto& error) { return hasError(error, code); });
}

template <class SceneType, class Wait>
void configuredRemoval(SceneType& scene, const Runtime& old, const Runtime& next,
                       const std::shared_ptr<IScheduler>& scheduler, const std::shared_ptr<Kernel>& kernel,
                       Wait waitFor)
{
    using namespace loader;
    const auto child = kernel->registerPlugin("configured.child", [](Context) { return completedTask(); }).value();
    const auto id = scene.qt ? "org.cordis.qt.page" : "org.cordis.plugin";
    const auto schema = [](const std::string&) { return Result<void>::success(); };
    const auto original = ConfigurationDefinitions::create({{{id, {}, schema, {"business", "old/module.json"}}, old},
                                                            {{"configured.child", {}, schema}, child}})
                              .value();
    const auto following = original->replacing({{{id, {}, schema, {"business", "next/module.json"}}, next}}).value();
    const auto text = [&](const char* directory) {
        return std::string("formatVersion: 1\nmodules: {business: {manifest: ") + directory +
               "/module.json}}\nplugins: [{id: root, plugin: " + id +
               ", module: business, config: " + (scene.qt ? "normal" : "sync") +
               ", children: [{id: child, plugin: configured.child}]}]\n";
    };
    const auto driver = KernelReconcileDriver::configured(scheduler, kernel, original);
    const auto path = scene.root + "/state-tree.yaml";
    const auto session = std::make_shared<ConfigurationSession>(
        scheduler, driver, std::make_shared<QtConfigurationWriter>(path), readConfigurationFile(path).value());
    CHECK(waitFor(session->apply(session->preview(text("old")).value()).value().settled)->value().outcome ==
          PlanOutcome::Applied);
    const auto before = driver->snapshot().value();
    const auto root = std::find_if(before.begin(), before.end(), [](const auto& item) { return item.path == "root"; });
    CHECK(root != before.end() && before.size() == 2);
    auto owner = std::make_shared<Owner>(scheduler);
    owner->change = Owner::Change::ConfiguredChild;
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.pluginRoot = scene.root.toStdString();
    request.definitions.push_back({old.id(), next});
    request.configurations.push_back({session, driver, following,
                                      std::make_shared<const ValidatedCandidate>(
                                          following->validate(parseConfiguration(text("next")).value()).value()),
                                      1});
    request.owners.emplace(root->instance, owner);
    const auto started = ReplacementOperation::begin(std::move(request));
    CHECK(!started.hasValue() && started.error().code == ErrorCode::ConfigConflict && owner->rebuilt == 0);
    const auto after = driver->snapshot().value();
    CHECK(after.size() == before.size());
    for (std::size_t i = 0; i != before.size(); ++i)
        CHECK(after[i].instance == before[i].instance && after[i].state == InstanceState::Active);
    CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    CHECK(waitFor(session->close())->hasValue());
    CHECK(waitFor(kernel->close())->value().quiescent);
}
} // namespace replacement_state_tree

template <class SceneType, class Wait>
bool operationStateTreeCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                            const std::shared_ptr<cordis::IScheduler>& scheduler,
                            const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    using namespace replacement_state_tree;
    if (mode == "operation_state_tree_removed_config") {
        configuredRemoval(scene, old, next, scheduler, kernel, waitFor);
        return false;
    }
    const bool budget = mode.startsWith("operation_state_tree_budget");
    const bool recoverySource = mode == "operation_state_tree_source_recovery";
    const bool sourceFault = mode == "operation_state_tree_source" || recoverySource;
    const bool legacy = mode == "operation_state_tree_legacy";
    const bool mixed = legacy || mode == "operation_state_tree_mixed";
    const bool restored = mode == "operation_state_tree_recovery" || mode == "operation_state_tree_delete_recovery";
    const bool failedConsumer = mode == "operation_state_tree_recovery_consumer";
    const bool deleted = mode == "operation_state_tree_delete" || mode == "operation_state_tree_delete_recovery";
    const bool invalidParent =
        mode == "operation_state_tree_removed_parent" || mode == "operation_state_tree_removed_new_parent";
    auto seen = std::make_shared<Observations>();
    auto owner = std::make_shared<Owner>(scheduler);
    owner->observations = seen;
    const auto root = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
    CHECK(waitFor(root.ready())->hasValue());
    const auto rootContext = root.whenActiveAfter().tryResult()->value();
    std::optional<ReplacementPeer> peer;
    InstanceId externalParent, externalProvider;
    if (sourceFault)
        peer.emplace(openReplacementPeer(scene, scheduler, kernel));
    if (recoverySource) {
        const auto factory = kernel->registerRuntime("recovery.peer", peer->previousRuntime).value();
        const auto parent = kernel->mount(factory, "\"sync\"").value();
        CHECK(waitFor(parent.ready())->hasValue());
        externalParent = parent.snapshot().value().instance;
        Runtime provider(
            [](Context context) {
                context.provide<int>({"recovery.only"}, std::make_shared<int>(7)).value();
                return completedTask();
            },
            {}, {ServiceOutput::of<int>({"recovery.only"})});
        const auto child = parent.whenActiveAfter().tryResult()->value().inject(provider).value();
        CHECK(waitFor(child.ready())->hasValue());
        externalProvider = child.snapshot().value().instance;
    }
    std::vector<Instance> originals{root};
    std::vector<std::shared_ptr<Value>> originalValues;
    const auto add = [&](const std::string& name, const Context& parent, bool provides) {
        const auto original =
            counter(seen, 1, provides, false, budget,
                    recoverySource && name == "alpha"
                        ? std::vector<ServiceRequirement>{ServiceRequirement::of<int>({"recovery.only"})}
                        : std::vector<ServiceRequirement>{});
        const auto following = counter(seen, 2, provides, false, budget);
        owner->recovery.emplace(name, original);
        owner->candidate.emplace(name, following);
        const auto instance = parent.inject(original, name).value();
        CHECK(waitFor(instance.ready())->hasValue());
        originals.push_back(instance);
        originalValues.push_back(seen->latest(name));
        return instance.whenActiveAfter().tryResult()->value();
    };
    if (budget) {
        const auto count = mode.endsWith("_boundary") ? 15 : mode.endsWith("_full") ? 16 : 17;
        const std::string json = '"' + std::string(maxStateJsonBytes - 2, 'x') + '"';
        for (int i = 0; i != count; ++i) {
            add("n" + std::to_string(i), rootContext, false);
            originalValues.back()->json = json;
        }
    } else {
        const auto alpha = add("alpha", rootContext, !mixed && !invalidParent);
        add("beta", invalidParent ? alpha : rootContext, false);
        originalValues[0]->json = "31";
        originalValues[1]->json = "71";
    }
    if (sourceFault && !recoverySource) {
        auto instance = rootContext.inject(peer->previousRuntime, "\"sync\"").value();
        CHECK(waitFor(instance.ready())->hasValue());
        // 原生业务需要 JSON 配置；拥有者在候选/恢复中都沿实际原配置定位该不透明成员。
        owner->recovery.emplace("\"sync\"", peer->previousRuntime);
        owner->candidate.emplace("\"sync\"", Runtime([](Context) { return completedTask(); }));
        originals.push_back(instance);
    }
    if (!budget && !mixed && !invalidParent) {
        const auto consumer = kernel->registerRuntime("state.consumer", counter(seen, 1, false, true)).value();
        const auto instance = kernel->mount(consumer, "consumer").value();
        CHECK(waitFor(instance.ready())->hasValue());
        originals.push_back(instance);
        seen->latest("consumer")->json = "93";
    }
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.definitions.push_back({old.id(), next});
    request.owners.emplace(root.snapshot().value().instance, owner);
    std::shared_ptr<OperationOwnerProbe> legacyOwner;
    std::shared_ptr<Owner> keyedOwner;
    if (mixed) {
        const auto other = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
        CHECK(waitFor(other.ready())->hasValue());
        originals.push_back(other);
        if (legacy) {
            legacyOwner = std::make_shared<OperationOwnerProbe>();
            legacyOwner->scheduler = scheduler;
            request.owners.emplace(other.snapshot().value().instance, legacyOwner);
        } else {
            keyedOwner = std::make_shared<Owner>(scheduler);
            request.owners.emplace(other.snapshot().value().instance, keyedOwner);
        }
    }
    if (deleted) {
        owner->change = Owner::Change::ReplaceBeta;
        owner->candidate.emplace("gamma", counter(seen, 2));
    } else if (invalidParent) {
        owner->change = mode.endsWith("_new_parent") ? Owner::Change::ChildOfRemovedParent : Owner::Change::ParentOnly;
        owner->candidate.emplace("gamma", counter(seen, 2));
    }
    seen->failConsumers = failedConsumer ? 2U : restored ? 1U : 0U;
    if (sourceFault)
        seen->validateHook = [&] {
            const auto scopes = kernel->replacements().value();
            CHECK(scopes.size() == 1);
            if (recoverySource)
                for (const auto& member : scopes.front().closure.members)
                    CHECK(member.instance != externalParent && member.instance != externalProvider);
            // 直接读取实际预约，并调用生产的路线判定；不自行构造 required/original 标志。
            auto& catalog = cordis::module::detail::moduleCatalog();
            {
                std::lock_guard<std::mutex> lock(catalog.mutex);
                const auto held =
                    std::find_if(catalog.replacements.begin(), catalog.replacements.end(), [&](const auto& entry) {
                        return entry.second->operation == scopes.front().operation;
                    });
                CHECK(held != catalog.replacements.end());
                CHECK(cordis::module::detail::requiresGeneration(*held->second, peer->previous, false) ==
                      !recoverySource);
                CHECK(cordis::module::detail::requiresGeneration(*held->second, peer->previous, true));
            }
            const auto fault =
                peer->previousModule.create("org.cordis.peer.plugin", "\"create-live-error\"", InstanceId{100001}, 1);
            CHECK(!fault.hasValue() && fault.error().code == ErrorCode::AbiProtocol);
        };
    const auto started = ReplacementOperation::begin(std::move(request));
    if (legacy || invalidParent) {
        CHECK(!started.hasValue() && owner->rebuilt == 0);
        CHECK(started.error().code == (legacy ? ErrorCode::ConfigInvalid : ErrorCode::ConfigConflict));
        for (const auto& instance : originals)
            CHECK(instance.snapshot().value().state == InstanceState::Active && instance.snapshot().value().epoch == 1);
        if (legacyOwner)
            CHECK(legacyOwner->rebuilt == 0);
    } else {
        CHECK(started.hasValue());
        const auto completed = waitFor(started.value().settled());
        CHECK(completed && completed->hasValue());
        const auto& report = completed->value();
        if (sourceFault) {
            CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && owner->rebuilt == 0);
            CHECK(!report.committed && !report.recovered && report.instances.empty() &&
                  hasError(report, ErrorCode::AbiProtocol));
            CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
            CHECK(scene.state(peer->previous).businessFailure);
            CHECK(waitFor(kernel->close())->hasValue());
            const auto close = waitFor(peer->previousModule.close());
            CHECK(!close->hasValue() && close->error().code == ErrorCode::ConfigConflict);
            return true;
        }
        if (failedConsumer) {
            CHECK(report.outcome == ReplacementOutcome::Failed && report.resourcesRetained && !report.committed &&
                  !report.recovered && report.instances.empty());
            CHECK(hasError(report, ErrorCode::RecoveryFailed) && hasError(report, ErrorCode::ActivationFailed));
            CHECK(owner->rebuilt == 2 && seen->failConsumers == 0);
            for (const auto& original : originalValues) {
                const auto actual = seen->latest(original->name);
                CHECK(actual->identity.instance != original->identity.instance && actual->identity.epoch == 1);
                CHECK(actual->version == 1 && actual->restores == 1 && actual->observed == original->json &&
                      actual->ready);
            }
            const auto consumer = seen->latest("consumer");
            CHECK(consumer->observed == "93" && consumer->restores == 1 && consumer->dependency == "31");
            CHECK(std::count_if(seen->values.begin(), seen->values.end(),
                                [](const auto& value) { return value->name == "consumer"; }) == 3);
            for (const auto& instance : originals)
                CHECK(instance.snapshot().value().state == InstanceState::Disposed);
            CHECK(scene.state(scene.current).role == module::GenerationRole::Current &&
                  scene.state(scene.candidate).role == module::GenerationRole::Candidate);
            CHECK(scene.state(scene.current).replacement && scene.state(scene.candidate).replacement);
            CHECK(waitFor(kernel->close())->value().quiescent);
            for (const auto generation : {scene.current, scene.candidate})
                for (const auto& record : scene.state(generation).resources.records)
                    CHECK(record.kind != module::ResourceKind::Call && record.kind != module::ResourceKind::Instance);
            return true;
        }
        if (budget && !mode.endsWith("_boundary")) {
            CHECK(report.outcome == ReplacementOutcome::Failed && !report.resourcesRetained && owner->rebuilt == 0);
            CHECK(!report.committed && !report.recovered && report.instances.empty() &&
                  hasError(report, ErrorCode::InvalidArgument));
            for (const auto& instance : originals)
                CHECK(instance.snapshot().value().state == InstanceState::Active &&
                      instance.snapshot().value().epoch == 1);
        } else {
            if (report.outcome != (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied))
                for (const auto& error : report.errors)
                    std::cerr << error.message << " [" << error.phase << "]\n";
            CHECK(report.outcome == (restored ? ReplacementOutcome::Restored : ReplacementOutcome::Applied));
            CHECK(report.committed == !restored && report.recovered == restored && !report.resourcesRetained);
            CHECK(owner->rebuilt == (restored ? 2U : 1U));
            if (keyedOwner)
                CHECK(keyedOwner->rebuilt == 1);
            for (const auto& value : originalValues) {
                const auto removed = deleted && !restored && value->name == "beta";
                const auto mapping =
                    std::find_if(report.instances.begin(), report.instances.end(),
                                 [&](const auto& item) { return item.previous == value->identity.instance; });
                if (removed) {
                    CHECK(mapping == report.instances.end());
                    continue;
                }
                CHECK(mapping != report.instances.end() && mapping->current != mapping->previous);
                const auto actual = seen->latest(value->name);
                CHECK(actual->identity.instance == mapping->current && actual->identity.epoch == 1 &&
                      actual->restores == 1);
                CHECK(actual->observed == (budget || restored ? value->json : value->json + '0'));
                CHECK(actual->version == (restored ? 1U : 2U));
            }
            if (!budget && !mixed) {
                const auto consumer = seen->latest("consumer");
                CHECK(consumer->observed == "93" && consumer->restores == 1);
                CHECK(consumer->dependency == (restored ? "31" : "310"));
                if (restored)
                    CHECK(hasError(report, ErrorCode::ActivationFailed));
                if (deleted && !restored)
                    CHECK(seen->latest("gamma")->observed == "0" && seen->latest("gamma")->restores == 0);
            }
            for (const auto& instance : originals)
                CHECK(instance.snapshot().value().state == InstanceState::Disposed);
            CHECK(scene.state(restored ? scene.current : scene.candidate).role == module::GenerationRole::Current);
        }
    }
    CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    CHECK(waitFor(kernel->close())->value().quiescent);
    return false;
}
