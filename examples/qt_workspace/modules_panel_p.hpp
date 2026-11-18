/** @file modules_panel_p.hpp
 * @brief 宿主模块视图只消费纯目录值，格式化不触碰控件或业务。
 */
#pragma once
#include "workspace.hpp"
#include <QString>

namespace notebook::detail {
QString moduleDiagnosticsText(const WorkspaceModulesSnapshot& snapshot);
}
