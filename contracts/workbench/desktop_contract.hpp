/** @file desktop_contract.hpp
 * @brief 启动器与同一产品版本桌面之间的进程内装配契约。
 */
#pragma once
#include "desktop_run_log.hpp"
#include "desktop_archive.hpp"
#include <eve/desktop/layout.hpp>
#include <eve/module/qt_module.hpp>
#include <eve/qt/scheduler.hpp>
#include <eve/ui/configuration.hpp>
#include <eve/ui/extensions.hpp>

namespace eve::workbench {
inline constexpr char desktopBootstrapKey[] = "eve.host.desktop.v2";
inline constexpr char schedulerService[] = "qt.scheduler";
inline constexpr char pagesService[] = "ui.pages";
inline constexpr char commandsService[] = "ui.commands";
inline constexpr char outputService[] = "ui.output";
inline constexpr char notificationsService[] = "ui.notifications";
inline constexpr char extensionsService[] = "workspace.extensions";
using ExtensionControlsHandle = std::shared_ptr<ui::ExtensionControls>;

/** @brief 仅宿主持有弱入口。窗口、配置页和全部 Qt 回调由桌面实例与页面 effect 托管。 */
class DesktopView {
public:
    virtual ~DesktopView() = default;
    virtual ui::ExtensionItem description() const = 0;
    virtual Result<void> show() = 0;
    virtual bool hasExternalLayout() const = 0;
    virtual bool externalLayoutVisible() const = 0;
    virtual Result<void> cancelCloseRequest() = 0;
    virtual Result<void> onCloseRequested(std::function<void()> callback) = 0;
    virtual Result<void> refresh() = 0;
    virtual std::shared_ptr<ui::DiagnosticsService> diagnostics() const = 0;
    virtual std::shared_ptr<ui::UiPagesService> pages() const = 0;
    virtual std::shared_ptr<ui::OutputService> output() const = 0;
    virtual Result<void> showOutputEvent(std::uint64_t sequence, QWidget* origin) = 0;
    virtual Result<void> registerQtOutputRuntime(RuntimeId runtime, GenerationId generation,
        const std::string& moduleId, const std::string& version, const std::string& plugin) = 0;
    virtual Result<void> bindWorkspace(const std::shared_ptr<Kernel>& kernel,
        const std::shared_ptr<loader::ConfigurationSession>& session, std::string initial,
        std::vector<RuntimeId> protectedRuntimes) = 0;
    virtual Result<Runtime> registerConfiguration(Kernel& kernel) = 0;
};

struct DesktopBootstrap {
    RuntimeId layoutRuntime;
    std::string preferencesFile;
    std::weak_ptr<DesktopView> window;
};

inline module::QtPlugin desktopContract()
{
    return {desktopId, {ServiceRequirement::of<ui::detail::DesktopArchiveHandle>({desktopLogServiceKey}),
                        ServiceRequirement::of<ExtensionControlsHandle>({extensionsService}),
                        ServiceRequirement::of<DesktopBootstrap>({desktopBootstrapKey}),
                        ServiceRequirement::of<std::shared_ptr<qt::QtScheduler>>({schedulerService})},
        {ServiceOutput::of<ui::UiPagesService>({pagesService}),
         ServiceOutput::of<ui::CommandService>({commandsService}),
         ServiceOutput::of<ui::OutputService>({outputService}),
         ServiceOutput::of<ui::NotificationService>({notificationsService}),
         ServiceOutput::of<desktop::LayoutServiceV1>({desktop::layoutService})}};
}

}
