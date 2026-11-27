/** @file host_resources_p.hpp
 * @brief 原生 dispatch/effect 的宿主资源桥，控制块只在 owner 上修改。
 */
#pragma once
#include "host_p.hpp"

namespace eve::module::detail {
struct NativeEffect;
struct NativeServices;
struct NativeServiceEndpoint;
/** @brief 消费者操作与目标请求分离，最后捕获析构后才归还字节和模块引用。 */
struct HostOperation {
    EveRequestId id = 0, target = 0;
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
    EveStatus dispatch(EveHandle callback, EveBytesV1 payload, EveRequestId* output);
    EveStatus cancel(EveRequestId request);
    EveStatus effectCreate(EveBytesV1 label, EveHandle callback, EveHandle* output);
    EveStatus effectReady(EveHandle effect, EveStatus status, EveBytesV1 details);
    EveStatus effectClose(EveHandle effect, EveRequestId* output);
    bool businessBusy() const;
    bool empty() const;
    void cancelBusiness();
    Task<void> close();
    Task<void> ready() const;
    Result<ScopeSnapshot> snapshot() const;
    std::size_t pending() const { return operations.size(); }
    std::shared_ptr<NativeServices> services;
    bool serviceEffectReady(EveHandle effect) const;
    bool serviceEffectExists(EveHandle effect) const;
    bool cleanupServices() const;

private:
    friend struct NativeServices;
    std::weak_ptr<NativeInstanceControl> owner;
    EffectScope scope;
    std::map<EveHandle, std::shared_ptr<NativeEffect>> effects;
    std::map<EveRequestId, std::shared_ptr<HostOperation>> operations;
    std::size_t payloadBytes = 0, retainedCaptures = 0;
    bool closing = false, closingEffect = false, effectsDone = false;
    std::optional<ErrorInfo> failure;
    Promise<void> stopped;
    EveStatus allowed(bool cleanup) const;
    Result<std::shared_ptr<HostOperation>> operation(bool business, std::string payload, std::string method = {});
    void dispatchOne(const std::shared_ptr<HostOperation>& operation, EveHandle callback);
    void deliver(const std::shared_ptr<HostOperation>& operation, EveStatus status, std::string json = {});
    Task<void> cleanup(const std::shared_ptr<NativeEffect>& effect);
    void nextEffect();
    void checkClosed();
    std::function<void()> retirement(std::size_t bytes = 0);
    void retired(std::size_t bytes);
};
} // namespace eve::module::detail
