/** @file replacement_outputs.hpp
 * @brief 真实两模块未来键冲突及未公开/退役占用必须在停止旧树前拒绝。
 */
#pragma once
#include "replacement_peer.hpp"
#include "replacement_subtree.hpp"
template <class SceneType, class Wait>
void operationOutputsCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                          const std::shared_ptr<cordis::IScheduler>& scheduler,
                          const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    auto peer = openReplacementPeer(scene, scheduler, kernel);
    kernel->registerRuntime("outputs.peer", peer.previousRuntime).value();
    const auto plain = Runtime([](Context) { return completedTask(); });
    auto created = std::make_shared<int>(0);
    const auto output = Runtime(
        [created](Context context) {
            ++*created;
            context.provide<int>({"future.output"}, std::make_shared<int>(7)).value();
            return completedTask();
        },
        {}, {ServiceOutput::of<int>({"future.output"})});
    std::vector<Instance> roots;
    std::vector<std::shared_ptr<SubtreeReplacementOwner>> owners;
    for (const auto& factory : {old, peer.previousRuntime}) {
        auto root = kernel->mount(factory, factory.id() == old.id() && scene.qt ? "normal" : "\"sync\"").value();
        CHECK(waitFor(root.ready())->hasValue());
        auto first = root.whenActiveAfter().tryResult()->value().inject(plain, "first").value();
        CHECK(waitFor(first.ready())->hasValue());
        auto second = first.whenActiveAfter().tryResult()->value().inject(plain, "second").value();
        CHECK(waitFor(second.ready())->hasValue());
        auto owner = std::make_shared<SubtreeReplacementOwner>(scheduler, plain);
        owner->candidateFactories.emplace("second", output);
        owners.push_back(owner);
        roots.push_back(root);
    }
    const auto request = [&](bool first) {
        ReplacementRequest value;
        value.scheduler = scheduler;
        value.kernel = kernel;
        value.modules = scene.managerOwner;
        value.previous = first ? scene.current : peer.previous;
        value.candidate = first ? scene.candidate : peer.candidate;
        value.definitions.push_back(
            {first ? old.id() : peer.previousRuntime.id(), first ? next : peer.candidateRuntime});
        value.owners.emplace(roots[first ? 0 : 1].snapshot().value().instance, owners[first ? 0 : 1]);
        return value;
    };
    std::optional<PublicationGroup> group;
    std::optional<Instance> held;
    std::optional<ProviderHandle> occupied;
    if (mode.endsWith("_held")) {
        auto factory = kernel->registerRuntime("outputs.held", output).value();
        group = kernel->holdPublication().value();
        held = kernel->mountHeld(*group, factory).value();
        CHECK(waitFor(held->ready())->hasValue());
    } else if (mode.endsWith("_staged") || mode.endsWith("_retiring")) {
        occupied = kernel->services().stage<int>({"future.output"}, std::make_shared<int>(1)).value();
        if (mode.endsWith("_retiring")) {
            occupied->publish().value();
            occupied->retire().value();
        }
    }
    auto first = ReplacementOperation::begin(request(true));
    if (group || occupied) {
        CHECK(!first.hasValue() && first.error().code == ErrorCode::ServiceConflict);
        CHECK(owners[0]->rebuilt == 0);
        CHECK(!scene.state(scene.current).replacement);
    } else {
        CHECK(first.hasValue());
        auto second = ReplacementOperation::begin(request(false));
        CHECK(!second.hasValue() && second.error().code == ErrorCode::ConfigConflict);
        CHECK(second.error().operation == first.value().snapshot().value().report.operation);
        CHECK(owners[1]->rebuilt == 0 && !scene.state(peer.previous).replacement);
        CHECK(roots[0].snapshot().value().state == InstanceState::Active &&
              roots[1].snapshot().value().state == InstanceState::Active);
        first.value().cancel().value();
        CHECK(waitFor(first.value().settled())->value().outcome == ReplacementOutcome::Cancelled);
        auto retry = ReplacementOperation::begin(request(false));
        CHECK(retry.hasValue());
        auto result = waitFor(retry.value().settled());
        CHECK(result && result->hasValue() && result->value().outcome == ReplacementOutcome::Applied);
        CHECK(scene.state(peer.candidate).role == module::GenerationRole::Current && *created == 1);
    }
    CHECK(roots[0].snapshot().value().state == InstanceState::Active);
    if (group) {
        group->abort().value();
        CHECK(waitFor(held->stop())->value().quiescent);
    }
    if (occupied) {
        occupied->retire().value();
        CHECK(waitFor(occupied->whenUnused())->hasValue());
        occupied->release().value();
    }
    CHECK(waitFor(kernel->close())->value().quiescent);
    CHECK(waitFor(peer.previousModule.close())->value().quiescent &&
          waitFor(peer.candidateModule.close())->value().quiescent);
}
