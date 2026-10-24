/** @file services_p.hpp
 * @brief 外壳与命令/通知后端的宿主私有桥，不进入插件 SDK。
 */
#pragma once
#include <cordis/qt/scheduler.hpp>
#include <cordis/task.hpp>
#include <memory>

class QLabel;
class QPlainTextEdit;
class QListView;
namespace cordis::ui::detail {
struct ServicesControl;
/** @brief 接入宿主持有的展示控件；控件信号只捕获宿主弱记录。 */
std::shared_ptr<ServicesControl> createUiServices(std::shared_ptr<qt::QtScheduler> scheduler,
                                               QListView* commands, QLabel* notification, QPlainTextEdit* output);
/** @brief 同步关门，再等待全部命令 effect 完整排空；通知只有值数据，无插件清理回调。 */
Task<void> closeUiServices(const std::shared_ptr<ServicesControl>& control);
} // namespace cordis::ui::detail
