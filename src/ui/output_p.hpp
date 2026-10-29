/** @file output_p.hpp
 * @brief 宿主输出路由及目标选择控件；插件只使用纯值 OutputService。
 */
#pragma once
#include <cordis/qt/scheduler.hpp>
#include <cordis/ui/output.hpp>
#include <functional>
#include <QString>
class QPlainTextEdit;
class QTabWidget;
class QWidget;
namespace cordis::ui::detail {
struct OutputControl;
using WindowAccess = std::function<void(const std::function<void()>&)>;
std::shared_ptr<OutputControl> createOutput(std::shared_ptr<qt::QtScheduler> scheduler,
                                           QPlainTextEdit* editor, WindowAccess access);
/** @brief 平台/适配器写入已复制的记录，不允许插件伪造平台身份。 */
Result<std::uint64_t> appendOutput(const std::shared_ptr<OutputControl>& control,
    std::optional<ContextIdentity> owner, std::string channel, std::string text, OutputLevel level = OutputLevel::Info);
void installOutputTools(const std::shared_ptr<OutputControl>& control, QTabWidget* panel);
/** @brief 复制当前目标的全部保留内容，不受显示窗口的行数限制。 */
QString currentOutputText(const std::shared_ptr<OutputControl>& control);
/** @brief 在发出任何控件信号前清空捕获的目标，容忍显示期间切换目标。 */
void clearOutput(const std::shared_ptr<OutputControl>& control);
void closeOutput(const std::shared_ptr<OutputControl>& control);
} // namespace cordis::ui::detail
