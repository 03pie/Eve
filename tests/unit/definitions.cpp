/** @file definitions.cpp
 * @brief 用真实目录、Kernel/Session 验证定义来源；协议替身仅注入预检重入。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Writer : IConfigurationWriter {
    ConfigurationFileSnapshot disk{{"memory/definitions.yaml", false, {}}, {}};
    int calls = 0;
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
                                         ConfigurationFileVersion expected) override
    {
        CHECK(expected.path == disk.version.path);
        ++calls;
        disk = {{expected.path, true, std::string(64, 'a')}, target->source().normalizedYaml()};
        Promise<ConfigurationFileCommit> result;
        result.resolve({FileCommitOutcome::Committed, disk, {}});
        return result.task();
    }
};
std::string text(const char* value = "old")
{
    return std::string("formatVersion: 1\nplugins: [{id: item, plugin: p, config: ") + value + "}]\n";
}
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();
    int activations = 0, schemas = 0;
    Runtime runtime = kernel
                          ->registerPlugin("p",
                                           [this](Context) {
                                               ++activations;
                                               return completedTask();
                                           })
                          .value();
    std::shared_ptr<const ConfigurationDefinitions> table(const std::string& accept = "old")
    {
        return ConfigurationDefinitions::create(
                   {{{"p",
                      {},
                      [this, accept](const std::string& value) {
                          ++schemas;
                          return value == '"' + accept + '"'
                                     ? Result<void>::success()
                                     : Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "schema mismatch"));
                      }},
                     runtime}})
            .value();
    }
    void run() { scheduler->runUntilIdle(30000).value(); }
    void close()
    {
        auto closed = kernel->close();
        run();
        CHECK(closed.tryResult()->value().quiescent);
    }
};
/** @brief 无实例协议替身，只注入目录身份变化；不用于声称业务已重建或已升级。 */
struct ProtocolDriver : IConfiguredReconcileDriver {
    std::shared_ptr<const ConfigurationDefinitions> current;
    std::function<void()> onPreflight;
    mutable std::function<void()> onRead;
    mutable std::function<void()> onSnapshot;
    int preflights = 0;
    std::shared_ptr<const ConfigurationDefinitions> configurationDefinitions() const override
    {
        auto hook = std::exchange(onRead, {});
        if (hook)
            hook();
        return current;
    }
    Result<void> preflight(const ValidatedCandidate&) override
    {
        ++preflights;
        auto hook = std::exchange(onPreflight, {});
        if (hook)
            hook();
        return Result<void>::success();
    }
    Result<std::vector<EntryOperation>> plan(const ValidatedCandidate&) override
    {
        return Result<std::vector<EntryOperation>>::success({});
    }
    Task<StepReport> apply(EntryOperation, std::shared_ptr<const ValidatedCandidate>, std::uint64_t) override
    {
        Promise<StepReport> result;
        result.resolve({});
        return result.task();
    }
    Result<std::vector<RunningEntry>> snapshot() const override
    {
        auto hook = std::exchange(onSnapshot, {});
        if (hook)
            hook();
        return Result<std::vector<RunningEntry>>::success({});
    }
    Task<CleanupReport> close() override
    {
        Promise<CleanupReport> result;
        result.resolve({true, {}});
        return result.task();
    }
};
void basic()
{
    Scene s;
    auto view = s.table();
    CHECK(s.schemas == 0 && s.activations == 0 && view->plugins() == std::vector<std::string>{"p"});
    CHECK(view->module("p").value().alias == "builtin" && !view->module("missing").hasValue());
    auto candidate = view->validate(parseConfiguration(text()).value()).value();
    CHECK(candidate.hasDefinitionIdentity() && view->owns(candidate) && s.schemas == 1 && s.activations == 0);
    auto other = s.table();
    CHECK(!other->owns(candidate));
    auto plain = validateCandidate(parseConfiguration(text()).value(),
                                   {{"p", {}, [](const std::string&) { return Result<void>::success(); }}})
                     .value();
    CHECK(!plain.hasDefinitionIdentity() && !view->owns(plain));
    auto copied = candidate;
    CHECK(view->owns(copied));
    auto source = parseConfiguration(text()).value();
    auto moved = std::move(source);
    CHECK(!view->validate(std::move(source)).hasValue());
    s.close();
}
void invalid()
{
    Scene s;
    auto schema = [](const std::string&) { return Result<void>::success(); };
    CHECK(!ConfigurationDefinitions::create({{{"p", {}, {}}, s.runtime}}).hasValue());
    CHECK(
        !ConfigurationDefinitions::create({{{"p", {}, schema}, s.runtime}, {{"p", {}, schema}, s.runtime}}).hasValue());
    CHECK(!ConfigurationDefinitions::create({{{"p", {}, schema, {"builtin", "wrong.json"}}, s.runtime}}).hasValue());
    auto moved = s.runtime;
    auto valid = std::move(moved);
    CHECK(!ConfigurationDefinitions::create({{{"p", {}, schema}, moved}}).hasValue());
    auto definitions = s.table();
    bool rejected = false;
    auto foreign = std::make_shared<Kernel>(s.scheduler);
    try {
        auto driver = KernelReconcileDriver::configured(s.scheduler, foreign, definitions);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    auto done = foreign->close();
    s.run();
    CHECK(done.tryResult()->value().quiescent);
    s.close();
}
void contracts()
{
    Scene s;
    auto schema = [](const std::string&) { return Result<void>::success(); };
    Runtime required([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"dep"}, 2, {"read"})});
    CHECK(!ConfigurationDefinitions::create({{{"p", {}, schema}, required}}).hasValue());
    CHECK(!ConfigurationDefinitions::create({{{"p", {{"dep", 1, 1, {"read"}}}, schema}, required}}).hasValue());
    CHECK(!ConfigurationDefinitions::create({{{"p", {{"dep", 1, 2, {}}}, schema}, required}}).hasValue());
    CHECK(!ConfigurationDefinitions::create({{{"p", {{"unknown", 1, 0, {}}}, schema}, s.runtime}}).hasValue());
    CHECK(ConfigurationDefinitions::create({{{"p", {{"dep", 1, 3, {"read", "more"}}}, schema}, required}}).hasValue());
    auto extra = ServiceRequirement::of<int>({"extra"}, 1, {"write"});
    CHECK(ConfigurationDefinitions::create({{{"p", {{"extra", 1, 1, {"write"}}}, schema}, s.runtime, {extra}}})
              .hasValue());
    CHECK(!ConfigurationDefinitions::create({{{"p", {}, schema}, s.runtime, {extra, extra}}}).hasValue());
    s.close();
}
void driver()
{
    Scene s;
    auto old = s.table(), next = s.table("new");
    auto target = old->validate(parseConfiguration(text()).value()).value();
    auto current = KernelReconcileDriver::configured(s.scheduler, s.kernel, next);
    CHECK(current->configurationDefinitions() == next &&
          current->preflight(target).error().code == ErrorCode::ConfigConflict);
    CHECK(!current->plan(target).hasValue() &&
          !current->apply({"item", ChangeKind::Add}, std::make_shared<const ValidatedCandidate>(target), 1)
               .tryResult()
               ->hasValue());
    CHECK(s.kernel->snapshot().value().instances.empty() && s.activations == 0);
    auto valid = next->validate(parseConfiguration(text("new")).value()).value();
    CHECK(current->preflight(valid).hasValue());
    auto closed = current->close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent);
    s.close();
}
void session()
{
    Scene s;
    auto table = s.table();
    auto driver = KernelReconcileDriver::configured(s.scheduler, s.kernel, table);
    ConfigurationSession session(s.scheduler, driver, s.writer, s.writer->disk);
    auto draft = session.preview(text()).value();
    CHECK(table->owns(draft.candidate()) && s.activations == 0);
    auto accepted = session.apply(draft).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied && s.activations == 1);
    CHECK(session.registeredPlugins().value() == std::vector<std::string>{"p"});
    session.addInstance("second", "p", "old", 1).value();
    s.run();
    CHECK(s.activations == 2);
    session.setEnabled("item", false, 2).value();
    s.run();
    auto saved = session.save(3).value();
    s.run();
    CHECK(saved.tryResult()->hasValue());
    auto done = session.close();
    s.run();
    CHECK(done.tryResult()->hasValue() && s.writer->calls == 1);
    s.close();
}
void constructors()
{
    Scene s;
    auto driver = KernelReconcileDriver::configured(s.scheduler, s.kernel, s.table());
    bool rejected = false;
    try {
        ConfigurationSession invalid(s.scheduler, driver, std::vector<PluginDefinition>{}, s.writer, s.writer->disk);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    auto legacy =
        std::make_shared<KernelReconcileDriver>(s.scheduler, s.kernel, std::vector<KernelPlugin>{{"p", s.runtime}});
    CHECK(!legacy->configurationDefinitions());
    rejected = false;
    try {
        ConfigurationSession invalid(s.scheduler, legacy, s.writer, s.writer->disk);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    for (const auto& value : {driver, legacy}) {
        auto done = value->close();
        s.run();
        CHECK(done.tryResult()->value().quiescent);
    }
    s.close();
}
void coordinator(bool change)
{
    Scene s;
    auto old = s.table(), next = s.table("new");
    auto driver = std::make_shared<ProtocolDriver>();
    driver->current = change ? old : next;
    ConfigurationCoordinator coordinator(s.scheduler, driver);
    auto target = old->validate(parseConfiguration(text()).value()).value();
    if (change)
        driver->onPreflight = [&] { driver->current = next; };
    CHECK(coordinator.accept(target, 0).error().code == ErrorCode::ConfigConflict &&
          coordinator.snapshot().value().desiredRevision == 0);
    CHECK(driver->preflights == (change ? 1 : 0));
    auto valid = next->validate(parseConfiguration(text("new")).value()).value();
    auto accepted = coordinator.accept(std::move(valid), 0).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    auto done = coordinator.close();
    s.run();
    CHECK(done.tryResult()->value().quiescent);
    s.close();
}
void staleDraft(bool duringPreview)
{
    Scene s;
    auto driver = std::make_shared<ProtocolDriver>();
    auto next = s.table("new");
    if (duringPreview) {
        driver->current = ConfigurationDefinitions::create({{{"p",
                                                              {},
                                                              [&](const std::string&) {
                                                                  driver->current = next;
                                                                  return Result<void>::success();
                                                              }},
                                                             s.runtime}})
                              .value();
    } else
        driver->current = s.table();
    ConfigurationSession session(s.scheduler, driver, s.writer, s.writer->disk);
    if (duringPreview)
        CHECK(!session.preview(text()).hasValue());
    else {
        auto draft = session.preview(text()).value();
        driver->current = next;
        CHECK(session.apply(draft).error().code == ErrorCode::ConfigConflict);
    }
    const auto state = session.snapshot().value();
    CHECK(state.coordinator.desiredRevision == 0 && !state.coordinator.desired && s.writer->calls == 0);
    auto done = session.close();
    s.run();
    CHECK(done.tryResult()->hasValue());
    s.close();
}
void lifetime()
{
    std::weak_ptr<int> lifetime;
    std::shared_ptr<const ConfigurationDefinitions> table;
    {
        auto value = std::make_shared<int>(7);
        lifetime = value;
        Runtime runtime([value](Context) { return completedTask(); });
        table = ConfigurationDefinitions::create({{{"p",
                                                    {},
                                                    [&](const std::string&) {
                                                        table.reset();
                                                        CHECK(!lifetime.expired());
                                                        return Result<void>::success();
                                                    }},
                                                   runtime}})
                    .value();
    }
    auto candidate = table->validate(parseConfiguration(text()).value()).value();
    CHECK(!table && lifetime.expired() && candidate.hasDefinitionIdentity());
}
void readReentry()
{
    Scene s;
    auto driver = std::make_shared<ProtocolDriver>();
    driver->current = s.table();
    auto session = std::make_unique<ConfigurationSession>(s.scheduler, driver, s.writer, s.writer->disk);
    std::optional<Task<void>> closed;
    driver->onRead = [&] {
        closed = session->close();
        session.reset();
    };
    auto names = session->registeredPlugins();
    s.run();
    CHECK(names.value() == std::vector<std::string>{"p"} && !session && closed->tryResult()->hasValue());
    s.close();
}
void snapshotReentry(bool close)
{
    Scene s;
    auto driver = std::make_shared<ProtocolDriver>();
    driver->current = s.table();
    auto next = s.table("new");
    ConfigurationSession session(s.scheduler, driver, s.writer, s.writer->disk);
    std::optional<Task<void>> closed;
    driver->onSnapshot = [&] {
        if (close)
            closed = session.close();
        else
            driver->current = next;
    };
    CHECK(!session.preview(text()).hasValue());
    CHECK(session.snapshot().value().coordinator.desiredRevision == 0 && s.writer->calls == 0);
    if (!close)
        closed = session.close();
    s.run();
    CHECK(closed->tryResult()->hasValue());
    s.close();
}
void getterFailure()
{
    Scene s;
    auto driver = std::make_shared<ProtocolDriver>();
    driver->current = s.table();
    ConfigurationSession session(s.scheduler, driver, s.writer, s.writer->disk);
    auto fail = [&] { driver->onRead = [] { throw std::runtime_error("definition source failed"); }; };
    fail();
    CHECK(!session.preview(text()).hasValue());
    fail();
    CHECK(!session.registeredPlugins().hasValue());
    fail();
    CHECK(!session.addInstance("new", "p", "old", 0).hasValue());
    CHECK(session.snapshot().value().coordinator.desiredRevision == 0 && s.writer->calls == 0);
    auto done = session.close();
    s.run();
    CHECK(done.tryResult()->hasValue());
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "basic")
            basic();
        else if (name == "invalid")
            invalid();
        else if (name == "contracts")
            contracts();
        else if (name == "driver")
            driver();
        else if (name == "session")
            session();
        else if (name == "constructors")
            constructors();
        else if (name == "foreign")
            coordinator(false);
        else if (name == "reentry")
            coordinator(true);
        else if (name == "draft")
            staleDraft(false);
        else if (name == "schema_reentry")
            staleDraft(true);
        else if (name == "lifetime")
            lifetime();
        else if (name == "read_reentry")
            readReentry();
        else if (name == "snapshot_reentry")
            snapshotReentry(false);
        else if (name == "snapshot_close")
            snapshotReentry(true);
        else if (name == "getter_failure")
            getterFailure();
        else
            throw std::runtime_error("unknown definitions case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
