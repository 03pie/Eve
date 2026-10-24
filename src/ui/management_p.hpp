/** @file management_p.hpp
 * @brief 管理页的宿主私有创建/关闭桥，Qt 控件不进入公开 SDK。
 */
#pragma once
#include <cordis/ui/management.hpp>
#include <cordis/qt/scheduler.hpp>
#include <memory>
class QWidget;
namespace cordis::ui::detail {
struct ManagementControl;
/** @brief 创建 permanent 宿主页及 owner 定时刷新，所有信号仅持宿主弱记录。 */
std::shared_ptr<ManagementControl> createManagement(QWidget* parent, std::shared_ptr<qt::QtScheduler> scheduler,
    std::shared_ptr<UiPagesService> pages, std::shared_ptr<CommandService> commands, std::shared_ptr<NotificationService> notifications);
/** @brief 仅宿主用于接入页面堆栈；关闭后返回的 QWidget 由外壳完整删除。 */
QWidget* managementWidget(const std::shared_ptr<ManagementControl>& control);
/** @brief 工作台菜单刷新沿用管理页同一状态门禁。 */
Result<void> refreshManagement(const std::shared_ptr<ManagementControl>& control);
/** @brief 同步关门、停止刷新，晚到结果只更新值账目，不再操作窗口。 */
Task<void> closeManagement(const std::shared_ptr<ManagementControl>& control);
} // namespace cordis::ui::detail
