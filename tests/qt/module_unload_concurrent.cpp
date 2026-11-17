/** @file module_unload_concurrent.cpp
 * @brief 两个首次 owner 的独立卸载；平台锁不能跨越系统释放边界。
 */
#include "module_unload_p.hpp"
#include <array>
#include <mutex>

namespace unload_test {
namespace {
struct OwnerRun {
    Event loaded, start, finished;
    GenerationId generation;
    std::mutex mutex;
    std::exception_ptr failure;
    bool unloaded = false;

    void reject(std::exception_ptr error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        failure = std::move(error);
    }
    void check()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (failure)
            std::rethrow_exception(failure);
    }
};
struct ReleasePause {
    Event entered, resume;
    HMODULE image = nullptr;
    std::exception_ptr failure;
};
thread_local ReleasePause* activePause = nullptr;

BOOL WINAPI pauseRelease(HMODULE image)
{
    auto* pause = activePause;
    if (pause && pause->image == image) {
        try {
            pause->entered.set();
            pause->resume.wait();
        } catch (...) {
            pause->failure = std::current_exception();
        }
    }
    return FreeLibrary(image);
}
/** @brief 断言、装载或线程创建失败时也放行所有等待者，先 join 再释放栈和事件。 */
struct JoinOwners {
    std::array<OwnerRun, 2>& owners;
    ReleasePause& pause;
    std::array<std::thread, 2>& threads;
    ~JoinOwners()
    {
        for (auto& owner : owners)
            SetEvent(owner.start.handle);
        SetEvent(pause.resume.handle);
        for (auto& thread : threads)
            if (thread.joinable())
                thread.join();
    }
};
} // namespace

bool concurrentOwners(Scene& scene, const QString& name)
{
    if (name != "parallel_owners")
        return false;
    const std::array<QString, 2> manifests{{scene.prepare(0, "first"), scene.prepare(1, "second")}};
    const std::array<NativeUnloadWhitelist, 2> approvals{{scene.approval(manifests[0]), scene.approval(manifests[1])}};
    std::array<OwnerRun, 2> owners;
    std::array<std::optional<Artifact>, 2> artifacts;
    ReleasePause pause;
    std::array<std::thread, 2> threads;
    JoinOwners joined{owners, pause, threads};
    const auto run = [&](std::size_t index) {
        auto& owner = owners[index];
        try {
            // 复用同一 Scene 类型和进程目录；每个 facade 真正绑定本线程。
            Scene local(scene.root, scene.fixtures);
            const auto loaded =
                local.manager.load(scene.root.toStdString(), manifests[index].toStdString(), {}, approvals[index]);
            if (!loaded.hasValue())
                throw std::runtime_error(loaded.error().phase + ": " + loaded.error().message);
            owner.generation = loaded.value().generation;
            owner.loaded.set();
            owner.start.wait();
            CHECK(artifacts[index] && local.manager.snapshot().hasValue());
            const auto generation = native::findGeneration(owner.generation);
            CHECK(generation && generation->ownerThread == std::this_thread::get_id() &&
                  generation->ownerIdentity == native::moduleThreadIdentity());
            if (index == 0) {
                struct Reset {
                    ReleasePause* previous;
                    ~Reset() { activePause = previous; }
                } reset{std::exchange(activePause, &pause)};
                native::NativeUnloadSystem system;
                system.release = pauseRelease;
                const auto result = native::unloadGeneration(owner.generation, system);
                if (pause.failure)
                    std::rethrow_exception(pause.failure);
                CHECK(result.hasValue());
            } else
                CHECK(local.manager.tryUnload(owner.generation).hasValue());
            local.gone(*artifacts[index]);
            owner.unloaded = true;
        } catch (...) {
            owner.reject(std::current_exception());
        }
        SetEvent(owner.loaded.handle);
        SetEvent(owner.finished.handle);
    };
    threads[0] = std::thread(run, 0);
    threads[1] = std::thread(run, 1);
    for (auto& owner : owners) {
        owner.loaded.wait();
        owner.check();
    }
    CHECK(owners[0].generation.valid() && owners[1].generation.valid() && owners[0].generation != owners[1].generation);
    // describe 和进程寿命 witness 槽只在主线程构造；工作线程尚未开始终结。
    for (std::size_t index = 0; index != artifacts.size(); ++index)
        artifacts[index].emplace(scene.describe(owners[index].generation, manifests[index]));
    CHECK(artifacts[0]->base != artifacts[1]->base);
    pause.image = reinterpret_cast<HMODULE>(artifacts[0]->base);
    owners[0].start.set();
    pause.entered.wait();
    CHECK(scene.state(owners[0].generation).state == ModuleState::Unloading && artifacts[0]->witness->read() == 0);
    owners[1].start.set();
    owners[1].finished.wait();
    owners[1].check();
    CHECK(owners[1].unloaded);
    scene.gone(*artifacts[1]);
    CHECK(scene.state(owners[0].generation).state == ModuleState::Unloading && artifacts[0]->witness->read() == 0);
    // B 已完成真实 detach、范围检查和独占写入，A 才获准进入 FreeLibrary。
    pause.resume.set();
    owners[0].finished.wait();
    owners[0].check();
    for (auto& thread : threads)
        thread.join();
    CHECK(owners[0].unloaded && owners[1].unloaded);
    scene.gone(*artifacts[0]);
    return true;
}
} // namespace unload_test
