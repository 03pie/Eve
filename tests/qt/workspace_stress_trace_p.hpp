/** @file workspace_stress_trace_p.hpp
 * @brief 有限动作文件与实时执行证据分开；回放不接受随机源。
 */
#pragma once
#include "workspace_stress_p.hpp"
#include <QFile>
#include <QJsonArray>
#include <vector>

namespace workspace_stress_test {
struct Trace {
    QString seed;
    QJsonArray artifacts;
    std::vector<Action> actions;
    QString generator = "splitmix64/1";
};

std::uint64_t parseDecimal(const QJsonValue& value);
QJsonArray artifactInputs(const QString& sourceBin);
void saveTrace(const Trace& trace, const QString& path);
Trace loadTrace(const QString& path, const QString& sourceBin);
Trace generateTrace(const QString& sourceBin, const QString& seed);

class Evidence {
public:
    explicit Evidence(const QString& path);
    void append(QJsonObject event);

private:
    QFile file_;
    std::uint64_t sequence_ = 0;
};
} // namespace workspace_stress_test
