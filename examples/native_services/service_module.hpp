/** @file service_module.hpp
 * @brief 两个实际 Native 服务的固定契约与稳定代次记录，不拥有平台或装载策略。
 */
#pragma once
#include <eve/loader/definitions.hpp>
#include <eve/module/manager.hpp>
#include <eve/module/runtime.hpp>
#include <memory>
#include <optional>

namespace native_services {
enum class ServiceKind { Counter, Reporter };
/** @brief 宿主在 prepare 前持有记录；换代替换 shared_ptr，不改写旧记录的代次。
 * 半装配时 session 已写入本记录，调用方仍须显式关闭；析构不代替异步退出。
 */
struct CurrentModule {
    CurrentModule(ServiceKind service, eve::GenerationId id) : kind(service), generation(id) {}
    CurrentModule(const CurrentModule&) = delete;
    CurrentModule& operator=(const CurrentModule&) = delete;
    const ServiceKind kind;
    const eve::GenerationId generation;
    std::optional<eve::module::NativeModule> session;
    std::optional<eve::Runtime> runtime;
};
/** @brief 使用已装载的代次和调用方索引。调用方可在会话期间持有日志接收端。 */
eve::Result<void> prepareServiceModule(const std::shared_ptr<CurrentModule>& record,
                                          eve::module::ModuleManager& manager,
                                          std::shared_ptr<eve::IScheduler> scheduler,
                                          eve::ServiceRegistry services,
                                          std::shared_ptr<const eve::module::NativeLogSink> logs = {});
/** @brief 同一固定业务契约的配置定义；available 只弱查原代记录，不保留会话副本。 */
eve::Result<eve::loader::ConfiguredPlugin> configuredServicePlugin(const std::shared_ptr<CurrentModule>& record,
                                                                         eve::loader::PluginModule source);
} // namespace native_services
