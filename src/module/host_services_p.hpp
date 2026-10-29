/** @file host_services_p.hpp
 * @brief 原生服务使用 Core 绑定、committed 租约及宿主值，不向 DLL 暴露 C++ 对象。
 */
#pragma once
#include "host_p.hpp"

namespace cordis::module::detail {
struct NativeBinding;
struct NativeCommit;
struct HostOperation;
struct NativeServiceEndpoint : std::enable_shared_from_this<NativeServiceEndpoint> {
    std::weak_ptr<NativeInstanceControl> owner;
    CordisHandle token = 0, effect = 0;
    ServiceMetadata metadata;
    ModuleLease lease;
    std::function<void()> retired;
    ~NativeServiceEndpoint();
    void detach();
};
struct NativeServices : std::enable_shared_from_this<NativeServices> {
    explicit NativeServices(std::weak_ptr<NativeInstanceControl> instance) : owner(std::move(instance)) {}
    static bool uses(const ServiceRegistry& registry, const std::shared_ptr<IScheduler>& scheduler);
    static bool same(const ServiceRegistry& first, const ServiceRegistry& second);
    Result<void> bind(const Context& context);
    CordisStatus add(CordisHandle effect, const CordisServiceKeyV1* key, const CordisServiceMetadataV1* metadata,
                     CordisHandle token, CordisHandle* output);
    CordisStatus withdraw(CordisHandle binding, CordisRequestId* output);
    CordisStatus invoke(const CordisServiceKeyV1* key, std::uint32_t minor, CordisBytesV1 capabilities,
                        CordisBytesV1 method, CordisBytesV1 payload, CordisRequestId* output);
    void publishReady();
    void retireAll();
    Task<void> retireEffect(CordisHandle effect);
    bool busy() const;
    bool empty() const;
    void releaseConsumers();
    std::vector<BindingSnapshot> snapshot() const;
    std::size_t committed() const { return consumers.size(); }

private:
    std::weak_ptr<NativeInstanceControl> owner;
    std::map<CordisHandle, std::shared_ptr<NativeBinding>> providers;
    std::map<ServiceKey, std::shared_ptr<NativeCommit>> consumers;
    bool released = false;
    ServiceRegistry& index();
    void retire(const std::shared_ptr<NativeBinding>& binding);
    void call(const std::shared_ptr<HostOperation>& operation);
};
} // namespace cordis::module::detail
