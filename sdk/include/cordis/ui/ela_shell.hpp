/** @file ela_shell.hpp
 * @brief 声明宿主侧 Ela 外壳操作，插件通过页面、命令和通知服务协作。
 */
#pragma once
#include <cordis/qt/scheduler.hpp>
#include <cordis/ui/commands.hpp>
#include <cordis/ui/management.hpp>
#include <cordis/ui/notifications.hpp>
#include <cordis/ui/output.hpp>
#include <cordis/ui/pages.hpp>
#include <functional>
#include <memory>
#include <string>

namespace cordis::ui {
/** @brief 外壳只在应用线程操作；退出先 Kernel.close，再 shell.close，再停 scheduler。 */
class ElaShell {
public:
    /** @brief 创建 Ela 安全页；可选 UTF-8 偏好文件只保存外观/布局，空路径不落盘。 */
    explicit ElaShell(std::shared_ptr<qt::QtScheduler> scheduler, std::string preferencesFile = {});
    /** @brief 不在不确定线程强制删除窗口；宿主必须显式等待 close。 */
    ~ElaShell();
    /** @brief 禁止复制外壳身份，服务及贡献句柄可共享。 */
    ElaShell(const ElaShell&) = delete;
    /** @brief 禁止覆盖仍有窗口与贡献的外壳。 */
    ElaShell& operator=(const ElaShell&) = delete;
    /** @brief 获取可注册为 ui.pages/v1 的服务；服务本身不拥有窗口。 */
    std::shared_ptr<UiPagesService> pages() const;
    /** @brief 获取可注册为 ui.commands/v1 的命令贡献服务。 */
    std::shared_ptr<CommandService> commands() const;
    /** @brief 获取可注册为 ui.notifications/v1 的纯值通知服务。 */
    std::shared_ptr<NotificationService> notifications() const;
    /** @brief ui.output/v1，按插件实例/代次隔离的有界输出。 */
    std::shared_ptr<OutputService> output() const;
    /** @brief 仅宿主写入平台输出；插件使用带 Context 的 OutputService。 */
    Result<std::uint64_t> writeOutput(std::string text, OutputLevel level = OutputLevel::Info) const;
    /** @brief 获取仅宿主持有的管理入口；绑定 Kernel 后可刷新、停止和重试实例。 */
    std::shared_ptr<ManagementController> management() const;
    /** @brief 获取可注册为 diagnostics/v1 的只读值快照服务。 */
    std::shared_ptr<DiagnosticsService> diagnostics() const;
    /** @brief owner 打开宿主诊断窗口，不占中央页签；部分插件缺依赖仍可进入。 */
    Result<void> showManagement() const;
    /** @brief owner 显示实际 Ela 窗口。 */
    Result<void> show() const;
    /** @brief owner 设置异步退出入口；窗口关闭请求先交宿主，不直接 quit。 */
    Result<void> onCloseRequested(std::function<void()> callback) const;
    /** @brief 宿主绑定平台配置文件窗口入口；普通插件仍通过页面服务贡献内容。 */
    Result<void> onConfigurationRequested(std::function<Result<void>()> callback) const;
    /** @brief 宿主保存失败而保留窗口时恢复关闭按钮；已经开始销毁后不能撤销关闭。 */
    Result<void> cancelCloseRequest() const;
    /** @brief owner 关门，等待页面/命令 effects 与窗口完整销毁，清理失败保留资源。 */
    Task<void> close() const;
private:
    std::shared_ptr<detail::ShellControl> control_;
    std::shared_ptr<UiPagesService> pages_;
    std::shared_ptr<CommandService> commands_;
    std::shared_ptr<NotificationService> notifications_;
    std::shared_ptr<OutputService> output_;
    std::shared_ptr<ManagementController> management_;
    std::shared_ptr<DiagnosticsService> diagnostics_;
};
} // namespace cordis::ui
