/** @file management_p.hpp
 * @brief 管理页的宿主私有创建/关闭桥，Qt 控件不进入公开 SDK。
 */
#pragma once
#include <QString>
#include <functional>
#include <cordis/qt/scheduler.hpp>
#include <cordis/ui/management.hpp>
#include <memory>
#include <optional>
#include <set>
class QWidget;
namespace cordis::ui::detail {
struct ManagementControl;
// Optional host adapter. Validation is side-effect-free and uses the same payload grammar as submission.
struct ConfigurationPayloadValidator {
    virtual ~ConfigurationPayloadValidator() = default;
    virtual Result<void> validatePayload(const std::string& text) const = 0;
};
QString outputPluginName(const std::shared_ptr<ManagementControl>& control, InstanceId instance);
void setManagementLogAction(const std::shared_ptr<ManagementControl>& control,
                            std::function<void(InstanceId, const std::string&, QWidget*)> action);
void setAddedInstanceAction(const std::shared_ptr<ManagementControl>& control,
                            std::function<void(const QString& path)> action);
/** @brief 创建 permanent 宿主页及 owner 定时刷新，所有信号仅持宿主弱记录。 */
std::shared_ptr<ManagementControl> createManagement(QWidget* parent, std::shared_ptr<qt::QtScheduler> scheduler,
    std::shared_ptr<UiPagesService> pages, std::shared_ptr<CommandService> commands, std::shared_ptr<NotificationService> notifications,
    std::function<void(QWidget*)> openNotifications);
/** @brief 仅宿主用于接入页面堆栈；关闭后返回的 QWidget 由外壳完整删除。 */
QWidget* managementWidget(const std::shared_ptr<ManagementControl>& control);
/** @brief 工作台菜单刷新沿用管理页同一状态门禁。 */
Result<void> refreshManagement(const std::shared_ptr<ManagementControl>& control);
/** @brief 单个宿主 Ela 窗口按稳定卡片身份定位，关闭窗口只隐藏，不停止插件。 */
Result<void> showManagementDialog(const std::shared_ptr<ManagementControl>& control, const QString& id = {},
                                  bool configuration = false);
/** @brief 平台表单只添加已注册的配置实例，不安装外部模块。 */
Result<void> showAddPluginDialog(const std::shared_ptr<ManagementControl>& control);
/** @brief 首页开关表达期望启停状态，按最新快照选择加载/重试/停止，不反转过时操作。 */
Result<void> setManagementEnabled(const std::shared_ptr<ManagementControl>& control, InstanceId instance, bool enabled);
/** @brief 首页配置动作保留发出时的修订，路径身份不随实例更换。 */
Result<void> setManagementConfigurationEnabled(const std::shared_ptr<ManagementControl>& control,
                                               const std::string& path, bool enabled, std::uint64_t revision);
/** @brief 一次 Home 刷新的纯值投影；不保留 provider、Kernel 或业务能力。 */
struct HomeSnapshot {
    std::optional<DiagnosticsSnapshot> diagnostics;
    std::set<InstanceId> hidden;
    bool allowContributions = false;
};
/** @brief 同一次权限读取生成卡片和贡献过滤；错误/关闭时禁止贡献兜底。 */
HomeSnapshot managementHomeSnapshot(const std::shared_ptr<ManagementControl>& control);
/** @brief 同步关门、停止刷新，晚到结果只更新值账目，不再操作窗口。 */
Task<void> closeManagement(const std::shared_ptr<ManagementControl>& control);
} // namespace cordis::ui::detail
