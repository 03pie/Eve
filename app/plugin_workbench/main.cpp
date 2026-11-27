/** @file main.cpp
 * @brief 选择桌面工作区，驱动宿主启动、关闭和 Qt 事件循环。
 */
#include "workbench.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

using namespace eve;

namespace {
/** @brief 将平台错误码和可用的操作标识附到对应日志事件。 */
void recordFailure(const ui::ElaShell& shell, const ErrorInfo& error, const char* summary, const char* code)
{
    ui::OutputDiagnostic diagnostic{{}, code};
    if (error.operation.valid())
        diagnostic.operation = error.operation;
    (void)shell.writeOutput(std::string(summary) + " · 错误码 " + std::to_string(static_cast<int>(error.code)),
                            ui::OutputLevel::Error, std::move(diagnostic));
}
}

/** @brief 按命令行和随附清单选择模块，若关闭时保存失败则维持事件循环以便重试。 */
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    application.setApplicationName("EvePluginWorkbench");
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser cli;
    cli.setApplicationDescription("Eve Qt UI plugin workbench");
    cli.addHelpOption();
    cli.addOption({{"m", "module"}, "Optional trusted external Qt UI module manifest.", "module.json"});
    cli.addOption({{"w", "workspace"}, "Writable workbench data directory.", "directory"});
    cli.process(application);
    if (!cli.positionalArguments().isEmpty()) cli.showHelp(1);
    const auto bundled = QDir(application.applicationDirPath()).filePath("plugins/current/module.json");
    std::optional<QString> manifest;
    if (cli.isSet("module"))
        manifest = cli.value("module");
    else if (QFileInfo::exists(bundled))
        manifest = bundled;
    const auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto workspace = cli.isSet("workspace") ? cli.value("workspace")
        : QFileInfo(workbench::workbenchDataFile(data, manifest ? QStringLiteral("session.yaml")
                                                  : QStringLiteral("platform-workspace/session.yaml"))).absolutePath();
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
        host.open(manifest, workspace);
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
        std::cerr << "Eve: " << error.what() << '\n';
        if (application.arguments().size() == 1)
            QMessageBox::critical(nullptr, QString::fromUtf8("无法打开插件工作台"),
                                  QString::fromUtf8(error.what()));
        status = EXIT_FAILURE;
        QTimer::singleShot(0, &application, close);
    }
    return application.exec();
}
