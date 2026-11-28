/** @file panel_p.hpp
 * @brief 根窗口的快速打开浮层和底部问题视图。
 */
#pragma once
#include "output_p.hpp"
#include <QString>
#include <eve/ui/management.hpp>
#include <functional>
#include <vector>
class QListView;
class QTabWidget;
namespace eve::ui::detail
{
enum class QuickAccessMode { Pages, Commands };
struct QuickAccessPage { QString key, title, category; };

/** @brief 页面用稳定键导航，命令列表由现有服务与执行回执管理。 */
QWidget* createCommandPanel(QWidget* parent, QListView*& list);
void configureQuickAccessPages(QWidget* panel, std::function<std::vector<QuickAccessPage>()> pages,
                               std::function<void(const QString&)> activate);
void refreshQuickAccessPages(QWidget* panel);
void showQuickAccess(QWidget* panel, QWidget* anchor, QuickAccessMode mode);
void dismissQuickAccess(QWidget* panel);
void dismissCommandPanel(QWidget* list);
/** @brief 只读问题来自当前 diagnostics，激活问题定位到原插件的管理窗口。 */
void installProblemsPanel(QTabWidget* panel, std::shared_ptr<DiagnosticsService> diagnostics,
                          std::shared_ptr<OutputControl> output, WindowAccess access,
                          std::function<Result<void>(InstanceId, const QString&)> locate);
} // namespace eve::ui::detail
