/** @file services_p.hpp
 * @brief 外壳与命令/通知后端的宿主私有桥，不进入插件 SDK。
 */
#pragma once
#include <eve/qt/scheduler.hpp>
#include <eve/task.hpp>
#include <eve/ui/commands.hpp>
#include <memory>
#include "output_p.hpp"
#include <vector>

class QLabel;
class QListView;
class QAction;
class QWidget;
namespace eve::ui::detail {
struct ServicesControl;
struct NotificationsEndpoint;
std::weak_ptr<NotificationsEndpoint> notificationsEndpoint(const std::shared_ptr<ServicesControl>& control);
struct CommandsEndpoint;
std::weak_ptr<CommandsEndpoint> commandsEndpoint(const std::shared_ptr<ServicesControl>& control);
/** Uses the same pure presentation gate as the command palette, without running enabled(). */
bool isPublishedCommand(const std::shared_ptr<ServicesControl>& control, const CommandSnapshot& command);
void configureNotificationCenter(const std::shared_ptr<ServicesControl>& control, QWidget* window,
                                 std::function<QString(InstanceId)> sourceName = {});
Result<void> showNotificationCenter(const std::shared_ptr<ServicesControl>& control, QWidget* origin = nullptr);
void setNotificationMuted(const std::shared_ptr<ServicesControl>& control, bool muted);
void setNotificationArchive(const std::shared_ptr<ServicesControl>& control,
                            std::shared_ptr<DesktopArchive> archive);
/** @brief 接入宿主持有的展示控件；控件信号只捕获宿主弱记录。 */
std::shared_ptr<ServicesControl> createUiServices(std::shared_ptr<qt::QtScheduler> scheduler, QListView* commands,
                                                  QLabel* notification);
/** @brief 关闭前纯值结算已发布输出和通知镜像，不刷新控件。 */
void settleUiValues(const std::shared_ptr<ServicesControl>& control);
/** @brief 同步关门，再等待全部命令 effect 完整排空；通知只有值数据，无插件清理回调。 */
Task<void> closeUiServices(const std::shared_ptr<ServicesControl>& control);
/** @brief 命令视图共享真实注册记录及平台 QAction，过滤只操作宿主值。 */
void registerPlatformCommands(const std::shared_ptr<ServicesControl>& control, const std::vector<QAction*>& actions);
void configureCommandPanel(const std::shared_ptr<ServicesControl>& control, QWidget* page,
    const std::vector<QAction*>& actions, std::shared_ptr<OutputControl> output, WindowAccess access);
} // namespace eve::ui::detail
