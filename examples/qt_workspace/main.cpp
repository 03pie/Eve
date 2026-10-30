/** @file main.cpp
 * @brief 可操作的独立插件笔记工作台；默认使用独立应用数据，不改原工作台配置。
 */
#include "workspace.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("CordisDllNotebook");
    try {
        const auto args = app.arguments();
        if (args.size() > 2)
            throw std::runtime_error("usage: cordis_qt_workspace_demo [session.yaml]");
        const auto root = QCoreApplication::applicationDirPath() + "/qt-workspace";
        auto file = args.size() == 2
                        ? args[1]
                        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/session.yaml";
        notebook::take(QDir().mkpath(QFileInfo(file).absolutePath())
                           ? cordis::Result<void>::success()
                           : cordis::Result<void>::failure(cordis::makeError(cordis::ErrorCode::ConfigInvalid,
                                                                             "cannot create configuration directory")));
        auto state = notebook::Workspace::open(root, file);
        auto initial = state->initial;
        state->ready().thenOn(state->scheduler, [state, initial](const cordis::Result<void>& ready) {
            if (!ready.hasValue()) {
                std::cerr << ready.error().message << '\n';
                QCoreApplication::exit(1);
                return;
            }
            auto accepted = state->apply(initial);
            if (!accepted.hasValue())
                state->shell.writeOutput(accepted.error().message, cordis::ui::OutputLevel::Error);
            else
                accepted.value().settled.thenOn(state->scheduler,
                                                [state](const auto&) { state->shell.management()->refresh(); });
            state->shell.show();
        });
        state->shell
            .onCloseRequested([weak = std::weak_ptr<notebook::Workspace>(state)] {
                auto state = weak.lock();
                if (!state)
                    return;
                state->close().thenOn(state->scheduler, [state](const cordis::Result<void>& result) {
                    if (!result.hasValue()) {
                        state->shell.writeOutput(result.error().message, cordis::ui::OutputLevel::Error);
                        state->shell.cancelCloseRequest();
                        return;
                    }
                    QTimer::singleShot(0, [state] {
                        state->scheduler->close();
                        QCoreApplication::quit();
                    });
                });
            })
            .value();
        return app.exec();
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
