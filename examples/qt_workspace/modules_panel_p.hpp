/** @file modules_panel_p.hpp
 * @brief 宿主模块视图只消费纯目录值，格式化不触碰控件或业务。
 */
#pragma once
#include "workspace.hpp"
#include <QString>

namespace notebook::detail {
QString moduleErrorText(const cordis::ErrorInfo& error, unsigned depth = 0);
QString moduleDiagnosticsText(const WorkspaceModulesSnapshot& snapshot);
QString upgradePreviewText(const UpgradePreview& preview);
QString upgradeReportText(const WorkspaceUpgradeSnapshot& upgrade, const WorkspaceModulesSnapshot& modules,
                          const std::string& alias);
QString upgradePhaseText(const WorkspaceUpgradeSnapshot& upgrade);
bool upgradePhaseTerminal(cordis::ReplacementPhase phase);
QString upgradeSaveText(const WorkspaceModulesSnapshot& modules,
                        const cordis::Task<cordis::loader::SaveReport>::Snapshot& result, bool pending);
}
