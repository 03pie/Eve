/** @file retirement_p.hpp
 * @brief 实际模块范围的纯值检查与原目录回收序列；不依赖界面或历史权限。
 */
#pragma once
#include "workspace.hpp"

namespace notebook::detail {
eve::Result<eve::module::ModuleGenerationSnapshot> retirementTarget(const WorkspaceModulesSnapshot& snapshot,
                                                                          eve::GenerationId generation);
WorkspaceRetirementReport attemptRetirement(eve::module::ModuleManager& manager,
                                            const eve::module::ModuleGenerationSnapshot& target);
} // namespace notebook::detail
