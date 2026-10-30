/** @file module_install.cpp
 * @brief 真实文件安装、保护、冲突和回收；候选接入另在独立子进程中映射 DLL。
 */
#include "../../src/module/install_directory_p.hpp"
#include "../support/module_artifacts.hpp"
#include <cordis/module/install.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/native.hpp>
#include <cordis/testing/fake_scheduler.hpp>
#ifdef CORDIS_TEST_QT_INSTALL
#include <cordis/module/qt_module.hpp>
#endif
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <optional>
#include <thread>

using namespace cordis;
using namespace cordis::module;
namespace files = cordis::testing::modules;
namespace native = cordis::module::detail;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

std::string utf8(const QString& value)
{
    return value.toUtf8().toStdString();
}
QString text(const std::string& value)
{
    return QString::fromUtf8(value.c_str());
}
QStringList entries(const QString& path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}
void modify(const QString& manifest, const QString& field, const QJsonValue& value)
{
    auto json = QJsonDocument::fromJson(files::read(manifest)).object();
    json[field] = value;
    files::write(manifest, QJsonDocument(json).toJson());
}
bool writable(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite);
}
struct Fixture {
    QString root, manifest, store, hash, destination;
    Fixture(QString directory, const QString& dll, bool qt) : root(std::move(directory)), store(root + "/store")
    {
        manifest = files::install(dll, root + "/source", qt ? "org.cordis.qt.example" : "org.cordis.business",
                                  qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}
                                     : QJsonArray{"org.cordis.plugin"},
                                  qt ? "qt-ui" : "c-abi", "2.0.0");
        CHECK(QDir().mkpath(store));
        hash = QJsonDocument::fromJson(files::read(manifest)).object()["sha256"].toString();
        destination = store + '/' + hash;
    }
    Result<InstalledArtifact> install(PreflightLimits limits = {}) const
    {
        return installModuleArtifact(utf8(root), utf8(manifest), utf8(store), limits);
    }
    void noTemporary() const
    {
        for (const auto& entry : entries(store))
            CHECK(!entry.startsWith(".cordis-install-"));
    }
};
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 4);
        const auto name = QString::fromUtf8(argv[1]);
        const auto root = QDir(QString::fromUtf8(argv[2])).absolutePath();
        const auto dlls = QString::fromUtf8(argv[3]).split(';');
        CHECK(dlls.size() == 4);
        const bool qt = name == "qt_stage";
        std::unique_ptr<QCoreApplication> app;
        if (qt)
            app = std::make_unique<QCoreApplication>(argc, argv);
        Fixture fixture(root, dlls[qt ? 3 : 1], qt);
        if (name == "invalid_source" || name == "limits" || name == "source_busy") {
            if (name == "invalid_source") {
                files::write(root + "/source/module.dll", "not a PE image");
                modify(fixture.manifest, "sha256",
                       QString::fromLatin1(
                           QCryptographicHash::hash("not a PE image", QCryptographicHash::Sha256).toHex()));
                auto invalid = fixture.install();
                CHECK(!invalid.hasValue() && invalid.error().phase == "module.image");
            } else if (name == "limits") {
                CHECK(!fixture.install({0, 1}).hasValue());
                CHECK(!fixture.install({1, 1}).hasValue());
                CHECK(!fixture.install({64 * 1024, 1}).hasValue());
                modify(fixture.manifest, "sha256", QString(64, '0'));
                CHECK(!fixture.install().hasValue());
            } else {
                QFile writer(root + "/source/module.dll");
                CHECK(writer.open(QIODevice::ReadWrite));
                CHECK(!fixture.install().hasValue());
                writer.close();
                CHECK(fixture.install().hasValue());
            }
            fixture.noTemporary();
            if (name != "source_busy")
                CHECK(entries(fixture.store).isEmpty());
            return 0;
        }
        if (name == "store_alias") {
            const auto alias = root + "/store-alias";
            CHECK(QProcess::execute("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(alias),
                                                QDir::toNativeSeparators(fixture.store)}) == 0);
            const auto installed = installModuleArtifact(utf8(root), utf8(fixture.manifest), utf8(alias)).value();
            CHECK(text(installed.artifact().manifestPath()) == fixture.destination + "/module.json");
            CHECK(RemoveDirectoryW(native::nativeModulePath(alias).c_str()));
            fixture.noTemporary();
            return 0;
        }
        if (name == "unicode") {
            const auto store = root + QString::fromUtf8("/插件安装目录");
            CHECK(QDir().mkpath(store));
            const auto installed = installModuleArtifact(utf8(root), utf8(fixture.manifest), utf8(store)).value();
            CHECK(text(installed.artifact().manifestPath()).startsWith(store + '/'));
            return 0;
        }
        if (name == "normalized_root") {
            auto installed = installModuleArtifact(utf8(root + "/source/.."), "source/./module.json",
                                                   utf8(fixture.store + "/../store/."))
                                 .value();
            CHECK(text(installed.artifact().manifestPath()) == fixture.destination + "/module.json");
            auto reused = installModuleArtifact(utf8("//?/" + root + "/source/.."), "source/module.json",
                                                utf8("//?/" + fixture.store + "/."))
                              .value();
            CHECK(reused.reused());
            fixture.noTemporary();
            return 0;
        }
        if (name == "roots") {
            for (const auto& invalid :
                 {std::string(), std::string("store"), std::string("D:store"), std::string("/store"),
                  std::string("bad\0root", 8), std::string("\xff", 1), utf8(root + "/missing"), utf8(fixture.manifest)})
                CHECK(!installModuleArtifact(utf8(root), utf8(fixture.manifest), invalid).hasValue());
            CHECK(entries(fixture.store).isEmpty());
            return 0;
        }
        if (name == "lock") {
            {
                auto held = native::lockModuleInstall(fixture.store, utf8(fixture.hash)).value();
                auto denied = fixture.install();
                CHECK(!denied.hasValue() && denied.error().phase == "module.install.lock");
                CHECK(entries(fixture.store).size() == 1);
            }
            CHECK(entries(fixture.store).isEmpty());
            // 不收养或清除一个已经存在但没有活动句柄的锁文件。
            const auto lock = fixture.store + "/.cordis-install-" + fixture.hash + ".lock";
            files::write(lock, "foreign lock");
            CHECK(!fixture.install().hasValue() && files::read(lock) == "foreign lock");
            CHECK(QFile::remove(lock));
            CHECK(fixture.install().hasValue());
            fixture.noTemporary();
            return 0;
        }
        if (name == "rollback" || name == "rollback_foreign" || name == "rollback_replaced" ||
            name == "publish_conflict" || name == "identity") {
            auto transaction = native::InstallDirectory::create(fixture.store).value();
            const auto directory = transaction->path();
            {
                native::OpenedPath source(root + "/source/module.dll", false);
                transaction->copy(source, "first.dll").value();
                transaction->copy(source, "second.dll").value();
            }
            if (name == "identity") {
                transaction->publish(fixture.destination).value();
                native::OpenedPath published(fixture.destination, true, false);
                CHECK(transaction->ownsDirectory(published));
                {
                    native::OpenedPath file(fixture.destination + "/first.dll", false);
                    CHECK(transaction->ownsFile("first.dll", file));
                }
                CHECK(QFile::remove(fixture.destination + "/first.dll"));
                CHECK(QFile::copy(root + "/source/module.dll", fixture.destination + "/first.dll"));
                native::OpenedPath replaced(fixture.destination + "/first.dll", false);
                CHECK(!transaction->ownsFile("first.dll", replaced));
                return 0;
            }
            if (name == "publish_conflict") {
                CHECK(QDir().mkpath(fixture.destination));
                files::write(fixture.destination + "/sentinel", "foreign directory");
                CHECK(!transaction->publish(fixture.destination).hasValue());
                CHECK(files::read(fixture.destination + "/sentinel") == "foreign directory");
            } else if (name == "rollback_foreign") {
                files::write(directory + "/sentinel", "foreign file");
            } else if (name == "rollback_replaced") {
                CHECK(QFile::remove(directory + "/first.dll"));
                files::write(directory + "/first.dll", "replacement file");
            }
            auto cleanup = transaction->rollback();
            if (name == "rollback_foreign" || name == "rollback_replaced") {
                CHECK(!cleanup.hasValue() && !cleanup.error().causes.empty());
                CHECK(!QFile::exists(directory + "/second.dll"));
                CHECK(files::read(directory + (name == "rollback_foreign" ? "/sentinel" : "/first.dll")) ==
                      (name == "rollback_foreign" ? "foreign file" : "replacement file"));
            } else {
                CHECK(cleanup.hasValue() && !QFile::exists(directory));
                fixture.noTemporary();
            }
            return 0;
        }
        if (name == "occupied") {
            CHECK(QDir().mkpath(fixture.destination));
            files::write(fixture.destination + "/sentinel", "do not overwrite");
            CHECK(!fixture.install().hasValue());
            CHECK(entries(fixture.destination).size() == 1 && entries(fixture.destination).first() == "sentinel");
            CHECK(files::read(fixture.destination + "/sentinel") == "do not overwrite");
            fixture.noTemporary();
            return 0;
        }
        if (name == "concurrent") {
            std::atomic<int> successes{0}, busy{0}, ready{0};
            std::atomic<bool> start{false};
            std::vector<std::thread> workers;
            for (int index = 0; index < 6; ++index)
                workers.emplace_back([&] {
                    ++ready;
                    while (!start.load())
                        std::this_thread::yield();
                    auto installed = fixture.install();
                    if (installed.hasValue())
                        ++successes;
                    else if (installed.error().phase == "module.install.lock")
                        ++busy;
                });
            while (ready.load() != 6)
                std::this_thread::yield();
            start = true;
            for (auto& worker : workers)
                worker.join();
            CHECK(successes > 0 && successes + busy == 6);
            CHECK(fixture.install().value().reused());
            CHECK(entries(fixture.store).size() == 1 && entries(fixture.store).first() == fixture.hash);
            fixture.noTemporary();
            return 0;
        }
        if (name == "long_path") {
            auto nested = fixture.store;
            while (nested.size() < 240)
                nested += "/long-path-segment";
            CHECK(QDir().mkpath(nested));
            const auto installed = installModuleArtifact(utf8(root), utf8(fixture.manifest), utf8(nested)).value();
            CHECK(installed.artifact().libraryPath().size() > MAX_PATH);
            CHECK(!installed.reused());
            return 0;
        }
        if (name == "multichunk") {
            auto bytes = files::read(root + "/source/module.dll");
            bytes.append(200000, '\0');
            files::write(root + "/source/module.dll", bytes);
            modify(fixture.manifest, "sha256",
                   QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
        }
        if (name == "manifest_name") {
            const auto renamed = root + "/source/incoming.json";
            CHECK(QFile::rename(fixture.manifest, renamed));
            fixture.manifest = renamed;
        }
        if (name == "redirected") {
            const auto external = root + "/external";
            const auto externalManifest =
                files::install(dlls[1], external, "org.cordis.business", {"org.cordis.plugin"}, "c-abi", "2.0.0");
            const auto externalBytes = files::read(externalManifest);
            const auto process =
                QProcess::execute("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(fixture.destination),
                                              QDir::toNativeSeparators(external)});
            CHECK(process == 0);
            auto denied = fixture.install();
            CHECK(!denied.hasValue());
            CHECK(files::read(externalManifest) == externalBytes && entries(external).size() == 2);
            CHECK(RemoveDirectoryW(native::nativeModulePath(fixture.destination).c_str()));
            fixture.noTemporary();
            return 0;
        }
        const auto original = files::read(fixture.manifest);
        {
            auto installed = fixture.install().value();
            CHECK(installed.isValid() && !installed.reused());
            const auto target = text(installed.artifact().libraryPath());
            const auto manifest = text(installed.artifact().manifestPath());
            CHECK(manifest.endsWith("/module.json") && files::read(manifest) == original);
            CHECK(files::read(target) == files::read(root + "/source/module.dll"));
            {
                native::OpenedPath source(root + "/source/module.dll", false), destination(target, false);
                CHECK(!source.sameFile(destination) && destination.links() == 1);
            }
            CHECK(!GetModuleHandleW(target.toStdWString().c_str()));
            CHECK(!GetModuleHandleW((root + "/source/module.dll").toStdWString().c_str()));
            CHECK(writable(root + "/source/module.dll") && writable(fixture.manifest));
            CHECK(!writable(target) && !writable(manifest));
            CHECK(!QFile::remove(target) && !QFile::remove(manifest));
            CHECK(!MoveFileW(native::nativeModulePath(QFileInfo(manifest).absolutePath()).c_str(),
                             native::nativeModulePath(fixture.store + "/renamed").c_str()));
            auto copy = installed;
            auto moved = std::move(installed);
            CHECK(!installed.isValid() && !installed.reused() && copy.isValid() && moved.isValid());
            bool threw = false;
            try {
                installed.artifact();
            } catch (const std::logic_error&) {
                threw = true;
            }
            CHECK(threw && !writable(target));
            if (name == "reuse") {
                std::exception_ptr failure;
                std::thread worker([&] {
                    try {
                        auto again = fixture.install().value();
                        CHECK(again.reused() && again.artifact().libraryPath() == moved.artifact().libraryPath());
                    } catch (...) {
                        failure = std::current_exception();
                    }
                });
                worker.join();
                if (failure)
                    std::rethrow_exception(failure);
            } else if (name == "conflict") {
                for (const auto& field :
                     {QString("moduleId"), QString("moduleVersion"), QString("loader"), QString("plugins")}) {
                    files::write(fixture.manifest, original);
                    modify(fixture.manifest, field,
                           field == "plugins" ? QJsonValue(QJsonArray{"org.cordis.other"})
                                              : QJsonValue(field == "moduleVersion" ? "3.0.0"
                                                           : field == "loader"      ? "qt-ui"
                                                                                    : "org.cordis.other"));
                    CHECK(!fixture.install().hasValue() && files::read(manifest) == original);
                }
                files::write(fixture.manifest, original);
                CHECK(QFile::copy(root + "/source/module.dll", root + "/source/renamed.dll"));
                modify(fixture.manifest, "library", "renamed.dll");
                CHECK(!fixture.install().hasValue());
                files::write(fixture.manifest, original);
            } else if (name == "native_stage" || name == "qt_stage") {
                const auto previous = files::install(
                    dlls[qt ? 2 : 0], root + "/previous", qt ? "org.cordis.qt.example" : "org.cordis.business",
                    qt ? QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"} : QJsonArray{"org.cordis.plugin"},
                    qt ? "qt-ui" : "c-abi");
                ModuleManager manager;
                const auto load = [&](const QString& path) {
#ifdef CORDIS_TEST_QT_INSTALL
                    if (qt)
                        return loadQtModule(utf8(root), utf8(path));
#endif
                    return manager.load(utf8(root), utf8(path));
                };
                const auto old = load(previous).value().generation;
                auto scheduler = std::make_shared<testing::FakeScheduler>();
                std::unique_ptr<NativeModule> business;
                std::optional<NativeInstance> instance;
                if (!qt) {
                    business = std::make_unique<NativeModule>(NativeModule::open(manager, old, scheduler).value());
                    instance.emplace(business->create("org.cordis.plugin", "\"sync\"", InstanceId{1}, 1).value());
                    auto started = instance->start();
                    scheduler->runUntilIdle().value();
                    CHECK(started.tryResult() && started.tryResult()->hasValue());
                }
                const auto staged =
                    [&] {
#ifdef CORDIS_TEST_QT_INSTALL
                        if (qt)
                            return stageQtModule(utf8(root), utf8(manifest), old);
#endif
                        return manager.stage(utf8(root), utf8(manifest), old);
                    }()
                        .value();
                CHECK(staged.generation != old && load(previous).value().generation == old);
                const auto generations = manager.snapshot().value();
                CHECK(generations.size() == 2);
                CHECK(std::count_if(generations.begin(), generations.end(), [](const auto& item) {
                          return item.role == GenerationRole::Candidate && item.state == ModuleState::Loaded;
                      }) == 1);
                CHECK(!GetModuleHandleW((root + "/source/module.dll").toStdWString().c_str()));
                if (business) {
                    CHECK(instance->snapshot().value().state == NativeInstanceState::Started);
                    auto closed = business->close();
                    scheduler->runUntilIdle().value();
                    CHECK(closed.tryResult() && closed.tryResult()->hasValue() &&
                          closed.tryResult()->value().quiescent);
                }
                scheduler->close();
            }
        }
        if (name == "hardlink") {
            CHECK(QFile::remove(fixture.destination + "/module.dll"));
            CHECK(CreateHardLinkW(native::nativeModulePath(fixture.destination + "/module.dll").c_str(),
                                  native::nativeModulePath(root + "/source/module.dll").c_str(), nullptr));
            CHECK(!fixture.install().hasValue());
            CHECK(files::read(fixture.destination + "/module.json") == original);
        } else if (name == "tamper") {
            files::write(fixture.destination + "/module.dll", "tampered cached bytes");
            CHECK(!fixture.install().hasValue());
            CHECK(files::read(fixture.destination + "/module.dll") == "tampered cached bytes");
        } else if (name == "basic") {
            CHECK(writable(fixture.destination + "/module.dll") && writable(fixture.destination + "/module.json"));
        }
        fixture.noTemporary();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
