/** @file workspace_stress_child_p.hpp
 * @brief 三个具体资格入口共用的独立进程回执；只保存文件、PID和原JSON值。
 */
#pragma once
#include "workspace_stress_trace_p.hpp"

namespace workspace_stress_test {
struct ChildResult {
    int exitCode = -1;
    QString status, signature, inputPath, evidencePath, processLog, inputSha256, evidenceSha256, executableSha256;
    QString scratchPath, harnessFailure;
    std::uint64_t pid = 0;
    QJsonArray events;
};
ChildResult generateChild(const QString& sourceBin, const QString& seed, const QString& prefix);
ChildResult replayChild(const QString& sourceBin, const QString& inputPath, const QString& prefix);
QJsonObject childSummary(const ChildResult& value);
void replayProof(const QString& sourceBin, const QString& directory);
void invalidTraceProof(const QString& sourceBin, const QString& directory);
void reductionProof(const QString& sourceBin, const QString& directory);
} // namespace workspace_stress_test
