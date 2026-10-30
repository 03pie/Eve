/** @file module_candidates.cpp
 * @brief 真实两个后端在同一目录中并存代次，不改变当前代或重入旧工厂。
 */
#include "../support/module_artifacts.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <cordis/module/manager.hpp>
#include <cordis/module/native.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#ifdef CORDIS_TEST_QT_CANDIDATES
#include <cordis/module/qt_module.hpp>
#endif
#include <algorithm>
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
struct Scenario {
    ModuleManager manager;
    const bool qt;
    QString root, id;
    QJsonArray plugins;
    Scenario(bool backend, QString directory)
        : qt(backend), root(std::move(directory)), id(qt ? "org.cordis.qt.example" : "org.cordis.business"),
          plugins(qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"} : QJsonArray{"org.cordis.plugin"})
    {
    }
    Result<ModuleLoadReport> load(const QString& path)
    {
#ifdef CORDIS_TEST_QT_CANDIDATES
        if (qt)
            return loadQtModule(root.toStdString(), path.toStdString());
#endif
        return manager.load(root.toStdString(), path.toStdString());
    }
    Result<ModuleLoadReport> stage(const QString& path, GenerationId previous)
    {
#ifdef CORDIS_TEST_QT_CANDIDATES
        if (qt)
            return stageQtModule(root.toStdString(), path.toStdString(), previous);
#endif
        return manager.stage(root.toStdString(), path.toStdString(), previous);
    }
    QString install(const QString& dll, const QString& directory, const QString& version = "1.0.0") const
    {
        return files::install(dll, root + '/' + directory, id, plugins, qt ? "qt-ui" : "c-abi", version);
    }
    ModuleGenerationSnapshot snapshot(GenerationId idValue) const
    {
        const auto all = manager.snapshot().value();
        const auto found =
            std::find_if(all.begin(), all.end(), [=](const auto& value) { return value.generation == idValue; });
        CHECK(found != all.end());
        return *found;
    }
    int count(const QString& dll) const
    {
        const auto image = GetModuleHandleW(dll.toStdWString().c_str());
        CHECK(image);
        auto address = GetProcAddress(image, qt ? "cordis_qt_fixture_roots" : "cordis_fixture_queries");
        CHECK(address);
        int(__cdecl * read)() = nullptr;
        static_assert(sizeof(read) == sizeof(address), "Windows entry");
        std::memcpy(&read, &address, sizeof(read));
        return read();
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const bool qt = args[1].startsWith("qt_");
        const auto name = args[1].mid(qt ? 3 : 7);
        const auto dlls = args[3].split(';');
        CHECK(dlls.size() == 3);
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        Scenario scene(qt, temporary.path());
        const auto oldPath = scene.install(dlls[0], "old");
        const auto nextPath = scene.install(dlls[1], "next", "2.0.0");
        if (name == "business_failed_current") {
            CHECK(!qt);
            const auto current = scene.load(nextPath).value().generation;
            auto scheduler = std::make_shared<testing::FakeScheduler>();
            CHECK(!NativeModule::open(scene.manager, current, scheduler).hasValue());
            const auto state = scene.snapshot(current);
            CHECK(state.state == ModuleState::Loaded && !state.failure && state.businessFailure);
            CHECK(!scene.stage(oldPath, current).hasValue() && scene.manager.snapshot().value().size() == 1);
            CHECK(scene.load(nextPath).value().generation == current); // Discovery reuse remains compatible.
            scheduler->close();
            return 0;
        }
        if (name == "failed_current") {
            const auto broken = scene.install(dlls[2], "broken-current", "2.0.0");
            CHECK(!scene.load(broken).hasValue());
            const auto rows = scene.manager.snapshot().value();
            CHECK(rows.size() == 1 && rows[0].role == GenerationRole::Current && rows[0].failure);
            CHECK(!scene.stage(nextPath, rows[0].generation).hasValue() &&
                  scene.manager.snapshot().value().size() == 1);
            return 0;
        }
        if (name == "physical") {
            CHECK(!qt);
            const auto oldFile = temporary.path() + "/old/module.dll";
            const auto nextFile = temporary.path() + "/next/module.dll";
            CHECK(QFile::remove(nextFile)); // Only the just-installed, not-yet-mapped test fixture.
            CHECK(CreateHardLinkW(nextFile.toStdWString().c_str(), oldFile.toStdWString().c_str(), nullptr));
            auto json = QJsonDocument::fromJson(files::read(nextPath)).object();
            json["sha256"] =
                QString::fromLatin1(QCryptographicHash::hash(files::read(oldFile), QCryptographicHash::Sha256).toHex());
            files::write(nextPath, QJsonDocument(json).toJson());
        }
        const auto previous = scene.load(oldPath).value().generation;
        CHECK(scene.snapshot(previous).role == GenerationRole::Current);
        CHECK(!scene.load(nextPath).hasValue() && scene.manager.snapshot().value().size() == 1);
        if (name == "physical")
            CHECK(GetModuleHandleW((temporary.path() + "/next/module.dll").toStdWString().c_str()) ==
                  GetModuleHandleW((temporary.path() + "/old/module.dll").toStdWString().c_str()));
        else
            CHECK(!GetModuleHandleW((temporary.path() + "/next/module.dll").toStdWString().c_str()));
        if (name == "physical") {
            const auto denied = scene.stage(nextPath, previous);
            CHECK(!denied.hasValue() && denied.error().phase == "module.identity" &&
                  scene.manager.snapshot().value().size() == 1 &&
                  scene.count(temporary.path() + "/old/module.dll") == 1);
            return 0;
        } else if (name == "guards") {
            CHECK(!scene.stage(nextPath, {}).hasValue());
            CHECK(!scene.stage(nextPath, GenerationId{999999}).hasValue());
            CHECK(!scene.stage(oldPath, previous).hasValue());
            bool rejected = false;
            std::thread worker([&] {
                auto result = scene.stage(nextPath, previous);
                rejected = !result.hasValue() && result.error().code == ErrorCode::WrongThread;
            });
            worker.join();
            CHECK(rejected && scene.manager.snapshot().value().size() == 1);
        } else if (name == "catalog" || name == "identity") {
            auto json = QJsonDocument::fromJson(files::read(nextPath)).object();
            if (name == "catalog")
                json["plugins"] = QJsonArray{"org.cordis.changed"};
            else
                json["moduleId"] = "org.cordis.foreign";
            files::write(nextPath, QJsonDocument(json).toJson());
            CHECK(!scene.stage(nextPath, previous).hasValue());
            CHECK(scene.manager.snapshot().value().size() == 1 && scene.load(oldPath).value().generation == previous);
            CHECK(!GetModuleHandleW((temporary.path() + "/next/module.dll").toStdWString().c_str()));
            return 0;
        } else if (name == "failed") {
            const auto rejectedPath = scene.install(dlls[2], "rejected", "2.0.0");
            CHECK(!scene.stage(rejectedPath, previous).hasValue());
            const auto rejected = scene.manager.snapshot().value();
            CHECK(rejected.size() == 2 && rejected[1].role == GenerationRole::Candidate &&
                  rejected[1].predecessor == previous && rejected[1].failure && rejected[1].nativePinned);
            CHECK(!scene.stage(rejectedPath, previous).hasValue() && scene.manager.snapshot().value().size() == 2);
            CHECK(scene.load(oldPath).value().generation == previous);
        } else if (name == "drained") {
            CHECK(!qt);
            scene.manager.drain(previous).value();
            CHECK(!scene.stage(nextPath, previous).hasValue() && scene.manager.snapshot().value().size() == 1);
            return 0;
        } else if (name == "live") {
            CHECK(!qt);
            auto scheduler = std::make_shared<testing::FakeScheduler>();
            auto module = NativeModule::open(scene.manager, previous, scheduler).value();
            auto business = module.create("org.cordis.plugin", "\"sync\"", InstanceId{1}, 1).value();
            auto started = business.start();
            scheduler->runUntilIdle().value();
            CHECK(started.tryResult()->hasValue() && business.snapshot().value().state == NativeInstanceState::Started);
            const auto candidate = scene.stage(nextPath, previous).value().generation;
            CHECK(business.snapshot().value().state == NativeInstanceState::Started &&
                  scene.snapshot(previous).state == ModuleState::Serving &&
                  scene.snapshot(candidate).role == GenerationRole::Candidate);
            auto closed = module.close();
            scheduler->runUntilIdle().value();
            CHECK(closed.tryResult()->value().quiescent && scene.snapshot(candidate).state == ModuleState::Loaded);
            scheduler->close();
            return 0;
        } else {
            CHECK(name == "coexist" || name == "reuse" || name == "reentry" || name == "budget" || name == "aliases" ||
                  name == "business_failed_candidate");
        }
        bool reentered = false;
        std::exception_ptr callbackFailure;
        if (name == "reentry") {
            CHECK(qt);
            QTimer::singleShot(0, [&] {
                try {
                    auto rows = scene.manager.snapshot().value();
                    reentered = rows.size() == 2 && rows[1].state == ModuleState::Validated &&
                                rows[1].role == GenerationRole::Candidate && rows[1].predecessor == previous &&
                                scene.load(oldPath).value().generation == previous &&
                                !scene.stage(nextPath, previous).hasValue();
                } catch (...) {
                    callbackFailure = std::current_exception();
                }
            });
        }
        const auto loaded = scene.stage(nextPath, previous).value();
        if (callbackFailure)
            std::rethrow_exception(callbackFailure);
        const auto next = scene.snapshot(loaded.generation);
        CHECK(!loaded.reused && loaded.generation != previous && next.role == GenerationRole::Candidate &&
              next.predecessor == previous && next.state == ModuleState::Loaded && next.nativePinned);
        CHECK(next.manifest.moduleVersion == "2.0.0" && scene.snapshot(previous).manifest.moduleVersion == "1.0.0");
        CHECK(scene.load(oldPath).value().generation == previous && !scene.load(nextPath).hasValue());
        CHECK(scene.stage(nextPath, previous).value().generation == loaded.generation);
        CHECK(!scene.stage(nextPath, loaded.generation).hasValue());
        CHECK(scene.count(temporary.path() + "/next/module.dll") == 1);
        CHECK(scene.count(temporary.path() + "/old/module.dll") == 1);
        if (name == "business_failed_candidate") {
            CHECK(!qt);
            auto scheduler = std::make_shared<testing::FakeScheduler>();
            CHECK(!NativeModule::open(scene.manager, loaded.generation, scheduler).hasValue());
            CHECK(scene.snapshot(loaded.generation).businessFailure && !scene.stage(nextPath, previous).hasValue() &&
                  scene.manager.snapshot().value().size() == 2);
            CHECK(scene.load(oldPath).value().generation == previous);
            scheduler->close();
        }
        if (name == "reuse") {
            const auto copy = scene.install(dlls[1], "same-content", "2.0.0");
            const auto reused = scene.stage(copy, previous).value();
            CHECK(reused.reused && reused.generation == loaded.generation);
            CHECK(!GetModuleHandleW((temporary.path() + "/same-content/module.dll").toStdWString().c_str()));
            if (!qt) {
                ModuleManager other;
                CHECK(other.stage(temporary.path().toStdString(), copy.toStdString(), previous).value().generation ==
                      loaded.generation);
            }
        }
        if (name == "aliases") {
            CHECK(!qt);
            const auto path = scene.install(dlls[1], "foreign", "2.0.0");
            auto json = QJsonDocument::fromJson(files::read(path)).object();
            json["moduleId"] = "org.cordis.foreign";
            files::write(path, QJsonDocument(json).toJson());
            CHECK(!scene.load(path).hasValue() && scene.manager.snapshot().value().size() == 3);
            json["moduleId"] = scene.id;
            files::write(path, QJsonDocument(json).toJson());
            auto denied = scene.stage(path, previous);
            CHECK(!denied.hasValue() && denied.error().phase == "module.identity");
        }
        if (name == "reentry")
            CHECK(reentered);
        if (name == "budget") {
            CHECK(!qt);
            for (int index = 0; index < 62; ++index) {
                const auto folder = temporary.path() + "/budget-" + QString::number(index);
                const auto path =
                    files::install(dlls[0], folder, "org.cordis.budget." + QString::number(index), scene.plugins);
                CHECK(!scene.manager.load(temporary.path().toStdString(), path.toStdString()).hasValue());
            }
            CHECK(scene.manager.snapshot().value().size() == 64);
            const auto path =
                files::install(dlls[0], temporary.path() + "/overflow", "org.cordis.overflow", scene.plugins);
            auto overflow = scene.manager.load(temporary.path().toStdString(), path.toStdString());
            CHECK(!overflow.hasValue() && overflow.error().phase == "module.catalog");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
