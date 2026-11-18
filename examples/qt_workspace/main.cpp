/** @file main.cpp
 * @brief Notebook 与产品业务入口共用应用/退出接线；产品只消费本构建嵌入名单。
 */
#include "native_catalog_p.hpp"
#include "workspace.hpp"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <iostream>
#include <set>
#ifndef CORDIS_PLATFORM_WORKBENCH
#define CORDIS_PLATFORM_WORKBENCH 0
#endif

namespace {
using namespace cordis;
using notebook::Workspace;
std::vector<QPointer<QWidget>> shellWindows()
{
    std::vector<QPointer<QWidget>> windows;
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->inherits("ElaWindow"))
            windows.emplace_back(widget);
    return windows;
}
bool failedOpenFinished(const std::shared_ptr<qt::QtScheduler>& scheduler,
                        const std::vector<QPointer<QWidget>>& windows, module::ModuleManager& manager,
                        const std::set<GenerationId>& previous)
{
    for (const auto& window : windows)
        if (window)
            return false;
    const auto queue = scheduler->snapshot();
    const auto modules = manager.snapshot();
    if (!queue.hasValue() || !modules.hasValue() || queue.value().queued || queue.value().running ||
        queue.value().discarded || queue.value().callbackErrors)
        return false;
    for (const auto& row : modules.value())
        if (!previous.count(row.generation)) {
            if (row.failure || row.businessFailure)
                return false;
            for (const auto& resource : row.resources.records)
                if (row.manifest.loader != module::ModuleLoader::QtUi ||
                    resource.kind != module::ResourceKind::QObject || resource.label != "qt.loader.root")
                    return false;
        }
    return true;
}
void finishFailedOpen(const std::shared_ptr<qt::QtScheduler>& scheduler, module::ModuleManager& manager,
                      const std::set<GenerationId>& previous, const std::vector<QPointer<QWidget>>& before)
{
    auto windows = shellWindows();
    windows.erase(std::remove_if(windows.begin(), windows.end(),
                                 [&](const auto& window) {
                                     return std::find(before.begin(), before.end(), window) != before.end();
                                 }),
                  windows.end());
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    const auto finished = [&] { return failedOpenFinished(scheduler, windows, manager, previous); };
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (finished())
            loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (!finished()) {
        poll.start(1);
        deadline.start(10000);
        loop.exec();
    }
    if (finished()) {
        notebook::take(scheduler->close());
        return;
    }
    // 未确认时只报告真实残留；不清 pin、不称静止，也不关闭队列丢弃原收尾。
    std::cerr << "workspace initialization cleanup remains unconfirmed\n";
    const auto queue = scheduler->snapshot();
    if (queue.hasValue())
        std::cerr << "queue: queued=" << queue.value().queued << " discarded=" << queue.value().discarded
                  << " callbackErrors=" << queue.value().callbackErrors << '\n';
    const auto modules = manager.snapshot();
    if (modules.hasValue())
        for (const auto& row : modules.value())
            if (!previous.count(row.generation)) {
                std::cerr << "generation " << row.generation.value() << " " << row.manifest.moduleId << '\n';
                for (const auto& resource : row.resources.records)
                    std::cerr << "  resource " << resource.label << '\n';
                for (const auto& reason : row.resources.pinReasons)
                    std::cerr << "  pin " << reason << '\n';
            }
}
void requestClose(const std::shared_ptr<Workspace>& state)
{
    state->close().thenOn(state->scheduler, [state](const Result<void>& result) {
        if (!result.hasValue()) {
            (void)state->shell.writeOutput(result.error().message, ui::OutputLevel::Error);
            (void)state->shell.cancelCloseRequest();
            return;
        }
        QTimer::singleShot(0, [state] {
            const auto queue = state->scheduler->snapshot();
            if (!queue.hasValue() || queue.value().queued || queue.value().discarded || queue.value().callbackErrors) {
                std::cerr << "workspace closed with unconfirmed caller queue\n";
                QCoreApplication::exit(1);
                return;
            }
            const auto closed = state->scheduler->close();
            if (!closed.hasValue()) {
                std::cerr << closed.error().message << '\n';
                QCoreApplication::exit(1);
            } else
                QCoreApplication::quit();
        });
    });
}
} // namespace
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    constexpr bool platform = CORDIS_PLATFORM_WORKBENCH != 0;
    app.setApplicationName(platform ? "CordisBusinessPlatform" : "CordisDllNotebook");
    try {
        const auto args = app.arguments();
        if (args.size() > (platform ? 3 : 2))
            throw std::runtime_error(platform ? "usage: cordis_platform_workbench_demo [session.yaml] [store]"
                                              : "usage: cordis_qt_workspace_demo [session.yaml]");
        const auto data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const auto file = args.size() > 1 ? args[1] : data + "/session.yaml";
        const auto store = args.size() > 2 ? args[2] : data + "/plugins";
        if (!QDir().mkpath(QFileInfo(file).absolutePath()) || (platform && !QDir().mkpath(store)))
            throw std::runtime_error("cannot create workspace configuration/store directory");
        const auto scheduler = std::make_shared<qt::QtScheduler>();
        module::ModuleManager manager;
        std::set<GenerationId> previous;
        for (const auto& row : notebook::take(manager.snapshot()))
            previous.insert(row.generation);
        const auto before = shellWindows();
        const auto root = QCoreApplication::applicationDirPath();
#if CORDIS_PLATFORM_WORKBENCH
        auto opened = Workspace::openPlatform(root, QDir(store).absolutePath(), file, scheduler,
                                              notebook::detail::productNativeApprovals());
#else
        auto opened = Workspace::open(root + "/qt-workspace", file, scheduler);
#endif
        if (!opened.hasValue()) {
            std::cerr << opened.error().message << '\n';
            finishFailedOpen(scheduler, manager, previous, before);
            return 1;
        }
        const auto state = std::move(opened).value();
        const auto initial = state->initial;
        state->ready().thenOn(scheduler, [state, initial](const Result<void>& ready) {
            if (!ready.hasValue()) {
                (void)state->shell.writeOutput(ready.error().message, ui::OutputLevel::Error);
                (void)state->shell.show();
                return;
            }
            const auto accepted = state->apply(initial);
            if (!accepted.hasValue())
                (void)state->shell.writeOutput(accepted.error().message, ui::OutputLevel::Error);
            else
                accepted.value().settled.thenOn(state->scheduler,
                                                [state](const auto&) { (void)state->shell.management()->refresh(); });
            (void)state->shell.show();
        });
        notebook::take(state->shell.onCloseRequested([weak = std::weak_ptr<Workspace>(state)] {
            if (const auto state = weak.lock())
                requestClose(state);
        }));
        return app.exec();
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
