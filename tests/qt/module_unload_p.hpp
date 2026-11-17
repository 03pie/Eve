/** @file module_unload_p.hpp
 * @brief 卸载测试共享实际装载、短作用域探针和纯值见证，不拥有另一套业务账目。
 */
#pragma once
#include "../../src/module/catalog_p.hpp"
#include "../../src/module/native_source_p.hpp"
#include "../../src/module/unload_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <cordis/kernel.hpp>
#include <cordis/module/native.hpp>
#include <cordis/module/runtime.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace unload_test {
using namespace cordis;
using namespace cordis::module;
namespace native = cordis::module::detail;
namespace files = cordis::testing::modules;
inline void events()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        CHECK(timer.elapsed() < 8000);
        events();
        QThread::msleep(1);
    }
    return task.tryResult();
}
template <class F> F entry(HMODULE image, const char* symbol)
{
    const auto address = GetProcAddress(image, symbol);
    CHECK(address);
    F result;
    static_assert(sizeof(result) == sizeof(address), "Windows entry");
    std::memcpy(&result, &address, sizeof(result));
    return result;
}
struct Witness {
    volatile LONG detaches = 0;
    LONG read() { return InterlockedCompareExchange(&detaches, 0, 0); }
};
inline std::shared_ptr<Witness> witnessSlot()
{
    // 仅测试的宿主值槽保留到进程退出；失败映像可能一直驻留，槽不借用测试栈。
    static auto* slots = new std::vector<std::shared_ptr<Witness>>;
    auto slot = std::make_shared<Witness>();
    slots->push_back(slot);
    return slot;
}
struct Artifact {
    QString manifest, library;
    GenerationId generation;
    std::uintptr_t base = 0;
    std::size_t bytes = 0;
    std::shared_ptr<Witness> witness = witnessSlot();
};
struct Scene {
    ModuleManager manager;
    QString root;
    QStringList fixtures;
    Scene(QString path, QStringList inputs) : root(std::move(path)), fixtures(std::move(inputs))
    {
        CHECK(fixtures.size() == 7);
    }
    QString prepare(unsigned fixture, const QString& folder = "module", bool resident = false) const
    {
        static const char* ids[] = {"org.cordis.business", "org.cordis.lifecycle", "org.cordis.services",
                                    "org.cordis.state",    "org.cordis.policy",    "org.cordis.dependent",
                                    "org.cordis.business"};
        const auto manifest =
            files::install(fixtures[static_cast<int>(fixture)], root + '/' + folder, ids[fixture],
                           QJsonArray{"org.cordis.plugin"}, "c-abi", fixture == 6 ? "2.0.0" : "1.0.0");
        auto json = QJsonDocument::fromJson(files::read(manifest)).object();
        json["unloadPolicy"] = resident ? "resident" : "explicit";
        files::write(manifest, QJsonDocument(json).toJson());
        return manifest;
    }
    static QString library(const QString& manifest) { return QFileInfo(manifest).absolutePath() + "/module.dll"; }
    NativeUnloadWhitelist approval(const QString& manifest) const
    {
        const auto checked = preflightModule(root.toStdString(), manifest.toStdString()).value();
        return {{checked.manifest().moduleId, checked.manifest().sha256}};
    }
    ModuleGenerationSnapshot state(GenerationId generation) const
    {
        const auto all = manager.snapshot().value();
        const auto at =
            std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.generation == generation; });
        CHECK(at != all.end());
        return *at;
    }
    template <class F, class... Args> auto call(const Artifact& value, const char* symbol, Args... args) const
    {
        const auto generation = native::findGeneration(value.generation);
        CHECK(generation);
        auto held = generation->ledger.acquire(ResourceKind::Call, "test.unload.probe").value();
        return entry<F>(reinterpret_cast<HMODULE>(value.base), symbol)(args...);
    }
    Artifact describe(GenerationId generation, const QString& manifest) const
    {
        Artifact result;
        result.generation = generation;
        result.manifest = manifest;
        result.library = library(manifest);
        const auto path = QDir::toNativeSeparators(result.library).toStdWString();
        result.base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(path.c_str()));
        CHECK(result.base);
        const auto data = files::read(result.library);
        IMAGE_DOS_HEADER dos{};
        CHECK(data.size() >= static_cast<int>(sizeof(dos)));
        std::memcpy(&dos, data.constData(), sizeof(dos));
        IMAGE_NT_HEADERS64 nt{};
        CHECK(dos.e_lfanew > 0 && data.size() >= dos.e_lfanew + static_cast<int>(sizeof(nt)));
        std::memcpy(&nt, data.constData() + dos.e_lfanew, sizeof(nt));
        result.bytes = nt.OptionalHeader.SizeOfImage;
        CHECK(result.bytes > 0);
        CHECK(call<CordisStatus(CORDIS_CALL*)(volatile LONG*)>(result, "cordis_fixture_unload_watch",
                                                               &result.witness->detaches) == CORDIS_OK);
        CHECK(state(generation).resources.records.empty());
        return result;
    }
    Artifact load(unsigned fixture = 0, const QString& folder = "module", bool resident = false)
    {
        const auto manifest = prepare(fixture, folder, resident);
        const auto generation =
            manager.load(root.toStdString(), manifest.toStdString(), {}, approval(manifest)).value().generation;
        return describe(generation, manifest);
    }
    std::size_t records(GenerationId generation, ResourceKind kind) const
    {
        const auto value = state(generation);
        return static_cast<std::size_t>(std::count_if(value.resources.records.begin(), value.resources.records.end(),
                                                      [&](const auto& row) { return row.kind == kind; }));
    }
    void protectedFile(const Artifact& value) const
    {
        QFile writer(value.library);
        CHECK(!writer.open(QIODevice::ReadWrite));
    }
    void blocked(const Artifact& value)
    {
        CHECK(!manager.tryUnload(value.generation).hasValue());
        CHECK(value.witness->read() == 0 && state(value.generation).nativeMapped);
        protectedFile(value);
    }
    void gone(const Artifact& value) const
    {
        const auto current = state(value.generation);
        CHECK(current.state == ModuleState::Unloaded && current.role == GenerationRole::Retired &&
              !current.nativeMapped && !current.nativePinned && !current.unloadFailure &&
              current.resources.records.empty());
        CHECK(value.witness->read() == 1);
        for (std::uintptr_t at = value.base; at < value.base + value.bytes;) {
            MEMORY_BASIC_INFORMATION memory{};
            CHECK(VirtualQuery(reinterpret_cast<LPCVOID>(at), &memory, sizeof(memory)) == sizeof(memory));
            CHECK(memory.Type != MEM_IMAGE || reinterpret_cast<std::uintptr_t>(memory.AllocationBase) != value.base);
            const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
            CHECK(next > at);
            at = next;
        }
        const auto generation = native::findGeneration(value.generation);
        CHECK(generation && !generation->image && !generation->backend && !generation->artifactFile);
        const auto path = native::nativeModulePath(value.library);
        const auto file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(file != INVALID_HANDLE_VALUE);
        const std::unique_ptr<void, decltype(&CloseHandle)> closed(file, &CloseHandle);
        unsigned char first = 0;
        DWORD bytes = 0;
        CHECK(ReadFile(file, &first, 1, &bytes, nullptr) && bytes == 1);
        const LARGE_INTEGER start{};
        CHECK(SetFilePointerEx(file, start, nullptr, FILE_BEGIN));
        // 真实独占写入同一字节，保留产物摘要，使下一次真实装载仍可重新核验。
        CHECK(WriteFile(file, &first, 1, &bytes, nullptr) && bytes == 1);
    }
};
struct Event {
    HANDLE handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Event() { CHECK(handle); }
    ~Event() { CloseHandle(handle); }
    void set() const { CHECK(SetEvent(handle)); }
    void wait() const { CHECK(WaitForSingleObject(handle, 8000) == WAIT_OBJECT_0); }
};
inline thread_local std::function<void()>* dllAction = nullptr;
inline thread_local std::exception_ptr* dllFailure = nullptr;
inline void CORDIS_CALL callback()
{
    try {
        const auto action = *dllAction;
        action();
    } catch (...) {
        if (dllFailure && !*dllFailure)
            *dllFailure = std::current_exception();
    }
}
inline void inDll(Scene& scene, const Artifact& value, std::function<void()> action)
{
    auto source = native::acquireNativeSource(value.generation).value();
    auto held = source.ledger.acquire(ResourceKind::Call, "test.unload.entered").value();
    source.session.reset();
    const auto function = entry<void(CORDIS_CALL*)(void(CORDIS_CALL*)())>(reinterpret_cast<HMODULE>(value.base),
                                                                          "cordis_fixture_unload_call");
    std::exception_ptr failure;
    struct Restore {
        std::function<void()>* action;
        std::exception_ptr* failure;
        ~Restore()
        {
            dllAction = action;
            dllFailure = failure;
        }
    } restore{dllAction, dllFailure};
    dllAction = &action;
    dllFailure = &failure;
    function(callback);
    if (failure)
        std::rethrow_exception(failure);
    (void)scene;
}
bool lifetime(Scene& scene, const QString& name);
bool createReentry(Scene& scene, const QString& name);
bool lineage(Scene& scene, const QString& name);
bool borrowedReader(Scene& scene, const QString& name);
} // namespace unload_test
