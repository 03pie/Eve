/** @file replacement_peer.hpp
 * @brief 并发替换场景共用真实 peer 模块两代，不复制装载/来源布置。
 */
#pragma once
#include "module_artifacts.hpp"
#include <cordis/module/native.hpp>
struct ReplacementPeer {
    cordis::GenerationId previous, candidate;
    cordis::module::NativeModule previousModule, candidateModule;
    cordis::Runtime previousRuntime, candidateRuntime;
};
template <class SceneType>
ReplacementPeer openReplacementPeer(SceneType& scene, const std::shared_ptr<cordis::IScheduler>& scheduler,
                                    const std::shared_ptr<cordis::Kernel>& kernel)
{
    using namespace cordis;
    using namespace cordis::module;
    const auto take = [](auto result, const char* step) {
        if (!result.hasValue())
            throw std::runtime_error(std::string(step) + ": " + result.error().message);
        return std::move(result).value();
    };
    const auto oldPath = testing::modules::install(scene.dlls[3], scene.root + "/peer-old", "org.cordis.peer",
                                                   {"org.cordis.peer.plugin"}, "c-abi");
    const auto nextPath = testing::modules::install(scene.dlls[4], scene.root + "/peer-next", "org.cordis.peer",
                                                    {"org.cordis.peer.plugin"}, "c-abi", "2.0.0");
    const auto previous =
        take(scene.manager.load(scene.root.toStdString(), oldPath.toStdString()), "load peer").generation;
    const auto candidate =
        take(scene.manager.stage(scene.root.toStdString(), nextPath.toStdString(), previous), "stage peer").generation;
    auto before = take(NativeModule::open(scene.manager, previous, scheduler, kernel->services()), "open old peer");
    auto after = take(NativeModule::open(scene.manager, candidate, scheduler, kernel->services()), "open new peer");
    auto old = take(before.runtime({"org.cordis.peer.plugin", {}, {}}), "old peer factory");
    auto next = take(after.runtime({"org.cordis.peer.plugin", {}, {}}), "new peer factory");
    return {previous, candidate, std::move(before), std::move(after), std::move(old), std::move(next)};
}
