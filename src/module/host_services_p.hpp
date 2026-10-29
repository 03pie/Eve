/** @file host_services_p.hpp
 * @brief Core Context 与 C 服务函数族之间的私有适配，不暴露模块指针。
 */
#pragma once
#include "host_resources_p.hpp"

namespace cordis::module::detail {
struct NativeProvider;
CordisStatus nativeWireError(const ErrorInfo& error);
struct NativeServices : std::enable_shared_from_this<NativeServices> {
    explicit NativeServices(std::weak_ptr<NativeInstanceControl> instance) : owner(std::move(instance)) {}
    Result<void> bind(const Context& context, const NativePlugin& plugin);
    CordisStatus provide(CordisHandle effect, const CordisServiceKeyV1* key, const CordisServiceMetadataV1* metadata,
                         CordisHandle token, CordisHandle* output);
    CordisStatus withdraw(CordisHandle binding, CordisRequestId* output);
    CordisStatus invoke(const CordisServiceKeyV1* key, std::uint32_t minor, CordisBytesV1 capabilities,
                        CordisBytesV1 method, CordisBytesV1 json, CordisRequestId* output);

private:
    std::weak_ptr<NativeInstanceControl> owner;
    std::optional<Context> context;
    NativePlugin definition;
    std::map<ServiceKey, ServiceRef<NativeService>> consumers;
    std::map<CordisHandle, std::shared_ptr<NativeProvider>> providers;
    Task<void> retire(const std::shared_ptr<NativeProvider>& provider);
    NativeServiceCall call(const std::shared_ptr<NativeProvider>& provider, std::string method, std::string json);
};
} // namespace cordis::module::detail
