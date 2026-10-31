/** @file runtime.cpp
 * @brief 在既有原生桥之上创建 Core 托管 Runtime，不替换低层 create。
 */
#include "admission_p.hpp"
#include "host_services_p.hpp"
#include "json_p.hpp"
#include <algorithm>
#include <stdexcept>

namespace cordis::module {
using namespace detail;
ServiceRequirement nativeRequirement(NativeDependency dependency)
{
    return ServiceRequirement::of<NativeServiceEndpoint>(std::move(dependency.key), dependency.minimumMinor,
                                                         std::move(dependency.capabilities));
}
Result<Runtime> NativeModule::runtime(NativePlugin plugin) const
{
    using Output = Result<Runtime>;
    auto root = control_;
    if (!root->ownerThread())
        return Output::failure(hostError(ErrorCode::WrongThread, "native runtime requires owner"));
    auto scheduler = root->scheduler.lock();
    if (!scheduler || !scheduler->isOpen())
        return Output::failure(hostError(ErrorCode::SchedulerClosed, "native scheduler closed"));
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        if (root->closing || root->faulted || root->closed)
            return Output::failure(hostError(ErrorCode::ContextClosing, "native module unavailable"));
    }
    const auto& api = *root->record->api;
    auto admission = admitGeneration(root->source.generation);
    if (!admission.hasValue())
        return Output::failure(admission.error());
    if (std::find(api.plugins.begin(), api.plugins.end(), plugin.id) == api.plugins.end() ||
        plugin.dependencies.size() > 128 || plugin.outputs.size() > 128 ||
        ((!plugin.dependencies.empty() || !plugin.outputs.empty()) && !(api.table.capabilities & CORDIS_CAP_SERVICES)))
        return Output::failure(hostError(ErrorCode::InvalidArgument, "invalid native plugin contract"));
    auto valid = [](const ServiceKey& key, const std::vector<std::string>& caps) {
        if (key.name.size() > CORDIS_MAX_ID_BYTES || !validAbiUtf8(key.name) || caps.size() > 64)
            return false;
        return std::all_of(caps.begin(), caps.end(), [](const std::string& value) {
            return value.size() <= CORDIS_MAX_ID_BYTES && validAbiUtf8(value);
        });
    };
    std::vector<ServiceRequirement> requirements;
    std::vector<ServiceOutput> outputs;
    for (auto& dependency : plugin.dependencies) {
        if (!valid(dependency.key, dependency.capabilities))
            return Output::failure(
                hostError(ErrorCode::InvalidArgument, "invalid native dependency budget or encoding"));
        requirements.push_back(nativeRequirement(std::move(dependency)));
    }
    for (auto& output : plugin.outputs) {
        if (!valid(output.key, output.metadata.capabilities))
            return Output::failure(hostError(ErrorCode::InvalidArgument, "invalid native output budget or encoding"));
        outputs.push_back(ServiceOutput::of<NativeServiceEndpoint>(std::move(output.key), output.required,
                                                                   std::move(output.metadata)));
    }
    try {
        auto module = *this;
        return Output::success(Runtime(
            [module, id = std::move(plugin.id)](Context context) {
                auto identity = context.identity();
                if (!identity.hasValue())
                    return failedTask(identity.error());
                auto created = module.createBound(id, context.config().empty() ? "null" : context.config(),
                                                  identity.value().instance, identity.value().epoch, &context);
                return created.hasValue() ? created.value().start() : failedTask(created.error());
            },
            std::move(requirements), std::move(outputs)));
    } catch (const std::invalid_argument& error) {
        return Output::failure(hostError(ErrorCode::InvalidArgument, error.what()));
    }
}
} // namespace cordis::module
