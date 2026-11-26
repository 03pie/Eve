#include "workbench.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

using namespace cordis;

namespace {
void recordFailure(const ui::ElaShell& shell, const ErrorInfo& error, const char* summary, const char* code)
{
    ui::OutputDiagnostic diagnostic{{}, code};
    if (error.operation.valid())
        diagnostic.operation = error.operation;
    (void)shell.writeOutput(std::string(summary) + " · 错误码 " + std::to_string(static_cast<int>(error.code)),
                            ui::OutputLevel::Error, std::move(diagnostic));
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setApplicationName("CordisPluginWorkbench");
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser cli;
    cli.setApplicationDescription("Cordis Qt UI plugin workbench");
    cli.addHelpOption();
    cli.addOption({{"m", "module"}, "Trusted external Qt UI module manifest.", "module.json",
                   QDir(application.applicationDirPath()).filePath("plugins/current/module.json")});
    cli.addOption({{"w", "workspace"}, "Writable session and plugin data directory.", "directory",
                   QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)});
    cli.process(application);
    if (!cli.positionalArguments().isEmpty()) cli.showHelp(1);
    workbench::Workbench host;
    int status = EXIT_SUCCESS;
    auto close = [&] {
        host.close().thenOn(host.scheduler, [&](const Result<void>& outcome) {
            if (!outcome.hasValue()) {
                std::cerr << outcome.error().message << '\n';
                if (status == EXIT_SUCCESS) {
                    recordFailure(host.shell, outcome.error(), "工作区未关闭。", "WORKBENCH_CLOSE_FAILED");
                    (void)host.shell.cancelCloseRequest();
                    QMessageBox::warning(nullptr, QString::fromUtf8("工作区未关闭"),
                                         QString::fromUtf8(outcome.error().message.c_str()));
                    return;
                }
            }
            QTimer::singleShot(0, &application, [&] {
                const auto stopped = host.scheduler->close();
                application.exit(stopped.hasValue() ? status : EXIT_FAILURE);
            });
        });
    };
    try {
        host.open(cli.value("module"), cli.value("workspace"));
        host.publishUi();
        const auto applied = host.applyInitial();
        applied.settled.thenOn(host.scheduler, [&](const Result<loader::PlanReport>& result) {
            if (!result.hasValue())
                recordFailure(host.shell, result.error(), "工作区目标启动失败。", "WORKBENCH_TARGET_FAILED");
            else for (const auto& error : result.value().errors)
                recordFailure(host.shell, error, "工作区目标启动失败。", "WORKBENCH_TARGET_FAILED");
            (void)host.shell.management()->refresh();
        });
        host.shell.onCloseRequested(close).value();
        host.shell.show().value();
    } catch (const std::exception& error) {
        const auto* preflight = dynamic_cast<const workbench::ModulePreflightError*>(&error);
        const std::string diagnostic = preflight ? preflight->diagnostic() : "工作台启动失败。请检查所选模块和工作区。";
        const auto code = preflight ? diagnostic.substr(0, diagnostic.find(':')) : "WORKBENCH_STARTUP_FAILED";
        (void)host.shell.writeOutput(diagnostic, ui::OutputLevel::Error, {{}, code});
        std::cerr << "Cordis: " << error.what() << '\n';
        if (application.arguments().size() == 1)
            QMessageBox::critical(nullptr, QString::fromUtf8("无法打开插件工作台"),
                                  QString::fromUtf8(error.what()));
        status = EXIT_FAILURE;
        QTimer::singleShot(0, &application, close);
    }
    return application.exec();
}
