/** @file module_unload_replacement_p.hpp
 * @brief 真实有状态两代、宿主会话与短探针；注册、挂载、关闭和回收由场景明确持有。
 */
#pragma once
#include "module_unload_p.hpp"
#include <array>
#include <cordis/replacement_operation.hpp>

namespace unload_test {
template <class T> T requireValue(Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
struct ReplacementSubject {
    Scene& scene;
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    Artifact previousImage, nextImage;
    std::array<std::optional<NativeModule>, 2> modules;
    std::optional<Runtime> previous, next;

    explicit ReplacementSubject(Scene& source)
        : scene(source), previousImage(scene.load(3, "previous")), nextImage(stageNext(scene, previousImage))
    {
        for (unsigned i = 0; i != 2; ++i)
            modules[i].emplace(
                requireValue(NativeModule::open(scene.manager, image(i).generation, scheduler, kernel->services())));
        previous.emplace(requireValue(host(0).runtime({"org.cordis.plugin", {}, {}})));
        next.emplace(requireValue(host(1).runtime({"org.cordis.plugin", {}, {}})));
        CHECK(previous->stateSchema() && previous->stateSchema()->version == 2);
        CHECK(next->stateSchema() && next->stateSchema()->version == 3);
    }
    const Artifact& image(unsigned index) const
    {
        CHECK(index < 2);
        return index ? nextImage : previousImage;
    }
    NativeModule& host(unsigned index) { return modules.at(index).value(); }
    ReplacementRequest request() const
    {
        ReplacementRequest result;
        result.scheduler = scheduler;
        result.kernel = kernel;
        result.modules = scene.managerOwner;
        result.previous = previousImage.generation;
        result.candidate = nextImage.generation;
        result.pluginRoot = scene.root.toStdString();
        result.definitions.push_back({previous->id(), *next});
        return result;
    }
    void barrier() const
    {
        Promise<void> reached;
        CHECK(scheduler->post(
            {[reached] { reached.resolve(); },
             [reached] { reached.reject(makeError(ErrorCode::SchedulerClosed, "owner barrier discarded")); }}));
        CHECK(wait(reached.task())->hasValue());
    }
    void mode(unsigned index, std::uint32_t value) const
    {
        scene.call<void(CORDIS_CALL*)(std::uint32_t)>(image(index), "cordis_state_set_mode", value);
    }
    unsigned count(unsigned index, std::uint32_t phase) const
    {
        return scene.call<std::uint32_t(CORDIS_CALL*)(std::uint32_t)>(image(index), "cordis_state_count", phase);
    }
    unsigned live(unsigned index) const
    {
        return scene.call<std::uint32_t(CORDIS_CALL*)()>(image(index), "cordis_state_live");
    }
    void setValue(unsigned index, std::uint32_t slot, const std::string& json) const
    {
        const CordisBytesV1 input{reinterpret_cast<const std::uint8_t*>(json.data()), json.size()};
        CHECK(scene.call<CordisStatus(CORDIS_CALL*)(std::uint32_t, CordisBytesV1)>(
                  image(index), "cordis_state_set_value", slot, input) == CORDIS_OK);
    }
    std::string value(unsigned index, std::uint32_t slot) const
    {
        std::array<std::uint8_t, 128> storage{};
        CordisStateBufferV1 output{sizeof(output), 0, storage.data(), storage.size(), 0};
        CHECK(scene.call<CordisStatus(CORDIS_CALL*)(std::uint32_t, CordisStateBufferV1*)>(
                  image(index), "cordis_state_value", slot, &output) == CORDIS_OK);
        CHECK(output.size <= storage.size());
        return {reinterpret_cast<const char*>(storage.data()), static_cast<std::size_t>(output.size)};
    }

private:
    static Artifact stageNext(Scene& scene, const Artifact& previous)
    {
        const auto path = scene.prepare(7, "candidate");
        const auto loaded = requireValue(scene.manager.stage(scene.root.toStdString(), path.toStdString(),
                                                             previous.generation, {}, scene.approval(path)));
        return scene.describe(loaded.generation, path);
    }
};
bool replacementCases(Scene&, const QString&);
bool configuredReplacementCases(Scene&, const QString&);
bool concurrentOwners(Scene&, const QString&);
} // namespace unload_test
