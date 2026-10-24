/** @file diagnostics.cpp
 * @brief 无 Qt 验证宿主名称/实例查询和精确依赖诊断不改变业务账目。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 名称是值快照，实例查询只返回本内核记录并遵循 owner 限制。 */
void lookup()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler), other(scheduler);
    int entered = 0;
    auto runtime = kernel.registerPlugin("notes", [&](Context) { ++entered; return completedTask(); }).value();
    auto instance = kernel.mount(runtime).value();
    auto id = instance.snapshot().value().instance;
    auto snapshot = kernel.snapshot().value();
    CHECK(snapshot.names.size() == 1 && snapshot.names[0].id == runtime.id() && snapshot.names[0].name == "notes" && entered == 0);
    snapshot.names[0].name = "changed";
    CHECK(kernel.snapshot().value().names[0].name == "notes");
    CHECK(kernel.findInstance(id).value().snapshot().value().instance == id);
    CHECK(!kernel.findInstance({}).hasValue() && !other.findInstance(id).hasValue());
    bool rejected = false;
    std::thread worker([&] { rejected = !kernel.findInstance(id).hasValue() && !instance.dependencies().hasValue(); });
    worker.join(); CHECK(rejected && kernel.usesScheduler(scheduler));
    scheduler->runUntilIdle().value(); CHECK(entered == 1);
    auto closed = kernel.close(); scheduler->runUntilIdle().value(); CHECK(closed.tryResult()->value().quiescent);
    CHECK(kernel.snapshot().value().instances.empty() && !kernel.findInstance(id).hasValue());
    CHECK(kernel.snapshot().value().names[0].name == "notes");
    scheduler->close();
}
/** @brief realm/minor/capability 与激活预检一致；读取诊断不新增 committed 租约。 */
void dependencies()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel(scheduler);
    auto registry = kernel.services();
    auto realm = registry.createRealm().value();
    auto view = RealmMap{}.isolate("store", realm).value();
    auto runtime = kernel.registerPlugin("consumer", [](Context) { return completedTask(); },
        {ServiceRequirement::of<int>({"store"}, 2, {"write"})}).value();
    auto instance = kernel.mount(runtime, "", view).value(); scheduler->runUntilIdle().value();
    auto missing = instance.dependencies().value();
    CHECK(missing.size() == 1 && missing[0].key.realm == realm && missing[0].error->code == ErrorCode::DependencyMissing);
    auto global = registry.stage({"store"}, std::make_shared<int>(1), {2, {"write"}}).value(); global.publish().value();
    CHECK(instance.dependencies().value()[0].error->code == ErrorCode::DependencyMissing);
    auto low = registry.stage({"store", 1, realm}, std::make_shared<int>(1), {1, {"write"}}).value(); low.publish().value();
    CHECK(instance.dependencies().value()[0].error->code == ErrorCode::DependencyVersion);
    low.retire().value(); low.release().value();
    auto capability = registry.stage({"store", 1, realm}, std::make_shared<int>(1), {2, {"read"}}).value(); capability.publish().value();
    CHECK(instance.dependencies().value()[0].error->message.find("capability") != std::string::npos);
    capability.retire().value(); capability.release().value();
    auto valid = registry.stage({"store", 1, realm}, std::make_shared<int>(1), {2, {"write"}}).value(); valid.publish().value();
    CHECK(!instance.dependencies().value()[0].error && valid.snapshot().value().committedLeases == 0);
    scheduler->runUntilIdle().value(); CHECK(instance.snapshot().value().state == InstanceState::Active);
    const auto retained = valid.snapshot().value().committedLeases;
    (void)instance.dependencies(); (void)instance.dependencies();
    CHECK(retained == 1 && valid.snapshot().value().committedLeases == retained);
    auto closed = kernel.close(); scheduler->runUntilIdle().value(); CHECK(closed.tryResult()->value().quiescent);
    valid.retire().value(); valid.release().value(); global.retire().value(); global.release().value();
    scheduler->close();
}
/** @brief 每个行为对应独立 CTest，不引入 Qt/Ela 依赖。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "lookup") lookup();
        else if (name == "dependencies") dependencies();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
