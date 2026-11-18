/** @file retirement_p.hpp
 * @brief 实际模块范围的纯值检查与原目录回收序列；不依赖界面或历史权限。
 */
#pragma once
#include "workspace.hpp"

namespace notebook::detail {
cordis::Result<cordis::module::ModuleGenerationSnapshot> retirementTarget(const WorkspaceModulesSnapshot& snapshot,
                                                                          cordis::GenerationId generation);
WorkspaceRetirementReport attemptRetirement(cordis::module::ModuleManager& manager,
                                            const cordis::module::ModuleGenerationSnapshot& target);
} // namespace notebook::detail
