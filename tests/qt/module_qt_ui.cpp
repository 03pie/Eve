/** @file module_qt_ui.cpp
 * @brief 宿主创建调度器和服务，真实 DLL 贡献 Ela 页面并验证完整析构。
 */
#include "../support/module_artifacts.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <algorithm>
#include <cordis/module/qt_module.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <cstring>
#include <iostream>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
using namespace cordis;
using namespace cordis::module;
using namespace cordis::ui;
namespace files = cordis::testing::modules;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
void pump(const std::function<bool()>& predicate, const char* phase = "task completion")
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() >= 5000)
            throw std::runtime_error(std::string("timeout: ") + phase);
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
}
template <class T> typename Task<T>::Snapshot wait(const Task<T>& task)
{
    pump([&] { return task.isReady(); });
    return task.tryResult();
}
struct Probe {
    HMODULE image;
    explicit Probe(const QString& path) : image(GetModuleHandleW(path.toStdWString().c_str())) { CHECK(image); }
    template <class F> F function(const char* name) const
    {
        const auto address = GetProcAddress(image, name);
        CHECK(address);
        F result;
        static_assert(sizeof(result) == sizeof(address), "Windows entry");
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
    int count(int index) const { return function<int(__cdecl*)(int)>("cordis_qt_ui_count")(index); }
    bool post(qt::QtScheduler* scheduler) const
    {
        return function<int(__cdecl*)(qt::QtScheduler*)>("cordis_qt_ui_post")(scheduler) == 1;
    }
};
std::size_t records(ModuleManager& manager, ResourceKind kind)
{
    const auto data = manager.snapshot().value().at(0).resources.records;
    return static_cast<std::size_t>(
        std::count_if(data.begin(), data.end(), [=](const ResourceRecord& value) { return value.kind == kind; }));
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        const auto name = args[1];
        QTemporaryDir temporary(args[2] + "/run-XXXXXX");
        CHECK(temporary.isValid());
        temporary.setAutoRemove(false);
        const auto manifest = files::install(args[3], temporary.path() + "/plugin", "org.cordis.qt.example",
                                             QJsonArray{"org.cordis.qt.page", "org.cordis.qt.settings"}, "qt-ui");
        auto scheduler = std::make_shared<qt::QtScheduler>();
        ElaShell shell(scheduler);
        auto services = std::make_shared<ServiceRegistry>(scheduler);
        auto provider = services->stage<UiPagesService>({"ui.pages"}, shell.pages()).value();
        provider.publish().value();
        auto loaded = loadQtModule(temporary.path().toStdString(), manifest.toStdString()).value();
        ModuleManager manager;
        Probe probe(temporary.path() + "/plugin/module.dll");
        std::optional<Runtime> runtime =
            qtRuntime(loaded.generation,
                      {"org.cordis.qt.page", {ServiceRequirement::of<UiPagesService>({"ui.pages"})}, {}}, scheduler)
                .value();
        auto first = Instance::mount(*runtime, scheduler, "first", services);
        CHECK(wait(first.ready())->hasValue());
        const auto initialPages = shell.pages()->snapshot().value();
        CHECK(initialPages.size() == 1 && initialPages[0].widgetAlive && initialPages[0].navigationPresent &&
              probe.count(0) == 1);
        if (name == "scheduler") {
            // Start outside the host scheduler's running drain; otherwise the duplicate wake type is masked.
            pump([&] {
                auto state = scheduler->snapshot().value();
                return !state.running && state.queued == 0;
            });
            CHECK(probe.post(scheduler.get()));
            pump([&] { return probe.count(7) == 1; }, "DLL posted job did not run");
            CHECK(probe.count(8) == 0 && scheduler->snapshot().value().queued == 0);
        } else if (name == "multi") {
            auto second = Instance::mount(*runtime, scheduler, "second", services);
            CHECK(wait(second.ready())->hasValue());
            CHECK(shell.pages()->snapshot().value().size() == 2 && records(manager, ResourceKind::Instance) == 2);
            CHECK(wait(second.stop())->value().quiescent);
            CHECK(shell.pages()->snapshot().value().size() == 1 && probe.count(5) == 1);
        } else if (name == "update") {
            const auto before = first.snapshot().value();
            auto operation = first.update("replacement", before.revision).value();
            CHECK(wait(operation.target)->hasValue());
            CHECK(probe.count(0) == 2 && probe.count(5) == 1 && probe.count(2) == 1);
            const auto pages = shell.pages()->snapshot().value();
            CHECK(pages.size() == 1 && pages[0].owner.epoch > before.epoch);
        } else {
            CHECK(name == "lifetime");
        }
        CHECK(wait(first.stop())->value().quiescent);
        CHECK(shell.pages()->snapshot().value().empty() && records(manager, ResourceKind::Instance) == 0);
        for (int index : {1, 2, 3, 4, 5})
            CHECK(probe.count(index) == probe.count(0));
        CHECK(probe.count(6) == 0);
        runtime.reset();
        CHECK(records(manager, ResourceKind::ExternalHandle) == 0);
        CHECK(records(manager, ResourceKind::QObject) == 1); // loader root remains resident
        CHECK(!manager.drain(loaded.generation).hasValue());
        provider.retire().value();
        CHECK(wait(provider.whenUnused())->hasValue());
        provider.release().value();
        CHECK(wait(shell.close())->hasValue());
        CHECK(scheduler->snapshot().value().callbackErrors == 0);
        scheduler->close().value();
        std::cout << "Qt/UI shared-runtime case passed: " << name.toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
