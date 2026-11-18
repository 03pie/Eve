/** @file platform_upgrade_p.hpp
 * @brief 同一真实 Workspace 的有限代码替换检查；不持有旧代能力的值断言。
 */
#pragma once
#include "platform_workspace_p.hpp"

namespace platform_upgrade_test {
using namespace platform_workspace_test;
using notebook::UpgradePreview;
using notebook::WorkspaceUpgradeReport;

RunningEntry running(const Scene& scene, const std::string& plugin);
ModuleGenerationSnapshot module(const Scene& scene, GenerationId generation);
UpgradePreview preview(Scene& scene, const QString& sourceBin, const std::string& alias,
                       const std::string& variant = "b");
WorkspaceUpgradeReport perform(Scene& scene, const UpgradePreview& value);
void applied(Scene& scene, const UpgradePreview& value, const WorkspaceUpgradeReport& report);
void finish(Scene& scene);
std::string counterValue(Scene& scene);
void reporterValue(const Scene& scene, const std::string& value);

bool nativeCase(const QString& name, const QString& sourceBin);
bool qtCase(const QString& name, const QString& sourceBin);
bool contractCase(const QString& name, const QString& sourceBin);
} // namespace platform_upgrade_test
