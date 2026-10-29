/** @file module_qt_load.cpp
 * @brief 真实 DLL 检查共享目录、Qt root 一次创建及错误驻留；不模拟业务工厂。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTimer>
#include <cordis/module/native.hpp>
#include <cordis/module/qt_manifest.hpp>
#include <cordis/module/qt_module.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstring>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
int roots(const QString& library)
{
    const auto image = GetModuleHandleW(library.toStdWString().c_str());
    CHECK(image);
    const auto address = GetProcAddress(image, "cordis_qt_fixture_roots");
    CHECK(address);
    int(__cdecl * read)() = nullptr;
    static_assert(sizeof(read) == sizeof(address), "Windows entry layout");
    std::memcpy(&read, &address, sizeof(read));
    return read();
}
int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "no_app") == 0) {
        auto result = loadQtModule({}, {});
        return !result.hasValue() && result.error().code == ErrorCode::WrongThread ? 0 : 1;
    }
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto name = args[1];
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        const auto root = temporary.path();
        const auto library = root + "/qt/module.dll";
        const auto path = files::install(args[3], root + "/qt", "org.cordis.qt.example",
                                         QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}, "qt-ui");
        ModuleManager manager;
        CHECK(manager.snapshot().value().empty());
        if (name == "thread") {
            bool rejected = false;
            std::thread worker([&] {
                auto result = loadQtModule(root.toStdString(), path.toStdString());
                rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
            });
            worker.join();
            CHECK(rejected && manager.snapshot().value().empty());
            CHECK(!GetModuleHandleW(library.toStdWString().c_str()));
            return 0;
        }
        if (name == "metadata" || name == "tamper" || name == "backend") {
            if (name == "tamper") {
                CHECK(inspectQtModule(root.toStdString(), path.toStdString()).hasValue());
                QFile file(library);
                CHECK(file.open(QIODevice::Append) && file.write("changed") == 7);
            } else if (name == "backend") {
                auto manifest = QJsonDocument::fromJson(files::read(path)).object();
                manifest["loader"] = "c-abi";
                files::write(path, QJsonDocument(manifest).toJson());
            }
            auto result = loadQtModule(root.toStdString(), path.toStdString());
            CHECK(!result.hasValue() && manager.snapshot().value().empty());
            CHECK(!GetModuleHandleW(library.toStdWString().c_str()));
            return 0;
        }
        // Successful and uncertain mappings retain file guards until process exit. CTest's
        // wrapper removes this isolated directory after the child process has terminated.
        temporary.setAutoRemove(false);
        std::unique_ptr<QPluginLoader> externalQt;
        if (name == "qt_existing") {
            externalQt = std::make_unique<QPluginLoader>(library);
            CHECK(externalQt->instance());
        }
        HMODULE external = nullptr;
        if (name == "existing") {
            external = LoadLibraryExW(library.toStdWString().c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            CHECK(external);
        }
        std::optional<ModuleLoadReport> nativeReport;
        if (name == "mixed") {
            const auto nativePath = files::install(QStringLiteral(CORDIS_NATIVE_FIXTURE), root + "/native",
                                                   "org.cordis.good", QJsonArray{"org.cordis.plugin"});
            auto loaded = manager.load(root.toStdString(), nativePath.toStdString());
            CHECK(loaded.hasValue());
            nativeReport = loaded.value();
        }
        bool reentered = false;
        if (name == "reentry") {
            QTimer::singleShot(0, &app, [&] {
                auto nested = loadQtModule(root.toStdString(), path.toStdString());
                CHECK(!nested.hasValue() && nested.error().phase == "module.load");
                CHECK(manager.snapshot().value().back().state == ModuleState::Validated);
                CHECK(!manager.drain(manager.snapshot().value().back().generation).hasValue());
                reentered = true;
            });
        }
        auto result = loadQtModule(root.toStdString(), path.toStdString());
        if (name == "reentry")
            CHECK(reentered);
        const auto generations = manager.snapshot().value();
        CHECK(generations.size() == (name == "mixed" ? 2u : 1u) &&
              generations.back().manifest.loader == ModuleLoader::QtUi);
        if (name == "parent" || name == "throw" || name == "existing" || name == "qt_existing") {
            CHECK(!result.hasValue());
            const auto failure = generations[0];
            CHECK(failure.state == ModuleState::Failed && failure.failure && !failure.descriptorAccepted);
            const bool mapped = name != "existing" && name != "qt_existing";
            CHECK(failure.nativeMapped == mapped);
            CHECK(failure.nativePinned == mapped);
            CHECK(!failure.resources.accepting);
            CHECK(failure.resources.records.size() == (mapped ? 1u : 0u));
            CHECK(roots(library) == (name == "existing" ? 0 : 1));
            CHECK(!loadQtModule(root.toStdString(), path.toStdString()).hasValue());
            CHECK(roots(library) == (name == "existing" ? 0 : 1));
            if (external)
                CHECK(FreeLibrary(external));
            return 0;
        }
        CHECK(result.hasValue() && !result.value().reused);
        const auto generation = generations.back();
        CHECK(generation.generation == result.value().generation && generation.state == ModuleState::Loaded &&
              !generation.failure);
        CHECK(generation.nativeMapped && generation.nativePinned && !generation.descriptorAccepted &&
              !generation.businessAccepted);
        CHECK(generation.resources.records.size() == 1 &&
              generation.resources.records[0].kind == ResourceKind::QObject);
        CHECK(roots(library) == 1);
        auto scheduler = std::make_shared<qt::QtScheduler>();
        auto native = NativeModule::open(manager, result.value().generation, scheduler);
        CHECK(!native.hasValue() && native.error().code == ErrorCode::AbiMismatch &&
              native.error().phase == "module.business");
        CHECK(manager.snapshot().value().back().resources.records.size() == 1);
        CHECK(inspectQtModule(root.toStdString(), path.toStdString()).error().code == ErrorCode::ModulePinned);
        QFile writing(library);
        CHECK(!writing.open(QIODevice::WriteOnly | QIODevice::Append));
        if (name == "mixed") {
            CHECK(nativeReport && nativeReport->generation != result.value().generation);
            CHECK(generations.front().manifest.loader == ModuleLoader::CAbi && generations.front().descriptorAccepted);
            CHECK(manager.drain(nativeReport->generation).hasValue());
            CHECK(manager.snapshot().value().front().state == ModuleState::Resident);
            CHECK(!manager.drain(result.value().generation).hasValue());
        }
        if (name == "reuse") {
            const auto other = files::install(args[3], root + "/copy", "org.cordis.qt.example",
                                              QJsonArray{"org.cordis.qt.settings", "org.cordis.qt.page"}, "qt-ui");
            auto reused = loadQtModule(root.toStdString(), other.toStdString());
            CHECK(reused.hasValue() && reused.value().reused && reused.value().generation == result.value().generation);
            CHECK(roots(library) == 1 && manager.snapshot().value().size() == 1);
            CHECK(!GetModuleHandleW((root + "/copy/module.dll").toStdWString().c_str()));
        } else if (name == "identity") {
            const auto other = files::install(args[3], root + "/different", "org.cordis.qt.example",
                                              QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}, "qt-ui");
            auto bytes = files::read(root + "/different/module.dll");
            bytes += "changed";
            files::write(root + "/different/module.dll", bytes);
            auto manifest = QJsonDocument::fromJson(files::read(other)).object();
            manifest["sha256"] =
                QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            files::write(other, QJsonDocument(manifest).toJson());
            auto rejected = loadQtModule(root.toStdString(), other.toStdString());
            CHECK(!rejected.hasValue() && rejected.error().phase == "module.identity");
            CHECK(manager.snapshot().value().size() == 1 && roots(library) == 1);
        } else if (name == "drain") {
            auto drained = manager.drain(result.value().generation);
            CHECK(!drained.hasValue() && drained.error().code == ErrorCode::ContextClosing);
            CHECK(manager.snapshot().value()[0].state == ModuleState::Loaded);
        }
        scheduler->close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
