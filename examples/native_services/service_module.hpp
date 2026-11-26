/** @file service_module.hpp
 * @brief 两个实际 Native 服务的固定契约与稳定代次记录，不拥有平台或装载策略。
 */
#pragma once
#include <cordis/loader/definitions.hpp>
#include <cordis/module/manager.hpp>
#include <cordis/module/runtime.hpp>
#include <memory>
#include <optional>

namespace native_services {
enum class ServiceKind { Counter, Reporter };
/** @brief 宿主在 prepare 前持有记录；换代替换 shared_ptr，不改写旧记录的代次。
 * 半装配时 session 已写入本记录，调用方仍须显式关闭；析构不代替异步退出。
 */
struct CurrentModule {
    CurrentModule(ServiceKind service, cordis::GenerationId id) : kind(service), generation(id) {}
    CurrentModule(const CurrentModule&) = delete;
    CurrentModule& operator=(const CurrentModule&) = delete;
    const ServiceKind kind;
    const cordis::GenerationId generation;
    std::optional<cordis::module::NativeModule> session;
    std::optional<cordis::Runtime> runtime;
};
/** @brief 使用已装载的代次和调用方索引。调用方可在会话期间持有日志接收端。 */
cordis::Result<void> prepareServiceModule(const std::shared_ptr<CurrentModule>& record,
                                          cordis::module::ModuleManager& manager,
                                          std::shared_ptr<cordis::IScheduler> scheduler,
                                          cordis::ServiceRegistry services,
                                          std::shared_ptr<const cordis::module::NativeLogSink> logs = {});
/** @brief 同一固定业务契约的配置定义；available 只弱查原代记录，不保留会话副本。 */
cordis::Result<cordis::loader::ConfiguredPlugin> configuredServicePlugin(const std::shared_ptr<CurrentModule>& record,
                                                                         cordis::loader::PluginModule source);
} // namespace native_services
