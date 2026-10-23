/** @file service.cpp
 * @brief 验证服务索引的状态、隔离域、调用门禁和资源释放边界。
 */
#include <cordis/service.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

/** @brief Staged 不可见，退役键继续保留，释放后新绑定不复活旧引用。 */
void lifecycle()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    const ServiceKey key{"storage", 1, RealmId{1}};
    CHECK(!registry.stage(key, std::shared_ptr<int>{}).hasValue());
    CHECK(!registry.stage({"", 1, RealmId{1}}, std::make_shared<int>(0)).hasValue());
    CHECK(!registry.stage({"invalid", 0, RealmId{1}}, std::make_shared<int>(0)).hasValue());
    CHECK(!registry.stage({"invalid", 1, RealmId{}}, std::make_shared<int>(0)).hasValue());
    auto object = std::make_shared<int>(7);
    std::weak_ptr<int> weak = object;
    auto provider = registry.stage(key, object).value();
    object.reset();
    CHECK(provider.snapshot().value().state == BindingState::Staged);
    CHECK(registry.find<int>(key).error().code == ErrorCode::DependencyMissing);
    CHECK(registry.stage(key, std::make_shared<int>(8)).error().code == ErrorCode::ServiceConflict);
    CHECK(!provider.release().hasValue());
    CHECK(provider.publish().hasValue() && provider.publish().hasValue());
    auto ref = registry.find<int>(key).value();
    int result = 0;
    CHECK(ref.withService([&](int& value) { result = value; }).hasValue() && result == 7);
    CHECK(registry.find<double>(key).error().code == ErrorCode::DependencyVersion);
    CHECK(provider.retire().hasValue());
    CHECK(ref.withService([](int&) {}).error().code == ErrorCode::ServiceRetiring);
    CHECK(!provider.publish().hasValue());
    CHECK(registry.stage(key, std::make_shared<int>(8)).error().code == ErrorCode::ServiceConflict);
    CHECK(!weak.expired());
    CHECK(provider.release().hasValue() && weak.expired());
    CHECK(provider.release().hasValue() && registry.snapshot().value().empty());
    auto replacement = registry.stage(key, std::make_shared<int>(9)).value();
    CHECK(replacement.snapshot().value().id != provider.snapshot().value().id);
    CHECK(replacement.publish().hasValue());
    CHECK(!ref.withService([](int&) {}).hasValue());
    CHECK(replacement.retire().hasValue() && replacement.release().hasValue());
}

/** @brief 完整键区分 realm 和接口主版本，外部注册表不能使用私有域。 */
void realms()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    ServiceRegistry other(scheduler);
    auto realm = registry.createRealm().value();
    CHECK(realm != RealmId{1} && realm != other.createRealm().value());
    auto root = registry.stage({"same", 1, RealmId{1}}, std::make_shared<int>(1)).value();
    auto isolated = registry.stage({"same", 1, realm}, std::make_shared<int>(2)).value();
    auto v2 = registry.stage({"same", 2, realm}, std::make_shared<int>(3)).value();
    CHECK(!other.stage({"foreign", 1, realm}, std::make_shared<int>(0)).hasValue());
    CHECK(root.publish().hasValue() && isolated.publish().hasValue() && v2.publish().hasValue());
    int value = 0;
    CHECK(registry.find<int>({"same", 1, realm}).value().withService([&](int& service) { value = service; }).hasValue());
    CHECK(value == 2);
    CHECK(registry.find<int>({"same", 3, realm}).error().code == ErrorCode::DependencyMissing);
    CHECK(other.find<int>({"same", 1, RealmId{1}}).error().code == ErrorCode::DependencyMissing);
    for (const auto& provider : {root, isolated, v2}) CHECK(provider.retire().hasValue() && provider.release().hasValue());
}

/** @brief 调用中重入退役立即拒绝新调用，但真实对象保持到调用退出。 */
void callBarrier()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto object = std::make_shared<int>(17);
    std::weak_ptr<int> weak = object;
    auto provider = registry.stage({"call"}, object).value();
    object.reset();
    CHECK(provider.publish().hasValue());
    auto ref = registry.find<int>({"call"}).value();
    CHECK(ref.withService([&](int& value) {
        CHECK(provider.snapshot().value().activeCalls == 1);
        CHECK(provider.retire().hasValue());
        CHECK(provider.release().error().code == ErrorCode::ContextClosing);
        CHECK(!ref.withService([](int&) {}).hasValue());
        CHECK(!weak.expired() && value == 17);
    }).hasValue());
    CHECK(provider.snapshot().value().activeCalls == 0);
    CHECK(provider.release().hasValue() && weak.expired());
    auto throwing = registry.stage({"throw"}, std::make_shared<int>(3)).value();
    CHECK(throwing.publish().hasValue());
    auto throws = registry.find<int>({"throw"}).value();
    CHECK(throws.withService([](int&) { throw std::runtime_error("call failed"); }).error().code == ErrorCode::CallbackFailed);
    CHECK(throwing.snapshot().value().activeCalls == 0);
    CHECK(!throws.withService(std::function<void(int&)>{}).hasValue());
    CHECK(throwing.retire().hasValue() && throwing.release().hasValue());
}

/** @brief 线程/关闭门禁不改账目，关闭后仍允许 owner 同步释放既有资源。 */
void threadsAndClose()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    ServiceRegistry registry(scheduler);
    auto provider = registry.stage({"thread"}, std::make_shared<int>(1)).value();
    CHECK(provider.publish().hasValue());
    auto ref = registry.find<int>({"thread"}).value();
    bool refused = false;
    std::thread worker([&] {
        refused = ref.withService([](int&) {}).error().code == ErrorCode::WrongThread
            && !provider.retire().hasValue() && !provider.release().hasValue()
            && !registry.createRealm().hasValue() && !registry.snapshot().hasValue();
    });
    worker.join();
    CHECK(refused && provider.snapshot().value().state == BindingState::Published);
    scheduler->close();
    CHECK(ref.withService([](int&) {}).error().code == ErrorCode::SchedulerClosed);
    CHECK(!registry.stage({"new"}, std::make_shared<int>(1)).hasValue());
    CHECK(!registry.createRealm().hasValue() && !provider.publish().hasValue());
    CHECK(provider.retire().hasValue() && provider.release().hasValue());
    CHECK(registry.snapshot().value().empty());
}

/** @brief 注册表句柄析构不释放未确认资源，释放对象时允许析构重入注册表。 */
void ownershipAndReentry()
{
    auto scheduler = std::make_shared<FakeScheduler>();
    std::optional<ProviderHandle> provider;
    auto object = std::make_shared<int>(1);
    std::weak_ptr<int> weak = object;
    {
        ServiceRegistry registry(scheduler);
        provider.emplace(registry.stage({"kept"}, object).value());
        object.reset();
    }
    CHECK(!weak.expired());
    CHECK(provider->retire().hasValue() && provider->release().hasValue() && weak.expired());
    ServiceRegistry registry(scheduler);
    /** @brief 在对象析构中验证旧键已移除，索引可以安全接纳同键新对象。 */
    struct Resource {
        std::function<void()> onDestroy;
        /** @brief 执行不抛异常的析构检查。 */
        ~Resource() { onDestroy(); }
    };
    bool empty = false;
    std::optional<ProviderHandle> replacement;
    auto resource = std::make_shared<Resource>();
    resource->onDestroy = [&] {
        empty = registry.snapshot().value().empty();
        auto next = registry.stage({"reenter"}, std::make_shared<int>(2));
        if (next.hasValue()) replacement.emplace(next.value());
    };
    auto original = registry.stage({"reenter"}, resource).value();
    resource.reset();
    CHECK(original.retire().hasValue() && original.release().hasValue());
    CHECK(empty && replacement.has_value());
    CHECK(replacement->retire().hasValue() && replacement->release().hasValue());
}

/** @brief 按命令行名称运行服务底座测试。 */
int main(int argc, char** argv)
{
    if (argc != 2) return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "lifecycle") lifecycle();
        else if (name == "realms") realms();
        else if (name == "call_barrier") callBarrier();
        else if (name == "threads_close") threadsAndClose();
        else if (name == "ownership_reentry") ownershipAndReentry();
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
