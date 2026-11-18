/** @file service_module.cpp
 * @brief 原 Native 示例与统一工作台共用真实服务契约，部分准备仍由原记录拥有。
 */
#include "service_module.hpp"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace native_services {
namespace {
using namespace cordis;
using namespace cordis::module;

bool supported(ServiceKind kind)
{
    return kind == ServiceKind::Counter || kind == ServiceKind::Reporter;
}
NativePlugin contract(ServiceKind kind)
{
    if (kind == ServiceKind::Counter)
        return {"example.counter", {}, {{{"example.counter", 1}, true, {1, {"next"}}}}};
    return {
        "example.reporter", {{{"example.counter", 1}, 1, {"next"}}}, {{{"example.report", 1}, true, {1, {"read"}}}}};
}
std::function<Result<void>()> available(const std::shared_ptr<CurrentModule>& record)
{
    const std::weak_ptr<const CurrentModule> weak = record;
    return [weak] {
        const auto module = weak.lock();
        if (!module || !module->session)
            return Result<void>::failure(makeError(ErrorCode::ContextClosing, "module unavailable"));
        const auto state = module->session->snapshot();
        if (!state.hasValue())
            return Result<void>::failure(state.error());
        return state.value().closing || state.value().closed || state.value().faulted
                   ? Result<void>::failure(makeError(ErrorCode::ContextClosing, "module unavailable"))
                   : Result<void>::success();
    };
}
} // namespace

cordis::Result<void> prepareServiceModule(const std::shared_ptr<CurrentModule>& record,
                                          cordis::module::ModuleManager& manager,
                                          std::shared_ptr<cordis::IScheduler> scheduler,
                                          cordis::ServiceRegistry services)
{
    using namespace cordis;
    const auto current = record;
    if (!current || !current->generation.valid() || !supported(current->kind))
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "service module record invalid"));
    if (current->session || current->runtime)
        return Result<void>::failure(makeError(ErrorCode::ConfigConflict, "service module already prepared"));
    auto session = module::NativeModule::open(manager, current->generation, std::move(scheduler), std::move(services));
    if (!session.hasValue())
        return Result<void>::failure(session.error());
    current->session.emplace(std::move(session).value());
    auto runtime = current->session->runtime(contract(current->kind));
    if (!runtime.hasValue())
        return Result<void>::failure(runtime.error());
    current->runtime.emplace(std::move(runtime).value());
    return Result<void>::success();
}
cordis::Result<cordis::loader::ConfiguredPlugin> configuredServicePlugin(const std::shared_ptr<CurrentModule>& record,
                                                                         cordis::loader::PluginModule source)
{
    using namespace cordis;
    using namespace cordis::loader;
    using Output = Result<ConfiguredPlugin>;
    if (!record || !record->generation.valid() || !supported(record->kind) || !record->session || !record->runtime ||
        !record->runtime->id().valid())
        return Output::failure(makeError(ErrorCode::InvalidArgument, "service module not prepared"));
    const auto plugin = contract(record->kind);
    std::vector<InjectSpec> dependencies;
    for (const auto& dependency : plugin.dependencies)
        dependencies.push_back(
            {dependency.key.name, dependency.key.interfaceMajor, dependency.minimumMinor, dependency.capabilities});
    const char* failure = record->kind == ServiceKind::Counter ? "{\"failCleanup\":true}" : "{\"failStart\":true}";
    return Output::success({{plugin.id, std::move(dependencies),
                             [failure](const std::string& config) {
                                 return config == "null" || config == failure
                                            ? Result<void>::success()
                                            : Result<void>::failure(
                                                  makeError(ErrorCode::ConfigInvalid, "unsupported example config"));
                             },
                             std::move(source)},
                            *record->runtime,
                            {},
                            available(record),
                            128});
}
} // namespace native_services
