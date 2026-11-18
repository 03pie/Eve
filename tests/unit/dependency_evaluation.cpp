/** @file dependency_evaluation.cpp
 * @brief 从真实公开激活路径验证依赖评估批次，不将通知、epoch 或成功激活当作计数。
 */
#include <cordis/kernel.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace cordis;
using cordis::testing::FakeScheduler;
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value))                                                                                                  \
            throw std::runtime_error(std::string(#value) + " at " + std::to_string(__LINE__));                         \
    } while (false)

namespace {
struct Scene {
    std::shared_ptr<FakeScheduler> scheduler = std::make_shared<FakeScheduler>();
    Kernel kernel{scheduler};
    ServiceRegistry registry = kernel.services();

    std::uint64_t count() const { return registry.dependencyEvaluationCount().value(); }
    void run() { scheduler->runUntilIdle().value(); }
    void close()
    {
        const auto closed = kernel.close();
        run();
        CHECK(closed.tryResult() && closed.tryResult()->value().quiescent);
        CHECK(kernel.snapshot().value().instances.empty());
        CHECK(registry.dependencyGraph().value().instances.empty());
    }
};

void accounting()
{
    Scene scene;
    auto copy = scene.registry;
    CHECK(scene.count() == 0 && copy.dependencyEvaluationCount().value() == 0);
    int activations = 0;
    const auto runtime = scene.kernel
                             .registerPlugin("plain",
                                             [&](Context) {
                                                 ++activations;
                                                 return completedTask();
                                             })
                             .value();
    const auto instance = scene.kernel.mount(runtime).value();
    CHECK(scene.count() == 0 && activations == 0);
    scene.run();
    CHECK(instance.ready().tryResult()->hasValue() && activations == 1 && scene.count() == 1);
    CHECK(copy.dependencyEvaluationCount().value() == 1);
    for (unsigned i = 0; i != 8; ++i) {
        CHECK(scene.registry.snapshot().value().empty());
        CHECK(scene.registry.dependencyGraph().value().instances.size() == 1);
        CHECK(scene.registry.ownsRealm(RealmId{1}).value());
        CHECK(scene.registry.find<int>({"missing"}).error().code == ErrorCode::DependencyMissing);
        CHECK(scene.kernel.snapshot().value().instances.size() == 1);
        CHECK(scene.count() == 1);
    }
    CHECK(scene.scheduler->pending() == 0);
    scene.close();
    CHECK(scene.count() == 1 && copy.dependencyEvaluationCount().value() == 1);
}

void pendingCoalescing()
{
    Scene scene;
    int activations = 0;
    const auto runtime =
        scene.kernel
            .registerPlugin(
                "waiting",
                [&](Context context) {
                    context.consume<int>({"needed"}).value().withService([](int& value) { CHECK(value == 9); }).value();
                    ++activations;
                    return completedTask();
                },
                {ServiceRequirement::of<int>({"needed"})})
            .value();
    const auto instance = scene.kernel.mount(runtime).value();
    scene.run();
    CHECK(scene.count() == 1 && activations == 0 && instance.snapshot().value().epoch == 0 &&
          instance.snapshot().value().state == InstanceState::Pending);
    const auto noise = scene.registry.stage({"unrelated"}, std::make_shared<int>(1)).value();
    for (unsigned i = 0; i != 32; ++i)
        noise.publish().value();
    CHECK(scene.scheduler->pending() >= 32 && scene.count() == 1);
    scene.run();
    // 同批通知先排入 FIFO，Instance::enqueue 合并到一个真实 Pending 评估。
    CHECK(scene.count() == 2 && activations == 0 && instance.snapshot().value().epoch == 0 &&
          instance.snapshot().value().state == InstanceState::Pending);
    const auto provider = scene.registry.stage({"needed"}, std::make_shared<int>(9)).value();
    provider.publish().value();
    CHECK(scene.count() == 2);
    scene.run();
    CHECK(scene.count() == 3 && activations == 1 && instance.snapshot().value().epoch == 1 &&
          instance.snapshot().value().state == InstanceState::Active);
    for (unsigned i = 0; i != 32; ++i)
        noise.publish().value();
    CHECK(scene.scheduler->pending() >= 32);
    scene.run();
    CHECK(scene.count() == 3 && activations == 1 && instance.snapshot().value().epoch == 1);
    scene.close();
    for (const auto& binding : {noise, provider}) {
        binding.retire().value();
        binding.release().value();
    }
    scene.run();
    CHECK(scene.registry.snapshot().value().empty() && scene.count() == 3);
}

void versionFailures()
{
    Scene scene;
    const auto provider = scene.registry.stage({"versioned"}, std::make_shared<int>(7), {1, {"read"}}).value();
    provider.publish().value();
    int activations = 0;
    const auto tooNew = scene.kernel
                            .registerPlugin("too-new",
                                            [&](Context) {
                                                ++activations;
                                                return completedTask();
                                            },
                                            {ServiceRequirement::of<int>({"versioned"}, 2)})
                            .value();
    const auto newer = scene.kernel.mount(tooNew).value();
    scene.run();
    CHECK(scene.count() == 1 && newer.ready().tryResult()->error().code == ErrorCode::DependencyVersion &&
          newer.snapshot().value().epoch == 0 && activations == 0);
    const auto missingCapability = scene.kernel
                                       .registerPlugin("missing-capability",
                                                       [&](Context) {
                                                           ++activations;
                                                           return completedTask();
                                                       },
                                                       {ServiceRequirement::of<int>({"versioned"}, 1, {"write"})})
                                       .value();
    const auto absent = scene.kernel.mount(missingCapability).value();
    scene.run();
    CHECK(scene.count() == 2 && absent.ready().tryResult()->error().code == ErrorCode::DependencyVersion &&
          absent.snapshot().value().epoch == 0 && activations == 0);
    CHECK(scene.registry.find<int>({"versioned"}, 2).error().code == ErrorCode::DependencyVersion);
    CHECK(scene.registry.find<int>({"versioned"}, 1, {"write"}).error().code == ErrorCode::DependencyVersion);
    CHECK(scene.count() == 2);
    scene.close();
    provider.retire().value();
    provider.release().value();
    scene.run();
    CHECK(scene.count() == 2 && scene.registry.snapshot().value().empty());
}

void cycleFailure()
{
    Scene scene;
    const auto group = scene.kernel.holdPublication().value();
    int activations = 0;
    const auto a = scene.kernel
                       .registerPlugin("a",
                                       [&](Context) {
                                           ++activations;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"b"})}, {ServiceOutput::of<int>({"a"})})
                       .value();
    const auto b = scene.kernel
                       .registerPlugin("b",
                                       [&](Context) {
                                           ++activations;
                                           return completedTask();
                                       },
                                       {ServiceRequirement::of<int>({"a"})}, {ServiceOutput::of<int>({"b"})})
                       .value();
    const auto first = scene.kernel.mountHeld(group, a).value();
    const auto second = scene.kernel.mountHeld(group, b).value();
    CHECK(scene.count() == 0);
    scene.run();
    CHECK(scene.count() == 2 && activations == 0 &&
          first.ready().tryResult()->error().code == ErrorCode::DependencyCycle &&
          second.ready().tryResult()->error().code == ErrorCode::DependencyCycle);
    CHECK(!group.commit().hasValue() && scene.count() == 2);
    group.abort().value();
    scene.close();
    CHECK(scene.count() == 2 && scene.registry.snapshot().value().empty());
}

void heldViews()
{
    Scene scene;
    const auto group = scene.kernel.holdPublication().value();
    const auto provider = scene.kernel
                              .registerPlugin("held-provider",
                                              [](Context context) {
                                                  context.provide({"held"}, std::make_shared<int>(11)).value();
                                                  return completedTask();
                                              },
                                              {}, {ServiceOutput::of<int>({"held"})})
                              .value();
    const auto provided = scene.kernel.mountHeld(group, provider).value();
    scene.run();
    CHECK(provided.ready().tryResult()->hasValue() && scene.count() == 1);
    int insideActivations = 0, outsideActivations = 0;
    const auto inside =
        scene.kernel
            .registerPlugin(
                "inside",
                [&](Context context) {
                    context.consume<int>({"held"}).value().withService([](int& value) { CHECK(value == 11); }).value();
                    ++insideActivations;
                    return completedTask();
                },
                {ServiceRequirement::of<int>({"held"})})
            .value();
    const auto internal = scene.kernel.mountHeld(group, inside).value();
    scene.run();
    CHECK(internal.ready().tryResult()->hasValue() && insideActivations == 1 && scene.count() == 2);
    CHECK(group.snapshot().value().heldBindings == 1 &&
          scene.registry.find<int>({"held"}).error().code == ErrorCode::DependencyMissing);
    const auto outside = scene.kernel
                             .registerPlugin("outside",
                                             [&](Context) {
                                                 ++outsideActivations;
                                                 return completedTask();
                                             },
                                             {ServiceRequirement::of<int>({"held"})})
                             .value();
    const auto external = scene.kernel.mount(outside).value();
    scene.run();
    CHECK(scene.count() == 3 && external.snapshot().value().state == InstanceState::Pending && outsideActivations == 0);
    group.commit().value();
    CHECK(scene.count() == 3);
    scene.run();
    CHECK(scene.count() == 4 && external.ready().tryResult()->hasValue() && outsideActivations == 1 &&
          insideActivations == 1);
    // 同一 scheduler 上的另一个真实 Kernel 拥有独立索引，不共享这个累计值。
    Kernel independent(scene.scheduler);
    const auto plain = independent.registerPlugin("separate", [](Context) { return completedTask(); }).value();
    const auto separate = independent.mount(plain).value();
    CHECK(independent.services().dependencyEvaluationCount().value() == 0);
    scene.run();
    CHECK(separate.ready().tryResult()->hasValue() && independent.services().dependencyEvaluationCount().value() == 1 &&
          scene.count() == 4);
    const auto closed = independent.close();
    scene.run();
    CHECK(closed.tryResult()->value().quiescent && scene.count() == 4);
    scene.close();
    CHECK(scene.registry.snapshot().value().empty());
}

void guards()
{
    Scene scene;
    const auto copy = scene.registry;
    bool rejected = false;
    std::thread worker([&] {
        const auto first = scene.registry.dependencyEvaluationCount();
        const auto second = copy.dependencyEvaluationCount();
        rejected = !first.hasValue() && first.error().code == ErrorCode::WrongThread && !second.hasValue() &&
                   second.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(rejected && scene.count() == 0);
    int activations = 0;
    const auto runtime = scene.kernel
                             .registerPlugin("guarded",
                                             [&](Context) {
                                                 ++activations;
                                                 return completedTask();
                                             })
                             .value();
    const auto stoppedEarly = scene.kernel.mount(runtime).value();
    const auto stopped = stoppedEarly.stop();
    scene.run();
    CHECK(stopped.tryResult()->value().quiescent && scene.count() == 0 && activations == 0);
    const auto active = scene.kernel.mount(runtime).value();
    scene.run();
    CHECK(active.ready().tryResult()->hasValue() && scene.count() == 1 && activations == 1);
    scene.close();
    CHECK(scene.count() == 1);
    scene.scheduler->close();
    CHECK(!scene.scheduler->isOpen() && scene.count() == 1 && copy.dependencyEvaluationCount().value() == 1);
    CHECK(!scene.registry.stage({"closed"}, std::make_shared<int>(1)).hasValue() && scene.count() == 1);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
        return EXIT_FAILURE;
    try {
        const std::string name = argv[1];
        if (name == "accounting")
            accounting();
        else if (name == "pending_coalescing")
            pendingCoalescing();
        else if (name == "version")
            versionFailures();
        else if (name == "cycle")
            cycleFailure();
        else if (name == "held")
            heldViews();
        else if (name == "guards")
            guards();
        else
            return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
