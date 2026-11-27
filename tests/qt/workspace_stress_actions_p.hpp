/** @file workspace_stress_actions_p.hpp
 * @brief 两个具体动作 TU 共用的原 Result 等待及配置回执值，不提供新运行入口。
 */
#pragma once
#include "workspace_stress_p.hpp"

namespace workspace_stress_test::detail {
using namespace eve;
using namespace eve::loader;

template <class T> T checked(Result<T> value)
{
    if (!value.hasValue())
        throw ProductError(value.error());
    return std::move(value).value();
}
inline void checked(Result<void> value)
{
    if (!value.hasValue())
        throw ProductError(value.error());
}
template <class T> T await(const Task<T>& task, const Scene& scene)
{
    return checked(*platform_workspace_test::wait(task, scene.scheduler));
}
inline void require(bool condition, const std::string& message)
{
    if (!condition)
        throw InvalidTrace(message);
}
inline std::string argument(const Action& action, const char* key)
{
    return action.args.value(QString::fromLatin1(key)).toString().toUtf8().toStdString();
}
inline SessionSnapshot session(const Scene& scene)
{
    return checked(scene.state->session->snapshot());
}
inline std::shared_ptr<const ValidatedCandidate> desired(const Scene& scene)
{
    auto value = session(scene).coordinator.desired;
    require(static_cast<bool>(value), "action requires an accepted desired document");
    return value;
}
inline const EntryValue& entry(const std::shared_ptr<const ValidatedCandidate>& target, const std::string& path)
{
    const auto& entries = target->source().structure().entries();
    const auto found = entries.find(path);
    require(found != entries.end(), "configuration path is absent: " + path);
    return found->second;
}

QJsonObject plan(const PlanReport& value);
QJsonObject accepted(Scene& scene, Result<AcceptedTarget> value);
QJsonObject upgradeAction(Scene& scene, const QString& sourceBin, const Action& action);
} // namespace workspace_stress_test::detail
