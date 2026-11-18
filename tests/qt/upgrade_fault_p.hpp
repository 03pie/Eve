/** @file upgrade_fault_p.hpp
 * @brief 真实故障测试仅共享纯查询和原拥有链断言，不拥有第二事务。
 */
#pragma once
#include "upgrade_view_p.hpp"
#include <QComboBox>
#include <QTabWidget>

namespace upgrade_fault_test {
using namespace upgrade_view_test;
QString faultPath(const QString& sourceBin, const std::string& name);
QPlainTextEdit* editor(const Scene& scene, InstanceId instance);
bool containsError(const ErrorInfo& error, ErrorCode code);
bool containsError(const ReplacementReport& report, ErrorCode code);
WorkspaceUpgradeReport retained(View& view);
void retainOwners(Scene& scene);
void closeExpectedFailure(Scene& scene, GenerationId generation, const std::string& phase);
bool stateCase(const QString& name, const QString& sourceBin);
bool stopCase(const QString& name, const QString& sourceBin);
bool nativeCase(const QString& name, const QString& sourceBin);
bool s09Case(const QString& name, const QString& sourceBin);
bool retirementCase(const QString& name, const QString& sourceBin);
} // namespace upgrade_fault_test
