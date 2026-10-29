/** @file module_catalog.cpp
 * @brief 目录冻结、无副作用校验、Runtime 身份和可停用门禁。
 */
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cordis;
using namespace cordis::loader;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    unsigned calls = 0, schemas = 0;
    Runtime runtime{[this](Context) {
        ++calls;
        return completedTask();
    }};
    PluginModule module{"native", "native/module.json"};
    std::vector<PluginDefinition> definitions()
    {
        return {{"plugin",
                 {},
                 [this](const std::string&) {
                     ++schemas;
                     return Result<void>::success();
                 },
                 module}};
    }
    std::string text(const std::string& path = "native/module.json", const std::string& alias = "native",
                     bool disabled = false)
    {
        return "formatVersion: 1\nmodules: {native: {manifest: '" + path +
               "'}}\nplugins: [{id: one, plugin: plugin, module: " + alias + (disabled ? ", disabled: true" : "") +
               "}]";
    }
    ValidatedCandidate candidate(bool disabled = false)
    {
        return validateCandidate(parseConfiguration(text("native/module.json", "native", disabled)).value(),
                                 definitions())
            .value();
    }
};
void registration()
{
    Scene s;
    CHECK(s.kernel->registerRuntime("runtime", s.runtime).value().id() == s.runtime.id() && s.calls == 0);
    CHECK(!s.kernel->registerRuntime("alias", s.runtime).hasValue());
    Runtime other([](Context) { return completedTask(); });
    CHECK(!s.kernel->registerRuntime("runtime", other).hasValue());
    CHECK(!s.kernel->registerRuntime("", other).hasValue());
    Runtime moved(std::move(other));
    CHECK(!s.kernel->registerRuntime("invalid", other).hasValue());
    bool wrong = false;
    std::thread worker(
        [&] { wrong = s.kernel->registerRuntime("worker", moved).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    auto instance = s.kernel->mount(s.runtime).value();
    CHECK(s.scheduler->runUntilIdle().hasValue() && s.calls == 1);
    auto closed = s.kernel->close();
    CHECK(s.scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
    CHECK(s.kernel->registerRuntime("closed", moved).error().code == ErrorCode::ContextClosing);
}
void catalog()
{
    Scene s;
    CHECK(s.candidate().source().structure().modules().size() == 1 && s.schemas == 1 && s.calls == 0);
    for (const auto& bad : {s.text("changed.json"), s.text("native/module.json", "builtin"),
                            std::string("formatVersion: 1\nmodules: {unknown: {manifest: x}}\nplugins: []")})
        CHECK(!validateCandidate(parseConfiguration(bad).value(), s.definitions()).hasValue() && s.schemas == 1);
    auto error =
        validateCandidate(parseConfiguration(s.text("native/module.json", "builtin")).value(), s.definitions()).error();
    CHECK(error.phase == "loader.definition" && error.entryPath == "one" && error.message.find("line 3,") == 0);
    auto definitions = s.definitions();
    definitions.push_back(
        {"second", {}, [](const std::string&) { return Result<void>::success(); }, {"native", "other.json"}});
    CHECK(!validateCandidate(parseConfiguration(s.text()).value(), definitions).hasValue());
    definitions = s.definitions();
    definitions[0].module = {"builtin", "forbidden"};
    CHECK(!validateCandidate(parseConfiguration("formatVersion: 1\nplugins: []").value(), definitions).hasValue());
}
void availability()
{
    Scene s;
    CHECK(s.kernel->registerRuntime("runtime", s.runtime).hasValue());
    bool ready = true, throws = false;
    auto gate = [&] {
        if (throws)
            throw std::runtime_error("host gate");
        return ready ? Result<void>::success()
                     : Result<void>::failure(makeError(ErrorCode::ModulePinned, "unavailable"));
    };
    auto driver = std::make_shared<KernelReconcileDriver>(
        s.scheduler, s.kernel, std::vector<KernelPlugin>{{"plugin", s.runtime, {}, s.module, gate}});
    CHECK(driver->preflight(s.candidate()).hasValue() && s.calls == 0);
    ready = false;
    CHECK(driver->preflight(s.candidate()).error().code == ErrorCode::ModulePinned);
    CHECK(driver->preflight(s.candidate(true)).hasValue());
    throws = true;
    CHECK(driver->preflight(s.candidate()).error().code == ErrorCode::ConfigInvalid);
    CHECK(driver->preflight(s.candidate(true)).hasValue());
    throws = false;
    ready = true;
    ConfigurationCoordinator coordinator(s.scheduler, driver);
    auto applied = coordinator.accept(s.candidate(), 0).value();
    CHECK(s.scheduler->runUntilIdle().hasValue() &&
          applied.settled.tryResult()->value().outcome == PlanOutcome::Applied);
    ready = false;
    auto disabled = coordinator.accept(s.candidate(true), applied.revision).value();
    CHECK(s.scheduler->runUntilIdle().hasValue() && disabled.settled.isReady() && driver->snapshot().value().empty());
    auto closed = coordinator.close();
    CHECK(s.scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}
void budget()
{
    Scene s;
    CHECK(s.kernel->registerRuntime("runtime", s.runtime).hasValue());
    KernelReconcileDriver driver(s.scheduler, s.kernel,
                                 {{"plugin",
                                   s.runtime,
                                   {ServiceRequirement::of<int>({"a", 1}), ServiceRequirement::of<int>({"b", 1})},
                                   s.module,
                                   {},
                                   1}});
    auto text = "formatVersion: 1\nmodules: {native: {manifest: native/module.json}}\nplugins: [{id: one, plugin: "
                "plugin, module: native, inject: [a/v1, b/v1]}]";
    auto candidate = validateCandidate(parseConfiguration(text).value(), s.definitions()).value();
    CHECK(driver.preflight(candidate).error().message == "plugin dependency budget exceeded" && s.calls == 0);
    CHECK(driver.close().isReady());
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "registration")
            registration();
        else if (name == "catalog")
            catalog();
        else if (name == "availability")
            availability();
        else if (name == "budget")
            budget();
        else
            return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
