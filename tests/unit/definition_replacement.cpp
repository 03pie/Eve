/** @file definition_replacement.cpp
 * @brief 完整目录换代保留未改定义，并以真实 Core 定义切换验证新 schema/工厂共同来源。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct SchemaProbe {
    std::shared_ptr<int> copies;
    std::shared_ptr<int> calls;
    SchemaProbe(std::shared_ptr<int> copied, std::shared_ptr<int> called)
        : copies(std::move(copied)), calls(std::move(called))
    {
    }
    SchemaProbe(const SchemaProbe& other) : copies(other.copies), calls(other.calls) { ++*copies; }
    Result<void> operator()(const std::string&) const
    {
        ++*calls;
        return Result<void>::success();
    }
};
PluginDefinition schema(const char* id, const char* accepted = "old", PluginModule module = {})
{
    return {id,
            {},
            [value = std::string(accepted)](const std::string& config) {
                return config == '"' + value + '"'
                           ? Result<void>::success()
                           : Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "schema mismatch"));
            },
            std::move(module)};
}
std::string text(const char* config)
{
    return std::string("formatVersion: 1\nplugins: [{id: item, plugin: p, config: ") + config +
           "}, {id: other, plugin: stable, config: old}]\n";
}
void immutable()
{
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    auto kernel = std::make_shared<Kernel>(scheduler);
    auto run = [&] { scheduler->runUntilIdle(30000).value(); };
    int previous = 0, following = 0, untouched = 0;
    auto old = kernel
                   ->registerPlugin("p",
                                    [&](Context) {
                                        ++previous;
                                        return completedTask();
                                    })
                   .value();
    auto stable = kernel
                      ->registerPlugin("stable",
                                       [&](Context) {
                                           ++untouched;
                                           return completedTask();
                                       })
                      .value();
    auto copies = std::make_shared<int>(0), calls = std::make_shared<int>(0);
    PluginDefinition other{"stable", {}, SchemaProbe(copies, calls)};
    auto base = ConfigurationDefinitions::create({{schema("p"), old}, {std::move(other), stable}}).value();
    auto oldDraft = base->validate(parseConfiguration(text("old")).value()).value();
    auto driver = KernelReconcileDriver::configured(scheduler, kernel, base);
    Runtime next([&](Context context) {
        CHECK(context.config() == "\"new\"");
        ++following;
        return completedTask();
    });
    *copies = *calls = 0;
    auto changed = base->replacing({{schema("p", "new"), next}}).value();
    CHECK(*copies == 0 && *calls == 0 && previous == 0 && following == 0);
    CHECK(base->owns(oldDraft) && !changed->owns(oldDraft));
    CHECK(base->validate(parseConfiguration(text("old")).value()).hasValue());
    CHECK(!base->validate(parseConfiguration(text("new")).value()).hasValue());
    CHECK(!changed->validate(parseConfiguration(text("old")).value()).hasValue());
    auto newDraft = changed->validate(parseConfiguration(text("new")).value()).value();
    CHECK(changed->owns(newDraft) && !base->owns(newDraft));
    CHECK(driver->preflight(newDraft).error().code == ErrorCode::ConfigConflict);
    auto first = kernel->mount(old, "\"old\"").value();
    auto unaffected = kernel->mount(stable).value();
    run();
    const auto stableId = unaffected.snapshot().value().instance;
    auto scope = kernel->reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    auto stopped = scope.stop();
    run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto replacement = scope.stage(next, "\"new\"").value();
    run();
    CHECK(replacement.ready().tryResult()->hasValue());
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    scope.release().value();
    auto current = KernelReconcileDriver::configured(scheduler, kernel, changed);
    current->preflight(newDraft).value();
    CHECK(previous == 1 && following == 1 && untouched == 1 && kernel->snapshot().value().runtimes == 2);
    CHECK(unaffected.snapshot().value().instance == stableId &&
          unaffected.snapshot().value().state == InstanceState::Active);
    auto a = driver->close(), b = current->close(), done = kernel->close();
    run();
    CHECK(a.tryResult()->value().quiescent && b.tryResult()->value().quiescent && done.tryResult()->value().quiescent);
}
void guards()
{
    Runtime old([](Context) { return completedTask(); }), next([](Context) { return completedTask(); });
    auto base = ConfigurationDefinitions::create({{schema("p"), old}}).value();
    CHECK(!base->replacing({}).hasValue());
    CHECK(!base->replacing({{schema("unknown"), next}}).hasValue());
    CHECK(!base->replacing({{schema("p"), next}, {schema("p"), next}}).hasValue());
    CHECK(!base->replacing({{{"p", {}, {}}, next}}).hasValue());
    auto moved = next;
    auto valid = std::move(moved);
    CHECK(!base->replacing({{schema("p"), moved}}).hasValue());
    auto modules = ConfigurationDefinitions::create({{schema("p", "old", {"native", "old/module.json"}), old},
                                                     {schema("q", "old", {"native", "old/module.json"}), old}})
                       .value();
    CHECK(!modules->replacing({{schema("p", "new", {"native", "next/module.json"}), next}}).hasValue());
    auto complete = modules
                        ->replacing({{schema("p", "new", {"native", "next/module.json"}), next},
                                     {schema("q", "new", {"native", "next/module.json"}), next}})
                        .value();
    CHECK(complete->module("p").value().manifest == "next/module.json" &&
          modules->module("p").value().manifest == "old/module.json");
}
void lifetime()
{
    std::weak_ptr<int> oldCapture, newCapture, unchanged;
    std::shared_ptr<const ConfigurationDefinitions> changed;
    std::optional<ValidatedCandidate> retained;
    {
        auto before = std::make_shared<int>(1), after = std::make_shared<int>(2), stable = std::make_shared<int>(3);
        oldCapture = before;
        newCapture = after;
        unchanged = stable;
        Runtime old([before](Context) { return completedTask(); });
        Runtime next([after](Context) { return completedTask(); });
        Runtime same([stable](Context) { return completedTask(); });
        auto base = ConfigurationDefinitions::create({{schema("p"), old}, {schema("stable"), same}}).value();
        retained.emplace(base->validate(parseConfiguration(text("old")).value()).value());
        changed = base->replacing({{schema("p", "new"), next}}).value();
    }
    CHECK(oldCapture.expired() && !newCapture.expired() && !unchanged.expired());
    CHECK(!changed->owns(*retained));
    changed.reset();
    CHECK(newCapture.expired() && unchanged.expired());
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "immutable")
            immutable();
        else if (name == "guards")
            guards();
        else if (name == "lifetime")
            lifetime();
        else
            throw std::runtime_error("unknown definition replacement case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
