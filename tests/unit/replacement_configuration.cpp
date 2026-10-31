/** @file replacement_configuration.cpp
 * @brief 排他作用域与真实 Session/Kernel 驱动；文件后端仅使用内存回执。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <stdexcept>
using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Writer final : IConfigurationWriter {
    ConfigurationFileSnapshot disk{{"memory/replacement.yaml", false, {}}, {}};
    int calls = 0;
    Task<ConfigurationFileCommit> commit(std::shared_ptr<const ValidatedCandidate> target,
                                         ConfigurationFileVersion expected) override
    {
        CHECK(expected.path == disk.version.path && expected.exists == disk.version.exists &&
              expected.sha256 == disk.version.sha256);
        ++calls;
        disk = {{disk.version.path, true, std::string(64, static_cast<char>('a' + calls % 5))},
                target->source().normalizedYaml()};
        Promise<ConfigurationFileCommit> done;
        done.resolve({FileCommitOutcome::Committed, disk, {}});
        return done.task();
    }
};
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();
    std::shared_ptr<ConfigurationSession> session;
    std::vector<KernelPlugin> plugins;
    std::vector<PluginDefinition> definitions;
    Runtime add(const char* name, Runtime::Activate activate, std::vector<ServiceRequirement> requirements = {})
    {
        auto runtime = kernel->registerPlugin(name, std::move(activate), requirements).value();
        plugins.push_back({name, runtime});
        std::vector<InjectSpec> required;
        for (const auto& item : requirements)
            required.push_back({item.key.name, item.key.interfaceMajor, item.minimumMinor, item.capabilities});
        definitions.push_back({name, std::move(required), [](const std::string&) { return Result<void>::success(); }});
        return runtime;
    }
    void start()
    {
        auto driver = std::make_shared<KernelReconcileDriver>(scheduler, kernel, plugins);
        session = std::make_shared<ConfigurationSession>(scheduler, driver, definitions, writer, writer->disk);
    }
    Result<ConfigurationDraft> draft(const std::string& entries) const
    {
        return session->preview("formatVersion: 1\nplugins: " + entries + "\n");
    }
    void run() { scheduler->runUntilIdle(30000).value(); }
    void close()
    {
        auto done = session->close();
        run();
        CHECK(done.tryResult()->hasValue());
        auto closed = kernel->close();
        run();
        CHECK(closed.tryResult()->value().quiescent && kernel->services().snapshot().value().empty());
    }
};
void cas()
{
    Scene s;
    int aCalls = 0, bCalls = 0;
    auto a = s.add("a", [&](Context) {
        ++aCalls;
        return completedTask();
    });
    s.add("b", [&](Context) {
        ++bCalls;
        return completedTask();
    });
    s.start();
    const std::string before = "[{id: a, plugin: a, config: old-a}, {id: b, plugin: b, config: old-b}]";
    s.session->apply(s.draft(before).value()).value();
    s.run();
    auto old = s.session->snapshot().value();
    auto prior = s.draft("[{id: a, plugin: a, config: new-a}, {id: b, plugin: b, config: old-b}]").value();
    auto scope = s.kernel->reserveReplacement({a.id()}).value();
    CHECK(!s.session->apply(prior).hasValue() && !s.draft("[{id: a, plugin: a, config: new-a}]").hasValue());
    CHECK(!s.session->setEnabled("a", false, 1).hasValue() &&
          !s.session->addInstance("extra", "a", "{}", 1).hasValue());
    auto unchanged = s.session->snapshot().value();
    CHECK(unchanged.coordinator.desiredRevision == 1 && unchanged.store.desiredRevision == 1 &&
          unchanged.store.desired == old.store.desired && s.writer->calls == 0);
    scope.freeze().value();
    auto unrelated = s.draft("[{id: a, plugin: a, config: old-a}, {id: b, plugin: b, config: new-b}]").value();
    auto accepted = s.session->apply(unrelated).value();
    s.run();
    CHECK(accepted.revision == 2 && accepted.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    CHECK(aCalls == 1 && bCalls == 2 && s.session->snapshot().value().store.desiredRevision == 2 &&
          s.writer->calls == 0);
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto whileStopped =
        s.session->apply(s.draft("[{id: a, plugin: a, config: old-a}, {id: b, plugin: b, config: next-b}]").value())
            .value();
    s.run();
    CHECK(whileStopped.revision == 3 && whileStopped.settled.tryResult()->value().outcome == PlanOutcome::Pending &&
          aCalls == 1 && bCalls == 3);
    scope.release().value();
    s.run();
    CHECK(aCalls == 1);
    auto resumed =
        s.session->apply(s.draft("[{id: a, plugin: a, config: old-a}, {id: b, plugin: b, config: next-b}]").value())
            .value();
    s.run();
    CHECK(resumed.settled.tryResult()->value().outcome == PlanOutcome::Applied && aCalls == 2 &&
          s.session->snapshot().value().coordinator.running.size() == 2);
    s.close();
    CHECK(s.writer->calls == 1);
}
void restore()
{
    Scene s;
    int children = 0;
    auto parent = s.add("parent", [](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"x"})});
    auto child = s.add("child", [&](Context) {
        ++children;
        return completedTask();
    });
    s.start();
    auto accepted =
        s.session->apply(s.draft("[{id: p, plugin: parent, children: [{id: c, plugin: child}]}]").value()).value();
    s.run();
    CHECK(accepted.settled.tryResult()->value().outcome == PlanOutcome::Pending);
    auto scope = s.kernel->reserveReplacement({child.id()}).value();
    CHECK(scope.snapshot().value().closure.members.empty());
    auto host = s.kernel->services().stage({"x"}, std::make_shared<int>(4)).value();
    host.publish().value();
    s.run();
    const auto waiting = s.session->snapshot().value();
    CHECK(children == 0 && !waiting.deliveryBlocked && waiting.coordinator.running.size() == 2);
    CHECK(waiting.coordinator.running[0].state == InstanceState::Active &&
          waiting.coordinator.running[1].state == InstanceState::Pending && !waiting.coordinator.running[1].failure);
    scope.freeze().value();
    scope.release().value();
    s.run();
    const auto resumed = s.session->snapshot().value();
    CHECK(children == 1 && resumed.coordinator.running[1].state == InstanceState::Active &&
          resumed.coordinator.running[1].parentEpoch == 1);
    CHECK(resumed.coordinator.desiredRevision == 1 && resumed.store.desiredRevision == 1 &&
          resumed.latestPlan->outcome == PlanOutcome::Pending);
    host.retire().value();
    s.run();
    host.release().value();
    s.close();
    (void)parent;
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "cas")
            cas();
        else if (name == "restore")
            restore();
        else
            throw std::runtime_error("unknown replacement configuration case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
