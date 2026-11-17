/** @file state_admission.cpp
 * @brief 在真实 Scope/父子实例上验证按 key 导入、绕过拒绝与阶段回执。
 */
#include "../../src/core/instance_state_p.hpp"
#include "../../src/core/replacement_inspect_p.hpp"
#include <cordis/testing/fake_scheduler.hpp>
#include <iostream>
#include <thread>
using namespace cordis;
using namespace cordis::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
bool digits(std::string_view text) noexcept
{
    return !text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos;
}
struct Data {
    std::string value = "0", observed;
};
using Values = std::vector<std::shared_ptr<Data>>;
Runtime counter(Values& values, std::uint32_t version, bool plain = false, std::optional<Runtime> child = {},
                bool bad = false)
{
    Runtime::Activate activate = [&values, plain, child](Context context) {
        auto data = std::make_shared<Data>();
        values.push_back(data);
        if (!plain) {
            auto bound = context.state({[data] { return Result<std::string>::success(data->value); },
                                        [data](std::string_view json) {
                                            data->value = json;
                                            return Result<void>::success();
                                        }});
            if (!bound.hasValue())
                return failedTask(bound.error());
        }
        data->observed = data->value;
        if (child) {
            auto created = context.inject(*child);
            return created.hasValue() ? created.value().ready() : failedTask(created.error());
        }
        return completedTask();
    };
    if (plain)
        return Runtime(std::move(activate));
    return Runtime::stateful(std::move(activate), {{"counter", version}, [bad](const StateSnapshot& source) {
                                                       return Result<std::string>::success(bad ? "bad" : source.json);
                                                   }});
}
struct Scene {
    std::shared_ptr<testing::FakeScheduler> scheduler = std::make_shared<testing::FakeScheduler>();
    Kernel kernel{scheduler};
    Values roots, children;
    std::shared_ptr<StateBudget> budget;
    std::shared_ptr<const OwnedStateSnapshot> rootState, childState;
    Runtime child, next, old;
    const bool hasChild;
    Scene(bool withChild = false, bool plain = false, bool bad = false, std::size_t limit = maxReplacementStateBytes)
        : budget(StateBudget::create(limit)), child(counter(children, 1, plain)),
          next(counter(roots, 2, plain, {}, bad)),
          old(kernel
                  .registerRuntime("old",
                                   counter(roots, 1, plain, withChild ? std::optional<Runtime>(child) : std::nullopt))
                  .value()),
          hasChild(withChild)
    {
        auto mounted = kernel.mount(old).value();
        run();
        CHECK(mounted.ready().tryResult()->hasValue());
        roots.front()->value = "31";
        if (hasChild)
            children.front()->value = "71";
    }
    void run() { scheduler->runUntilIdle(30000).value(); }
    ReplacementScope stopped()
    {
        auto scope = kernel.reserveReplacement({old.id()}).value();
        scope.prepareDefinitions({{old.id(), next}}).value();
        auto original = CoreReplacementAccess::original(scope, kernel).value();
        scope.freeze().value();
        for (const auto& member : original.members) {
            auto captured = InstanceStateAccess::capture(scope, member.instance).value();
            if (!captured)
                continue;
            auto owned = ownStateSnapshot(std::move(*captured), budget, digits).value();
            if (member.input.parent.valid())
                childState = std::move(owned);
            else
                rootState = std::move(owned);
        }
        auto stopped = scope.stop();
        run();
        CHECK(stopped.tryResult()->value().quiescent);
        return scope;
    }
    std::vector<StateConstruction> members() const
    {
        std::vector<StateConstruction> result{{10, {}, rootState}};
        if (hasChild)
            result.push_back({20, 10, childState});
        return result;
    }
    void install(ReplacementScope& scope, bool required = true)
    {
        CoreReplacementAccess::statePlan(scope, kernel, members(), budget, required).value();
    }
    Instance root(const ReplacementBuilder& builder)
    {
        auto instance = builder.stageMember(10, next).value();
        CHECK(!InstanceStateAccess::receipt(instance).hasValue());
        run();
        CHECK(instance.ready().tryResult()->hasValue());
        return instance;
    }
    Context context(const Instance& instance)
    {
        auto ready = instance.whenActiveAfter().tryResult();
        CHECK(ready && ready->hasValue());
        return ready->value();
    }
    void discard(ReplacementScope& scope)
    {
        auto discarded = scope.discard();
        run();
        CHECK(discarded.tryResult()->value().quiescent);
    }
    void finish(ReplacementScope& scope, bool committed = false)
    {
        if (!committed)
            discard(scope);
        scope.release().value();
        rootState.reset();
        childState.reset();
        CHECK(budget->used() == 0);
        auto closed = kernel.close();
        run();
        CHECK(closed.tryResult()->value().quiescent);
        scheduler->close();
    }
};
void roundtrip()
{
    Scene s(true);
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope);
    auto root = s.root(builder);
    auto child = builder.injectMember(20, s.context(root), s.child).value();
    s.run();
    CHECK(child.ready().tryResult()->hasValue());
    CHECK(s.roots.back()->observed == "31" && s.children.back()->observed == "71");
    const auto proof = InstanceStateAccess::receipt(root).value();
    CHECK(proof.key == 10 && proof.epoch == 1 && proof.operation == scope.snapshot().value().operation &&
          InstanceStateAccess::receipt(child).value().key == 20);
    s.rootState.reset();
    s.childState.reset();
    CHECK(s.budget->used() == 4);
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    scope.release().value();
    CHECK(s.budget->used() == 0);
    auto ordinary = s.context(root).inject(s.child).value();
    s.run();
    CHECK(ordinary.ready().tryResult()->hasValue() && s.children.back()->observed == "0");
    s.finish(scope, true);
    CHECK(!InstanceStateAccess::receipt(root).hasValue() && proof.key == 10);
}
void guards(const std::string& name)
{
    Scene s(name == "unkeyed_child" || name == "omitted" || name == "wrong_parent");
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    if (name == "wrong_parent") {
        auto members = s.members();
        members.push_back({11, {}, {}});
        CoreReplacementAccess::statePlan(scope, s.kernel, std::move(members), s.budget, true).value();
    } else
        s.install(scope);
    CHECK(!builder.stageMember(999, s.next).hasValue());
    if (name == "unkeyed_root") {
        CHECK(!builder.stage(s.next).hasValue() && s.roots.size() == 1);
    } else if (name == "wrong_thread") {
        bool rejected = false;
        std::thread worker([&] {
            auto result = builder.stageMember(10, s.next);
            rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
        });
        worker.join();
        CHECK(rejected && s.roots.size() == 1);
    } else {
        auto root = s.root(builder);
        if (name == "duplicate")
            CHECK(!builder.stageMember(10, s.next).hasValue() && s.roots.size() == 2);
        else if (name == "unkeyed_child")
            CHECK(!s.context(root).inject(s.child).hasValue() && s.children.size() == 1);
        else if (name == "omitted")
            CHECK(scope.prepareCommit().error().code == ErrorCode::ProvisionIncomplete);
        else if (name == "wrong_parent") {
            auto extra = builder.stageMember(11, s.next).value();
            s.run();
            CHECK(extra.ready().tryResult()->hasValue());
            CHECK(!builder.injectMember(20, s.context(extra), s.child).hasValue() && s.children.size() == 1);
            auto child = builder.injectMember(20, s.context(root), s.child).value();
            s.run();
            CHECK(child.ready().tryResult()->hasValue() && s.children.back()->observed == "71");
            scope.prepareCommit().value();
        }
    }
    s.finish(scope);
}
void plans()
{
    Scene s;
    auto scope = s.stopped();
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, s.members(), s.budget, true).hasValue());
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, {{10, {}, s.rootState}, {10, {}, {}}}, s.budget, true)
               .hasValue());
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, {{10, 99, s.rootState}}, s.budget, true).hasValue());
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, {{10, 11, s.rootState}, {11, 10, {}}}, s.budget, true)
               .hasValue());
    auto otherBudget = StateBudget::create();
    auto foreign = ownStateSnapshot({{"counter", 1}, "31"}, otherBudget).value();
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, {{10, {}, foreign}}, s.budget, true).hasValue());
    s.install(scope);
    CHECK(!CoreReplacementAccess::statePlan(scope, s.kernel, s.members(), s.budget, true).hasValue());
    s.root(builder);
    s.finish(scope);
}
void recovery()
{
    Scene s;
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope);
    auto candidate = s.root(builder);
    s.discard(scope);
    CHECK(!builder.stageMember(10, s.next).hasValue() && !InstanceStateAccess::receipt(candidate).hasValue());
    auto recovery = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope);
    CHECK(!builder.stageMember(10, s.next).hasValue());
    s.roots.front()->value = "999"; // 保存的是独立原值，而非旧业务存储的视图。
    auto restored = recovery.stageMember(10, s.old).value();
    s.run();
    CHECK(restored.ready().tryResult()->hasValue() && s.roots.back()->observed == "31");
    scope.prepareCommit(ReplacementCommitMode::Recovery).value();
    scope.commitPrepared().value();
    s.finish(scope, true);
}
void coldEpoch()
{
    Scene s;
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope);
    // 未封存的低层 Scope 允许外部依赖换代；不伪造实例 epoch，也不绕过 update 门禁。
    auto host = s.kernel.services().stage({"host"}, std::make_shared<int>(1)).value();
    host.publish().value();
    auto candidate = builder.stageMember(10, s.next, {}, {}, {ServiceRequirement::of<int>({"host"})}).value();
    s.run();
    CHECK(candidate.ready().tryResult()->hasValue() && s.roots.back()->observed == "31");
    const auto imported = InstanceStateAccess::receipt(candidate).value();
    CHECK(imported.key == 10 && imported.epoch == 1);
    auto reactivated = candidate.whenActiveAfter(imported.epoch);
    host.retire().value();
    s.run();
    CHECK(host.whenUnused().tryResult()->hasValue() && !reactivated.isReady());
    host.release().value();
    auto replacement = s.kernel.services().stage({"host"}, std::make_shared<int>(2)).value();
    replacement.publish().value();
    s.run();
    CHECK(reactivated.tryResult() && reactivated.tryResult()->hasValue());
    CHECK(candidate.snapshot().value().epoch == 2 && s.roots.size() == 3 && s.roots.back()->observed == "0");
    const auto receipt = InstanceStateAccess::receipt(candidate);
    const auto prepared = scope.prepareCommit();
    CHECK(!receipt.hasValue() && receipt.error().code == ErrorCode::ConfigConflict);
    CHECK(!prepared.hasValue() && prepared.error().code == ErrorCode::ConfigConflict);
    s.discard(scope);
    replacement.retire().value();
    replacement.release().value();
    s.finish(scope, true);
}
void rejected(bool budget)
{
    Scene s(false, false, !budget, budget ? 3 : maxReplacementStateBytes);
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope);
    auto candidate = builder.stageMember(10, s.next).value();
    s.run();
    CHECK(!candidate.ready().tryResult()->hasValue() && s.roots.size() == 1);
    CHECK(!InstanceStateAccess::receipt(candidate).hasValue() && s.budget->used() == 2);
    CHECK(!scope.prepareCommit().hasValue());
    s.finish(scope);
}
void legacy()
{
    Scene s(false, true);
    auto scope = s.stopped();
    auto builder = CoreReplacementAccess::builder(scope, s.kernel).value();
    s.install(scope, false);
    auto candidate = builder.stage(s.next).value();
    s.run();
    CHECK(candidate.ready().tryResult()->hasValue());
    scope.prepareCommit().value();
    scope.commitPrepared().value();
    s.finish(scope, true);
}
} // namespace
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "roundtrip")
            roundtrip();
        else if (name == "plans")
            plans();
        else if (name == "recovery")
            recovery();
        else if (name == "cold_epoch")
            coldEpoch();
        else if (name == "budget" || name == "format")
            rejected(name == "budget");
        else if (name == "legacy")
            legacy();
        else if (name == "unkeyed_root" || name == "unkeyed_child" || name == "omitted" || name == "wrong_parent" ||
                 name == "duplicate" || name == "wrong_thread")
            guards(name);
        else
            CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
