/** @file workspace_stress_p.hpp
 * @brief 原工作台有限动作与纯值证据；不建立事务、配置或运行资源的第二拥有者。
 */
#pragma once
#include "platform_upgrade_p.hpp"
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace workspace_stress_test {
using platform_upgrade_test::Scene;

struct Action {
    QString id, kind;
    QJsonObject args;
};

/** @brief 保存的动作无法按原前置条件执行，不是产品故障复现。 */
struct InvalidTrace final : std::runtime_error {
    explicit InvalidTrace(const std::string& message) : std::runtime_error(message) {}
};

/** @brief 原 Result 的失败保留完整 ErrorInfo，入口按原错误树保存。 */
struct ProductError final : std::runtime_error {
    cordis::ErrorInfo reason;
    explicit ProductError(cordis::ErrorInfo value) : std::runtime_error(value.message), reason(std::move(value)) {}
};

QString decimal(std::uint64_t value);
QJsonObject error(const cordis::ErrorInfo& value);
void validateAction(const Action& action);
/** @brief 普通动作之外收敛原队列/Store/DeferredDelete，不执行或保存任何新目标。 */
void settle(Scene& scene);
/** @brief 执行已经保存的完整动作，不接受随机源；目标原生故障在证据中返回签名。 */
QJsonObject execute(Scene& scene, const QString& sourceBin, const Action& action);
QJsonObject observe(const Scene& scene);
void verifySettled(const Scene& scene);
} // namespace workspace_stress_test
