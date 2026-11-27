/** @file main.cpp
 * @brief 选择桌面工作区，驱动宿主启动、关闭和 Qt 事件循环。
 */
#include "workbench.hpp"
#include "desktop_selection.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <iostream>

using namespace eve;

namespace {
struct StartupDiagnostic { const char* code; const char* message; };

/** @brief 用户提供的清单和业务错误可能含私有路径和内容。归档只记录固定的阶段名称和说明。 */
StartupDiagnostic startupDiagnostic(const ErrorInfo& error)
{
    static constexpr StartupDiagnostic known[] = {
        {"QT_MANIFEST_READ_FAILED", "无法读取或完整校验模块清单。"},
        {"QT_MODULE_INCOMPATIBLE", "模块预检发现 ABI、加载器或工具链不兼容。"},
        {"QT_MANIFEST_INVALID", "模块清单格式或字段校验失败。"},
        {"QT_MODULE_PREFLIGHT_FAILED", "模块产物或元数据未通过预检。"},
        {"WORKBENCH_RESERVED_PLUGIN", "工作区配置引用了受保护的系统插件。"},
        {"WORKBENCH_DESKTOP_MANIFEST_MISSING", "缺少官方桌面清单 system/desktop/module.json。"},
        {"WORKBENCH_DESKTOP_LIBRARY_MISSING", "缺少官方桌面组件 eve_desktop.dll。"},
        {"WORKBENCH_DESKTOP_IDENTITY_INVALID", "系统桌面的模块或插件标识与官方安装布局不符。"},
        {"WORKBENCH_DESKTOP_HASH_INVALID", "系统桌面文件未通过完整性校验。"},
        {"WORKBENCH_DESKTOP_INCOMPATIBLE", "系统桌面的映像、Qt 元数据或 SDK 契约与宿主不兼容。"},
        {"WORKBENCH_DESKTOP_LOAD_FAILED", "系统桌面装载失败，请检查安装文件及运行库。"},
        {"WORKBENCH_DESKTOP_ACTIVATION_FAILED", "系统桌面实例未能完成激活。"},
        {"WORKBENCH_SELECTED_DESKTOP_FAILED", "所选桌面启动失败，将恢复内置 Ela 桌面。"},
    };
    for (const auto& entry : known)
        if (error.phase == entry.code) return entry;
    return {"WORKBENCH_STARTUP_FAILED", "工作台启动失败。请检查桌面组件、所选模块和工作区。"};
}

/** @brief 将平台错误码和可用的操作标识附到对应日志事件。 */
void recordFailure(const workbench::Workbench& host, const ErrorInfo& error, const char* summary, const char* code)
{
    ui::OutputDiagnostic diagnostic{{}, code};
    if (error.operation.valid())
        diagnostic.operation = error.operation;
    (void)host.writeOutput(std::string(summary) + " · 错误码 " + std::to_string(static_cast<int>(error.code)),
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
    cli.addOption({"safe-desktop", "Start with the bundled Ela desktop."});
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
    workbench::Workbench host(cli.isSet("safe-desktop"));
    int status = EXIT_SUCCESS;
    enum class Restart { None, Requested, Recovery };
    auto restart = Restart::None;
    bool stopping = false;
    auto close = [&] {
        if (stopping) return;
        stopping = true;
        host.close().thenOn(host.scheduler, [&](const Result<void>& outcome) {
            if (!outcome.hasValue()) {
                std::cerr << outcome.error().message << '\n';
                if (status == EXIT_SUCCESS && host.canRetryClose()) {
                    stopping = false;
                    restart = Restart::None;
                    recordFailure(host, outcome.error(), "工作区未关闭。", "WORKBENCH_CLOSE_FAILED");
                    (void)host.desktop()->cancelCloseRequest();
                    QMessageBox::warning(nullptr, QString::fromUtf8("工作区未关闭"),
                                         QString::fromUtf8(outcome.error().message.c_str()));
                    return;
                }
                status = EXIT_FAILURE;
            }
            QTimer::singleShot(0, &application, [&] {
                const auto stopped = host.scheduler->close();
                if (!stopped.hasValue()) status = EXIT_FAILURE;
                if ((restart == Restart::Requested && status == EXIT_SUCCESS) || restart == Restart::Recovery) {
                    auto args = QCoreApplication::arguments().mid(1);
                    args.removeAll("--safe-desktop");
                    if (restart == Restart::Recovery) args.append("--safe-desktop");
                    if (!QProcess::startDetached(application.applicationFilePath(), args)) {
                        std::cerr << "Could not restart Eve. Reopen it using --safe-desktop for recovery.\n";
                        status = EXIT_FAILURE;
                    }
                }
                application.exit(status);
            });
        });
    };
    host.restartRequested = [&] { if (!stopping) { restart = Restart::Requested; close(); } };
    auto failed = [&](const ErrorInfo& error) {
        const auto diagnostic = startupDiagnostic(error);
        const auto text = std::string(diagnostic.code) + ": " + diagnostic.message;
        recordFailure(host, error, text.c_str(), diagnostic.code);
        if (error.phase == "WORKBENCH_SELECTED_DESKTOP_FAILED" && !cli.isSet("safe-desktop")) {
            const auto reset = host.selection().recover();
            if (!reset.hasValue()) std::cerr << reset.error().message << '\n';
            restart = Restart::Recovery;
        } else if (host.selection().attemptedExternal()) {
            const auto deferred = host.selection().defer();
            if (!deferred.hasValue()) std::cerr << deferred.error().message << '\n';
        }
        std::cerr << "Eve: " << error.message << '\n';
        status = EXIT_FAILURE;
        QTimer::singleShot(0, &application, close);
    };
    host.open(manifest, workspace).thenOn(host.scheduler, [&](const Result<void>& opened) {
        if (!opened.hasValue()) { failed(opened.error()); return; }
        auto view = host.desktop();
        auto bound = view->onCloseRequested(close);
        if (!bound.hasValue()) { failed(bound.error()); return; }
        auto shown = view->show();
        if (!shown.hasValue()) {
            failed(host.selection().attemptedExternal() ? workbench::selectedDesktopFailure(shown.error()) : shown.error());
            return;
        }
        QTimer::singleShot(0, &application, [&] {
          if (stopping) return;
          try {
            if (host.selection().attemptedExternal()) {
                if (!host.desktop() || !host.desktop()->externalLayoutVisible()) {
                    failed(workbench::selectedDesktopFailure(makeError(ErrorCode::ActivationFailed,
                        "Selected desktop did not present a visible workbench.")));
                    return;
                }
                auto confirmed = host.selection().confirm();
                if (!confirmed.hasValue()) { failed(workbench::selectedDesktopFailure(confirmed.error())); return; }
            }
            const auto applied = host.applyInitial();
            applied.settled.thenOn(host.scheduler, [&](const Result<loader::PlanReport>& result) {
                if (!result.hasValue())
                    recordFailure(host, result.error(), "工作区目标启动失败。", "WORKBENCH_TARGET_FAILED");
                else for (const auto& error : result.value().errors)
                    recordFailure(host, error, "工作区目标启动失败。", "WORKBENCH_TARGET_FAILED");
                if (auto view = host.desktop()) (void)view->refresh();
            });
          } catch (const std::exception& error) {
            failed(makeError(ErrorCode::ActivationFailed, error.what()));
          }
        });
    });
    return application.exec();
}
