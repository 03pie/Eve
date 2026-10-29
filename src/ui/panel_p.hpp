/** @file panel_p.hpp
 * @brief 底部命令及真实问题视图，不把面板内容送入中央页面。
 */
#pragma once
#include "output_p.hpp"
#include <QString>
#include <cordis/ui/management.hpp>
class QListView;
class QTabWidget;
namespace cordis::ui::detail {
/** @brief 创建搜索、命令列表、执行按钮和结果区；后台由 services 接入。 */
QWidget* createCommandPanel(QWidget* parent, QListView*& list);
/** @brief 只读问题来自当前 diagnostics，激活问题定位到原插件的管理窗口。 */
void installProblemsPanel(QTabWidget* panel, std::shared_ptr<DiagnosticsService> diagnostics,
    std::shared_ptr<OutputControl> output, WindowAccess access,
    std::function<Result<void>(InstanceId, const QString&)> locate);
} // namespace cordis::ui::detail
