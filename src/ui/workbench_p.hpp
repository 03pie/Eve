/** @file workbench_p.hpp
 * @brief 宿主工作台复用 Ela Example 的菜单、搜索、设置和窗口内路由，不授予插件窗口能力。
 */
#pragma once
#include "status_p.hpp"
#include <QString>
#include <cordis/result.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
class ElaWindow;
class QWidget;
class QTabBar;
class QTabWidget;
class QToolButton;
class QPlainTextEdit;
namespace cordis::ui::detail {
struct WorkbenchControl;
/** @brief 首页只展示插件值状态，电源操作由宿主按实例身份重新校验。 */
struct HomePlugin {
    QString id, title, subtitle;
    bool canStop = false, canLoad = false, canRetry = false, busy = false;
    bool enabled = false;
    std::uint64_t revision = 0;
    QString identity{};
    bool configuration = false;
    StatusPresentation status{};
};
/** @brief 所有导航仍由 Shell 校验贡献及 epoch，刷新只调用宿主管理器。 */
struct WorkbenchActions {
    std::function<Result<void>(const QString&, bool)> navigate;
    std::function<void()> refresh;
    std::function<void()> empty;
    std::function<std::vector<HomePlugin>()> plugins;
    /** @brief 在控件写入期间保留根窗口，关闭任务等访问栈返回后才允许销毁。 */
    std::function<void(const std::function<void()>&)> withWindowAccess;
    std::function<Result<void>(const QString&, bool, std::uint64_t)> power;
    /** @brief 已由配置目录或内部宿主覆盖的贡献不重复生成卡片。 */
    std::function<bool(const QString&)> hideContribution;
    /** @brief 宿主提供通知历史视图入口，只有用户显式操作时打开。 */
    std::function<Result<void>()> showNotifications{};
    std::function<Result<void>(const QString&, bool)> managePlugin{};
    std::function<Result<void>()> addPlugin{};
    std::function<Result<void>()> configurationFile{};
    std::function<bool()> canAddPlugin{};
    std::function<bool()> hasConfigurationFile{};
};
/** @brief 创建 Example 风格滚动首页，入口在工作台构造时连接。 */
QWidget* createWelcomePage();
/** @brief 构造宿主菜单/搜索/历史与设置页，已有页签和底部面板继续由外壳拥有。 */
std::shared_ptr<WorkbenchControl> createWorkbench(ElaWindow* window, QTabBar* tabs, QTabWidget* panel,
                                                  QPlainTextEdit* output, QToolButton* togglePanel,
                                                  const QString& homeKey, const QString& managementKey,
                                                  WorkbenchActions actions, const QString& preferencesFile = {});
/** @brief 永久宿主页由 ElaWindow 接管，页面通过 key 加入同一导航。 */
QWidget* workbenchSettings(const std::shared_ptr<WorkbenchControl>& control);
/** @brief 关于页保留在中央工作区，不创建不受控独立窗口。 */
QWidget* workbenchAbout(const std::shared_ptr<WorkbenchControl>& control);
/** @brief 先登记完整键/标题，再刷新搜索结果；同名标题仍以不同 key 区分。 */
void registerWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key, const QString& title,
                           const QString& category = {}, const QString& plugin = {});
/** @brief 页面撤销同步裁剪历史、建议和页签，旧搜索结果不能恢复旧贡献。 */
void removeWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key);
/** @brief 所有鼠标/键盘/程序入口统一记录选择，历史重放不产生新分支。 */
void selectWorkbenchPage(const std::shared_ptr<WorkbenchControl>& control, const QString& key);
/** @brief 无活动页签时清空当前选择，可通过历史重新打开已关闭视图。 */
void selectWorkbenchEmpty(const std::shared_ptr<WorkbenchControl>& control);
/** @brief 同步关闭所有新入口，清除搜索弹层与快捷键，控件仍由窗口清理屏障销毁。 */
void closeWorkbench(const std::shared_ptr<WorkbenchControl>& control);
} // namespace cordis::ui::detail
