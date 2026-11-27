/** @file module_unload.cpp
 * @brief 同一存活进程的物理卸载、系统失败所有权、线程与目录边界。
 */
#include "module_unload_p.hpp"
#include <atomic>
#include <iostream>
namespace unload_test {
struct HookState {
    unsigned releases = 0;
    bool fail = false, failQuery = false, failOther = false;
    HMODULE other = nullptr;
    std::function<void(HMODULE)> before;
    std::exception_ptr failure;
};
thread_local HookState* activeHook = nullptr;
BOOL WINAPI releaseHook(HMODULE image)
{
    auto& state = *activeHook;
    ++state.releases;
    try {
        const auto action = std::move(state.before);
        state.before = {};
        if (action)
            action(image);
    } catch (...) {
        if (!state.failure)
            state.failure = std::current_exception();
    }
    if (std::exchange(state.fail, false) || (state.failOther && image == state.other)) {
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    return FreeLibrary(image);
}
BOOL WINAPI probeHook(DWORD flags, LPCWSTR address, HMODULE* output)
{
    auto& state = *activeHook;
    if (state.other)
        address = reinterpret_cast<LPCWSTR>(state.other);
    return GetModuleHandleExW(flags, address, output);
}
SIZE_T WINAPI queryHook(LPCVOID address, PMEMORY_BASIC_INFORMATION output, SIZE_T size)
{
    if (activeHook->failQuery) {
        SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
    return VirtualQuery(address, output, size);
}
Result<void> injected(GenerationId generation, HookState& hook)
{
    struct Reset {
        HookState* previous;
        ~Reset() { activeHook = previous; }
    } reset{activeHook};
    activeHook = &hook;
    native::NativeUnloadSystem system;
    system.release = releaseHook;
    system.probe = probeHook;
    system.query = queryHook;
    auto result = native::unloadGeneration(generation, system);
    if (hook.failure)
        std::rethrow_exception(hook.failure);
    return result;
}
void physical(Scene& scene, const QString& name)
{
    auto module = scene.load();
    CHECK(scene.call<int(EVE_CALL*)()>(module, "eve_fixture_queries") == 1);
    const auto oldSnapshot = scene.state(module.generation);
    CHECK(scene.manager.tryUnload(module.generation).hasValue());
    scene.gone(module);
    CHECK(!NativeModule::open(scene.manager, module.generation, std::make_shared<qt::QtScheduler>()).hasValue());
    CHECK(!native::acquireNativeSource(module.generation).hasValue());
    if (name == "file") {
        const auto moved = module.library + ".retired";
        const auto from = native::nativeModulePath(module.library), to = native::nativeModulePath(moved);
        CHECK(MoveFileExW(from.c_str(), to.c_str(), 0));
        const auto bytes = files::read(moved);
        CHECK(DeleteFileW(to.c_str()));
        files::write(module.library, bytes);
    }
    if (name != "physical") {
        const auto id =
            scene.manager
                .load(scene.root.toStdString(), module.manifest.toStdString(), {}, scene.approval(module.manifest))
                .value()
                .generation;
        auto again = scene.describe(id, module.manifest);
        CHECK(id != module.generation && oldSnapshot.generation == module.generation);
        CHECK(scene.call<int(EVE_CALL*)()>(again, "eve_fixture_queries") == 1);
        CHECK(scene.manager.tryUnload(id).hasValue());
        scene.gone(again);
    }
}
void rejected(Scene& scene, bool resident)
{
    if (resident) {
        auto module = scene.load(0, "module", true);
        scene.blocked(module);
        CHECK(scene.state(module.generation).nativePinned);
        return;
    }
    const auto manifest = scene.prepare(4);
    CHECK(
        !scene.manager.load(scene.root.toStdString(), manifest.toStdString(), {}, scene.approval(manifest)).hasValue());
    const auto value = scene.manager.snapshot().value().front();
    CHECK(value.nativeMapped && value.state == ModuleState::Failed &&
          !scene.manager.tryUnload(value.generation).hasValue());
    CHECK(std::find(value.resources.pinReasons.begin(), value.resources.pinReasons.end(), "loadRejected") !=
          value.resources.pinReasons.end());
}
void failure(Scene& scene, const QString& name)
{
    auto module = scene.load();
    HookState hook;
    if (name == "release_retry") {
        hook.fail = true;
        CHECK(!injected(module.generation, hook).hasValue());
        const auto value = scene.state(module.generation);
        CHECK(value.nativeMapped && value.state == ModuleState::Resident && value.unloadFailure &&
              native::findGeneration(module.generation)->image == reinterpret_cast<HMODULE>(module.base));
        CHECK(module.witness->read() == 0 && hook.releases == 1 && value.resources.records.empty());
        scene.protectedFile(module);
        CHECK(injected(module.generation, hook).hasValue() && hook.releases == 2);
        scene.gone(module);
    } else if (name == "query_failure") {
        hook.failQuery = true;
        CHECK(!injected(module.generation, hook).hasValue());
        const auto value = scene.state(module.generation);
        CHECK(module.witness->read() == 1 && !value.nativeMapped && value.unloadFailure &&
              value.state != ModuleState::Unloaded);
        CHECK(!native::findGeneration(module.generation)->image && !value.resources.pinReasons.empty());
        scene.protectedFile(module);
        CHECK(!injected(module.generation, hook).hasValue() && hook.releases == 1);
    } else if (name == "external") {
        const auto path = native::nativeModulePath(module.library);
        const auto foreign =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        CHECK(foreign == reinterpret_cast<HMODULE>(module.base));
        CHECK(!injected(module.generation, hook).hasValue());
        const auto value = scene.state(module.generation);
        CHECK(value.nativeMapped && value.unloadFailure && value.state == ModuleState::Resident &&
              module.witness->read() == 0);
        CHECK(std::find(value.resources.pinReasons.begin(), value.resources.pinReasons.end(),
                        "unknownExternalMapping") != value.resources.pinReasons.end());
        CHECK(FreeLibrary(foreign)); // 只归还测试自己取得的外部引用。
        CHECK(!injected(module.generation, hook).hasValue() && hook.releases == 1 && module.witness->read() == 0);
        scene.protectedFile(module);
    } else {
        auto other = scene.load(1, "other");
        hook.other = reinterpret_cast<HMODULE>(other.base);
        hook.failOther = name == "probe_release_failure";
        const auto result = injected(module.generation, hook);
        CHECK(result.hasValue() == !hook.failOther && hook.releases == 2 && module.witness->read() == 1 &&
              other.witness->read() == 0);
        if (hook.failOther) {
            const auto value = scene.state(module.generation);
            CHECK(!value.nativeMapped && value.unloadFailure && !value.resources.pinReasons.empty());
            const auto actual = native::findGeneration(module.generation);
            CHECK(!actual->image && actual->observationImage == hook.other);
            scene.protectedFile(module);
            CHECK(!injected(module.generation, hook).hasValue() && hook.releases == 2);
        } else {
            scene.gone(module);
            CHECK(scene.manager.tryUnload(other.generation).hasValue());
            scene.gone(other);
        }
    }
}
void reentry(Scene& scene, bool seal)
{
    auto module = scene.load();
    HookState hook;
    const auto ledger = native::findGeneration(module.generation)->ledger;
    std::atomic_bool running{true};
    std::atomic<unsigned> acquisitions{0}, refusals{0};
    Event competing;
    std::thread competitor;
    if (seal)
        competitor = std::thread([&] {
            bool announced = false;
            while (running.load()) {
                auto lease = ledger.acquire(ResourceKind::Call, "concurrent acquire and release");
                if (lease.hasValue()) {
                    ++acquisitions;
                    if (!announced) {
                        competing.set();
                        announced = true;
                    }
                    std::this_thread::yield();
                } else
                    ++refusals;
            }
        });
    hook.before = [&](HMODULE) {
        CHECK(scene.state(module.generation).state == ModuleState::Unloading);
        CHECK(!scene.manager.tryUnload(module.generation).hasValue());
        CHECK(!scene.manager.drain(module.generation).hasValue());
        CHECK(!scene.manager
                   .load(scene.root.toStdString(), module.manifest.toStdString(), {}, scene.approval(module.manifest))
                   .hasValue());
        if (seal) {
            CHECK(ledger.snapshot().records.empty() && !ledger.snapshot().accepting);
            bool refused = false;
            std::thread worker([&] { refused = !ledger.acquire(ResourceKind::Call, "late racing call").hasValue(); });
            worker.join();
            CHECK(refused && ledger.snapshot().records.empty() && !ledger.snapshot().accepting);
            QElapsedTimer timer;
            timer.start();
            while (!refusals.load()) {
                CHECK(timer.elapsed() < 8000);
                std::this_thread::yield();
            }
        } else {
            auto other = scene.load(1, "independent");
            CHECK(scene.manager.tryUnload(other.generation).hasValue());
            scene.gone(other);
        }
    };
    bool done = false;
    try {
        if (seal)
            competing.wait();
        QElapsedTimer timer;
        timer.start();
        while (!done) {
            CHECK(timer.elapsed() < 8000);
            done = injected(module.generation, hook).hasValue();
            if (!done) {
                CHECK(seal && module.witness->read() == 0);
                std::this_thread::yield();
            }
        }
    } catch (...) {
        running.store(false);
        if (competitor.joinable())
            competitor.join();
        throw;
    }
    running.store(false);
    if (competitor.joinable())
        competitor.join();
    CHECK(hook.releases == 1 && (!seal || (acquisitions.load() > 0 && refusals.load() > 0)));
    CHECK(ledger.snapshot().records.empty() && !ledger.snapshot().accepting);
    scene.gone(module);
}
void lastLease(Scene& scene)
{
    auto module = scene.load();
    const auto ledger = native::findGeneration(module.generation)->ledger;
    auto source = native::acquireNativeSource(module.generation).value();
    scene.blocked(module);
    std::thread releaser([held = std::move(source)]() mutable { held.session.reset(); });
    releaser.join();
    CHECK(module.witness->read() == 0 && scene.state(module.generation).nativeMapped &&
          ledger.snapshot().records.empty());
    CHECK(scene.manager.tryUnload(module.generation).hasValue());
    scene.gone(module);
}
void owner(Scene& scene, bool exited)
{
    const auto manifest = scene.prepare(0);
    const auto approvals = scene.approval(manifest);
    if (exited) {
        GenerationId id;
        std::exception_ptr error;
        std::thread original([&] {
            try {
                ModuleManager manager;
                id = manager.load(scene.root.toStdString(), manifest.toStdString(), {}, approvals).value().generation;
            } catch (...) {
                error = std::current_exception();
            }
        });
        original.join();
        if (error)
            std::rethrow_exception(error);
        auto artifact = scene.describe(id, manifest);
        const auto generation = native::findGeneration(id);
        const auto originalIdentity = generation->ownerIdentity;
        std::uint64_t replacementIdentity = 0;
        bool refused = false;
        std::thread replacement([&] {
            try {
                ModuleManager manager;
                replacementIdentity = native::moduleThreadIdentity();
                const auto result = manager.tryUnload(id);
                refused = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
            } catch (...) {
                error = std::current_exception();
            }
        });
        replacement.join();
        if (error)
            std::rethrow_exception(error);
        CHECK(refused && replacementIdentity != originalIdentity && generation->ownerIdentity == originalIdentity);
        scene.blocked(artifact);
        return;
    }
    Event loaded, release;
    GenerationId generation;
    bool finished = false;
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            ModuleManager manager;
            generation =
                manager.load(scene.root.toStdString(), manifest.toStdString(), {}, approvals).value().generation;
            loaded.set();
            release.wait();
            finished = manager.tryUnload(generation).hasValue();
        } catch (...) {
            failure = std::current_exception();
            loaded.set();
        }
    });
    std::optional<Artifact> artifact;
    try {
        loaded.wait();
        if (failure)
            std::rethrow_exception(failure);
        artifact = scene.describe(generation, manifest);
        const auto denied = scene.manager.tryUnload(generation);
        CHECK(!denied.hasValue() && denied.error().code == ErrorCode::WrongThread && artifact->witness->read() == 0);
    } catch (...) {
        release.set();
        worker.join();
        throw;
    }
    release.set();
    worker.join();
    if (failure)
        std::rethrow_exception(failure);
    CHECK(finished && artifact);
    scene.gone(*artifact);
}
void budgets(Scene& scene, bool history)
{
    std::vector<GenerationId> failed;
    const unsigned failures = history ? 2U : 63U;
    for (unsigned i = 0; i != failures; ++i) {
        const auto path = scene.prepare(5, "failed-" + QString::number(i), true);
        auto json = QJsonDocument::fromJson(files::read(path)).object();
        json["moduleId"] = "org.eve.unload.failed" + QString::number(i);
        files::write(path, QJsonDocument(json).toJson());
        CHECK(!scene.manager.load(scene.root.toStdString(), path.toStdString()).hasValue());
        const auto value = scene.manager.snapshot().value().back();
        CHECK(value.state == ModuleState::Failed && !value.nativeMapped);
        failed.push_back(value.generation);
    }
    auto module = scene.load(0, "healthy");
    if (!history) {
        const auto extra = scene.prepare(0, "extra");
        auto json = QJsonDocument::fromJson(files::read(extra)).object();
        json["moduleId"] = "org.eve.unload.extra";
        files::write(extra, QJsonDocument(json).toJson());
        const auto denied =
            scene.manager.load(scene.root.toStdString(), extra.toStdString(), {}, scene.approval(extra));
        CHECK(!denied.hasValue() && denied.error().phase == "module.catalog" &&
              scene.manager.snapshot().value().size() == 64);
        CHECK(scene.manager.tryUnload(module.generation).hasValue());
        scene.gone(module);
        const auto id =
            scene.manager
                .load(scene.root.toStdString(), module.manifest.toStdString(), {}, scene.approval(module.manifest))
                .value()
                .generation;
        auto again = scene.describe(id, module.manifest);
        CHECK(id != module.generation && scene.manager.snapshot().value().size() == 65);
        CHECK(scene.manager.tryUnload(id).hasValue());
        scene.gone(again);
    } else {
        std::vector<GenerationId> historyIds;
        const auto retainedValue = scene.state(module.generation);
        for (unsigned i = 0; i != 130; ++i) {
            if (i) {
                const auto id = scene.manager
                                    .load(scene.root.toStdString(), module.manifest.toStdString(), {},
                                          scene.approval(module.manifest))
                                    .value()
                                    .generation;
                module = scene.describe(id, module.manifest);
            }
            CHECK(scene.call<int(EVE_CALL*)()>(module, "eve_fixture_queries") == 1);
            historyIds.push_back(module.generation);
            CHECK(scene.manager.tryUnload(module.generation).hasValue());
            scene.gone(module);
        }
        const auto all = scene.manager.snapshot().value();
        CHECK(all.size() == 130 && retainedValue.generation == historyIds.front() &&
              !retainedValue.manifest.moduleId.empty());
        CHECK(!native::findGeneration(historyIds[0]) && !native::findGeneration(historyIds[1]));
        CHECK(native::findGeneration(historyIds[2]) && native::findGeneration(historyIds.back()));
        CHECK(std::count_if(all.begin(), all.end(),
                            [](const auto& row) { return row.state == ModuleState::Unloaded; }) == 128);
        CHECK(std::adjacent_find(historyIds.begin(), historyIds.end()) == historyIds.end());
    }
    for (auto id : failed) {
        const auto value = scene.state(id);
        CHECK(value.state == ModuleState::Failed && !value.resources.pinReasons.empty());
    }
}
} // namespace unload_test
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        unload_test::Scene scene(args[2], args[3].split(';'));
        const auto name = args[1];
        if (name == "physical" || name == "file" || name == "reload")
            unload_test::physical(scene, name);
        else if (name == "resident" || name == "rejected")
            unload_test::rejected(scene, name == "resident");
        else if (name == "release_retry" || name == "external" || name == "query_failure" || name == "probe_other" ||
                 name == "probe_release_failure")
            unload_test::failure(scene, name);
        else if (name == "reentry" || name == "seal")
            unload_test::reentry(scene, name == "seal");
        else if (name == "owner" || name == "owner_gone")
            unload_test::owner(scene, name == "owner_gone");
        else if (name == "last_lease")
            unload_test::lastLease(scene);
        else if (name == "heavy" || name == "history")
            unload_test::budgets(scene, name == "history");
        else
            CHECK(unload_test::lifetime(scene, name) || unload_test::createReentry(scene, name) ||
                  unload_test::lineage(scene, name) || unload_test::borrowedReader(scene, name));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
