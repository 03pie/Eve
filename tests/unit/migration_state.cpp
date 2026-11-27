/** @file migration_state.cpp
 * @brief 真实实例的检查点、导入先后关系及重入关闭存活屏障。
 */
#include "../../src/core/instance_state_p.hpp"
#include "../../src/core/replacement_inspect_p.hpp"
#include <eve/kernel.hpp>
#include <eve/testing/fake_scheduler.hpp>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace eve;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
StateContract contract(std::uint32_t version = 1)
{
    return {{"test.counter", version},
            [](const StateSnapshot& value) { return Result<std::string>::success(value.json); }};
}
StateParticipant participant()
{
    return {[] { return Result<std::string>::success("7"); }, [](std::string_view) { return Result<void>::success(); }};
}
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel{scheduler};
    void run() { scheduler->runUntilIdle(30000).value(); }
    void close()
    {
        auto closed = kernel.close();
        run();
        CHECK(closed.tryResult() && closed.tryResult()->hasValue() && closed.tryResult()->value().quiescent);
        CHECK(kernel.replacements().value().empty());
    }
    Runtime add(std::string name, Runtime value) { return kernel.registerRuntime(std::move(name), value).value(); }
};
void schemas()
{
    for (auto schema : {StateSchema{"", 1}, StateSchema{"with space", 1}, StateSchema{"name", 0},
                        StateSchema{std::string(129, 'a'), 1}}) {
        bool rejected = false;
        try {
            Runtime::stateful([](Context) { return completedTask(); }, {std::move(schema), contract().validate});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    bool rejected = false;
    try {
        Runtime::stateful([](Context) { return completedTask(); }, {{"valid", 1}, {}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    auto runtime = Runtime::stateful(
        [](Context context) {
            context.state(participant()).value();
            return completedTask();
        },
        contract());
    auto copy = runtime;
    CHECK(copy.id() == runtime.id() && copy.stateSchema()->id == "test.counter" && copy.stateSchema()->version == 1);
    Runtime moved(std::move(copy));
    CHECK(!copy.id().valid() && !copy.stateSchema() && moved.id() == runtime.id());
    auto boundary = contract();
    boundary.schema.id = std::string(maxStateSchemaBytes, 'a');
    boundary.schema.version = UINT32_MAX;
    auto maximum = Runtime::stateful([](Context) { return completedTask(); }, std::move(boundary));
    CHECK(maximum.stateSchema()->id.size() == maxStateSchemaBytes && maximum.stateSchema()->version == UINT32_MAX);
}
void roundtrip()
{
    Scene scene;
    std::vector<int> values;
    int nextValue = 21;
    auto old =
        scene.add("old", Runtime::stateful(
                             [&](Context context) {
                                 auto value = std::make_shared<int>(nextValue++);
                                 context
                                     .state({[value] { return Result<std::string>::success(std::to_string(*value)); },
                                             [value](std::string_view json) {
                                                 *value = std::stoi(std::string(json));
                                                 return Result<void>::success();
                                             }})
                                     .value();
                                 return completedTask();
                             },
                             contract()));
    auto one = scene.kernel.mount(old).value(), two = scene.kernel.mount(old).value();
    scene.run();
    int validations = 0;
    auto targetContract = contract(2);
    targetContract.validate = [&](const StateSnapshot& input) {
        ++validations;
        CHECK(input.schema.version == 1 && input.schema.id == "test.counter");
        return Result<std::string>::success(std::to_string(std::stoi(input.json) + 100));
    };
    auto next = Runtime::stateful(
        [&](Context context) {
            auto imported = std::make_shared<int>(0);
            context
                .state({[imported] { return Result<std::string>::success(std::to_string(*imported)); },
                        [imported](std::string_view input) {
                            *imported = std::stoi(std::string(input));
                            return Result<void>::success();
                        }})
                .value();
            values.push_back(*imported); // start/activate 的业务部分在导入之后。
            return completedTask();
        },
        std::move(targetContract));
    auto scope = scene.kernel.reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), next}}).value();
    scope.freeze().value();
    auto first = detail::InstanceStateAccess::capture(scope, one).value().value();
    auto second = detail::InstanceStateAccess::capture(scope, two).value().value();
    CHECK(first.json == "21" && second.json == "22");
    auto stopped = scope.stop();
    scene.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto builder = detail::CoreReplacementAccess::builder(scope, scene.kernel).value();
    auto a = builder.stage(next, {}, {}, {}, first).value();
    auto b = scope.stage(next, {}, {}, {}, second).value();
    scene.run();
    CHECK(a.ready().tryResult()->hasValue() && b.ready().tryResult()->hasValue());
    CHECK(validations == 2 && values == std::vector<int>({121, 122}));
    CHECK(first.json == "21" && first.schema.version == 1 && second.json == "22");
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    scope.release().value();
    scene.close();
}
void activation(const std::string& mode)
{
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    auto schema = contract();
    bool activated = false;
    if (mode == "validate_fail")
        schema.validate = [](const StateSnapshot&) {
            return Result<std::string>::failure(makeError(ErrorCode::ConfigInvalid, "schema rejected"));
        };
    if (mode == "validate_throw")
        schema.validate = [](const StateSnapshot&) -> Result<std::string> { throw std::runtime_error("validation"); };
    if (mode == "converted_budget")
        schema.validate = [](const StateSnapshot&) {
            return Result<std::string>::success(std::string(maxStateJsonBytes + 1, '0'));
        };
    auto runtime = Runtime::stateful(
        [&](Context context) {
            activated = true;
            if (mode == "missing")
                return completedTask();
            auto callbacks = participant();
            if (mode == "restore_fail")
                callbacks.restore = [](std::string_view) {
                    return Result<void>::failure(makeError(ErrorCode::ConfigInvalid, "restore rejected"));
                };
            if (mode == "restore_throw")
                callbacks.restore = [](std::string_view) -> Result<void> { throw std::runtime_error("restore"); };
            if (mode == "reentry_register")
                callbacks.restore = [context](std::string_view) {
                    CHECK(!context.state(participant()).hasValue());
                    return Result<void>::success();
                };
            if (mode == "invalid_participant")
                callbacks.capture = {};
            (void)context.state(std::move(callbacks)); // 即使忽略登记错误也不能激活。
            if (mode == "duplicate")
                (void)context.state(participant());
            return completedTask();
        },
        std::move(schema));
    auto instance = Instance::mount(runtime, scheduler, {}, {}, {}, {}, StateSnapshot{{"test.counter", 1}, "9"});
    scheduler->runUntilIdle(30000).value();
    CHECK(instance.ready().tryResult() && !instance.ready().tryResult()->hasValue());
    auto expected = ErrorCode::InvalidArgument;
    if (mode == "missing")
        expected = ErrorCode::ActivationFailed;
    else if (mode == "validate_fail" || mode == "restore_fail")
        expected = ErrorCode::ConfigInvalid;
    else if (mode == "validate_throw" || mode == "restore_throw")
        expected = ErrorCode::CallbackFailed;
    else if (mode == "reentry_register")
        expected = ErrorCode::ContextClosing;
    CHECK(instance.ready().tryResult()->error().code == expected);
    CHECK(activated == (mode != "validate_fail" && mode != "validate_throw" && mode != "converted_budget"));
    auto stopped = instance.stop();
    scheduler->runUntilIdle(30000).value();
    CHECK(stopped.tryResult()->value().quiescent);
}
void captureFailures()
{
    Scene scene;
    std::string mode = "busy";
    auto old = scene.add("old", Runtime::stateful(
                                    [&](Context context) {
                                        context
                                            .state({[&]() -> Result<std::string> {
                                                        if (mode == "busy")
                                                            return Result<std::string>::failure(makeError(
                                                                ErrorCode::ConfigConflict, "checkpoint busy"));
                                                        if (mode == "throw")
                                                            throw std::runtime_error("capture");
                                                        return Result<std::string>::success(mode);
                                                    },
                                                    participant().restore})
                                            .value();
                                        return completedTask();
                                    },
                                    contract()));
    auto instance = scene.kernel.mount(old).value();
    scene.run();
    auto context = instance.whenActiveAfter().tryResult()->value();
    auto scope = scene.kernel.reserveReplacement({old.id()}).value();
    scope.freeze().value();
    for (auto value :
         {std::string("busy"), std::string("throw"), std::string(), std::string(maxStateJsonBytes + 1, '0')}) {
        mode = std::move(value);
        CHECK(!detail::InstanceStateAccess::capture(scope, instance).hasValue());
    }
    mode = "null";
    auto captured = detail::InstanceStateAccess::capture(scope, instance).value();
    CHECK(captured && captured->json == "null");
    mode = "\"" + std::string(maxStateJsonBytes - 2, '0') + "\"";
    CHECK(detail::InstanceStateAccess::capture(scope, instance).value()->json.size() == maxStateJsonBytes);
    scope.release().value();
    CHECK(context.identity().hasValue() && instance.snapshot().value().state == InstanceState::Active);
    scene.close();
}
void guards()
{
    Scene scene;
    std::optional<Context> context;
    auto old = scene.add("old", Runtime::stateful(
                                    [&](Context c) {
                                        context = c;
                                        c.state(participant()).value();
                                        return completedTask();
                                    },
                                    contract()));
    auto other = scene.add("other", Runtime([](Context) { return completedTask(); }));
    auto instance = scene.kernel.mount(old).value(), foreign = scene.kernel.mount(other).value();
    scene.run();
    CHECK(context->state(participant()).error().code == ErrorCode::InvalidArgument);
    auto scope = scene.kernel.reserveReplacement({old.id()}).value();
    CHECK(!detail::InstanceStateAccess::capture(scope, instance).hasValue());
    scope.freeze().value();
    CHECK(!detail::InstanceStateAccess::capture(scope, foreign).hasValue());
    std::optional<ErrorCode> wrongCapture, wrongRegister;
    std::thread worker([&] {
        wrongCapture = detail::InstanceStateAccess::capture(scope, instance).error().code;
        wrongRegister = context->state(participant()).error().code;
    });
    worker.join();
    CHECK(wrongCapture == ErrorCode::WrongThread && wrongRegister == ErrorCode::WrongThread);
    CHECK(!context->state(participant()).hasValue());
    auto stopped = scope.stop();
    scene.run();
    CHECK(stopped.tryResult()->value().quiescent && !context->state(participant()).hasValue());
    CHECK(!detail::InstanceStateAccess::capture(scope, instance).hasValue());
    scope.release().value();
    scene.close();
}
void lifetime()
{
    Scene scene;
    std::weak_ptr<int> captured;
    std::vector<Context> contexts;
    int finalizers = 0;
    auto runtime = scene.add(
        "stateful", Runtime::stateful(
                        [&](Context context) {
                            contexts.push_back(context);
                            auto resource = std::make_shared<int>(42);
                            captured = resource;
                            context
                                .afterCleanup([&] {
                                    CHECK(captured.expired());
                                    ++finalizers;
                                    return completedTask();
                                })
                                .value();
                            context
                                .state({[resource] { return Result<std::string>::success(std::to_string(*resource)); },
                                        [resource](std::string_view) { return Result<void>::success(); }})
                                .value();
                            return completedTask();
                        },
                        contract()));
    auto instance = scene.kernel.mount(runtime).value();
    scene.run();
    CHECK(!captured.expired());
    instance.update("new config", 1).value();
    scene.run();
    CHECK(finalizers == 1 && !captured.expired() && contexts.size() == 2);
    CHECK(!contexts.front().state(participant()).hasValue());
    CHECK(instance.snapshot().value().epoch == 2);
    scene.close();
    CHECK(finalizers == 2 && captured.expired());
}
void reentryCapture()
{
    Scene scene;
    bool finalized = false, inside = false;
    std::optional<Task<CleanupReport>> closed;
    auto runtime = scene.add("old", Runtime::stateful(
                                        [&](Context context) {
                                            context
                                                .afterCleanup([&] {
                                                    CHECK(!inside);
                                                    finalized = true;
                                                    return completedTask();
                                                })
                                                .value();
                                            context
                                                .state({[&] {
                                                            inside = true;
                                                            closed = scene.kernel.close();
                                                            scene.run(); // 故意模拟插件违规泵事件，验证宿主的存活屏障。
                                                            CHECK(!finalized && !closed->isReady());
                                                            inside = false;
                                                            return Result<std::string>::success("42");
                                                        },
                                                        participant().restore})
                                                .value();
                                            return completedTask();
                                        },
                                        contract()));
    auto instance = scene.kernel.mount(runtime).value();
    scene.run();
    auto scope = scene.kernel.reserveReplacement({runtime.id()}).value();
    scope.freeze().value();
    CHECK(!detail::InstanceStateAccess::capture(scope, instance).hasValue());
    scene.run();
    CHECK(finalized && closed->tryResult()->value().quiescent);
}
void inputs()
{
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    auto runtime = Runtime::stateful(
        [](Context context) {
            context.state(participant()).value();
            return completedTask();
        },
        contract());
    for (auto snapshot : {StateSnapshot{{"valid", 0}, "null"}, StateSnapshot{{"valid", 1}, ""},
                          StateSnapshot{{"valid", 1}, std::string(maxStateJsonBytes + 1, '0')}}) {
        bool rejected = false;
        try {
            Instance::mount(runtime, scheduler, {}, {}, {}, {}, std::move(snapshot));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
    Runtime stateless([](Context) { return completedTask(); });
    bool rejected = false;
    try {
        Instance::mount(stateless, scheduler, {}, {}, {}, {}, StateSnapshot{{"valid", 1}, "null"});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}
void children()
{
    Scene scene;
    int imports = 0, observed = 0;
    std::optional<Instance> child;
    auto runtime = Runtime::stateful(
        [&](Context context) {
            auto value = std::make_shared<int>(0);
            context
                .state({[value] { return Result<std::string>::success(std::to_string(*value)); },
                        [&, value](std::string_view json) {
                            ++imports;
                            *value = std::stoi(std::string(json));
                            return Result<void>::success();
                        }})
                .value();
            observed = *value;
            return completedTask();
        },
        contract());
    auto parent = scene.add("parent", Runtime([&](Context context) {
                                child =
                                    context.inject(runtime, {}, {}, StateSnapshot{{"test.counter", 1}, "57"}).value();
                                return completedTask();
                            }));
    auto root = scene.kernel.mount(parent).value();
    scene.run();
    CHECK(root.ready().tryResult()->hasValue() && child->ready().tryResult()->hasValue() && imports == 1 &&
          observed == 57);
    child->update("new config", 1).value();
    scene.run();
    CHECK(child->snapshot().value().epoch == 2 && imports == 1 && observed == 0);
    auto scope = scene.kernel.reserveReplacement({parent.id()}).value();
    scope.freeze().value();
    CHECK(!detail::InstanceStateAccess::capture(scope, root).value());
    CHECK(detail::InstanceStateAccess::capture(scope, *child).value()->json == "0");
    scope.release().value();
    scene.close();
}
void unstarted(bool waiting)
{
    auto scheduler = std::make_shared<testing::FakeScheduler>();
    auto services = std::make_shared<ServiceRegistry>(scheduler);
    auto owner = std::make_shared<int>(1);
    std::weak_ptr<int> retained = owner;
    int activated = 0, validated = 0;
    auto instance = [&] {
        auto schema = contract();
        schema.validate = [&, owner](const StateSnapshot& input) {
            ++validated;
            return Result<std::string>::success(input.json);
        };
        auto runtime = Runtime::stateful(
            [&](Context context) {
                ++activated;
                context.state(participant()).value();
                return completedTask();
            },
            std::move(schema),
            waiting ? std::vector<ServiceRequirement>{ServiceRequirement::of<int>({"missing"})}
                    : std::vector<ServiceRequirement>{});
        return Instance::mount(
            runtime, scheduler, {}, services, {}, {},
            StateSnapshot{{"test.counter", 1}, "\"" + std::string(maxStateJsonBytes - 2, '0') + "\""});
    }();
    owner.reset();
    if (waiting)
        scheduler->runUntilIdle(30000).value();
    CHECK(!retained.expired() && instance.snapshot().value().epoch == 0);
    auto stopped = instance.stop();
    scheduler->runUntilIdle(30000).value();
    CHECK(stopped.tryResult()->value().quiescent && instance.snapshot().value().state == InstanceState::Disposed);
    CHECK(retained.expired() && activated == 0 && validated == 0);
}
/** @brief 专门允许嵌套运行，模拟 Qt 外部代码非法泵事件；不替换 FakeScheduler 的严格契约。 */
struct NestedScheduler : IScheduler {
    const std::thread::id owner = std::this_thread::get_id();
    std::deque<ScheduledJob> jobs;
    bool post(ScheduledJob job) override
    {
        jobs.push_back(std::move(job));
        return true;
    }
    bool isCurrentThread() const noexcept override { return owner == std::this_thread::get_id(); }
    bool isOpen() const override { return true; }
    void run()
    {
        std::size_t count = 0;
        while (!jobs.empty()) {
            CHECK(++count < 30000);
            auto job = std::move(jobs.front());
            jobs.pop_front();
            job.run();
        }
    }
};
void reentryActivation(bool validating)
{
    auto scheduler = std::make_shared<NestedScheduler>();
    Kernel kernel(scheduler);
    auto old = kernel.registerPlugin("old", [](Context) { return completedTask(); }).value();
    kernel.mount(old).value();
    scheduler->run();
    bool inside = false, finalized = false, business = false;
    std::optional<Task<CleanupReport>> closed;
    auto closeInside = [&] {
        inside = true;
        closed = kernel.close();
        scheduler->run();
        CHECK(!closed->isReady() && !finalized);
        inside = false;
    };
    auto schema = contract();
    if (validating)
        schema.validate = [&](const StateSnapshot& input) {
            closeInside();
            return Result<std::string>::success(input.json);
        };
    auto runtime = Runtime::stateful(
        [&](Context context) {
            context
                .afterCleanup([&] {
                    CHECK(!inside);
                    finalized = true;
                    return completedTask();
                })
                .value();
            auto bound = context.state({participant().capture, [&](std::string_view) {
                                            closeInside();
                                            return Result<void>::success();
                                        }});
            CHECK(!bound.hasValue() && !context.identity().hasValue());
            business = bound.hasValue();
            return completedTask(); // 不得通过忽略错误发布。
        },
        std::move(schema));
    auto scope = kernel.reserveReplacement({old.id()}).value();
    scope.prepareDefinitions({{old.id(), runtime}}).value();
    scope.freeze().value();
    auto stopped = scope.stop();
    scheduler->run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto instance = scope.stage(runtime, {}, {}, {}, StateSnapshot{{"test.counter", 1}, "3"}).value();
    scheduler->run();
    CHECK(!business && closed && closed->tryResult() && closed->tryResult()->value().quiescent);
    CHECK(finalized == !validating && !instance.ready().tryResult()->hasValue());
}
} // namespace
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "schemas")
            schemas();
        else if (name == "roundtrip")
            roundtrip();
        else if (name == "capture")
            captureFailures();
        else if (name == "guards")
            guards();
        else if (name == "lifetime")
            lifetime();
        else if (name == "reentry_capture")
            reentryCapture();
        else if (name == "inputs")
            inputs();
        else if (name == "children")
            children();
        else if (name == "waiting")
            unstarted(true);
        else if (name == "unstarted")
            unstarted(false);
        else if (name == "reentry_restore")
            reentryActivation(false);
        else if (name == "reentry_validate")
            reentryActivation(true);
        else
            activation(name);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
