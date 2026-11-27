/** @file modules_panel_p.hpp
 * @brief 宿主模块视图只消费纯目录值，格式化不触碰控件或业务。
 */
#pragma once
#include "workspace.hpp"
#include <QString>

namespace notebook::detail {
QString moduleErrorText(const eve::ErrorInfo& error, unsigned depth = 0);
QString moduleDiagnosticsText(const WorkspaceModulesSnapshot& snapshot);
QString retirementStatusText(const WorkspaceModulesSnapshot& modules, eve::GenerationId selected,
                             const eve::Result<eve::module::ModuleGenerationSnapshot>& target,
                             eve::GenerationId attempted, const eve::Result<WorkspaceRetirementReport>* result);
QString upgradePreviewText(const UpgradePreview& preview);
QString upgradeReportText(const WorkspaceUpgradeSnapshot& upgrade, const WorkspaceModulesSnapshot& modules,
                          const std::string& alias);
QString upgradePhaseText(const WorkspaceUpgradeSnapshot& upgrade);
bool upgradePhaseTerminal(eve::ReplacementPhase phase);
QString upgradeSaveText(const WorkspaceModulesSnapshot& modules,
                        const eve::Task<eve::loader::SaveReport>::Snapshot& result, bool pending);
}
