/** @file module_qt_preflight.cpp
 * @brief 独立检查 Qt 元数据、工具链拒绝和无映射副作用；不加入原生业务测试入口。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cordis/module/manager.hpp>
#include <cordis/module/qt_manifest.hpp>
#include <iostream>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace cordis;
using namespace cordis::module;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
namespace files = cordis::testing::modules;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 3);
        const auto name = args[1];
        QTemporaryDir root;
        CHECK(root.isValid());
        const auto plugins = name == "order" ? QJsonArray{"org.cordis.qt.settings", "org.cordis.qt.page"}
                                             : QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"};
        const auto path = files::install(args[2], root.path() + "/plugin", "org.cordis.qt.example", plugins, "qt-ui");
        const auto library = (root.path() + "/plugin/module.dll").toStdWString();
        CHECK(!GetModuleHandleW(library.c_str()));
        const auto before = ModuleManager{}.snapshot().value().size();
        if (name == "thread") {
            bool rejected = false;
            std::thread worker([&] {
                auto checked = inspectQtModule(root.path().toStdString(), path.toStdString());
                rejected = !checked.hasValue() && checked.error().code == ErrorCode::WrongThread;
            });
            worker.join();
            CHECK(rejected);
        } else if (name == "backend") {
            CHECK(ModuleManager{}.load(root.path().toStdString(), path.toStdString()).error().phase == "module.loader");
            auto document = QJsonDocument::fromJson(files::read(path)).object();
            document["loader"] = "c-abi";
            files::write(path, QJsonDocument(document).toJson());
            CHECK(inspectQtModule(root.path().toStdString(), path.toStdString()).error().code ==
                  ErrorCode::AbiMismatch);
        } else {
            auto checked = inspectQtModule(root.path().toStdString(), path.toStdString());
            if (name == "good" || name == "order") {
                if (!checked.hasValue())
                    throw std::runtime_error(checked.error().message);
                CHECK(checked.hasValue() && checked.value().artifact.isValid());
                CHECK(checked.value().artifact.manifest().loader == ModuleLoader::QtUi);
                CHECK(checked.value().toolchain.qtVersion == "5.15.2" && checked.value().toolchain.pointerBits == 64);
                // A snapshot is not permanent authorization after its file guards have been released.
                QFile libraryFile(root.path() + "/plugin/module.dll");
                CHECK(libraryFile.open(QIODevice::Append) && libraryFile.write("changed") == 7);
                libraryFile.close();
                CHECK(!inspectQtModule(root.path().toStdString(), path.toStdString()).hasValue());
            } else {
                CHECK(!checked.hasValue() && checked.error().code == ErrorCode::AbiMismatch);
                CHECK(checked.error().phase == "module.qt.preflight");
            }
        }
        CHECK(!GetModuleHandleW(library.c_str()));
        CHECK(ModuleManager{}.snapshot().value().size() == before);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
