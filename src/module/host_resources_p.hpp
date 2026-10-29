/** @file host_resources_p.hpp
 * @brief 原生 dispatch/effect 的宿主资源桥，控制块只在 owner 上修改。
 */
#pragma once
#include "host_p.hpp"

namespace cordis::module::detail {
struct NativeEffect;
struct HostOperation;
struct NativeServices;
CordisHandle allocateNativeResourceHandle();
struct NativeResources : std::enable_shared_from_this<NativeResources> {
    NativeResources(std::weak_ptr<NativeInstanceControl> instance, const std::shared_ptr<IScheduler>& scheduler);
    CordisStatus dispatch(CordisHandle callback, CordisBytesV1 payload, CordisRequestId* output);
    CordisStatus cancel(CordisRequestId request);
    CordisStatus effectCreate(CordisBytesV1 label, CordisHandle callback, CordisHandle* output);
    CordisStatus effectReady(CordisHandle effect, CordisStatus status, CordisBytesV1 details);
    CordisStatus effectClose(CordisHandle effect, CordisRequestId* output);
    Result<void> bind(const Context& context, const NativePlugin& plugin);
    CordisStatus serviceRegister(CordisHandle effect, const CordisServiceKeyV1* key,
                                 const CordisServiceMetadataV1* metadata, CordisHandle token, CordisHandle* output);
    CordisStatus serviceWithdraw(CordisHandle binding, CordisRequestId* output);
    CordisStatus serviceInvoke(const CordisServiceKeyV1* key, std::uint32_t minor, CordisBytesV1 capabilities,
                               CordisBytesV1 method, CordisBytesV1 json, CordisRequestId* output);
    CordisStatus serviceCall(bool business, std::string payload,
                             std::function<NativeServiceCall(const std::string&)> call, CordisRequestId* output);
    bool canAttach(CordisHandle effect) const;
    void attachService(CordisHandle effect, CleanupFunction retire, std::function<void()> released);
    bool businessBusy() const;
    bool empty() const;
    void cancelBusiness();
    Task<void> close();
    Task<void> ready();
    Result<ScopeSnapshot> snapshot() const;
    std::size_t pending() const { return operations.size(); }

private:
    std::weak_ptr<NativeInstanceControl> owner;
    std::shared_ptr<NativeServices> services;
    EffectScope scope;
    std::map<CordisHandle, std::shared_ptr<NativeEffect>> effects;
    std::map<CordisRequestId, std::shared_ptr<HostOperation>> operations;
    std::size_t payloadBytes = 0, retainedCaptures = 0;
    bool closing = false, closingEffect = false, effectsDone = false;
    std::optional<ErrorInfo> failure;
    Promise<void> stopped;
    CordisStatus allowed(bool cleanup) const;
    Result<std::shared_ptr<HostOperation>> operation(bool business, std::string payload);
    void dispatchOne(const std::shared_ptr<HostOperation>& operation, CordisHandle callback);
    void deliver(const std::shared_ptr<HostOperation>& operation, CordisStatus status, std::string json = {});
    Task<void> cleanup(const std::shared_ptr<NativeEffect>& effect);
    void cleanupProviders(const std::shared_ptr<NativeEffect>& effect, std::size_t index, Promise<void> done);
    void nextEffect();
    void checkClosed();
    std::function<void()> retirement(std::size_t bytes = 0);
    void retired(std::size_t bytes);
};
} // namespace cordis::module::detail
