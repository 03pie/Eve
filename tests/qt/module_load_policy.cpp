/** @file module_load_policy.cpp
 * @brief 真实 DLL 的首次装载策略、发现协商与重新固定产物授权；本段不卸载。
 */
#include "../../src/module/catalog_p.hpp"
#include "../../src/module/native_source_p.hpp"
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <eve_abi/module_query.h>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <thread>
using namespace eve;
using namespace eve::module;
namespace files = eve::testing::modules;
namespace native = eve::module::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
static_assert(EVE_MODULE_QUERY_MINOR == 0 && EVE_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR == 1, "minor compatibility");
static_assert(EVE_MODULE_RESIDENT == 1 && EVE_MODULE_EXPLICIT_UNLOAD == 2, "exclusive declarations");
static_assert(sizeof(EveModuleQueryV1) == 32 && sizeof(EveModuleDescriptorV1) == 80, "V1 x64 layout");
static_assert(offsetof(EveModuleDescriptorV1, plugins) == 72, "V1 catalog offset");
namespace {
template <class F> F entry(HMODULE image, const char* name)
{
    const auto address = GetProcAddress(image, name);
    CHECK(address);
    F function;
    static_assert(sizeof(function) == sizeof(address), "Windows function pointer");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}
HMODULE image(const QString& path)
{
    return GetModuleHandleW(QDir::toNativeSeparators(path).toStdWString().c_str());
}
struct QueryObservation {
    int calls;
    std::uint32_t minor;
};
QueryObservation observe(const ModuleGenerationSnapshot& value)
{
    // 所有入口与 NativeSource.session 都在本栈退出；返回值不携带 DLL 能力。
    auto source = native::acquireNativeSource(value.generation).value();
    auto call = source.ledger.acquire(ResourceKind::Call, "test.load.policy.observe").value();
    const auto module = image(QString::fromStdString(value.libraryPath));
    CHECK(module);
    return {entry<int(EVE_QUERY_CALL*)()>(module, "eve_fixture_queries")(),
            entry<std::uint32_t(EVE_QUERY_CALL*)()>(module, "eve_fixture_last_query_minor")()};
}
struct WireObservation {
    EveQueryStatus status;
    std::uint32_t minor;
    std::uint64_t flags;
};
WireObservation ask(const ModuleGenerationSnapshot& value, std::uint32_t minor, std::uint64_t flags = 0,
                    std::uint32_t runtimeMinor = 0)
{
    auto source = native::acquireNativeSource(value.generation).value();
    auto call = source.ledger.acquire(ResourceKind::Call, "test.load.policy.query").value();
    const auto module = image(QString::fromStdString(value.libraryPath));
    CHECK(module);
    EveModuleQueryV1 request{{sizeof(request), EVE_MODULE_QUERY_MAJOR, minor, 0}, 1, runtimeMinor, flags};
    EveModuleDescriptorV1 descriptor{};
    descriptor.header.struct_size = sizeof(descriptor);
    const auto status = entry<EveModuleQueryFnV1>(module, "eve_module_query_v1")(&request, &descriptor);
    return {status, descriptor.header.query_minor, descriptor.flags};
}
bool pinned(const ModuleGenerationSnapshot& value, const char* reason)
{
    return std::find(value.resources.pinReasons.begin(), value.resources.pinReasons.end(), reason) !=
           value.resources.pinReasons.end();
}
struct Scene {
    ModuleManager manager;
    const QString root;
    const QStringList fixtures;
    Scene(QString directory, QStringList paths) : root(std::move(directory)), fixtures(std::move(paths))
    {
        CHECK(fixtures.size() == 7);
    }
    QString put(unsigned fixture, const QString& folder = "module", bool explicitPolicy = true) const
    {
        const auto id = fixture == 0   ? "org.eve.business"
                        : fixture == 3 ? "org.eve.policy.blocking"
                        : fixture == 4 ? "org.eve.bad_flags"
                                       : "org.eve.policy";
        const auto manifest =
            files::install(fixtures[static_cast<int>(fixture)], root + '/' + folder, id,
                           QJsonArray{"org.eve.plugin"}, "c-abi", fixture == 2 ? "2.0.0" : "1.0.0");
        policy(manifest, explicitPolicy);
        return manifest;
    }
    static QString library(const QString& manifest) { return QFileInfo(manifest).absolutePath() + "/module.dll"; }
    static void policy(const QString& manifest, bool explicitPolicy)
    {
        auto json = QJsonDocument::fromJson(files::read(manifest)).object();
        json["unloadPolicy"] = explicitPolicy ? "explicit" : "resident";
        files::write(manifest, QJsonDocument(json).toJson());
    }
    NativeUnloadWhitelist approve(const QString& manifest) const
    {
        const auto checked = preflightModule(root.toStdString(), manifest.toStdString()).value();
        return {{checked.manifest().moduleId, checked.manifest().sha256}};
    }
    Result<ModuleLoadReport> load(const QString& manifest, const NativeUnloadWhitelist& approvals = {})
    {
        return manager.load(root.toStdString(), manifest.toStdString(), {}, approvals);
    }
    Result<ModuleLoadReport> stage(const QString& manifest, GenerationId previous,
                                   const NativeUnloadWhitelist& approvals = {})
    {
        return manager.stage(root.toStdString(), manifest.toStdString(), previous, {}, approvals);
    }
    ModuleGenerationSnapshot state(GenerationId generation) const
    {
        const auto all = manager.snapshot().value();
        const auto at =
            std::find_if(all.begin(), all.end(), [&](const auto& value) { return value.generation == generation; });
        CHECK(at != all.end());
        return *at;
    }
    void empty(const QString& manifest) const
    {
        CHECK(manager.snapshot().value().empty() && !image(library(manifest)));
        // 拒绝前临时固定的文件保护必须真正退出，不能只看目录没有记录。
        QFile writable(library(manifest));
        CHECK(writable.open(QIODevice::ReadWrite));
    }
    void loaded(GenerationId generation, bool approved, std::uint32_t minor) const
    {
        const auto value = state(generation);
        CHECK(value.state == ModuleState::Loaded && value.nativeMapped && value.descriptorAccepted && !value.failure);
        CHECK(value.loadPolicy == (approved ? ModuleLoadPolicy::ApprovedNative : ModuleLoadPolicy::Resident));
        CHECK(value.nativePinned == !approved && pinned(value, "residentPolicy") == !approved &&
              pinned(value, "nativeProcessPin") == !approved);
        const auto actual = observe(value);
        CHECK(actual.calls == 1 && actual.minor == minor);
        CHECK(state(generation).resources.records.empty());
    }
};
void denied(Scene& scene, const QString& name)
{
    const auto manifest = scene.put(1);
    auto approvals = scene.approve(manifest);
    if (name == "unauthorized")
        approvals.clear();
    else if (name == "wrong_id")
        approvals[0].moduleId = "org.eve.other";
    else if (name == "wrong_hash")
        approvals[0].sha256 = std::string(64, '0');
    else
        approvals[0].sha256 = "invalid";
    CHECK(!scene.load(manifest, approvals).hasValue());
    scene.empty(manifest);
}
void tampered(Scene& scene)
{
    const auto manifest = scene.put(1);
    const auto preview = preflightModule(scene.root.toStdString(), manifest.toStdString()).value();
    const NativeUnloadWhitelist original{{preview.manifest().moduleId, preview.manifest().sha256}};
    auto bytes = files::read(Scene::library(manifest));
    bytes.append("changed after preview"); // PE overlay 保持真实 DLL 可装载，但必须重新授权其摘要。
    files::write(Scene::library(manifest), bytes);
    auto json = QJsonDocument::fromJson(files::read(manifest)).object();
    json["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    files::write(manifest, QJsonDocument(json).toJson());
    const auto revised = scene.approve(manifest);
    CHECK(revised[0].sha256 != original[0].sha256);
    CHECK(!scene.load(manifest, original).hasValue());
    scene.empty(manifest);
    const auto accepted = scene.load(manifest, revised).value();
    scene.loaded(accepted.generation, true, 1);
}
void initial(Scene& scene, const QString& name)
{
    const bool approved = name == "approved";
    const auto manifest = scene.put(name == "resident" ? 0U : 1U, "module", approved);
    const auto report = scene.load(manifest, scene.approve(manifest)).value();
    scene.loaded(report.generation, approved, approved ? 1U : 0U);
    if (name == "resident")
        CHECK(ask(scene.state(report.generation), 1).status == EVE_QUERY_UNSUPPORTED);
    const auto module = image(Scene::library(manifest));
    CHECK(scene.manager.drain(report.generation).hasValue());
    const auto drained = scene.state(report.generation);
    CHECK(drained.state == ModuleState::Resident && drained.resources.drained() && drained.nativeMapped &&
          drained.nativePinned == !approved && image(Scene::library(manifest)) == module);
    CHECK(drained.loadPolicy == (approved ? ModuleLoadPolicy::ApprovedNative : ModuleLoadPolicy::Resident));
    QFile writer(Scene::library(manifest));
    CHECK(!writer.open(QIODevice::ReadWrite)); // drain 仍不释放实际映像或固定文件。
}
void wire(Scene& scene)
{
    const auto manifest = scene.put(1);
    const auto generation = scene.load(manifest, scene.approve(manifest)).value().generation;
    scene.loaded(generation, true, 1);
    const auto value = scene.state(generation);
    const auto current = ask(value, 1), legacy = ask(value, 0);
    CHECK(current.status == EVE_QUERY_OK && current.minor == 1 && current.flags == EVE_MODULE_EXPLICIT_UNLOAD);
    CHECK(legacy.status == EVE_QUERY_OK && legacy.minor == 0 && legacy.flags == EVE_MODULE_RESIDENT);
    CHECK(ask(value, 2).status == EVE_QUERY_UNSUPPORTED);
    CHECK(ask(value, 1, EVE_MODULE_EXPLICIT_UNLOAD).status == EVE_QUERY_UNSUPPORTED);
    CHECK(ask(value, 1, 0, 1).status == EVE_QUERY_UNSUPPORTED);
    CHECK(scene.state(generation).resources.records.empty());
}
void rejectedDiscovery(Scene& scene, const QString& name)
{
    const bool resident = name == "legacy_flags";
    const auto fixture = resident ? 4U : name == "legacy_reject" ? 0U : name == "explicit_flags" ? 5U : 6U;
    const auto manifest = scene.put(fixture, "module", !resident);
    const auto result = scene.load(manifest, scene.approve(manifest));
    CHECK(!result.hasValue() && result.error().code == ErrorCode::AbiMismatch);
    if (name == "legacy_reject")
        CHECK(result.error().phase == "module.query" && result.error().message.find("status -1") != std::string::npos);
    const auto all = scene.manager.snapshot().value();
    CHECK(all.size() == 1);
    const auto& value = all.front();
    CHECK(value.state == ModuleState::Failed && value.nativeMapped && value.nativePinned == resident &&
          !value.descriptorAccepted && value.failure && value.resources.drained() && pinned(value, "loadRejected"));
    CHECK(value.loadPolicy == (resident ? ModuleLoadPolicy::Resident : ModuleLoadPolicy::ApprovedNative));
    CHECK(pinned(value, "residentPolicy") == resident && pinned(value, "nativeProcessPin") == resident);
    CHECK(image(Scene::library(manifest)));
    // 故障代已关门，不绕过 ledger 再调用 getter。
    CHECK(!scene.load(manifest, scene.approve(manifest)).hasValue() && scene.manager.snapshot().value().size() == 1);
}
void reuse(Scene& scene)
{
    const auto manifest = scene.put(1);
    const auto approvals = scene.approve(manifest);
    const auto first = scene.load(manifest, approvals).value();
    const auto owner = native::findGeneration(first.generation)->ownerThread;
    CHECK(owner == std::this_thread::get_id());
    CHECK(!scene.load(manifest).hasValue() && scene.manager.snapshot().value().size() == 1);
    std::optional<ModuleLoadReport> copied;
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            ModuleManager other;
            copied = other.load(scene.root.toStdString(), manifest.toStdString(), {}, approvals).value();
        } catch (...) {
            failure = std::current_exception();
        }
    });
    worker.join();
    if (failure)
        std::rethrow_exception(failure);
    CHECK(copied && copied->reused && copied->generation == first.generation);
    CHECK(native::findGeneration(first.generation)->ownerThread == owner &&
          scene.manager.snapshot().value().size() == 1);
    scene.loaded(first.generation, true, 1);
}
void immutable(Scene& scene, const QString& name)
{
    const bool approved = name == "immutable_approved";
    const auto manifest = scene.put(1, "module", approved);
    const auto approvals = scene.approve(manifest);
    const auto original = scene.load(manifest, approvals).value().generation;
    Scene::policy(manifest, !approved);
    CHECK(!scene.load(manifest, approvals).hasValue() && scene.manager.snapshot().value().size() == 1);
    CHECK(scene.state(original).manifest.unloadPolicy ==
          (approved ? ModuleUnloadPolicy::Explicit : ModuleUnloadPolicy::Resident));
    scene.loaded(original, approved, approved ? 1U : 0U);
}
void aliases(Scene& scene, const QString& name)
{
    const bool approved = name == "alias_approved";
    const auto manifest = scene.put(1, "original", approved);
    const auto aliasDirectory = scene.root + "/alias";
    CHECK(QDir().mkpath(aliasDirectory));
    const auto alias = aliasDirectory + "/module.json";
    const auto link = native::nativeModulePath(aliasDirectory + "/module.dll");
    const auto originalFile = native::nativeModulePath(Scene::library(manifest));
    CHECK(CreateHardLinkW(link.c_str(), originalFile.c_str(), nullptr));
    files::write(alias, files::read(manifest));
    Scene::policy(alias, true);
    const auto approvals = scene.approve(alias);
    const auto first = scene.load(manifest, approvals).value().generation;
    const auto result = scene.load(alias, approvals);
    CHECK(result.hasValue() == approved && scene.manager.snapshot().value().size() == 1);
    if (approved)
        CHECK(result.value().reused && result.value().generation == first);
    scene.loaded(first, approved, approved ? 1U : 0U);
}
void candidates(Scene& scene, bool sameHash)
{
    const auto first = scene.put(1, "old", false), next = scene.put(sameHash ? 1U : 2U, "next");
    const auto oldApproval = scene.approve(first), nextApproval = scene.approve(next);
    CHECK((oldApproval[0].sha256 == nextApproval[0].sha256) == sameHash);
    const auto current = scene.load(first, oldApproval).value().generation;
    if (!sameHash) {
        CHECK(!scene.stage(next, current, oldApproval).hasValue());
        CHECK(scene.manager.snapshot().value().size() == 1 && !image(Scene::library(next)));
    }
    const auto candidate = scene.stage(next, current, nextApproval).value().generation;
    CHECK(current != candidate && image(Scene::library(first)) != image(Scene::library(next)));
    scene.loaded(current, false, 0);
    scene.loaded(candidate, true, 1);
    CHECK(scene.state(current).role == GenerationRole::Current &&
          scene.state(candidate).role == GenerationRole::Candidate && scene.state(candidate).predecessor == current);
    auto held = scene.manager.reserveReplacement(current, candidate).value();
    CHECK(!scene.stage(next, current, nextApproval).hasValue());
    CHECK(!scene.manager.drain(current).hasValue() && !scene.manager.drain(candidate).hasValue());
    held.reset();
    const auto repeated = scene.stage(next, current, nextApproval).value();
    CHECK(repeated.reused && repeated.generation == candidate && scene.manager.snapshot().value().size() == 2);
    scene.loaded(current, false, 0);
    scene.loaded(candidate, true, 1);
}
void threads(Scene& scene)
{
    const auto manifest = scene.put(1);
    const auto approvals = scene.approve(manifest);
    bool rejected = false;
    std::thread worker([&] {
        auto result = scene.manager.load(scene.root.toStdString(), manifest.toStdString(), {}, approvals);
        rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
    });
    worker.join();
    CHECK(rejected);
    scene.empty(manifest);
    scene.loaded(scene.load(manifest, approvals).value().generation, true, 1);
}
void reentry(Scene& scene)
{
    const auto manifest = scene.put(3);
    const auto approvals = scene.approve(manifest);
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    struct Event {
        HANDLE handle;
        ~Event()
        {
            if (handle)
                CloseHandle(handle);
        }
    } entered{CreateEventW(nullptr, TRUE, FALSE, (L"Local\\EveQueryEntered" + suffix).c_str())},
        release{CreateEventW(nullptr, TRUE, FALSE, (L"Local\\EveQueryRelease" + suffix).c_str())};
    CHECK(entered.handle && release.handle);
    std::optional<ModuleLoadReport> accepted;
    std::exception_ptr failure;
    std::thread::id owner;
    std::thread worker([&] {
        try {
            owner = std::this_thread::get_id();
            ModuleManager loader;
            accepted = loader.load(scene.root.toStdString(), manifest.toStdString(), {}, approvals).value();
        } catch (...) {
            failure = std::current_exception();
        }
    });
    try {
        CHECK(WaitForSingleObject(entered.handle, 5000) == WAIT_OBJECT_0);
        const auto all = scene.manager.snapshot().value();
        CHECK(all.size() == 1);
        const auto& value = all.front();
        CHECK(value.state == ModuleState::Validated && value.nativeMapped && !value.nativePinned &&
              value.loadPolicy == ModuleLoadPolicy::ApprovedNative && !value.descriptorAccepted &&
              value.resources.records.size() == 1 && value.resources.records[0].kind == ResourceKind::Call);
        CHECK(!scene.load(manifest, approvals).hasValue() && !scene.load(manifest).hasValue());
        CHECK(!scene.manager.drain(value.generation).hasValue() && scene.manager.snapshot().value().size() == 1);
    } catch (...) {
        SetEvent(release.handle);
        worker.join();
        throw;
    }
    const bool signalled = SetEvent(release.handle) != FALSE;
    worker.join();
    CHECK(signalled);
    if (failure)
        std::rethrow_exception(failure);
    CHECK(accepted && native::findGeneration(accepted->generation)->ownerThread == owner);
    scene.loaded(accepted->generation, true, 1);
}
} // namespace
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        Scene scene(args[2], args[3].split(';'));
        const auto name = args[1];
        if (name == "approved" || name == "resident" || name == "capable_resident")
            initial(scene, name);
        else if (name == "wire")
            wire(scene);
        else if (name == "unauthorized" || name == "wrong_id" || name == "wrong_hash" || name == "invalid_entry")
            denied(scene, name);
        else if (name == "tampered_preview")
            tampered(scene);
        else if (name == "legacy_reject" || name == "legacy_flags" || name == "explicit_flags" ||
                 name == "explicit_minor")
            rejectedDiscovery(scene, name);
        else if (name == "reuse")
            reuse(scene);
        else if (name == "immutable_resident" || name == "immutable_approved")
            immutable(scene, name);
        else if (name == "alias_resident" || name == "alias_approved")
            aliases(scene, name);
        else if (name == "candidates" || name == "same_hash_copy")
            candidates(scene, name == "same_hash_copy");
        else if (name == "threads")
            threads(scene);
        else if (name == "reentry")
            reentry(scene);
        else
            CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
