/** @file output_p.hpp
 * @brief 宿主输出路由及目标选择控件；插件只使用纯值 OutputService。
 */
#pragma once
#include <eve/qt/scheduler.hpp>
#include <eve/ui/output.hpp>
#include <functional>
#include <QString>
class QPlainTextEdit;
class QTabWidget;
class QWidget;
namespace eve::module { struct NativeLogEvent; }
namespace eve::ui::detail
{
struct OutputControl;
struct OutputEndpoint;
std::weak_ptr<OutputEndpoint> outputEndpoint(const std::shared_ptr<OutputControl>& control);
class WorkbenchReturnPoint;
class RuntimeLogArchive;
QPlainTextEdit* createOutputEditor(QWidget* parent);
Result<void> setOutputArchive(const std::shared_ptr<OutputControl>& control, std::shared_ptr<RuntimeLogArchive> archive);
void setOutputSourceResolver(const std::shared_ptr<OutputControl>& control, std::function<QString(InstanceId)> resolve);
void setOutputConfigurationResolver(const std::shared_ptr<OutputControl>& control,
                                    std::function<QString(ContextIdentity)> resolve);
Result<void> registerQtOutputRuntime(const std::shared_ptr<OutputControl>& control, RuntimeId runtime,
                                     GenerationId generation, const std::string& moduleId,
                                     const std::string& moduleVersion, const std::string& pluginId);
void showOutput(const std::shared_ptr<OutputControl>& control, std::optional<std::uint64_t> sequence = {},
                InstanceId instance = {}, QWidget* returnFocus = nullptr, const QString& runId = {},
                const QString& configurationPath = {}, quint64 eventId = 0);
void showPreviousOutput(const std::shared_ptr<OutputControl>& control, QWidget* returnFocus);
void showNotificationOutput(const std::shared_ptr<OutputControl>& control, std::uint64_t sequence,
                            const QString& runId, WorkbenchReturnPoint origin, quint64 eventId);
void toggleOutputPanel(const std::shared_ptr<OutputControl>& control);
void openOutputDirectory(const std::shared_ptr<OutputControl>& control);
using WindowAccess = std::function<void(const std::function<void()>&)>;
std::shared_ptr<OutputControl> createOutput(std::shared_ptr<qt::QtScheduler> scheduler, QPlainTextEdit* editor,
                                            WindowAccess access);
/** @brief 平台/适配器写入已复制的记录，不允许插件伪造平台身份。 */
Result<std::uint64_t> appendOutput(const std::shared_ptr<OutputControl>& control, std::optional<ContextIdentity> owner,
                                   std::string channel, std::string text, OutputLevel level = OutputLevel::Info,
                                   bool render = true, std::optional<OperationId> notification = {},
                                   OutputDiagnostic diagnostic = {});
Result<std::uint64_t> appendNativeOutput(const std::shared_ptr<OutputControl>& control, const module::NativeLogEvent& event);
/** @brief 结算原组已决定的值；false 不操作控件或调用窗口访问回调。 */
void settleOutput(const std::shared_ptr<OutputControl>& control, bool render = true);
/** @brief 仅刷新已接纳纯值的 dirty 投影，允许排队观察在来源释放后继续刷新。 */
void refreshOutput(const std::shared_ptr<OutputControl>& control);
void installOutputTools(const std::shared_ptr<OutputControl>& control, QTabWidget* panel);
/** @brief 复制当前目标的全部保留内容，不受显示窗口的行数限制。 */
QString currentOutputText(const std::shared_ptr<OutputControl>& control);
/** @brief 在发出任何控件信号前清空捕获的目标，容忍显示期间切换目标。 */
void clearOutput(const std::shared_ptr<OutputControl>& control);
void closeOutput(const std::shared_ptr<OutputControl>& control);
} // namespace eve::ui::detail
