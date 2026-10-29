/** @file host_resources_p.hpp
 * @brief 原生 dispatch/effect 的宿主资源桥，控制块只在 owner 上修改。
 */
#pragma once
#include "host_p.hpp"

namespace cordis::module::detail {
struct NativeEffect;
struct NativeServices;
struct NativeServiceEndpoint;
/** @brief 消费者操作与目标请求分离，最后捕获析构后才归还字节和模块引用。 */
struct HostOperation {
    CordisRequestId id = 0, target = 0;
    bool business = true, cancelled = false, entered = false, delivering = false;
    std::string payload, method;
    ModuleLease callbackLease, payloadLease;
    std::weak_ptr<NativeInstanceControl> targetOwner;
    std::shared_ptr<NativeServiceEndpoint> endpoint;
    std::optional<Task<NativeOutcome>> source;
    std::optional<Task<void>> serviceCall;
    std::optional<NativeOutcome> outcome;
    Promise<void> serviceDone;
    std::function<void()> retired;
    ~HostOperation();
};
struct NativeResources : std::enable_shared_from_this<NativeResources> {
    NativeResources(std::weak_ptr<NativeInstanceControl> instance, const std::shared_ptr<IScheduler>& scheduler);
    CordisStatus dispatch(CordisHandle callback, CordisBytesV1 payload, CordisRequestId* output);
    CordisStatus cancel(CordisRequestId request);
    CordisStatus effectCreate(CordisBytesV1 label, CordisHandle callback, CordisHandle* output);
    CordisStatus effectReady(CordisHandle effect, CordisStatus status, CordisBytesV1 details);
    CordisStatus effectClose(CordisHandle effect, CordisRequestId* output);
    bool businessBusy() const;
    bool empty() const;
    void cancelBusiness();
    Task<void> close();
    Task<void> ready() const;
    Result<ScopeSnapshot> snapshot() const;
    std::size_t pending() const { return operations.size(); }
    std::shared_ptr<NativeServices> services;
    bool serviceEffectReady(CordisHandle effect) const;
    bool serviceEffectExists(CordisHandle effect) const;
    bool cleanupServices() const;

private:
    friend struct NativeServices;
    std::weak_ptr<NativeInstanceControl> owner;
    EffectScope scope;
    std::map<CordisHandle, std::shared_ptr<NativeEffect>> effects;
    std::map<CordisRequestId, std::shared_ptr<HostOperation>> operations;
    std::size_t payloadBytes = 0, retainedCaptures = 0;
    bool closing = false, closingEffect = false, effectsDone = false;
    std::optional<ErrorInfo> failure;
    Promise<void> stopped;
    CordisStatus allowed(bool cleanup) const;
    Result<std::shared_ptr<HostOperation>> operation(bool business, std::string payload, std::string method = {});
    void dispatchOne(const std::shared_ptr<HostOperation>& operation, CordisHandle callback);
    void deliver(const std::shared_ptr<HostOperation>& operation, CordisStatus status, std::string json = {});
    Task<void> cleanup(const std::shared_ptr<NativeEffect>& effect);
    void nextEffect();
    void checkClosed();
    std::function<void()> retirement(std::size_t bytes = 0);
    void retired(std::size_t bytes);
};
} // namespace cordis::module::detail
