/** @file configuration_commit.cpp
 * @brief 真实 Core/Driver/Session 的最终元数据提交；不代替完整模块替换操作。
 */
#include "../../src/core/instance_ownership_p.hpp"
#include "../../src/loader/configuration_commit_p.hpp"
#include <algorithm>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
using namespace cordis;
using namespace cordis::loader;
using cordis::loader::detail::ConfigurationCommitAccess;
using cordis::loader::detail::DriverCommitAccess;
using cordis::loader::detail::ReplacementEntry;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
template <class T> T take(Result<T> result, const char* phase)
{
    if (!result.hasValue())
        throw std::runtime_error(std::string(phase) + ": " + result.error().message + " [" + result.error().phase +
                                 "] " + result.error().entryPath.value_or(""));
    return std::move(result).value();
}
struct Scheduler final : IScheduler {
    testing::FakeScheduler queue;
    mutable int boundaries = 0;
    bool reject = false;
    bool post(ScheduledJob job) override
    {
        ++boundaries;
        return !reject && queue.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override
    {
        ++boundaries;
        return queue.isCurrentThread();
    }
    bool isOpen() const override
    {
        ++boundaries;
        return queue.isOpen();
    }
};
struct Writer final : IConfigurationWriter {
    ConfigurationFileSnapshot disk{{"memory/config.yaml", false, {}}, {}};
    int writes = 0;
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
                                         ConfigurationFileVersion) override
    {
        ++writes;
        disk = {{disk.version.path, true, std::string(64, static_cast<char>('a' + writes % 6))},
                target->source().normalizedYaml()};
        Promise<ConfigurationFileCommit> result;
        result.resolve({FileCommitOutcome::Committed, disk, {}});
        return result.task();
    }
};
struct Scene {
    std::shared_ptr<Scheduler> scheduler = std::make_shared<Scheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();
    Runtime old, next, consumer, stable;
    std::shared_ptr<const ConfigurationDefinitions> definitions, following;
    std::shared_ptr<KernelReconcileDriver> driver;
    std::unique_ptr<ConfigurationSession> session;
    bool tree;
    RealmId privateRealm;
    int seen = 0;
    static Runtime provider(int value, bool failInitially = false)
    {
        const auto fail = std::make_shared<bool>(failInitially);
        return Runtime(
            [value, fail](Context context) {
                if (std::exchange(*fail, false))
                    return failedTask(makeError(ErrorCode::ActivationFailed, "initial provider failed"));
                context.provide({"a"}, std::make_shared<int>(value)).value();
                return completedTask();
            },
            {}, {ServiceOutput::of<int>({"a"})});
    }
    PluginDefinition schema(const char* id)
    {
        return {id, {}, [](const std::string&) { return Result<void>::success(); }};
    }
    std::string text(const char* configuration) const
    {
        return std::string("formatVersion: 1\nplugins: [{id: p, plugin: p, config: ") + configuration +
               (tree ? ", children: [{id: c, plugin: consumer}]}" : "}, {id: c, plugin: consumer}") +
               ", {id: stable, plugin: stable, config: keep}]\n";
    }
    explicit Scene(bool nested = false, bool failInitially = false)
        : old(provider(7, failInitially)), next(provider(42)), consumer([](Context) { return completedTask(); }),
          stable([](Context) { return completedTask(); }), tree(nested)
    {
        kernel->registerRuntime("p", old).value();
        consumer =
            kernel
                ->registerPlugin(
                    "consumer",
                    [&](Context context) {
                        context.consume<int>({"a"}).value().withService([&](int& value) { seen = value; }).value();
                        return completedTask();
                    },
                    {ServiceRequirement::of<int>({"a"})})
                .value();
        kernel->registerRuntime("stable", stable).value();
        auto consuming = schema("consumer");
        consuming.requiredInject.push_back({"a", 1, 0, {}});
        definitions = take(ConfigurationDefinitions::create(
                               {{schema("p"), old}, {std::move(consuming), consumer}, {schema("stable"), stable}}),
                           "initial definitions");
        following = take(definitions->replacing({{schema("p"), next}}), "replacement definitions");
        std::map<std::string, RealmId> aliases;
        if (failInitially) {
            privateRealm = kernel->services().createRealm().value();
            aliases.emplace("private", privateRealm);
        }
        driver = KernelReconcileDriver::configured(scheduler, kernel, definitions, std::move(aliases));
        session = std::make_unique<ConfigurationSession>(scheduler, driver, writer, writer->disk);
        auto accepted = session->apply(session->preview(text("old")).value()).value();
        run();
        CHECK(accepted.settled.tryResult()->value().outcome ==
              (failInitially ? PlanOutcome::Failed : PlanOutcome::Applied));
        CHECK(seen == (failInitially ? 0 : 7));
        auto saved = session->save(1).value();
        run();
        CHECK(saved.tryResult()->hasValue());
    }
    void run() { scheduler->queue.runUntilIdle(30000).value(); }
    std::map<std::string, RunningEntry> rows()
    {
        std::map<std::string, RunningEntry> result;
        for (auto row : driver->snapshot().value())
            result.emplace(row.path, std::move(row));
        return result;
    }
    std::shared_ptr<const ValidatedCandidate> target()
    {
        return std::make_shared<const ValidatedCandidate>(
            following->validate(parseConfiguration(text("new")).value()).value());
    }
    ReplacementScope reserve()
    {
        auto scope = kernel->reserveReplacement({old.id()}).value();
        scope.prepareDefinitions({{old.id(), next}}).value();
        return scope;
    }
    ReplacementScope stop()
    {
        auto scope = reserve();
        auto stopped = scope.stop();
        run();
        CHECK(stopped.tryResult()->value().quiescent);
        return scope;
    }
    Instance stageConsumer(ReplacementScope& scope, const Instance& parent, bool child)
    {
        auto result = child ? parent.whenActiveAfter().tryResult()->value().inject(consumer, "{}",
                                                                                   {ServiceRequirement::of<int>({"a"})})
                            : scope.stage(consumer, "{}", {}, {ServiceRequirement::of<int>({"a"})});
        auto instance = result.value();
        run();
        CHECK(instance.ready().tryResult()->hasValue());
        return instance;
    }
    std::vector<ReplacementEntry> mapping(const std::map<std::string, RunningEntry>& before, const Instance& p,
                                          const Instance& c)
    {
        return {{"p", before.at("p").instance, p.snapshot().value().instance},
                {tree ? "p/c" : "c", before.at(tree ? "p/c" : "c").instance, c.snapshot().value().instance}};
    }
    void close()
    {
        auto closed = session->close();
        run();
        CHECK(closed.tryResult()->hasValue());
        auto done = kernel->close();
        run();
        CHECK(done.tryResult()->value().quiescent);
    }
};
void commit()
{
    Scene s;
    int manualSeen = 0;
    auto manual =
        s.kernel
            ->registerPlugin(
                "manual",
                [&](Context context) {
                    context.consume<int>({"a"}).value().withService([&](int& value) { manualSeen = value; }).value();
                    return completedTask();
                },
                {ServiceRequirement::of<int>({"a"})})
            .value();
    s.kernel->mount(manual).value();
    s.run();
    CHECK(manualSeen == 7);
    auto old = s.rows();
    auto draft = s.session->preview(s.text("old")).value();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    auto extra = scope.stage(manual).value();
    s.run();
    scope.prepareCommit().value();
    auto prepared = ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(),
                                                       s.mapping(old, p, c), 1)
                        .value();
    CHECK(prepared.ready() && !s.session->snapshot().hasValue());
    CHECK(!cordis::detail::InstanceOwnershipAccess::owner(p).value() &&
          !cordis::detail::InstanceOwnershipAccess::owner(c).value());
    auto accepted = *prepared.accepted();
    CHECK(accepted.revision == 2 && !accepted.settled.isReady());
    bool readableOnCompletion = false;
    cordis::detail::TaskAccess::observe(accepted.settled, [&](Task<PlanReport>::Snapshot result) {
        readableOnCompletion =
            result->hasValue() && s.session->snapshot().hasValue() && s.driver->snapshot().hasValue();
    });
    bool wrong = true;
    std::thread worker([&] { wrong = prepared.ready(); });
    worker.join();
    CHECK(!wrong);
    const int boundaries = s.scheduler->boundaries;
    scope.commitPrepared().value();
    prepared.publish();
    prepared.publish();
    CHECK(s.scheduler->boundaries == boundaries && !accepted.settled.isReady());
    auto notification = scope.release();
    prepared.finish(std::move(notification)).value();
    CHECK(readableOnCompletion);
    s.run();
    auto state = s.session->snapshot().value();
    auto current = s.rows();
    CHECK(state.coordinator.desiredRevision == 2 && state.store.desiredRevision == 2 && state.store.dirty);
    CHECK(state.latestPlan && state.latestPlan->revision == 2 && state.latestPlan->outcome == PlanOutcome::Applied);
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(current.at("p").instance == p.snapshot().value().instance &&
          current.at("c").instance == c.snapshot().value().instance);
    CHECK(current.at("stable").instance == old.at("stable").instance && s.seen == 42 && manualSeen == 42);
    CHECK(DriverCommitAccess::owns(*s.driver, p).value() && DriverCommitAccess::owns(*s.driver, c).value());
    CHECK(!DriverCommitAccess::owns(*s.driver, extra).value());
    CHECK(extra.snapshot().value().state == InstanceState::Active &&
          s.driver->configurationDefinitions() == s.following);
    CHECK(!s.session->apply(draft).hasValue());
    CHECK(s.kernel->snapshot().value().runtimes == 4);
    s.close();
}
void guards()
{
    Scene s;
    auto old = s.rows();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    scope.prepareCommit().value();
    const auto correct = s.mapping(old, p, c);
    auto attempt = [&](std::vector<ReplacementEntry> rows) {
        return ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(), rows, 1);
    };
    auto missing = correct;
    missing.pop_back();
    CHECK(!attempt(missing).hasValue());
    auto duplicate = correct;
    duplicate[1].candidate = duplicate[0].candidate;
    CHECK(!attempt(duplicate).hasValue());
    auto foreign = correct;
    foreign[0].candidate = old.at("stable").instance;
    CHECK(!attempt(foreign).hasValue());
    auto previous = correct;
    previous[0].previous = old.at("stable").instance;
    CHECK(!attempt(previous).hasValue());
    Kernel other(s.scheduler);
    other.registerRuntime("same-runtime", s.old).value();
    auto foreignScope = other.reserveReplacement({s.old.id()}).value();
    foreignScope.prepareDefinitions({{s.old.id(), s.next}}).value();
    auto stopped = foreignScope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    foreignScope.prepareCommit().value();
    CHECK(!ConfigurationCommitAccess::prepare(*s.session, *s.driver, foreignScope, s.following, s.target(), correct, 1)
               .hasValue());
    CHECK(foreignScope.discard().tryResult()->value().quiescent);
    foreignScope.release().value();
    auto otherClosed = other.close();
    s.run();
    CHECK(otherClosed.tryResult()->value().quiescent);
    CHECK(!ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(), correct, 0)
               .hasValue());
    auto prepared = attempt(correct).value();
    CHECK(prepared.ready());
    scope.commitPrepared().value();
    prepared.publish();
    prepared.finish(scope.release()).value();
    s.run();
    s.close();
}
void tree()
{
    Scene s(true);
    auto old = s.rows();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    scope.prepareCommit().value();
    CHECK(!ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(),
                                              s.mapping(old, p, c), 1)
               .hasValue());
    auto discarded = scope.discard();
    s.run();
    CHECK(discarded.tryResult()->value().quiescent);
    auto restored = scope.stage(s.old, "\"old\"").value();
    s.run();
    auto child = s.stageConsumer(scope, restored, true);
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    auto desired = s.session->snapshot().value().coordinator.desired;
    auto prepared = ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.definitions, desired,
                                                       s.mapping(old, restored, child), 1)
                        .value();
    CHECK(!prepared.accepted());
    scope.commitPrepared().value();
    prepared.publish();
    prepared.finish(scope.release()).value();
    s.run();
    auto recovered = s.session->snapshot().value();
    CHECK(recovered.coordinator.desiredRevision == 1 && recovered.store.desiredRevision == 1 && !recovered.store.dirty);
    CHECK(recovered.latestPlan->revision == 1 && s.rows().at("p/c").parentEpoch == restored.snapshot().value().epoch);
    CHECK(DriverCommitAccess::owns(*s.driver, restored).value() && DriverCommitAccess::owns(*s.driver, child).value());
    const auto previousChild = s.rows().at("p/c").instance;
    auto changed = s.session->apply(s.session->preview(s.text("again")).value()).value();
    s.run();
    CHECK(changed.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    CHECK(s.rows().at("p/c").instance != previousChild &&
          s.rows().at("p/c").parentEpoch == restored.snapshot().value().epoch);
    s.close();
}
void restamp()
{
    Scene s;
    auto old = s.rows();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    scope.prepareCommit().value();
    std::optional<cordis::loader::detail::PreparedConfigurationCommit> prepared;
    prepared.emplace(ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(),
                                                        s.mapping(old, p, c), 1)
                         .value());
    auto abandoned = *prepared->accepted();
    auto discarded = scope.discard();
    s.run();
    CHECK(discarded.tryResult()->value().quiescent);
    auto restored = scope.stage(s.old, "\"old\"").value();
    s.run();
    auto consumer = s.stageConsumer(scope, restored, false);
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    CHECK(!prepared->ready());
    prepared.reset();
    s.run();
    CHECK(abandoned.settled.tryResult() && !abandoned.settled.tryResult()->hasValue());
    auto desired = s.session->snapshot().value().coordinator.desired;
    auto recovered = ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.definitions, desired,
                                                        s.mapping(old, restored, consumer), 1)
                         .value();
    scope.commitPrepared().value();
    recovered.publish();
    recovered.finish(scope.release()).value();
    s.run();
    CHECK(s.session->snapshot().value().coordinator.desiredRevision == 1);
    s.close();
}
void preflight()
{
    Scene s;
    auto manual = s.kernel->mount(s.consumer).value();
    s.run();
    auto manualChild = manual.whenActiveAfter().tryResult()->value().inject(s.stable).value();
    auto p = s.kernel->findInstance(s.rows().at("p").instance).value();
    auto ownedChild = p.whenActiveAfter().tryResult()->value().inject(s.stable).value();
    s.run();
    auto scope = s.reserve();
    auto plan = take(DriverCommitAccess::inspectReconstruction(*s.driver, scope, s.following, s.target()), "preflight");
    CHECK(plan.entries.size() == 2 && plan.ownership.size() == 5 && plan.original.members.size() == 5);
    const auto root =
        std::find_if(plan.entries.begin(), plan.entries.end(), [](const auto& row) { return row.path == "p"; });
    CHECK(root != plan.entries.end() && root->runtime.id() == s.next.id() && root->config == "\"new\"");
    const auto kind = [&](InstanceId id) {
        const auto found = std::find_if(plan.ownership.begin(), plan.ownership.end(),
                                        [&](const auto& row) { return row.instance == id; });
        CHECK(found != plan.ownership.end());
        return found->kind;
    };
    using cordis::loader::detail::ReconstructionKind;
    CHECK(kind(manual.snapshot().value().instance) == ReconstructionKind::ManualRoot);
    CHECK(kind(manualChild.snapshot().value().instance) == ReconstructionKind::OwnerManagedChild);
    CHECK(kind(ownedChild.snapshot().value().instance) == ReconstructionKind::OwnerManagedChild);
    CHECK(kind(p.snapshot().value().instance) == ReconstructionKind::ConfiguredEntry);
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Reserved && s.seen == 7);
    CHECK(s.rows().at("p").state == InstanceState::Active &&
          s.session->snapshot().value().coordinator.desiredRevision == 1);
    CHECK(s.writer->writes == 1);
    scope.release().value();
    s.run();
    s.close();
}
void preflightForeign()
{
    Scene s;
    auto otherDriver = KernelReconcileDriver::configured(s.scheduler, s.kernel, s.definitions);
    ConfigurationSession other(s.scheduler, otherDriver, s.writer, s.writer->disk);
    auto accepted =
        other.apply(other.preview("formatVersion: 1\nplugins: [{id: other, plugin: consumer}]\n").value()).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto scope = s.reserve();
    using cordis::loader::detail::ConfigurationReconstructionAccess;
    CHECK(!ConfigurationReconstructionAccess::inspect(scope, {{s.driver, s.following, s.target()}}).hasValue());
    auto otherTarget = std::make_shared<const ValidatedCandidate>(
        s.following->validate(other.snapshot().value().coordinator.desired->source()).value());
    auto joined = ConfigurationReconstructionAccess::inspect(
                      scope, {{s.driver, s.following, s.target()}, {otherDriver, s.following, otherTarget}})
                      .value();
    const int boundaries = s.scheduler->boundaries;
    CHECK(ConfigurationReconstructionAccess::current(joined));
    bool wrongThread = true;
    std::thread worker([&] { wrongThread = ConfigurationReconstructionAccess::current(joined); });
    worker.join();
    CHECK(!wrongThread && s.scheduler->boundaries == boundaries);
    CHECK(s.rows().at("p").state == InstanceState::Active &&
          otherDriver->snapshot().value()[0].state == InstanceState::Active);
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Reserved);
    scope.release().value();
    CHECK(!ConfigurationReconstructionAccess::current(joined));
    auto closed = other.close();
    s.run();
    CHECK(closed.tryResult()->hasValue());
    s.close();
}
void preflightGuards()
{
    Scene s(true);
    auto scope = s.reserve();
    auto plan = DriverCommitAccess::inspectReconstruction(*s.driver, scope, s.following, s.target()).value();
    CHECK(plan.entries.size() == 2 && plan.entries[1].path == "p/c");
    auto changed =
        s.following->validate(setEntryConfiguration(s.target()->source(), "stable", "changed").value()).value();
    CHECK(!DriverCommitAccess::inspectReconstruction(*s.driver, scope, s.following,
                                                     std::make_shared<const ValidatedCandidate>(std::move(changed)))
               .hasValue());
    CHECK(!DriverCommitAccess::inspectReconstruction(*s.driver, scope, s.definitions, s.target()).hasValue());
    Kernel other(s.scheduler);
    other.registerRuntime("p", s.old).value();
    auto foreign = other.reserveReplacement({s.old.id()}).value();
    foreign.prepareDefinitions({{s.old.id(), s.next}}).value();
    CHECK(!DriverCommitAccess::inspectReconstruction(*s.driver, foreign, s.following, s.target()).hasValue());
    foreign.release().value();
    auto closed = other.close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent);
    ConfiguredPlugin replacing{s.schema("p"), s.next};
    replacing.available = [&] {
        scope.release().value();
        return Result<void>::success();
    };
    auto reentrant = s.definitions->replacing({std::move(replacing)}).value();
    auto target = std::make_shared<const ValidatedCandidate>(
        reentrant->validate(parseConfiguration(s.text("new")).value()).value());
    CHECK(!DriverCommitAccess::inspectReconstruction(*s.driver, scope, reentrant, target).hasValue());
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Released &&
          s.rows().at("p").state == InstanceState::Active);
    s.run();
    s.close();
}
void preflightEmpty()
{
    using cordis::loader::detail::ConfigurationReconstructionAccess;
    Scene s;
    auto emptyDriver = KernelReconcileDriver::configured(s.scheduler, s.kernel, s.definitions);
    ConfigurationSession empty(s.scheduler, emptyDriver, s.writer, s.writer->disk);
    auto accepted = empty.apply(empty.preview("formatVersion: 1\nplugins: []\n").value()).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto emptyTarget = std::make_shared<const ValidatedCandidate>(
        s.following->validate(empty.snapshot().value().coordinator.desired->source()).value());
    auto scope = s.reserve();
    auto inspected = ConfigurationReconstructionAccess::inspect(
                         scope, {{s.driver, s.following, s.target()}, {emptyDriver, s.following, emptyTarget}})
                         .value();
    CHECK(inspected.drivers[1].entries.empty() && !inspected.drivers[1].previousTarget);
    CHECK(!ConfigurationReconstructionAccess::inspect(scope, {{s.driver, s.following, s.target()},
                                                              {emptyDriver, s.following, emptyTarget},
                                                              {emptyDriver, s.following, emptyTarget}})
               .hasValue());
    CHECK(s.rows().at("p").state == InstanceState::Active);
    scope.release().value();
    auto closed = empty.close();
    s.run();
    CHECK(closed.tryResult()->hasValue());
    s.close();
}
void preflightContract()
{
    Scene s;
    Runtime bad([](Context) { return completedTask(); }, {}, {ServiceOutput::of<std::string>({"a"})});
    auto definitions = s.definitions->replacing({{s.schema("p"), bad}}).value();
    auto target = std::make_shared<const ValidatedCandidate>(
        definitions->validate(parseConfiguration(s.text("new")).value()).value());
    auto scope = s.kernel->reserveReplacement({s.old.id()}).value();
    scope.prepareDefinitions({{s.old.id(), bad}}).value();
    auto inspected = DriverCommitAccess::inspectReconstruction(*s.driver, scope, definitions, target);
    CHECK(!inspected.hasValue() && inspected.error().code == ErrorCode::ConfigInvalid);
    CHECK(s.rows().at("p").state == InstanceState::Active &&
          scope.snapshot().value().state == ReplacementScopeState::Reserved);
    scope.release().value();
    s.run();
    s.close();
}
void ownership()
{
    using cordis::detail::InstanceOwnershipAccess;
    Scene s;
    auto p = s.kernel->findInstance(s.rows().at("p").instance).value();
    const auto owner = InstanceOwnershipAccess::owner(p).value();
    CHECK(owner && DriverCommitAccess::owns(*s.driver, p).value());
    auto child = p.whenActiveAfter().tryResult()->value().inject(s.stable).value();
    s.run();
    CHECK(InstanceOwnershipAccess::owner(child).value() == owner);
    auto manual = s.kernel->mount(s.stable).value();
    s.run();
    CHECK(!InstanceOwnershipAccess::owner(manual).value());
    CHECK(!InstanceOwnershipAccess::prepare(manual, owner).hasValue());
    auto manualChild = manual.whenActiveAfter().tryResult()->value().inject(s.stable).value();
    s.run();
    CHECK(!InstanceOwnershipAccess::owner(manualChild).value());

    auto otherDriver = KernelReconcileDriver::configured(s.scheduler, s.kernel, s.definitions);
    auto other = std::make_unique<ConfigurationSession>(s.scheduler, otherDriver, s.writer, s.writer->disk);
    auto accepted =
        other->apply(other->preview("formatVersion: 1\nplugins: [{id: other, plugin: stable}]\n").value()).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto otherInstance = s.kernel->findInstance(otherDriver->snapshot().value()[0].instance).value();
    CHECK(InstanceOwnershipAccess::owner(otherInstance).value() &&
          !DriverCommitAccess::owns(*s.driver, otherInstance).value() &&
          !DriverCommitAccess::owns(*otherDriver, p).value());
    CHECK(!InstanceOwnershipAccess::prepare(otherInstance, owner).hasValue());
    auto closed = other->close();
    s.run();
    CHECK(closed.tryResult()->hasValue());

    bool markedBeforeActivation = false;
    auto witness =
        s.kernel
            ->registerPlugin("ownership-witness",
                             [&](Context context) {
                                 auto actual = s.kernel->findInstance(context.identity().value().instance).value();
                                 markedBeforeActivation = InstanceOwnershipAccess::owner(actual).value() == owner;
                                 return completedTask();
                             })
            .value();
    auto owned = InstanceOwnershipAccess::mount(*s.kernel, owner, witness, {}, {}, {}).value();
    s.run();
    CHECK(markedBeforeActivation);
    auto stopped = owned.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto remounted = s.kernel->remount(owned).value();
    s.run();
    CHECK(InstanceOwnershipAccess::owner(remounted).value() == owner);
    bool wrongThread = false;
    std::thread worker([&] {
        const auto result = InstanceOwnershipAccess::owner(p);
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrongThread);
    // 空标记与另一驱动的标记可区分；拥有者销毁也不能把存活实例重分类为手动实例。
    other.reset();
    otherDriver.reset();
    CHECK(InstanceOwnershipAccess::owner(otherInstance).value());
    s.close();
}
void abandoned()
{
    Scene s;
    auto old = s.rows();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    scope.prepareCommit().value();
    std::optional<cordis::loader::detail::PreparedConfigurationCommit> prepared;
    prepared.emplace(ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(),
                                                        s.mapping(old, p, c), 1)
                         .value());
    auto accepted = *prepared->accepted();
    scope.commitPrepared().value();
    prepared->publish();
    scope.release().value();
    prepared.reset();
    s.run();
    const auto result = accepted.settled.tryResult();
    CHECK(result && !result->hasValue() && result->error().code == ErrorCode::OperationCancelled);
    auto state = s.session->snapshot().value();
    CHECK(state.coordinator.desiredRevision == 2 && state.store.desiredRevision == 2 && state.store.dirty);
    CHECK(s.driver->configurationDefinitions() == s.following &&
          s.rows().at("p").instance == p.snapshot().value().instance);
    s.close();
}
void notification()
{
    Scene s;
    auto old = s.rows();
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, false);
    scope.prepareCommit().value();
    auto prepared = ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, s.target(),
                                                       s.mapping(old, p, c), 1)
                        .value();
    auto accepted = *prepared.accepted();
    scope.commitPrepared().value();
    prepared.publish();
    s.scheduler->reject = true;
    auto released = scope.release();
    s.scheduler->reject = false;
    CHECK(!released.hasValue());
    CHECK(!prepared.finish(std::move(released)).hasValue());
    s.run();
    auto state = s.session->snapshot().value();
    CHECK(state.coordinator.desiredRevision == 2 && state.store.desiredRevision == 2);
    CHECK(state.latestPlan && state.latestPlan->outcome == PlanOutcome::Failed && !state.latestPlan->errors.empty());
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Failed &&
          s.driver->configurationDefinitions() == s.following);
    // 通知失败不等于后续整体关闭必定失败；这里只验证提交事实与非成功报告。
    s.kernel->close();
    s.run();
}
void unstartedCommit()
{
    Scene s(true, true);
    const auto before = s.rows();
    CHECK(before.size() == 1 && before.at("p").state == InstanceState::Failed);
    auto scope = s.stop();
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    CHECK(p.ready().tryResult()->hasValue());
    auto context = p.whenActiveAfter().tryResult()->value().isolate("unused", s.privateRealm).value();
    auto c = context.inject(s.consumer, "{}", {ServiceRequirement::of<int>({"a"})}).value();
    s.run();
    CHECK(c.ready().tryResult()->hasValue());
    auto source = s.text("new");
    const auto position = source.find("id: c, plugin: consumer");
    CHECK(position != std::string::npos);
    source.insert(position + std::string("id: c, plugin: consumer").size(), ", realms: {unused: private}");
    auto target =
        std::make_shared<const ValidatedCandidate>(s.following->validate(parseConfiguration(source).value()).value());
    scope.prepareCommit().value();
    const std::vector<ReplacementEntry> mappings{{"p", before.at("p").instance, p.snapshot().value().instance},
                                                 {"p/c", {}, c.snapshot().value().instance}};
    {
        auto abandoned =
            take(ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, target, mappings, 1),
                 "prepare unstarted then abandon");
        CHECK(abandoned.ready());
    }
    CHECK(s.rows().empty()); // 原父实例已停止；放弃准备不能把新子项写进执行表。
    auto original = s.session->snapshot().value();
    CHECK(original.coordinator.desiredRevision == 1 && original.store.persistedRevision == 1 && !original.store.dirty);
    CHECK(s.driver->configurationDefinitions() == s.definitions);
    auto extraSource = s.text("new");
    extraSource.insert(extraSource.rfind(']'), ", {id: injected, plugin: stable}");
    auto extra = std::make_shared<const ValidatedCandidate>(
        s.following->validate(parseConfiguration(extraSource).value()).value());
    CHECK(
        !ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, extra, mappings, 1).hasValue());
    CHECK(s.rows().empty() && s.session->snapshot().value().coordinator.desiredRevision == 1);
    auto prepared =
        take(ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, target, mappings, 1),
             "prepare unstarted publish");
    CHECK(prepared.ready());
    scope.commitPrepared().value();
    const auto boundaries = s.scheduler->boundaries;
    prepared.publish();
    CHECK(s.scheduler->boundaries == boundaries);
    auto notifications = scope.release();
    prepared.finish(notifications).value();
    s.run();
    const auto rows = s.rows();
    CHECK(rows.size() == 2 && rows.at("p").state == InstanceState::Active &&
          rows.at("p/c").state == InstanceState::Active);
    CHECK(rows.at("p/c").instance == c.snapshot().value().instance && !rows.count("stable"));
    CHECK(s.session->snapshot().value().coordinator.desiredRevision == 2 && s.seen == 42);
    s.close();
}
void anchors()
{
    const auto scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel(scheduler), foreign(scheduler);
    const auto old = kernel.registerPlugin("absent", [](Context) { return completedTask(); }).value();
    const Runtime next([](Context) { return completedTask(); });
    const auto provider = kernel.registerRuntime("owner", Scene::provider(7)).value();
    const auto root = kernel.mount(provider).value();
    scheduler->runUntilIdle().value();
    const Runtime transient([](Context) { return completedTask(); });
    const auto child = root.whenActiveAfter().tryResult()->value().inject(transient).value();
    const auto consumer =
        kernel.registerPlugin("consumer", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})})
            .value();
    const auto dependent = kernel.mount(consumer).value();
    const auto foreignFactory = foreign.registerPlugin("foreign", [](Context) { return completedTask(); }).value();
    const auto other = foreign.mount(foreignFactory).value();
    scheduler->runUntilIdle().value();
    const auto rootId = root.snapshot().value().instance;
    const auto otherId = other.snapshot().value().instance;
    using cordis::detail::CoreReplacementAccess;
    CHECK(!CoreReplacementAccess::reserve(kernel, {old.id()}, {otherId}).hasValue());
    CHECK(kernel.replacements().value().empty());
    auto scope = take(CoreReplacementAccess::reserve(kernel, {old.id()}, {rootId, rootId}), "logical owner reserve");
    scope.prepareDefinitions({{old.id(), next}}).value();
    const auto actual = take(CoreReplacementAccess::original(scope, kernel), "logical owner input");
    CHECK(actual.members.size() == 3);
    CHECK(std::any_of(actual.members.begin(), actual.members.end(), [&](const auto& item) {
        return item.input.instance == child.snapshot().value().instance && item.input.parent == rootId;
    }));
    CHECK(std::any_of(actual.members.begin(), actual.members.end(),
                      [&](const auto& item) { return item.input.instance == dependent.snapshot().value().instance; }));
    const auto unrelated = kernel.registerPlugin("other-target", [](Context) { return completedTask(); }).value();
    CHECK(!CoreReplacementAccess::reserve(kernel, {unrelated.id()}, {rootId}).hasValue());
    CHECK(kernel.replacements().value().size() == 1);
    // freeze 必须用同一祖先选择重新计算，而不是只按没有活动实例的目标 Runtime 计算空闭包。
    CHECK(scope.freeze().hasValue());
    auto stopped = scope.stop();
    scheduler->runUntilIdle().value();
    CHECK(stopped.tryResult()->value().quiescent);
    CHECK(root.snapshot().value().state == InstanceState::Disposed &&
          child.snapshot().value().state == InstanceState::Disposed &&
          dependent.snapshot().value().state == InstanceState::Disposed);
    scope.release().value();
    CHECK(kernel.replacements().value().empty());
    auto closed = kernel.close();
    auto foreignClosed = foreign.close();
    scheduler->runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent && foreignClosed.tryResult()->value().quiescent);
    scheduler->close();
}
void claims()
{
    Scene s(true, true);
    const auto original = s.rows();
    auto draft = s.session->preview(s.text("changed")).value();
    auto stopped = s.kernel->findInstance(original.at("p").instance).value().stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto scope = s.reserve();
    using cordis::loader::detail::DriverReplacementClaim;
    std::optional<DriverReplacementClaim> claim;
    claim.emplace(take(ConfigurationCommitAccess::reserve(*s.session, *s.driver, scope, s.following, s.target(), 1),
                       "claim paths"));
    auto otherScope = s.kernel->reserveReplacement({s.consumer.id()}).value();
    Runtime consumerNext([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"a"})});
    otherScope.prepareDefinitions({{s.consumer.id(), consumerNext}}).value();
    auto consumerSchema = s.schema("consumer");
    consumerSchema.requiredInject.push_back({"a", 1, 0, {}});
    auto otherDefinitions = s.definitions->replacing({{consumerSchema, consumerNext}}).value();
    auto otherTarget = std::make_shared<const ValidatedCandidate>(
        otherDefinitions->validate(parseConfiguration(s.text("old")).value()).value());
    auto denied =
        ConfigurationCommitAccess::reserve(*s.session, *s.driver, otherScope, otherDefinitions, otherTarget, 1);
    CHECK(!denied.hasValue() && denied.error().operation == scope.snapshot().value().operation);
    otherScope.release().value();
    CHECK(!s.session->apply(draft).hasValue());
    CHECK(!s.session->setEnabled("p", false, 1).hasValue());
    CHECK(s.session->snapshot().value().coordinator.desiredRevision == 1);
    auto source = setEntryConfiguration(parseConfiguration(s.text("old")).value(), "stable", "\"edited\"").value();
    auto unchanged = std::make_shared<const ValidatedCandidate>(s.definitions->validate(source).value());
    const auto direct = s.driver->apply({"p", ChangeKind::Add}, unchanged, 2).tryResult();
    CHECK(direct && !direct->hasValue() && direct->error().code == ErrorCode::ConfigConflict);
    auto accepted = s.session->apply(s.session->preview(source.normalizedYaml()).value()).value();
    s.run();
    CHECK(accepted.settled.isReady() && s.session->snapshot().value().coordinator.desiredRevision == 2);
    CHECK(!s.rows().count("p/c") && s.rows().count("stable"));
    auto oldStopped = scope.stop();
    s.run();
    CHECK(oldStopped.tryResult()->value().quiescent);
    auto p = scope.stage(s.next, "\"new\"").value();
    s.run();
    auto c = s.stageConsumer(scope, p, true);
    scope.prepareCommit().value();
    auto finalSource = setEntryConfiguration(source, "p", "\"new\"").value();
    auto target = std::make_shared<const ValidatedCandidate>(s.following->validate(std::move(finalSource)).value());
    const std::vector<ReplacementEntry> mappings{{"p", original.at("p").instance, p.snapshot().value().instance},
                                                 {"p/c", {}, c.snapshot().value().instance}};
    CHECK(
        !ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, target, mappings, 2).hasValue());
    auto prepared = take(ConfigurationCommitAccess::prepare(*s.session, *s.driver, scope, s.following, target, mappings,
                                                            2, nullptr, &*claim),
                         "prepare with claim");
    scope.commitPrepared().value();
    prepared.publish();
    claim.reset();
    prepared.finish(scope.release()).value();
    s.run();
    CHECK(s.rows().at("stable").configuration.config == "\"edited\"" && s.seen == 42);
    CHECK(s.session->snapshot().value().coordinator.desiredRevision == 3);
    s.close();
}
void emptyClaim()
{
    Scene s;
    auto old = s.kernel->registerPlugin("unused", [](Context) { return completedTask(); }).value();
    Runtime next([](Context) { return completedTask(); });
    auto before = ConfigurationDefinitions::create({{s.schema("unused"), old}}).value();
    auto after = before->replacing({{s.schema("unused"), next}}).value();
    auto driver = KernelReconcileDriver::configured(s.scheduler, s.kernel, before);
    auto writer = std::make_shared<Writer>();
    ConfigurationSession session(s.scheduler, driver, writer, writer->disk);
    const std::string empty = "formatVersion: 1\nplugins: []\n";
    auto initial = session.apply(session.preview(empty).value()).value();
    s.run();
    CHECK(initial.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto scope = s.kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    auto target =
        std::make_shared<const ValidatedCandidate>(after->validate(parseConfiguration(empty).value()).value());
    std::optional<cordis::loader::detail::DriverReplacementClaim> claim;
    claim.emplace(take(ConfigurationCommitAccess::reserve(session, *driver, scope, after, target, 1), "empty claim"));
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    scope.prepareCommit().value();
    auto prepared =
        take(ConfigurationCommitAccess::prepare(session, *driver, scope, after, target, {}, 1, nullptr, &*claim),
             "empty prepare");
    scope.commitPrepared().value();
    prepared.publish();
    claim.reset();
    prepared.finish(scope.release()).value();
    s.run();
    CHECK(session.snapshot().value().coordinator.desiredRevision == 2 && driver->snapshot().value().empty());
    auto closed = session.close();
    s.run();
    CHECK(closed.tryResult()->hasValue());
    s.close();
}
void sessionHold(bool deliveryFailure)
{
    using cordis::loader::detail::SessionCommitAccess;
    using cordis::loader::detail::SessionReplacementHold;
    using cordis::loader::detail::TargetCommitMode;
    Scene s;
    Promise<void> done, otherDone;
    auto hold = SessionCommitAccess::hold(*s.session, *s.driver, done.task()).value();
    const auto target = SessionCommitAccess::inspect(*s.session, *s.driver).value().desired;
    if (deliveryFailure) {
        s.scheduler->reject = true;
        std::thread worker([&] { done.reject(makeError(ErrorCode::OperationCancelled, "worker stopped")); });
        worker.join();
        const auto closed = s.session->close().tryResult();
        CHECK(closed && !closed->hasValue() && closed->error().code == ErrorCode::SchedulerClosed);
        s.scheduler->reject = false;
        auto stopped = s.kernel->close();
        s.run();
        CHECK(stopped.tryResult()->value().quiescent);
        return;
    }
    std::optional<SessionReplacementHold> second;
    second.emplace(SessionCommitAccess::hold(*s.session, *s.driver, otherDone.task()).value());
    auto closed = s.session->close();
    s.run();
    CHECK(hold.closing().isReady() && !closed.isReady() && s.rows().size() == 3);
    CHECK(!s.session->save(1).hasValue() && !SessionCommitAccess::inspect(*s.session, *s.driver).hasValue());
    CHECK(SessionCommitAccess::inspect(*s.session, *s.driver, &hold).hasValue());
    CHECK(!SessionCommitAccess::prepare(*s.session, *s.driver, target, 1, TargetCommitMode::Advance, {}, &hold)
               .hasValue());
    {
        auto prepared =
            SessionCommitAccess::prepare(*s.session, *s.driver, target, 1, TargetCommitMode::Preserve, {}, &hold);
        CHECK(prepared.hasValue() && prepared.value().ready());
    }
    {
        Scene foreign;
        Promise<void> foreignDone;
        {
            auto external = SessionCommitAccess::hold(*foreign.session, *foreign.driver, foreignDone.task()).value();
            CHECK(!SessionCommitAccess::inspect(*s.session, *s.driver, &external).hasValue());
            CHECK(!SessionCommitAccess::prepare(*s.session, *s.driver, target, 1, TargetCommitMode::Preserve, {},
                                                &external)
                       .hasValue());
        }
        foreign.close(); // 未兑现的结算任务不阻止已放弃持有的关闭。
    }
    std::thread wrong([&] {
        const auto result = SessionCommitAccess::inspect(*s.session, *s.driver, &hold);
        CHECK(!result.hasValue() && result.error().code == ErrorCode::WrongThread);
    });
    wrong.join();
    done.resolve();
    s.run();
    CHECK(!closed.isReady() && !SessionCommitAccess::inspect(*s.session, *s.driver, &hold).hasValue());
    std::thread worker([&] { otherDone.reject(makeError(ErrorCode::OperationCancelled, "operation failed")); });
    worker.join();
    s.run();
    CHECK(closed.tryResult() && closed.tryResult()->hasValue() && s.rows().empty());
    second.reset(); // 已由任务释放，不得再次递减持有数量。
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "unstarted")
            unstartedCommit();
        else if (name == "anchors")
            anchors();
        else if (name == "commit")
            commit();
        else if (name == "guards")
            guards();
        else if (name == "tree")
            tree();
        else if (name == "claims")
            claims();
        else if (name == "claims_empty")
            emptyClaim();
        else if (name == "session_hold" || name == "session_hold_delivery")
            sessionHold(name == "session_hold_delivery");
        else if (name == "restamp")
            restamp();
        else if (name == "notification")
            notification();
        else if (name == "abandoned")
            abandoned();
        else if (name == "ownership")
            ownership();
        else if (name == "preflight")
            preflight();
        else if (name == "preflight_foreign")
            preflightForeign();
        else if (name == "preflight_guards")
            preflightGuards();
        else if (name == "preflight_contract")
            preflightContract();
        else if (name == "preflight_empty")
            preflightEmpty();
        else
            throw std::runtime_error("unknown case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
