/** @file main.cpp
 * @brief 选择桌面工作区，驱动宿主启动、关闭和 Qt 事件循环。
 */
#include "workbench.hpp"
#include "desktop_selection.hpp"
#include "desktop_text.hpp"
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
/** @brief 用户提供的清单和业务错误可能含私有路径和内容。归档只记录固定的阶段名称和说明。 */
const char* startupDiagnostic(const ErrorInfo& error)
{
    static constexpr const char* known[] = {
        "QT_MANIFEST_READ_FAILED",
        "QT_MODULE_INCOMPATIBLE",
        "QT_MANIFEST_INVALID",
        "QT_MODULE_PREFLIGHT_FAILED",
        "WORKBENCH_RESERVED_PLUGIN",
        "WORKBENCH_DESKTOP_MANIFEST_MISSING",
        "WORKBENCH_DESKTOP_LIBRARY_MISSING",
        "WORKBENCH_DESKTOP_IDENTITY_INVALID",
        "WORKBENCH_DESKTOP_HASH_INVALID",
        "WORKBENCH_DESKTOP_INCOMPATIBLE",
        "WORKBENCH_DESKTOP_LOAD_FAILED",
        "WORKBENCH_DESKTOP_ACTIVATION_FAILED",
        "WORKBENCH_SELECTED_DESKTOP_FAILED",
    };
    for (const auto* code : known)
        if (error.phase == code) return code;
    return "WORKBENCH_STARTUP_FAILED";
}

/** @brief 将平台错误码和可用的操作标识附到对应日志事件。 */
void recordFailure(const workbench::Workbench& host, const ErrorInfo& error, const QString& summary, const char* code)
{
    ui::OutputDiagnostic diagnostic{{}, code};
    if (error.operation.valid())
        diagnostic.operation = error.operation;
    const auto text = eve::desktopText("startup.errorCode").arg(summary).arg(static_cast<int>(error.code));
    (void)host.writeOutput(text.toStdString(), ui::OutputLevel::Error, std::move(diagnostic));
}
}

/** @brief 按命令行和随附清单选择模块，若关闭时保存失败则维持事件循环以便重试。 */
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto startupTranslations = eve::installDesktopDefaultTranslator();
    application.setApplicationName("EvePluginWorkbench");
    application.setQuitOnLastWindowClosed(false);
    QCommandLineParser cli;
    cli.setApplicationDescription(eve::desktopText("startup.eveQtUIPluginWorkbench"));
    cli.addHelpOption();
    cli.addOption({{"m", "module"}, eve::desktopText("startup.optionalTrustedExternalQtUIModuleManifest"), "module.json"});
    cli.addOption({{"w", "workspace"}, eve::desktopText("startup.writableWorkbenchDataDirectory"), eve::desktopText("startup.directory")});
    cli.addOption({"safe-desktop", eve::desktopText("startup.startWithTheBundledElaDesktop")});
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
                    recordFailure(host, outcome.error(), eve::desktopText("startup.couldNotCloseTheWorkspace"), "WORKBENCH_CLOSE_FAILED");
                    (void)host.desktop()->cancelCloseRequest();
                    QMessageBox::warning(nullptr, eve::desktopText("startup.workspaceCouldNotClose"),
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
                        std::cerr << eve::desktopText("startup.couldNotRestartEveReopenItUsing").toStdString() << '\n';
                        status = EXIT_FAILURE;
                    }
                }
                application.exit(status);
            });
        });
    };
    host.restartRequested = [&] { if (!stopping) { restart = Restart::Requested; close(); } };
    auto failed = [&](const ErrorInfo& error) {
        const auto code = startupDiagnostic(error);
        const auto text = QString::fromLatin1(code) + ": " + eve::desktopText(code);
        recordFailure(host, error, text, code);
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
                        eve::desktopText("startup.selectedDesktopDidNotPresentAVisible").toStdString())));
                    return;
                }
                auto confirmed = host.selection().confirm();
                if (!confirmed.hasValue()) { failed(workbench::selectedDesktopFailure(confirmed.error())); return; }
            }
            const auto applied = host.applyInitial();
            applied.settled.thenOn(host.scheduler, [&](const Result<loader::PlanReport>& result) {
                const auto summary = eve::desktopText("startup.couldNotApplyTheWorkspaceTarget");
                if (!result.hasValue())
                    recordFailure(host, result.error(), summary, "WORKBENCH_TARGET_FAILED");
                else for (const auto& error : result.value().errors)
                    recordFailure(host, error, summary, "WORKBENCH_TARGET_FAILED");
                if (auto view = host.desktop()) (void)view->refresh();
            });
          } catch (const std::exception& error) {
            failed(makeError(ErrorCode::ActivationFailed, error.what()));
          }
        });
    });
    return application.exec();
}
