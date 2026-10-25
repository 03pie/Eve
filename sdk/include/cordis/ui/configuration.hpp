/** @file configuration.hpp
 * @brief 可选配置页扩展；普通 Cordis::Ui SDK 不依赖 Loader，也不授予插件会话能力。
 */
#pragma once
#include <cordis/loader/session.hpp>
#include <cordis/ui/management.hpp>
#include <memory>
#include <string>
class QWidget;
namespace cordis::loader {
class QtConfigurationWatcher;
}
namespace cordis::ui {
/** @brief owner 创建无 parent 的 Ela 配置页；宿主须强持会话并用页面资源屏障销毁控件。 */
std::unique_ptr<QWidget> createConfigurationPage(const std::shared_ptr<loader::ConfigurationSession>& session,
                                                 std::string initialText = {},
                                                 std::shared_ptr<loader::QtConfigurationWatcher> watcher = {});
/** @brief 可选模块将 Session 适配为通用管理值协议，适配器弱持会话；宿主保留返回值。 */
std::shared_ptr<IConfigurationManagement>
createConfigurationManagement(const std::shared_ptr<loader::ConfigurationSession>& session,
                              std::shared_ptr<IScheduler> scheduler);
} // namespace cordis::ui
