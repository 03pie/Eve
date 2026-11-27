/** Exercise the installed desktop SDK through its archive, kernel and real Qt window. */
#include "workbench.hpp"
#include "desktop_selection.hpp"
#include <eve/extensions/catalog.hpp>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>
#include <QToolBar>
#include <cstdlib>
#include <iostream>

namespace {
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
template<class T> auto settle(const eve::Task<T>& task)
{
    QElapsedTimer time;
    time.start();
    while (!task.isReady() && time.elapsed() < 10000) QTest::qWait(5);
    check(task.isReady(), "Workbench task did not settle.");
    return task.tryResult();
}
template<class T> void require(const eve::Result<T>& result)
{
    if (!result.hasValue()) throw std::runtime_error(result.error().message);
}
void exercise(const QString& package, const QString& workspace, const QString& mode)
{
    using namespace eve;
    using namespace eve::workbench;
    const auto root = extensions::Catalog::defaultRoot();
    DesktopSelection choice(root);
    extensions::Catalog catalog(root);
    if (mode == "install") {
        auto preview = catalog.preview(package);
        require(preview);
        auto installed = catalog.install(package, preview.value().packageHash);
        require(installed);
        require(choice.request(installed.value().moduleId));
        return;
    }
    if (mode == "unacknowledged") {
        require(choice.begin(false));
        check(choice.attemptedExternal(), "Expected a trial launch.");
        DesktopSelection concurrent(root);
        check(concurrent.begin(false).value() == desktopModuleId, "Concurrent process must use Ela.");
        check(!concurrent.request(desktopModuleId).hasValue(), "Live trial must retain selection.");
        std::_Exit(EXIT_SUCCESS); // Leave both the startup marker and on-disk PID lock like a crashed process.
    }
    const bool safe = mode == "safe";
    const bool bundled = safe || mode == "recovered";
    const bool rejected = mode == "incompatible";
    Workbench host(safe);
    try {
        auto opened = settle(host.open({}, workspace));
        if (rejected) {
            check(!opened->hasValue() && opened->error().phase == "WORKBENCH_SELECTED_DESKTOP_FAILED",
                  "An incompatible selected plugin must fail in the desktop phase.");
            require(host.selection().recover());
        } else {
            require(*opened);
            auto view = host.desktop();
            check(view && view->hasExternalLayout() != bundled, "Wrong desktop layout mounted.");
            bool closeRequested = false;
            require(view->onCloseRequested([&] { closeRequested = true; }));
            require(view->show());
            QTest::qWait(30);
            check(view->externalLayoutVisible() != bundled, "External workbench visibility mismatch.");
            if (!bundled) {
                QWidget* window = nullptr;
                for (auto top : QApplication::topLevelWidgets())
                    if (top->isVisible() && top->objectName() == "WorkshopDesktop") window = top;
                check(window != nullptr, "The third-party top-level window was not shown.");
                check(window->findChild<QWidget*>("EveWorkbenchPanel"), "Shared diagnostics missing from the layout.");
                auto bar = window->findChild<QToolBar*>();
                check(bar != nullptr, "The desktop's own navigation was not built.");
                for (auto action : bar->actions()) {
                    action->trigger();
                    QApplication::processEvents();
                    if (action->text() == "Commands") {
                        auto panel = window->findChild<QWidget*>("EveCommandPanel");
                        check(panel && panel->isVisible(), "Platform command navigation failed.");
                    }
                }
                require(host.selection().confirm());
                window->close();
                check(closeRequested, "Outer window did not request host shutdown.");
            }
            if (mode == "recovered")
                check(choice.requested().value() == desktopModuleId, "Crash recovery must persist Ela.");
            if (safe)
                check(choice.requested().value() != desktopModuleId, "Safe launch discarded the stored desktop.");
        }
    } catch (...) {
        (void)settle(host.close());
        (void)host.scheduler->close();
        throw;
    }
    require(*settle(host.close()));
    require(host.scheduler->close());
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4) return 1;
    QStandardPaths::setTestModeEnabled(true);
    const auto id = QFileInfo(QFileInfo(args[2]).absolutePath()).fileName();
    app.setApplicationName("EveDesktopConsumer-" + id);
    try {
        exercise(args[1], args[2], args[3]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
