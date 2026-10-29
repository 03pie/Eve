/** @file module_native.cpp
 * @brief 用真实 C DLL 验证描述 ABI、固定文件、目录搜索及进程驻留；不激活业务。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cordis/module/manager.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../src/module/abi_p.hpp"
#include "../abi/business_flow.hpp"
#include <Windows.h>

using namespace cordis;
using namespace cordis::module;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
QString root, fixtures;
void write(const QString& path, const QByteArray& data)
{
    cordis::testing::modules::write(path, data);
}
QByteArray read(const QString& path)
{
    return cordis::testing::modules::read(path);
}
QString install(const QString& name, const QString& folder = "module")
{
    return cordis::testing::modules::install(fixtures + "/cordis_fixture_" + name + ".dll", root + '/' + folder,
                                             "org.cordis." + name, QJsonArray{"org.cordis.plugin"});
}
int queryCount(const ModuleGenerationSnapshot& value)
{
    const auto path = QDir::toNativeSeparators(QString::fromUtf8(value.libraryPath.c_str())).toStdWString();
    const auto image = GetModuleHandleW(path.c_str());
    CHECK(image != nullptr);
    const auto address = GetProcAddress(image, "cordis_fixture_queries");
    CHECK(address != nullptr);
    int(__cdecl * function)() = nullptr;
    static_assert(sizeof(function) == sizeof(address), "function pointer");
    std::memcpy(&function, &address, sizeof(function));
    return function();
}
#include "native_host_cases.hpp"
#include "native_lifecycle_cases.hpp"
#include "native_managed_cases.hpp"
#include "native_resource_cases.hpp"
#include "native_service_cases.hpp"
void loaded()
{
    const auto manifest = install("good", QString::fromUtf8("插件"));
    ModuleManager manager;
    const auto report = manager.load(root.toUtf8().toStdString(), manifest.toUtf8().toStdString());
    CHECK(report.hasValue() && !report.value().reused && report.value().generation.valid());
    const auto values = manager.snapshot().value();
    CHECK(values.size() == 1);
    const auto& value = values[0];
    CHECK(value.generation == report.value().generation && value.state == ModuleState::Loaded);
    CHECK(value.nativeMapped && value.nativePinned && value.descriptorAccepted && !value.failure);
    CHECK(value.resources.records.empty() && value.resources.accepting && !value.resources.pinReasons.empty());
    CHECK(queryCount(value) == 1);
}
void duplicate()
{
    const auto manifest = install("good");
    ModuleManager manager, second;
    const auto first = manager.load(root.toStdString(), manifest.toStdString()).value();
    const auto again = second.load(root.toStdString(), manifest.toStdString()).value();
    CHECK(first.generation == again.generation && again.reused);
    CHECK(manager.snapshot().value().size() == 1 && queryCount(manager.snapshot().value()[0]) == 1);
}
void business()
{
    const auto path = install("business");
    ModuleManager manager;
    BIZ_CHECK(manager.load(root.toStdString(), path.toStdString()).hasValue());
    const auto value = manager.snapshot().value()[0];
    const auto native = QDir::toNativeSeparators(QString::fromUtf8(value.libraryPath.c_str())).toStdWString();
    const auto image = GetModuleHandleW(native.c_str());
    BIZ_CHECK(image && value.nativePinned);
    const auto entry = GetProcAddress(image, "cordis_query_v1");
    const auto finishEntry = GetProcAddress(image, "cordis_business_fixture_finish");
    CordisQueryFnV1 query = nullptr;
    business_test::Finish finish = nullptr;
    static_assert(sizeof(query) == sizeof(entry) && sizeof(finish) == sizeof(finishEntry), "entry pointers");
    std::memcpy(&query, &entry, sizeof(query));
    std::memcpy(&finish, &finishEntry, sizeof(finish));
    BIZ_CHECK(query && finish);
    const auto api = cordis::module::detail::queryBusiness(query, business_test::fullHost, value.manifest).value();
    business_test::exercise(api.table, finish);
    BIZ_CHECK(manager.drain(value.generation).hasValue());
    BIZ_CHECK(manager.snapshot().value()[0].state == ModuleState::Resident);
}
void publication()
{
    const auto manifest = install("blocking");
    const auto suffix = std::to_wstring(GetCurrentProcessId());
    struct Event {
        HANDLE handle;
        ~Event()
        {
            if (handle)
                CloseHandle(handle);
        }
    } entered{CreateEventW(nullptr, TRUE, FALSE, (L"Local\\CordisQueryEntered" + suffix).c_str())},
        release{CreateEventW(nullptr, TRUE, FALSE, (L"Local\\CordisQueryRelease" + suffix).c_str())};
    CHECK(entered.handle && release.handle);
    bool accepted = false;
    std::thread worker([&] {
        try {
            ModuleManager loader;
            accepted = loader.load(root.toStdString(), manifest.toStdString()).hasValue();
        } catch (...) {
            accepted = false;
        }
    });
    ModuleManager observer;
    try {
        CHECK(WaitForSingleObject(entered.handle, 5000) == WAIT_OBJECT_0);
        const auto values = observer.snapshot().value();
        CHECK(values.size() == 1);
        const auto& value = values[0];
        CHECK(value.state == ModuleState::Validated && !value.descriptorAccepted);
        CHECK(value.nativeMapped && value.nativePinned && !value.failure);
        CHECK(value.resources.records.size() == 1 && value.resources.records[0].kind == ResourceKind::Call);
        auto duplicate = observer.load(root.toStdString(), manifest.toStdString());
        CHECK(!duplicate.hasValue() && duplicate.error().code == ErrorCode::ModuleLoad);
        auto drain = observer.drain(value.generation);
        CHECK(!drain.hasValue() && drain.error().code == ErrorCode::ContextClosing);
    } catch (...) {
        SetEvent(release.handle);
        worker.join();
        throw;
    }
    SetEvent(release.handle);
    worker.join();
    CHECK(accepted);
    const auto value = observer.snapshot().value()[0];
    CHECK(value.state == ModuleState::Loaded && value.descriptorAccepted && value.resources.records.empty());
    CHECK(queryCount(value) == 1);
}
void conflict()
{
    const auto manifest = install("good");
    ModuleManager manager;
    const auto first = manager.load(root.toStdString(), manifest.toStdString()).value();
    auto changed = QJsonDocument::fromJson(read(manifest)).object();
    changed["moduleVersion"] = "2.0.0";
    write(manifest, QJsonDocument(changed).toJson());
    auto denied = manager.load(root.toStdString(), manifest.toStdString());
    CHECK(!denied.hasValue() && denied.error().code == ErrorCode::ModulePinned);
    CHECK(manager.snapshot().value()[0].generation == first.generation &&
          queryCount(manager.snapshot().value()[0]) == 1);
}
void tampered()
{
    const auto manifest = install("good");
    auto initial = preflightModule(root.toStdString(), manifest.toStdString());
    CHECK(initial.hasValue());
    write(QFileInfo(manifest).absolutePath() + "/module.dll", "changed after preview");
    ModuleManager manager;
    CHECK(!manager.load(root.toStdString(), manifest.toStdString()).hasValue());
    CHECK(manager.snapshot().value().empty());
}
void incompatible()
{
    ModuleManager manager;
    for (const auto* name : {"bad_header", "bad_identity", "bad_reserved", "bad_null", "rejected", "short_table",
                             "bad_runtime", "bad_flags", "bad_catalog", "bad_span", "missing", "throwing"}) {
        const auto manifest = install(name, name);
        auto result = manager.load(root.toStdString(), manifest.toStdString());
        CHECK(!result.hasValue());
        auto values = manager.snapshot().value();
        const auto& value = values.back();
        CHECK(value.state == ModuleState::Failed && value.nativeMapped && value.nativePinned &&
              !value.descriptorAccepted && value.failure);
        CHECK(value.resources.drained() && !value.resources.pinReasons.empty());
        auto again = manager.load(root.toStdString(), manifest.toStdString());
        CHECK(!again.hasValue());
        if (std::string(name) != "missing")
            CHECK(queryCount(value) == 1);
    }
}
void image()
{
    auto manifest = install("good");
    auto object = QJsonDocument::fromJson(read(manifest)).object();
    const auto library = QFileInfo(manifest).absolutePath() + "/module.dll";
    const auto invalid = QByteArray("not a PE");
    write(library, invalid);
    object["sha256"] = QString::fromLatin1(QCryptographicHash::hash(invalid, QCryptographicHash::Sha256).toHex());
    write(manifest, QJsonDocument(object).toJson());
    ModuleManager manager;
    auto result = manager.load(root.toStdString(), manifest.toStdString());
    CHECK(!result.hasValue() && result.error().phase == "module.image" && manager.snapshot().value().empty());
    const auto original = read(fixtures + "/cordis_fixture_good.dll");
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, original.constData(), sizeof(dos));
    const auto headerOffset = dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, FileHeader);
    IMAGE_FILE_HEADER header{};
    std::memcpy(&header, original.constData() + headerOffset, sizeof(header));
    for (int mode = 0; mode < 2; ++mode) {
        auto bytes = original;
        auto changed = header;
        if (mode == 0)
            changed.Characteristics &= static_cast<WORD>(~IMAGE_FILE_DLL);
        else
            changed.Machine = IMAGE_FILE_MACHINE_ARM64;
        std::memcpy(bytes.data() + headerOffset, &changed, sizeof(changed));
        write(library, bytes);
        object["sha256"] = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        write(manifest, QJsonDocument(object).toJson());
        auto denied = manager.load(root.toStdString(), manifest.toStdString());
        CHECK(!denied.hasValue() && denied.error().phase == "module.image" && manager.snapshot().value().empty());
    }
}
void budget()
{
    ModuleManager manager;
    for (int i = 0; i < 65; ++i) {
        // 填充真实原生失败代次证明目录预算，不重复 64 次 ASan DLL 初始化。
        const auto path = install("dependent", "candidate" + QString::number(i));
        auto manifest = QJsonDocument::fromJson(read(path)).object();
        manifest["moduleId"] = "org.cordis.candidate" + QString::number(i);
        write(path, QJsonDocument(manifest).toJson());
        auto denied = manager.load(root.toStdString(), path.toStdString());
        CHECK(!denied.hasValue());
        if (i == 64)
            CHECK(denied.error().code == ErrorCode::ModulePinned && denied.error().phase == "module.catalog");
        else
            CHECK(denied.error().code == ErrorCode::ModuleLoad && denied.error().phase == "module.load");
    }
    CHECK(manager.snapshot().value().size() == 64);
}
void aliases()
{
    const auto original = install("good", "original");
    const auto other = install("good", "other");
    ModuleManager manager;
    CHECK(manager.load(root.toStdString(), original.toStdString()).hasValue());
    auto manifest = QJsonDocument::fromJson(read(other)).object();
    manifest["moduleId"] = "org.cordis.other";
    write(other, QJsonDocument(manifest).toJson());
    CHECK(!manager.load(root.toStdString(), other.toStdString()).hasValue());
    CHECK(manager.snapshot().value().size() == 2);
    manifest["moduleId"] = "org.cordis.good";
    write(other, QJsonDocument(manifest).toJson());
    auto denied = manager.load(root.toStdString(), other.toStdString());
    CHECK(!denied.hasValue() && denied.error().phase == "module.identity");
    CHECK(queryCount(manager.snapshot().value()[0]) == 1);
}
void untracked()
{
    const auto manifest = install("good");
    const auto path = QDir::toNativeSeparators(QFileInfo(manifest).absolutePath() + "/module.dll").toStdWString();
    const auto foreign =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(foreign != nullptr);
    ModuleManager manager;
    auto denied = manager.load(root.toStdString(), manifest.toStdString());
    const auto state = manager.snapshot().value()[0];
    const bool rejected = !denied.hasValue() && denied.error().phase == "module.existing" && !state.nativeMapped &&
                          !state.nativePinned && queryCount(state) == 0;
    CHECK(FreeLibrary(foreign));
    CHECK(rejected);
}
void lifetime()
{
    const auto manifest = install("good");
    GenerationId id;
    {
        ModuleManager manager;
        id = manager.load(root.toStdString(), manifest.toStdString()).value().generation;
        CHECK(manager.drain(id).hasValue() && manager.drain(id).hasValue());
        auto value = manager.snapshot().value()[0];
        CHECK(value.state == ModuleState::Resident && value.resources.drained());
        CHECK(!manager.load(root.toStdString(), manifest.toStdString()).hasValue());
        const auto native = QDir::toNativeSeparators(QString::fromUtf8(value.libraryPath.c_str())).toStdWString();
        const auto handle = GetModuleHandleW(native.c_str());
        CHECK(handle);
        CHECK(FreeLibrary(handle)); // PIN must survive even a mismatched external release.
        CHECK(GetModuleHandleW(native.c_str()) == handle && queryCount(value) == 1);
    }
    ModuleManager later;
    auto value = later.snapshot().value()[0];
    CHECK(value.generation == id && value.nativeMapped && value.nativePinned && value.state == ModuleState::Resident);
    CHECK(!later.drain(GenerationId{99999}).hasValue());
}
void sharing()
{
    const auto manifest = install("good");
    ModuleManager manager;
    CHECK(manager.load(root.toStdString(), manifest.toStdString()).hasValue());
    QFile writer(QFileInfo(manifest).absolutePath() + "/module.dll");
    CHECK(!writer.open(QIODevice::WriteOnly));
    CHECK(!QFile::remove(writer.fileName()));
    QFile reader(writer.fileName());
    CHECK(reader.open(QIODevice::ReadOnly));
}
void threads()
{
    const auto manifest = install("good");
    ModuleManager manager;
    bool rejected = false;
    std::thread worker([&] {
        auto load = manager.load(root.toStdString(), manifest.toStdString());
        auto state = manager.snapshot();
        auto drain = manager.drain({});
        rejected =
            !load.hasValue() && load.error().code == ErrorCode::WrongThread && !state.hasValue() && !drain.hasValue();
    });
    worker.join();
    CHECK(rejected && manager.snapshot().value().empty());
}
void dependency(bool present)
{
    const auto manifest = install("dependent");
    const auto destination = present ? QFileInfo(manifest).absolutePath() : root + "/ambient";
    CHECK(QDir().mkpath(destination));
    CHECK(QFile::copy(fixtures + "/cordis_fixture_dependency.dll", destination + "/cordis_fixture_dependency.dll"));
    const auto previous = QDir::currentPath();
    CHECK(QDir::setCurrent(destination));
    ModuleManager manager;
    auto result = manager.load(root.toStdString(), manifest.toStdString());
    CHECK(QDir::setCurrent(previous));
    if (present)
        CHECK(result.hasValue());
    else {
        CHECK(!result.hasValue() && result.error().code == ErrorCode::ModuleLoad);
        const auto value = manager.snapshot().value()[0];
        CHECK(value.state == ModuleState::Failed && !value.nativeMapped);
    }
}
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        const auto arguments = application.arguments();
        CHECK(arguments.size() == 4);
        const auto name = arguments[1];
        root = arguments[2];
        fixtures = arguments[3];
        if ((native_host_test::run(name) || native_resource_test::run(name) || native_service_test::run(name) ||
             native_managed_test::run(name) || native_lifecycle_test::run(name))) {
        } else if (name == "loaded")
            loaded();
        else if (name == "business")
            business();
        else if (name == "publication")
            publication();
        else if (name == "duplicate")
            duplicate();
        else if (name == "conflict")
            conflict();
        else if (name == "aliases")
            aliases();
        else if (name == "untracked")
            untracked();
        else if (name == "tampered")
            tampered();
        else if (name == "incompatible")
            incompatible();
        else if (name == "image")
            image();
        else if (name == "lifetime")
            lifetime();
        else if (name == "sharing")
            sharing();
        else if (name == "threads")
            threads();
        else if (name == "dependency")
            dependency(true);
        else if (name == "search")
            dependency(false);
        else if (name == "budget")
            budget();
        else
            throw std::runtime_error("unknown case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
