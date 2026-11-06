/** @file plan.cpp
 * @brief 将真实工厂和拥有者纯值描述合成唯一构造计划，不执行或登记运行对象。
 */
#include "../core/service_contract_p.hpp"
#include "operation_p.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace cordis::detail {
namespace {
Result<ReplacementServiceContract> normalize(ReplacementServiceContract value, const RealmMap& realms)
{
    using Output = Result<ReplacementServiceContract>;
    try {
        value.requirements = mergeDependencies(value.requirements, {}, realms);
        std::set<ServiceKey> keys;
        for (auto& output : value.outputs) {
            output.key = realms.resolve(output.key);
            if (!validServiceDescriptor(output.key, output.type, output.metadata.capabilities) ||
                !keys.insert(output.key).second)
                return Output::failure(replacementError(ErrorCode::ConfigInvalid, "invalid future service output"));
            std::sort(output.metadata.capabilities.begin(), output.metadata.capabilities.end());
        }
        std::sort(value.outputs.begin(), value.outputs.end(),
                  [](const auto& a, const auto& b) { return a.key < b.key; });
        return Output::success(std::move(value));
    } catch (const std::invalid_argument& error) {
        return Output::failure(replacementError(ErrorCode::ConfigInvalid, error.what()));
    }
}
bool sameKey(const ServiceKey& a, const ServiceKey& b)
{
    return !(a < b) && !(b < a);
}
} // namespace
Result<ReplacementServiceContract>
serviceContract(const Runtime& runtime, const std::vector<ServiceRequirement>& additional, const RealmMap& realms)
{
    auto requirements = runtime.requirements();
    requirements.insert(requirements.end(), additional.begin(), additional.end());
    return normalize({std::move(requirements), runtime.outputs()}, realms);
}
Result<ReplacementServiceContract> serviceContract(const ReplacementInput& input)
{
    return normalize({input.dependencies, input.outputs}, input.realms);
}
bool sameContract(const ReplacementServiceContract& a, const ReplacementServiceContract& b)
{
    if (a.requirements.size() != b.requirements.size() || a.outputs.size() != b.outputs.size())
        return false;
    for (std::size_t i = 0; i != a.requirements.size(); ++i) {
        const auto& x = a.requirements[i];
        const auto& y = b.requirements[i];
        if (!sameKey(x.key, y.key) || x.type != y.type || x.minimumMinor != y.minimumMinor ||
            x.capabilities != y.capabilities)
            return false;
    }
    for (std::size_t i = 0; i != a.outputs.size(); ++i) {
        const auto& x = a.outputs[i];
        const auto& y = b.outputs[i];
        if (!sameKey(x.key, y.key) || x.type != y.type || x.required != y.required ||
            x.metadata.minor != y.metadata.minor || x.metadata.capabilities != y.metadata.capabilities)
            return false;
    }
    return true;
}
Result<void> planConstruction(ReplacementBuildPlan& plan, const std::vector<ReplacementDynamicMember>& dynamic,
                              std::size_t& nextKey)
{
    auto fail = [](const char* message) {
        return Result<void>::failure(replacementError(ErrorCode::ConfigConflict, message));
    };
    std::map<std::size_t, std::size_t> keys, positions;
    for (std::size_t i = 0; i != plan.members.size(); ++i) {
        auto& item = plan.members[i];
        keys.emplace(item.key, item.key);
        positions.emplace(item.key, i);
        if (item.factory) {
            auto value = serviceContract(*item.factory, item.additionalDependencies, item.realms);
            if (!value.hasValue())
                return Result<void>::failure(value.error());
            item.contract = std::move(value).value();
        }
    }
    std::set<std::size_t> described;
    for (const auto& item : dynamic) {
        if (!described.insert(item.key).second)
            return fail("duplicate dynamic member key");
        if (positions.count(item.key)) {
            const auto& original = plan.members[positions.at(item.key)];
            if (original.factory || item.parent != original.parent)
                return fail("owner cannot override canonical factory or actual parent");
        } else {
            if (!item.parent || nextKey == std::numeric_limits<std::size_t>::max())
                return fail("new dynamic member needs an owned parent and available key");
            keys.emplace(item.key, nextKey++);
        }
    }
    for (const auto& item : dynamic) {
        if (!item.parent || !keys.count(*item.parent))
            return fail("dynamic parent is outside its owning unit");
        auto value = normalize(item.contract, item.realms);
        if (!value.hasValue())
            return Result<void>::failure(value.error());
        const auto parent = keys.at(*item.parent);
        if (!positions.count(item.key)) {
            positions.emplace(item.key, plan.members.size());
            plan.members.push_back({keys.at(item.key), parent, {}, {}, item.config, item.realms, {}});
        }
        auto& input = plan.members[positions.at(item.key)];
        if (plan.recovery && input.previous) {
            auto original = serviceContract(*input.previous);
            if (!original.hasValue())
                return Result<void>::failure(original.error());
            if (item.config != input.previous->config ||
                item.realms.overrides() != input.previous->realms.overrides() ||
                !sameContract(value.value(), original.value()))
                return fail("recovery dynamic member differs from original input");
        }
        input.source = item.source;
        input.config = item.config;
        input.realms = item.realms;
        input.parent = parent;
        input.additionalDependencies = item.contract.requirements;
        input.contract = std::move(value).value();
    }
    for (const auto& item : plan.members)
        if (!item.contract)
            return fail("owner omitted future dynamic contract");
    return Result<void>::success();
}
} // namespace cordis::detail
