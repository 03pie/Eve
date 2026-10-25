/** @file service_contract.cpp
 * @brief 验证 W4 输出描述、继承域视图、版本能力及异步调用租约。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief 缺少必供输出时全部回滚不发布，可选输出可缺席，错误输出不能被忽略。 */
void requiredOutputs()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    int started = 0, cleaned = 0;
    auto consumer = kernel.registerPlugin("consumer", [&](Context) { ++started; return completedTask(); },
        {ServiceRequirement::of<int>({"left"})}).value();
    CHECK(kernel.mount(consumer).hasValue());
    auto incomplete = kernel.registerPlugin("incomplete", [&](Context context) {
        CHECK(context.provide({"left"}, std::make_shared<int>(1)).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { ++cleaned; return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"left"}), ServiceOutput::of<int>({"right"})}).value();
    auto bad = kernel.mount(incomplete).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started == 0 && cleaned == 1 && kernel.services().snapshot().value().empty());
    CHECK(bad.ready().tryResult()->error().code == ErrorCode::ProvisionIncomplete);
    CHECK(bad.ready().tryResult()->error().message.find("right") != std::string::npos);
    auto wrong = kernel.registerPlugin("wrong", [](Context context) {
        (void)context.provide({"typed"}, std::make_shared<double>(2));
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"typed"})}).value();
    auto typed = kernel.mount(wrong).value();
    auto undeclared = kernel.registerPlugin("undeclared", [](Context context) {
        (void)context.provide({"unexpected"}, std::make_shared<int>(1));
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"optional"}, false)}).value();
    auto unexpected = kernel.mount(undeclared).value();
    auto noOutputs = kernel.registerPlugin("no-outputs", [](Context context) {
        (void)context.provide({"implicit"}, std::make_shared<int>(1));
        return completedTask();
    }).value();
    auto implicit = kernel.mount(noOutputs).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(typed.ready().tryResult()->error().code == ErrorCode::DependencyVersion);
    CHECK(unexpected.ready().tryResult()->error().code == ErrorCode::ProvisionIncomplete);
    CHECK(implicit.ready().tryResult()->error().code == ErrorCode::ProvisionIncomplete);
    auto valid = kernel.registerPlugin("valid", [](Context context) {
        if (context.config() != "missing") CHECK(context.provide({"left"}, std::make_shared<int>(1)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"left"}), ServiceOutput::of<int>({"optional"}, false)}).value();
    auto active = kernel.mount(valid).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(active.ready().tryResult()->hasValue() && started == 1);
    auto missingLater = active.update("missing", 1).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(missingLater.target.tryResult()->value().outcome == StableOutcome::Failed);
    CHECK(active.ready().tryResult()->hasValue());
    CHECK(active.snapshot().value().failure->code == ErrorCode::ProvisionIncomplete);
    CHECK(active.snapshot().value().failure->message.find("left") != std::string::npos);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief minor 和能力必须满足声明需求，失败不调用消费者 activate。 */
void versions()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    int started = 0;
    auto provider = kernel.registerPlugin("provider", [](Context context) {
        CHECK(context.provide({"versioned"}, std::make_shared<int>(4)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"versioned"}, true, {2, {"read", "write"}})}).value();
    CHECK(kernel.mount(provider).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    auto good = kernel.registerPlugin("good", [&](Context context) {
        CHECK(context.consume<int>({"versioned"}).value().withService([](int& value) { CHECK(value == 4); }).hasValue());
        ++started; return completedTask();
    }, {ServiceRequirement::of<int>({"versioned"}, 2, {"read"})}).value();
    auto tooNew = kernel.registerPlugin("new", [&](Context) { ++started; return completedTask(); },
        {ServiceRequirement::of<int>({"versioned"}, 3)}).value();
    auto missing = kernel.registerPlugin("missing", [&](Context) { ++started; return completedTask(); },
        {ServiceRequirement::of<int>({"versioned"}, 0, {"admin"})}).value();
    CHECK(kernel.mount(good).hasValue());
    auto newer = kernel.mount(tooNew).value();
    auto absent = kernel.mount(missing).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(started == 1);
    CHECK(newer.ready().tryResult()->error().code == ErrorCode::DependencyVersion);
    CHECK(absent.ready().tryResult()->error().message.find("admin") != std::string::npos);
    CHECK(kernel.services().snapshot().value()[0].metadata.minor == 2);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 派生 Context 仅覆盖一个服务并继承其他域，不修改父视图或跨域回退。 */
void realmViews()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    auto registry = kernel.services();
    auto r1 = registry.createRealm().value(), r2 = registry.createRealm().value();
    auto r3 = registry.createRealm().value(), absent = registry.createRealm().value();
    auto base = RealmMap{}.isolate("svc", r1).value();
    auto inherited = base.isolate("other", r2).value();
    auto external = registry.stage({"svc", 1, r3}, std::make_shared<int>(33)).value();
    CHECK(external.publish().hasValue());
    auto provider = kernel.registerPlugin("provider", [](Context context) {
        const int value = context.config() == "private" ? 11 : 1;
        CHECK(context.provide({"svc"}, std::make_shared<int>(value)).hasValue());
        CHECK(context.provide({"other"}, std::make_shared<int>(value * 2)).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"svc"}), ServiceOutput::of<int>({"other"})}).value();
    auto rootProvider = kernel.mount(provider).value();
    auto privateProvider = kernel.mount(provider, "private", inherited).value();
    std::optional<Context> captured;
    auto consumer = kernel.registerPlugin("consumer", [&](Context context) {
        const bool privateView = context.config() == "private";
        const int expected = privateView ? 11 : 1;
        CHECK(context.consume<int>({"svc"}).value().withService([&](int& value) { CHECK(value == expected); }).hasValue());
        if (privateView) {
            captured.emplace(context);
            auto child = context.isolate("svc", r3).value();
            CHECK(child.consume<int>({"svc"}).value().withService([](int& value) { CHECK(value == 33); }).hasValue());
            CHECK(child.consume<int>({"other"}).value().withService([](int& value) { CHECK(value == 22); }).hasValue());
            CHECK(context.consume<int>({"svc"}).value().withService([](int& value) { CHECK(value == 11); }).hasValue());
            CHECK(context.isolate("svc", absent).value().consume<int>({"svc"}).error().code == ErrorCode::DependencyMissing);
        }
        return completedTask();
    }, {ServiceRequirement::of<int>({"svc"}), ServiceRequirement::of<int>({"other"})}).value();
    auto root = kernel.mount(consumer).value();
    auto isolated = kernel.mount(consumer, "private", inherited).value();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(root.ready().tryResult()->hasValue() && isolated.ready().tryResult()->hasValue());
    CHECK(base.resolve({"other"}).realm == RealmId{1});
    auto stopped = rootProvider.stop();
    CHECK(scheduler->runUntilIdle().hasValue() && stopped.tryResult()->value().quiescent);
    CHECK(root.snapshot().value().state == InstanceState::Pending);
    CHECK(privateProvider.snapshot().value().state == InstanceState::Active);
    CHECK(isolated.snapshot().value().state == InstanceState::Active);
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
    CHECK(!captured->isolate("svc", r1).hasValue());
    CHECK(external.retire().hasValue() && external.release().hasValue());
}

/** @brief 异步调用持有提供者和消费者直到真实任务完成，异常和失败正常收敛。 */
void asyncCalls()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    Promise<void> gate;
    bool cleaned = false, read = false;
    std::weak_ptr<int> resource;
    std::optional<ServiceRef<int>> service;
    auto p = kernel.registerPlugin("p", [&](Context context) {
        auto object = std::make_shared<int>(42);
        resource = object;
        CHECK(context.provide({"async"}, object).hasValue());
        CHECK(context.onCleanup([&](CleanupContext) { cleaned = true; return completedTask(); }).hasValue());
        return completedTask();
    }, {}, {ServiceOutput::of<int>({"async"})}).value();
    auto c = kernel.registerPlugin("c", [&](Context context) {
        service.emplace(context.consume<int>({"async"}).value());
        return completedTask();
    }, {ServiceRequirement::of<int>({"async"})}).value();
    auto provider = kernel.mount(p).value();
    CHECK(kernel.mount(c).hasValue());
    CHECK(scheduler->runUntilIdle().hasValue());
    auto failure = service->invoke([](int&) -> Task<void> { throw std::runtime_error("invoke threw"); });
    auto rejected = service->invoke([](int&) { return failedTask(makeError(ErrorCode::ActivationFailed, "source failed")); });
    CHECK(!failure.isReady());
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(failure.tryResult()->error().code == ErrorCode::CallbackFailed);
    CHECK(rejected.tryResult()->error().code == ErrorCode::ActivationFailed);
    auto operation = service->invoke([&](int& value) {
        return gate.task().thenOn(scheduler, [&](const Result<void>& result) { CHECK(result.hasValue() && value == 42); read = true; });
    });
    auto stopped = provider.stop();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(!operation.isReady() && !stopped.isReady() && !cleaned && !resource.expired());
    CHECK(kernel.services().snapshot().value()[0].activeCalls == 1);
    CHECK(service->invoke([](int&) { return completedTask(); }).tryResult()->hasValue() == false);
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    CHECK(scheduler->runUntilIdle().hasValue());
    CHECK(operation.tryResult()->hasValue() && stopped.tryResult()->value().quiescent && read && cleaned);
    CHECK(resource.expired());
    auto closed = kernel.close();
    CHECK(scheduler->runUntilIdle().hasValue() && closed.tryResult()->value().quiescent);
}

/** @brief 调度器关闭使异步完成投递受阻时，计数与资源仍保留，不伪报完成。 */
void asyncBlocked()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    Promise<void> gate;
    auto object = std::make_shared<int>(1);
    std::weak_ptr<int> weak = object;
    auto provider = registry.stage({"blocked"}, object).value();
    object.reset();
    CHECK(provider.publish().hasValue());
    auto ref = registry.find<int>({"blocked"}).value();
    bool wrongThread = false;
    std::thread wrong([&] { wrongThread = ref.invoke([](int&) { return completedTask(); }).tryResult()->error().code == ErrorCode::WrongThread; });
    wrong.join();
    CHECK(wrongThread);
    auto operation = ref.invoke([gate](int&) { return gate.task(); });
    scheduler->close();
    std::thread worker([gate] { gate.resolve(); });
    worker.join();
    CHECK(!operation.isReady() && !weak.expired());
    CHECK(provider.snapshot().value().deliveryBlocked && provider.snapshot().value().activeCalls == 1);
    CHECK(provider.retire().hasValue() && !provider.release().hasValue());
}

/** @brief 无效描述、域归属与派生视图合并后的重键在修改资源前拒绝。 */
void schemaValidation()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler), other(scheduler);
    const auto activate = [](Context) { return completedTask(); };
    CHECK(!kernel.registerPlugin("duplicate", activate, {}, {ServiceOutput::of<int>({"x"}), ServiceOutput::of<int>({"x"})}).hasValue());
    CHECK(!kernel.registerPlugin("caps", activate, {ServiceRequirement::of<int>({"x"}, 0, {""})}).hasValue());
    CHECK(!kernel.registerPlugin("output-caps", activate, {}, {ServiceOutput::of<int>({"x"}, true, {1, {"a", "a"}})}).hasValue());
    CHECK(!kernel.registerPlugin("key", activate, {ServiceRequirement::of<int>({"", 0})}).hasValue());
    const auto foreign = other.services().createRealm().value();
    auto valid = kernel.registerPlugin("valid", activate).value();
    CHECK(!kernel.mount(valid, {}, RealmMap{}.isolate("x", foreign).value()).hasValue());
    auto realm = kernel.services().createRealm().value();
    auto collapse = kernel.registerPlugin("collapse", activate, {},
        {ServiceOutput::of<int>({"x"}), ServiceOutput::of<int>({"x", 1, realm})}).value();
    CHECK(!kernel.mount(collapse, {}, RealmMap{}.isolate("x", realm).value()).hasValue());
    auto closed = kernel.close();
    CHECK(closed.tryResult()->value().quiescent && kernel.snapshot().value().instances.empty());
    CHECK(other.close().tryResult()->value().quiescent);
}

/** @brief 附加依赖按精确域联合，弱要求不削弱 Runtime，新增依赖真正阻止提前激活。 */
void additionalDependencies()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    const auto realm = kernel.services().createRealm().value();
    int calls = 0;
    auto runtime = kernel
                       .registerPlugin("additional",
                                       [&](Context context) {
                                           context.consume<int>({"x"}).value();
                                           context.consume<int>({"y"}).value();
                                           ++calls;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"x"}, 2, {"read"})})
                       .value();
    auto instance = kernel
                        .mount(runtime, {}, RealmMap{}.isolate("x", realm).value(),
                               {ServiceRequirement::of<int>({"x"}, 1, {"write"}), ServiceRequirement::of<int>({"y"})})
                        .value();
    scheduler->runUntilIdle().value();
    CHECK(calls == 0 && instance.snapshot().value().state == InstanceState::Pending);
    auto dependencies = instance.dependencies().value();
    CHECK(dependencies.size() == 2 && dependencies[0].key.realm == realm && dependencies[0].minimumMinor == 2 &&
          dependencies[0].capabilities == std::vector<std::string>({"read", "write"}));
    auto x = kernel.services().stage<int>({"x", 1, realm}, std::make_shared<int>(1), {2, {"read", "write"}}).value();
    x.publish().value();
    scheduler->runUntilIdle().value();
    CHECK(calls == 0);
    auto y = kernel.services().stage<int>({"y"}, std::make_shared<int>(2)).value();
    y.publish().value();
    scheduler->runUntilIdle().value();
    CHECK(calls == 1 && instance.snapshot().value().committedLeases == 2);
    CHECK(runtime.requirements().size() == 1 &&
          runtime.requirements().front().capabilities == std::vector<std::string>({"read"}));
    auto closed = kernel.close();
    scheduler->runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
    x.retire().value();
    x.release().value();
    y.retire().value();
    y.release().value();
    scheduler->close();
}

/** @brief 非法附加契约及解析域后的类型冲突在挂载前拒绝，不增加运行实例。 */
void additionalGuards()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler), foreign(scheduler);
    int calls = 0;
    auto runtime = kernel
                       .registerPlugin("guard",
                                       [&](Context) {
                                           ++calls;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"x"})})
                       .value();
    const auto realm = kernel.services().createRealm().value();
    const auto other = foreign.services().createRealm().value();
    for (const auto& dependency : std::vector<ServiceRequirement>{
             ServiceRequirement::of<std::string>({"x"}), ServiceRequirement::of<int>({""}),
             ServiceRequirement::of<int>({"y", 0}), ServiceRequirement::of<int>({"y", 1, other}),
             ServiceRequirement::of<void>({"y"}), ServiceRequirement::of<int>({"y"}, 0, {""}),
             ServiceRequirement::of<int>({"y"}, 0, {"read", "read"})}) {
        CHECK(!kernel.mount(runtime, {}, {}, {dependency}).hasValue());
    }
    CHECK(!kernel
               .mount(runtime, {}, RealmMap{}.isolate("x", realm).value(),
                      {ServiceRequirement::of<std::string>({"x", 1, realm})})
               .hasValue());
    CHECK(kernel.snapshot().value().instances.empty() && calls == 0);
    CHECK(kernel.close().tryResult()->value().quiescent);
    CHECK(foreign.close().tryResult()->value().quiescent);
    scheduler->close();
}

/** @brief 更新及停止后重新加载保留附加依赖，Runtime 身份和固定定义不膨胀。 */
void additionalRemount()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    const auto realm = kernel.services().createRealm().value();
    auto provider = kernel.services().stage<int>({"extra", 1, realm}, std::make_shared<int>(3), {1, {"read"}}).value();
    provider.publish().value();
    int calls = 0;
    auto runtime = kernel
                       .registerPlugin("remount",
                                       [&](Context context) {
                                           context.consume<int>({"extra", 1, realm}).value();
                                           ++calls;
                                           return completedTask();
                                       })
                       .value();
    auto instance =
        kernel.mount(runtime, "first", {}, {ServiceRequirement::of<int>({"extra", 1, realm}, 1, {"read"})}).value();
    scheduler->runUntilIdle().value();
    auto updated = instance.update("latest", 1).value();
    scheduler->runUntilIdle().value();
    CHECK(updated.target.tryResult()->value().outcome == StableOutcome::Active && calls == 2);
    auto stopped = instance.stop();
    scheduler->runUntilIdle().value();
    CHECK(stopped.tryResult()->value().quiescent && kernel.snapshot().value().instances.empty());
    auto remounted = kernel.remount(instance).value();
    scheduler->runUntilIdle().value();
    CHECK(calls == 3 && remounted.snapshot().value().state == InstanceState::Active &&
          remounted.snapshot().value().instance != instance.snapshot().value().instance);
    auto requirement = remounted.dependencies().value().front();
    CHECK(requirement.key.realm == realm && requirement.minimumMinor == 1 &&
          requirement.capabilities == std::vector<std::string>({"read"}));
    CHECK(runtime.requirements().empty() && kernel.snapshot().value().runtimes == 1);
    auto closed = kernel.close();
    scheduler->runUntilIdle().value();
    CHECK(closed.tryResult()->value().quiescent);
    provider.retire().value();
    provider.release().value();
    scheduler->close();
}

/** @brief 根据名称运行 W4 收尾契约场景。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "required") requiredOutputs();
        else if (name == "versions") versions();
        else if (name == "realms") realmViews();
        else if (name == "async") asyncCalls();
        else if (name == "async_blocked") asyncBlocked();
        else if (name == "validation") schemaValidation();
        else if (name == "additional")
            additionalDependencies();
        else if (name == "additional_guards")
            additionalGuards();
        else if (name == "additional_remount")
            additionalRemount();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
