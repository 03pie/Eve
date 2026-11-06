/** @file replacement_graph.hpp
 * @brief 两个实际拥有单元的未来服务依赖、重复提供者及跨单元环。
 */
#pragma once
#include "replacement_subtree.hpp"
template <class SceneType, class Wait>
void operationGraphCase(SceneType& scene, const cordis::Runtime& old, const cordis::Runtime& next,
                        const std::shared_ptr<cordis::IScheduler>& scheduler,
                        const std::shared_ptr<cordis::Kernel>& kernel, Wait waitFor, const QString& mode)
{
    using namespace cordis;
    const bool cycle = mode.endsWith("_cycle"), conflict = mode.endsWith("_conflict");
    auto left = std::make_shared<int>(0), right = std::make_shared<int>(0);
    const auto leaf = [&](bool first) {
        const std::string name = first || conflict ? "graph.a" : "graph.b";
        std::vector<ServiceRequirement> required;
        if (first || cycle)
            required.push_back(ServiceRequirement::of<int>({first ? "graph.b" : "graph.a"}));
        return Runtime(
            [first, name, left, right](Context context) {
                if (first)
                    CHECK(*right == 1);
                ++*(first ? left : right);
                context.provide<int>({name}, std::make_shared<int>(7)).value();
                return completedTask();
            },
            std::move(required), {ServiceOutput::of<int>({name})});
    };
    const auto plain = Runtime([](Context) { return completedTask(); });
    std::vector<Instance> originals;
    std::vector<std::shared_ptr<SubtreeReplacementOwner>> owners;
    ReplacementRequest request;
    request.scheduler = scheduler;
    request.kernel = kernel;
    request.modules = scene.managerOwner;
    request.previous = scene.current;
    request.candidate = scene.candidate;
    request.definitions.push_back({old.id(), next});
    for (int i = 0; i != 2; ++i) {
        auto root = kernel->mount(old, scene.qt ? "normal" : "\"sync\"").value();
        CHECK(waitFor(root.ready())->hasValue());
        auto first = root.whenActiveAfter().tryResult()->value().inject(plain, "first").value();
        CHECK(waitFor(first.ready())->hasValue());
        auto second = first.whenActiveAfter().tryResult()->value().inject(plain, "second").value();
        CHECK(waitFor(second.ready())->hasValue());
        auto owner = std::make_shared<SubtreeReplacementOwner>(scheduler, plain);
        owner->candidateFactories.emplace("second", leaf(i == 0));
        request.owners.emplace(root.snapshot().value().instance, owner);
        owners.push_back(owner);
        originals.insert(originals.end(), {root, first, second});
    }
    auto started = ReplacementOperation::begin(std::move(request));
    if (cycle || conflict) {
        CHECK(!started.hasValue());
        CHECK(started.error().code == (cycle ? ErrorCode::DependencyCycle : ErrorCode::ConfigInvalid));
        CHECK(!*left && !*right);
        for (const auto& item : originals)
            CHECK(item.snapshot().value().state == InstanceState::Active);
        for (const auto& owner : owners)
            CHECK(owner->rebuilt == 0);
        CHECK(!scene.state(scene.current).replacement && !scene.state(scene.candidate).replacement);
    } else {
        CHECK(started.hasValue());
        auto result = waitFor(started.value().settled());
        CHECK(result && result->hasValue());
        if (result->value().outcome != ReplacementOutcome::Applied)
            for (const auto& error : result->value().errors)
                std::cerr << error.message << " [" << error.phase << "]\n";
        CHECK(result->value().outcome == ReplacementOutcome::Applied && result->value().instances.size() == 6);
        CHECK(*left == 1 && *right == 1);
        for (const auto& item : originals)
            CHECK(item.snapshot().value().state == InstanceState::Disposed);
        for (const auto& owner : owners)
            for (const auto& item : owner->observed->members)
                CHECK(item.instance.snapshot().value().state == InstanceState::Active);
        CHECK(scene.state(scene.candidate).role == module::GenerationRole::Current);
    }
    CHECK(waitFor(kernel->close())->value().quiescent);
}
