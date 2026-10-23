/** @file ela_shell.hpp
 * @brief 声明宿主侧 Ela 外壳操作，插件接口仅使用 pages.hpp。
 */
#pragma once
#include <cordis/ui/pages.hpp>
#include <cordis/qt/scheduler.hpp>
#include <functional>
#include <memory>

namespace cordis::ui {
/** @brief 外壳只在应用线程操作；退出先 Kernel.close，再 shell.close，再停 scheduler。 */
class ElaShell {
public:
    /** @brief 创建永久安全页，禁用可能持有已删除页面的动画和独立页面窗口。 */
    explicit ElaShell(std::shared_ptr<qt::QtScheduler> scheduler);
    /** @brief 不在不确定线程强制删除窗口；宿主必须显式等待 close。 */
    ~ElaShell();
    /** @brief 禁止复制外壳身份，服务及贡献句柄可共享。 */
    ElaShell(const ElaShell&) = delete;
    /** @brief 禁止覆盖仍有窗口与贡献的外壳。 */
    ElaShell& operator=(const ElaShell&) = delete;
    /** @brief 获取可注册为 ui.pages/v1 的服务；服务本身不拥有窗口。 */
    std::shared_ptr<UiPagesService> pages() const;
    /** @brief owner 显示实际 Ela 窗口。 */
    Result<void> show() const;
    /** @brief owner 设置异步退出入口；窗口关闭请求先交宿主，不直接 quit。 */
    Result<void> onCloseRequested(std::function<void()> callback) const;
    /** @brief owner 关门、等待全部页面 effects 与窗口完整销毁，清理失败保留资源。 */
    Task<void> close() const;
private:
    std::shared_ptr<detail::ShellControl> control_;
    std::shared_ptr<UiPagesService> pages_;
};
} // namespace cordis::ui
