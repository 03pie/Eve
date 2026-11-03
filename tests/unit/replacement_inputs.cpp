/** @file replacement_inputs.cpp
 * @brief 重建输入来自真实实例及原始登记定义，不延长临时子工厂的资源寿命。
 */
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel{scheduler};
    void run() { scheduler->runUntilIdle(30000).value(); }
    void close()
    {
        auto task = kernel.close();
        run();
        CHECK(task.tryResult()->value().quiescent);
    }
};
const ReplacementInput& find(const std::vector<ReplacementInput>& inputs, InstanceId id)
{
    auto at = std::find_if(inputs.begin(), inputs.end(), [id](const auto& input) { return input.instance == id; });
    CHECK(at != inputs.end());
    return *at;
}
void captured()
{
    Scene s;
    const auto realm = s.kernel.services().createRealm().value();
    const auto realms = RealmMap{}.isolate("value", realm).value();
    auto provider = s.kernel
                        .registerPlugin("provider",
                                        [](Context context) {
                                            context.provide({"value"}, std::make_shared<int>(7)).value();
                                            return completedTask();
                                        },
                                        {}, {ServiceOutput::of<int>({"value"}, true, {3, {"base", "extra"}})})
                        .value();
    auto consumer = s.kernel
                        .registerPlugin("manual", [](Context) { return completedTask(); },
                                        {ServiceRequirement::of<int>({"value"}, 1, {"base"})})
                        .value();
    auto p = s.kernel.mount(provider, "provider-input", realms).value();
    auto c = s.kernel.mount(consumer, "first", realms, {ServiceRequirement::of<int>({"value"}, 3, {"extra"})}).value();
    s.run();
    auto changed = c.update("accepted", 1).value();
    s.run();
    CHECK(changed.target.tryResult()->value().outcome == StableOutcome::Active);
    const auto before = c.snapshot().value();
    auto scope = s.kernel.reserveReplacement({provider.id()}).value();
    auto inputs = scope.inputs().value();
    CHECK(inputs.size() == 2);
    const auto& input = find(inputs, before.instance);
    CHECK(!input.parent.valid() && input.runtime == consumer.id() && input.factory &&
          input.factory->id() == consumer.id() && input.config == "accepted" && input.epoch == before.epoch &&
          input.revision == before.revision && input.realms.overrides() == realms.overrides());
    CHECK(input.factory->requirements().size() == 1 && input.factory->requirements()[0].minimumMinor == 1 &&
          input.factory->requirements()[0].capabilities == std::vector<std::string>{"base"});
    CHECK(input.dependencies.size() == 1 && input.dependencies[0].key.realm == realm &&
          input.dependencies[0].minimumMinor == 3 && input.dependencies[0].capabilities.size() == 2);
    const auto providerInput = find(inputs, p.snapshot().value().instance);
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent && scope.inputs().value().size() == 2);
    CHECK(input.additionalDependencies.size() == 1 && input.additionalDependencies[0].minimumMinor == 3);
    auto restored = scope.stage(*input.factory, input.config, input.realms, input.additionalDependencies).value();
    auto replacement = scope
                           .stage(*providerInput.factory, providerInput.config, providerInput.realms,
                                  providerInput.additionalDependencies)
                           .value();
    s.run();
    CHECK(restored.ready().tryResult()->hasValue() && replacement.ready().tryResult()->hasValue());
    CHECK(restored.snapshot().value().instance != before.instance);
    CHECK(restored.dependencies().value()[0].minimumMinor == 3);
    inputs[0].config = "local-only";
    CHECK(scope.inputs().value()[0].config != "local-only");
    auto discard = scope.discard();
    s.run();
    CHECK(discard.tryResult()->value().quiescent);
    scope.release().value();
    CHECK(!scope.inputs().hasValue());
    s.close();
}
void contracts()
{
    Scene s;
    auto provider = s.kernel
                        .registerPlugin("provider",
                                        [](Context context) {
                                            context.provide({"value"}, std::make_shared<int>(7)).value();
                                            return completedTask();
                                        },
                                        {}, {ServiceOutput::of<int>({"value"}, true, {3, {"base", "extra"}})})
                        .value();
    auto original = s.kernel
                        .registerPlugin("consumer", [](Context) { return completedTask(); },
                                        {ServiceRequirement::of<int>({"value"}, 3, {"base"})})
                        .value();
    s.kernel.mount(provider).value();
    auto old = s.kernel.mount(original, "input", {}, {ServiceRequirement::of<int>({"value"}, 1, {"extra"})}).value();
    s.run();
    auto stop = old.stop();
    s.run();
    CHECK(stop.tryResult()->value().quiescent);
    auto remounted = s.kernel.remount(old).value();
    s.run();
    auto scope = s.kernel.reserveReplacement({original.id()}).value();
    const auto inputs = scope.inputs().value();
    CHECK(inputs.size() == 1);
    const auto& input = inputs[0];
    CHECK(input.instance == remounted.snapshot().value().instance && input.dependencies[0].minimumMinor == 3);
    CHECK(input.additionalDependencies.size() == 1 && input.additionalDependencies[0].minimumMinor == 1 &&
          input.additionalDependencies[0].capabilities == std::vector<std::string>{"extra"});
    Runtime candidate([](Context) { return completedTask(); }, {ServiceRequirement::of<int>({"value"})});
    scope.prepareDefinitions({{original.id(), candidate}}).value();
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto next = scope.stage(candidate, input.config, input.realms, input.additionalDependencies).value();
    s.run();
    CHECK(next.ready().tryResult()->hasValue());
    const auto effective = next.dependencies().value();
    CHECK(effective.size() == 1 && effective[0].minimumMinor == 1 &&
          effective[0].capabilities == std::vector<std::string>{"extra"});
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    scope.release().value();
    s.close();
}
void children()
{
    Scene s;
    std::weak_ptr<int> capturedResource;
    std::optional<Instance> dynamic, registered;
    bool parentFinalized = false;
    auto childDefinition = s.kernel.registerPlugin("registered-child", [](Context) { return completedTask(); }).value();
    auto parent = s.kernel
                      .registerPlugin("parent",
                                      [&](Context context) {
                                          auto resource = std::make_shared<int>(9);
                                          capturedResource = resource;
                                          Runtime child([resource](Context) {
                                              CHECK(*resource == 9);
                                              return completedTask();
                                          });
                                          dynamic.emplace(context.inject(child, "dynamic").value());
                                          registered.emplace(context.inject(childDefinition, "registered").value());
                                          context
                                              .afterCleanup([&] {
                                                  CHECK(capturedResource.expired());
                                                  parentFinalized = true;
                                                  return completedTask();
                                              })
                                              .value();
                                          return completedTask();
                                      })
                      .value();
    auto root = s.kernel.mount(parent, "root").value();
    s.run();
    const auto parentId = root.snapshot().value().instance;
    auto scope = s.kernel.reserveReplacement({parent.id()}).value();
    const auto inputs = scope.inputs().value();
    CHECK(inputs.size() == 3 && !capturedResource.expired());
    const auto& automatic = find(inputs, dynamic->snapshot().value().instance);
    CHECK(automatic.parent == parentId && automatic.runtime == dynamic->snapshot().value().runtime &&
          automatic.config == "dynamic" && !automatic.factory);
    const auto& explicitChild = find(inputs, registered->snapshot().value().instance);
    CHECK(explicitChild.parent == parentId && explicitChild.factory &&
          explicitChild.factory->id() == childDefinition.id());
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent && parentFinalized && capturedResource.expired());
    CHECK(scope.inputs().value().size() == 3);
    scope.release().value();
    s.close();
}
void guards()
{
    Scene s;
    auto runtime = s.kernel.registerPlugin("root", [](Context) { return completedTask(); }).value();
    s.kernel.mount(runtime).value();
    s.run();
    ReplacementScope empty;
    CHECK(empty.inputs().error().code == ErrorCode::InvalidArgument);
    auto scope = s.kernel.reserveReplacement({runtime.id()}).value();
    std::optional<ErrorCode> error;
    std::thread other([&] { error = scope.inputs().error().code; });
    other.join();
    CHECK(error == ErrorCode::WrongThread);
    auto moved = std::move(scope);
    CHECK(scope.inputs().error().code == ErrorCode::InvalidArgument);
    s.close();
    CHECK(moved.inputs().error().code == ErrorCode::ContextClosing);
}
void consumerIntent()
{
    Scene s;
    auto provider = s.kernel
                        .registerPlugin("provider",
                                        [](Context context) {
                                            context.provide({"value"}, std::make_shared<int>(7)).value();
                                            return completedTask();
                                        },
                                        {}, {ServiceOutput::of<int>({"value"})})
                        .value();
    auto consumer = s.kernel
                        .registerPlugin("consumer", [](Context) { return completedTask(); },
                                        {ServiceRequirement::of<int>({"value"})})
                        .value();
    s.kernel.mount(provider).value();
    s.kernel.mount(consumer).value();
    s.run();
    auto scope = s.kernel.reserveReplacement({provider.id()}).value();
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    // 旧消费者已从服务图消失，但本事务仍要用它的原始定义重建。
    auto conflicting = s.kernel.reserveReplacement({consumer.id()});
    CHECK(!conflicting.hasValue() && conflicting.error().code == ErrorCode::ConfigConflict);
    scope.release().value();
    auto allowed = s.kernel.reserveReplacement({consumer.id()}).value();
    allowed.release().value();
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "captured")
            captured();
        else if (name == "contracts")
            contracts();
        else if (name == "children")
            children();
        else if (name == "guards")
            guards();
        else if (name == "consumer_intent")
            consumerIntent();
        else
            throw std::runtime_error("unknown replacement input case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
