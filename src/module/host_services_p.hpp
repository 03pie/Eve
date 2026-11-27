/** @file host_services_p.hpp
 * @brief 原生服务使用 Core 绑定、committed 租约及宿主值，不向 DLL 暴露 C++ 对象。
 */
#pragma once
#include "host_p.hpp"

namespace eve::module::detail {
/** @brief 原生服务和 effect 准入共用同一 Core 错误映射。 */
EveStatus statusOf(const ErrorInfo&);
struct NativeBinding;
struct NativeCommit;
struct HostOperation;
struct NativeServiceEndpoint : std::enable_shared_from_this<NativeServiceEndpoint> {
    std::weak_ptr<NativeInstanceControl> owner;
    EveHandle token = 0, effect = 0;
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
    EveStatus add(EveHandle effect, const EveServiceKeyV1* key, const EveServiceMetadataV1* metadata,
                     EveHandle token, EveHandle* output);
    EveStatus withdraw(EveHandle binding, EveRequestId* output);
    EveStatus invoke(const EveServiceKeyV1* key, std::uint32_t minor, EveBytesV1 capabilities,
                        EveBytesV1 method, EveBytesV1 payload, EveRequestId* output);
    void publishReady();
    Result<void> retirementAccess(std::optional<EveHandle> effect = {}) const;
    void retireAll();
    Task<void> retireEffect(EveHandle effect);
    bool busy() const;
    bool empty() const;
    void releaseConsumers();
    std::vector<BindingSnapshot> snapshot() const;
    std::size_t committed() const { return consumers.size(); }

private:
    std::weak_ptr<NativeInstanceControl> owner;
    std::optional<ServiceRegistry> managedIndex;
    std::map<EveHandle, std::shared_ptr<NativeBinding>> providers;
    std::map<ServiceKey, std::shared_ptr<NativeCommit>> consumers;
    bool released = false;
    ServiceRegistry& index();
    Result<void> retirementAccess(const std::shared_ptr<NativeBinding>& binding) const;
    void retire(const std::shared_ptr<NativeBinding>& binding);
    void call(const std::shared_ptr<HostOperation>& operation);
};
} // namespace eve::module::detail
