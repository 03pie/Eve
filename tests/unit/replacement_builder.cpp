/** @file replacement_builder.cpp
 * @brief 当次发布组权限、空组丢弃、线程和寿命边界。
 */
#include "../../src/core/replacement_inspect_p.hpp"
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
using namespace cordis;
using cordis::detail::CoreReplacementAccess;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

struct Scene {
    std::shared_ptr<testing::FakeScheduler> queue = std::make_shared<testing::FakeScheduler>();
    Kernel kernel{queue};
    Runtime old = kernel.registerPlugin("old", [](Context) { return completedTask(); }).value();
    Runtime next{[](Context) { return completedTask(); }};
    Scene()
    {
        kernel.mount(old).value();
        run();
    }
    void run() { queue->runUntilIdle(10000).value(); }
    ReplacementScope stopped()
    {
        auto scope = kernel.reserveReplacement({old.id()}).value();
        scope.prepareDefinitions({{old.id(), next}}).value();
        auto stopped = scope.stop();
        run();
        CHECK(stopped.tryResult()->value().quiescent);
        return scope;
    }
    void discard(ReplacementScope& scope)
    {
        auto task = scope.discard();
        run();
        CHECK(task.tryResult()->value().quiescent);
    }
    void close()
    {
        auto task = kernel.close();
        run();
        CHECK(task.tryResult()->value().quiescent);
        queue->close();
    }
};
void releaseOrder()
{
    Scene s;
    auto scope = s.stopped();
    auto candidate = CoreReplacementAccess::builder(scope, s.kernel).value().stage(s.next).value();
    s.run();
    CHECK(candidate.ready().tryResult()->hasValue());
    bool unlocked = false, notifiedAfterUnlock = false;
    auto unlock = [&]() noexcept { unlocked = true; };
    CHECK(!CoreReplacementAccess::release(scope, unlock).hasValue() && !unlocked);
    CHECK(candidate.snapshot().value().state == InstanceState::Active);
    s.discard(scope);
    bool wrongThread = false;
    std::thread worker([&] {
        auto result = CoreReplacementAccess::release(scope, unlock);
        wrongThread = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrongThread && !unlocked);
    auto released = s.kernel.whenReplacementReleased(scope.snapshot().value().operation);
    cordis::detail::TaskAccess::observe(
        released, [&](Task<void>::Snapshot result) { notifiedAfterUnlock = result->hasValue() && unlocked; });
    CoreReplacementAccess::release(scope, unlock).value();
    CHECK(unlocked && notifiedAfterUnlock);
    unlocked = false;
    CoreReplacementAccess::release(scope, unlock).value();
    CHECK(unlocked);
    s.close();
}
void outputPlan()
{
    Scene s;
    Kernel foreign(s.queue);
    const auto producer = [](const char* name) {
        const auto key = ServiceKey{name};
        return Runtime(
            [key](Context context) {
                context.provide<int>(key, std::make_shared<int>(7)).value();
                return completedTask();
            },
            {}, {ServiceOutput::of<int>(key)});
    };
    auto foo = producer("output.foo"), bar = producer("output.bar");
    auto scope = s.kernel.reserveReplacement({s.old.id()}).value();
    scope.prepareDefinitions({{s.old.id(), foo}}).value();
    CHECK(!CoreReplacementAccess::sealPlan(scope, foreign, {{"output.foo"}}).hasValue());
    bool wrong = false;
    std::thread worker([&] {
        auto result = CoreReplacementAccess::sealPlan(scope, s.kernel, {{"output.foo"}});
        wrong = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    CoreReplacementAccess::sealPlan(scope, s.kernel, {{"output.foo"}}).value();
    CHECK(!CoreReplacementAccess::sealPlan(scope, s.kernel, {{"output.bar"}}).hasValue());
    auto rejected = s.kernel.services().stage<int>({"output.foo"}, std::make_shared<int>(1));
    CHECK(!rejected.hasValue() && rejected.error().operation == scope.snapshot().value().operation);
    auto otherRuntime = s.kernel.registerPlugin("output.other", [](Context) { return completedTask(); }).value();
    auto other = s.kernel.reserveReplacement({otherRuntime.id()}).value();
    other.prepareDefinitions({{otherRuntime.id(), bar}}).value();
    auto conflict = CoreReplacementAccess::sealPlan(other, s.kernel, {{"output.bar"}, {"output.foo"}});
    CHECK(!conflict.hasValue() && conflict.error().operation == scope.snapshot().value().operation);
    auto untouched = s.kernel.services().stage<int>({"output.bar"}, std::make_shared<int>(1)).value();
    untouched.retire().value();
    untouched.release().value();
    auto stopped = scope.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    auto extra = s.kernel.registerRuntime("output.extra", producer("output.extra")).value();
    auto outside = scope.stage(extra);
    CHECK(!outside.hasValue() && outside.error().code == ErrorCode::ConfigConflict &&
          outside.error().message == "output is outside the sealed plan");
    auto created = scope.stage(foo).value();
    s.run();
    CHECK(created.ready().tryResult()->hasValue());
    s.discard(scope);
    scope.release().value();
    auto restored = s.kernel.services().stage<int>({"output.foo"}, std::make_shared<int>(1)).value();
    restored.retire().value();
    restored.release().value();
    auto oldStopped = other.stop();
    s.run();
    CHECK(oldStopped.tryResult()->value().quiescent);
    // 封存失败不改变低层动态模式，也不留下半组键占用。
    auto legacy = other.stage(bar).value();
    s.run();
    CHECK(legacy.ready().tryResult()->hasValue());
    s.discard(other);
    other.release().value();
    auto closed = foreign.close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent);
    s.close();
}
void inputPlan()
{
    Scene s;
    auto service = s.kernel.services().stage<int>({"read.service"}, std::make_shared<int>(1)).value();
    auto otherService = s.kernel.services().stage<int>({"read.other"}, std::make_shared<int>(2)).value();
    service.publish().value();
    otherService.publish().value();
    auto first = s.kernel.reserveReplacement({s.old.id()}).value();
    const auto peer = s.kernel.registerPlugin("read.peer", [](Context) { return completedTask(); }).value();
    auto second = s.kernel.reserveReplacement({peer.id()}).value();
    const CoreReplacementAccess::InputBindings wrong{{{"read.service"}, otherService.snapshot().value().id}};
    CHECK(!CoreReplacementAccess::sealPlan(first, s.kernel, {{"read.reserved"}}, wrong).hasValue());
    auto untouched = s.kernel.services().stage<int>({"read.reserved"}, std::make_shared<int>(3)).value();
    untouched.retire().value();
    untouched.release().value();
    const CoreReplacementAccess::InputBindings input{{{"read.service"}, service.snapshot().value().id}};
    CoreReplacementAccess::sealPlan(first, s.kernel, {}, input).value();
    CoreReplacementAccess::sealPlan(second, s.kernel, {}, {}, input).value();
    bool wrongThread = false;
    std::thread worker([&] {
        auto inspected = CoreReplacementAccess::planningContext(first, s.kernel);
        wrongThread = !inspected.hasValue() && inspected.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrongThread);
    CHECK(service.snapshot().value().committedLeases == 0); // 事务读者不是伪造的实例租约。
    auto denied = service.retire();
    CHECK(!denied.hasValue() && denied.error().operation == first.snapshot().value().operation);
    CHECK(s.kernel.services().find<int>({"read.service"}).hasValue());
    first.release().value();
    const auto sharedDenied = service.retire();
    CHECK(!sharedDenied.hasValue() && sharedDenied.error().operation == second.snapshot().value().operation);
    second.release().value();
    service.retire().value();
    service.release().value();
    otherService.retire().value();
    otherService.release().value();

    std::optional<BindingId> childOutput;
    const auto parentRuntime = s.kernel.registerPlugin("read.parent", [](Context) { return completedTask(); }).value();
    const auto sourceRuntime =
        s.kernel
            .registerPlugin("read.source",
                            [&](Context context) {
                                childOutput = context.provide<int>({"read.child"}, std::make_shared<int>(4)).value();
                                return completedTask();
                            },
                            {}, {ServiceOutput::of<int>({"read.child"})})
            .value();
    auto parent = s.kernel.mount(parentRuntime).value();
    s.run();
    auto child = parent.whenActiveAfter().tryResult()->value().inject(sourceRuntime).value();
    s.run();
    CHECK(child.ready().tryResult()->hasValue() && childOutput);
    auto scope = s.kernel.reserveReplacement({s.old.id()}).value();
    CoreReplacementAccess::sealPlan(scope, s.kernel, {}, {{{"read.child"}, *childOutput}}).value();
    auto stopped = parent.stop().tryResult();
    CHECK(stopped && !stopped->hasValue() && stopped->error().operation == scope.snapshot().value().operation);
    auto updated = parent.update("changed", parent.snapshot().value().revision);
    CHECK(!updated.hasValue() && updated.error().operation == scope.snapshot().value().operation);
    auto replacement = s.kernel.reserveReplacement({sourceRuntime.id()});
    CHECK(!replacement.hasValue() && replacement.error().operation == scope.snapshot().value().operation);
    const auto lost = CoreReplacementAccess::whenInputsLost(scope, s.kernel, false);
    s.close(); // Kernel 内部清理必须绕过主动变更门禁，不等待事务租约。
    CHECK(lost.tryResult() && !lost.tryResult()->hasValue());
    CHECK(child.snapshot().value().state == InstanceState::Disposed);
}
void phases()
{
    Scene s;
    auto scope = s.stopped();
    auto candidate = CoreReplacementAccess::builder(scope, s.kernel).value();
    bool wrong = false;
    std::thread worker([&] { wrong = candidate.stage(s.next).error().code == ErrorCode::WrongThread; });
    worker.join();
    CHECK(wrong);
    auto next = candidate.stage(s.next).value();
    s.run();
    CHECK(next.ready().tryResult()->hasValue());
    s.discard(scope);
    CHECK(!candidate.stage(s.old).hasValue());
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Stopped);
    auto recovery = CoreReplacementAccess::builder(scope, s.kernel).value();
    CHECK(!candidate.stage(s.old).hasValue());
    auto old = recovery.stage(s.old).value();
    s.run();
    CHECK(old.ready().tryResult()->hasValue());
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    CHECK(!recovery.stage(s.old).hasValue());
    scope.commitPrepared().value();
    scope.release().value();
    CHECK(!candidate.stage(s.old).hasValue() && !recovery.stage(s.old).hasValue());
    s.close();
}
void empty()
{
    Scene s;
    auto scope = s.stopped();
    Kernel foreign(s.queue);
    CHECK(!CoreReplacementAccess::builder(scope, foreign).hasValue());
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Stopped);
    auto first = CoreReplacementAccess::builder(scope, s.kernel).value();
    CHECK(scope.snapshot().value().state == ReplacementScopeState::Staging);
    s.discard(scope);
    auto second = CoreReplacementAccess::builder(scope, s.kernel).value();
    CHECK(!first.stage(s.next).hasValue());
    s.discard(scope);
    scope.release().value();
    CHECK(!second.stage(s.next).hasValue());
    auto closed = foreign.close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent);
    s.close();
}
void lifetime()
{
    const auto queue = std::make_shared<testing::FakeScheduler>();
    std::optional<ReplacementBuilder> retained;
    std::weak_ptr<int> weak;
    {
        Kernel kernel(queue);
        const auto token = std::make_shared<int>(7);
        weak = token;
        auto old = kernel.registerPlugin("old", [token](Context) { return completedTask(); }).value();
        Runtime next([](Context) { return completedTask(); });
        kernel.mount(old).value();
        queue->runUntilIdle().value();
        auto scope = kernel.reserveReplacement({old.id()}).value();
        scope.prepareDefinitions({{old.id(), next}}).value();
        auto stopped = scope.stop();
        queue->runUntilIdle().value();
        CHECK(stopped.tryResult()->value().quiescent);
        retained = CoreReplacementAccess::builder(scope, kernel).value();
        auto discarded = scope.discard();
        queue->runUntilIdle().value();
        CHECK(discarded.tryResult()->value().quiescent);
        scope.release().value();
        auto closed = kernel.close();
        queue->runUntilIdle().value();
        CHECK(closed.tryResult()->value().quiescent);
    }
    queue->runUntilIdle().value();
    CHECK(weak.expired());
    Runtime unused([](Context) { return completedTask(); });
    CHECK(!retained->stage(unused).hasValue());
    queue->close();
}
void readers()
{
    Scene s;
    const auto peer = s.kernel.registerPlugin("peer", [](Context) { return completedTask(); }).value();
    const auto shared = s.kernel.registerPlugin("shared", [](Context) { return completedTask(); }).value();
    const auto root = s.kernel.findInstance(s.kernel.snapshot().value().instances.front().instance).value();
    auto child = root.whenActiveAfter().tryResult()->value().inject(shared).value();
    s.run();
    const Runtime peerNext([](Context) { return completedTask(); });
    auto first = s.kernel.reserveReplacement({s.old.id()}).value();
    first.prepareDefinitions({{s.old.id(), s.next}}).value();
    auto second = s.kernel.reserveReplacement({peer.id()}).value();
    second.prepareDefinitions({{peer.id(), peerNext}}).value();
    Kernel foreign(s.queue);
    const auto legacyFactory = s.kernel.registerPlugin("legacy", [](Context) { return completedTask(); }).value();
    const Runtime legacyNext([](Context) { return completedTask(); });
    auto legacy = s.kernel.reserveReplacement({legacyFactory.id()}).value();
    legacy.prepareDefinitions({{legacyFactory.id(), legacyNext}}).value();
    auto emptyStopped = legacy.stop();
    s.run();
    CHECK(emptyStopped.tryResult()->value().quiescent);
    auto denied = legacy.stage(shared);
    CHECK(!denied.hasValue() && denied.error().operation == first.snapshot().value().operation);
    s.discard(legacy);
    legacy.release().value();
    CHECK(!CoreReplacementAccess::borrow(first, foreign, {shared}).hasValue());
    Runtime unregistered([](Context) { return completedTask(); });
    CHECK(!CoreReplacementAccess::borrow(first, s.kernel, {unregistered}).hasValue());
    bool wrong = false;
    std::thread worker([&] {
        wrong = CoreReplacementAccess::borrow(first, s.kernel, {shared}).error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(wrong);
    CoreReplacementAccess::borrow(first, s.kernel, {shared, shared, s.old, s.next}).value();
    CoreReplacementAccess::borrow(second, s.kernel, {shared}).value();
    CHECK(!s.kernel.reserveReplacement({shared.id()}).hasValue());
    first.release().value();
    CHECK(!s.kernel.reserveReplacement({shared.id()}).hasValue());
    second.release().value();
    auto childStopped = child.stop();
    s.run();
    CHECK(childStopped.tryResult()->value().quiescent);
    auto exclusive = s.kernel.reserveReplacement({shared.id()}).value();
    const Runtime sharedNext([](Context) { return completedTask(); });
    exclusive.prepareDefinitions({{shared.id(), sharedNext}}).value();
    auto third = s.kernel.reserveReplacement({s.old.id()}).value();
    third.prepareDefinitions({{s.old.id(), s.next}}).value();
    CHECK(!CoreReplacementAccess::borrow(third, s.kernel, {shared}).hasValue());
    exclusive.release().value();
    CoreReplacementAccess::borrow(third, s.kernel, {shared}).value();
    auto stopped = third.stop();
    s.run();
    CHECK(stopped.tryResult()->value().quiescent);
    CHECK(!CoreReplacementAccess::borrow(third, s.kernel, {shared}).hasValue());
    third.release().value();
    CHECK(!CoreReplacementAccess::borrow(third, s.kernel, {shared}).hasValue());
    auto closed = foreign.close();
    s.run();
    CHECK(closed.tryResult()->value().quiescent);
    s.close();
}
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "phases")
            phases();
        else if (name == "release_order")
            releaseOrder();
        else if (name == "output_plan")
            outputPlan();
        else if (name == "input_plan")
            inputPlan();
        else if (name == "readers")
            readers();
        else if (name == "empty")
            empty();
        else if (name == "lifetime")
            lifetime();
        else
            throw std::runtime_error("unknown case");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
