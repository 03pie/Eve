/** @file configuration.hpp
 * @brief 可选配置页扩展；普通 Cordis::Ui SDK 不依赖 Loader，也不授予插件会话能力。
 */
#pragma once
#include <cordis/loader/session.hpp>
#include <cordis/ui/management.hpp>
#include <cordis/ui/output.hpp>
#include <functional>
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
/** @brief 宿主接入本页操作结果与定位入口，记录仅含动作、目标修订及结果码。 */
void bindConfigurationFeedback(QWidget* page, std::function<std::uint64_t(std::string, OutputLevel)> publish,
                               std::function<void(std::uint64_t, QWidget*)> locate);
void bindConfigurationFeedback(QWidget* page,
                               std::function<std::uint64_t(std::string, OutputLevel, OutputDiagnostic)> publish,
                               std::function<void(std::uint64_t, QWidget*)> locate);
/** @brief 可选模块将 Session 适配为通用管理值协议，适配器弱持会话；宿主保留返回值。 */
std::shared_ptr<IConfigurationManagement>
createConfigurationManagement(const std::shared_ptr<loader::ConfigurationSession>& session,
                              std::shared_ptr<IScheduler> scheduler);
} // namespace cordis::ui
