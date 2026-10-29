/** @file host_p.hpp
 * @brief 原生宿主共享控制与资源适配私有接口；不对插件公开。
 */
#pragma once
#include "abi_p.hpp"
#include "native_source_p.hpp"
#include "request_p.hpp"
#include <cordis/module/native.hpp>
#include <cordis/module/runtime.hpp>
#include <map>
#include <mutex>
#include <thread>

namespace cordis::module::detail {
struct NativeResources;
ErrorInfo hostError(ErrorCode code, const char* message);
bool finalStatus(CordisStatus status);
CordisBytesV1 bytes(const std::string& value);
ErrorInfo operationError(CordisStatus status, bool stopping, const std::string& details = {});
struct BusinessRecord {
    std::optional<BusinessApi> api;
    std::optional<ErrorInfo> failure;
    bool negotiating = false;
    std::shared_ptr<NativeModuleControl> active;
};

struct NativeModuleControl : std::enable_shared_from_this<NativeModuleControl> {
    NativeModuleControl(NativeSource value, std::shared_ptr<BusinessRecord> business,
                        std::shared_ptr<IScheduler> target, std::optional<ServiceRegistry> index)
        : source(std::move(value)), record(std::move(business)), scheduler(target), owner(std::this_thread::get_id()),
          services(std::move(index))
    {
    }
    NativeSource source;
    std::shared_ptr<BusinessRecord> record;
    std::weak_ptr<IScheduler> scheduler;
    const std::thread::id owner;
    std::optional<ServiceRegistry> services;
    mutable std::mutex mutex;
    bool closing = false, closed = false, faulted = false;
    std::size_t waiting = 0;
    CleanupReport report{true, {}};
    std::map<CordisHandle, std::shared_ptr<NativeInstanceControl>> instances;
    Promise<CleanupReport> stopped;
    bool ownerThread() const { return owner == std::this_thread::get_id(); }
    void finishOne(const Result<CleanupReport>& value);
    void finish();
};
struct NativeInstanceControl : std::enable_shared_from_this<NativeInstanceControl> {
    NativeInstanceControl(std::shared_ptr<NativeModuleControl> module, std::shared_ptr<IScheduler> target,
                          std::string plugin, InstanceId id, Epoch activation, ModuleLease lease)
        : root(std::move(module)), channel(std::move(target), root->source.ledger, id, activation),
          pluginId(std::move(plugin)), instance(id), epoch(activation), instanceLease(std::move(lease))
    {
    }
    std::shared_ptr<NativeModuleControl> root;
    std::shared_ptr<NativeResources> resources;
    std::optional<Context> managed;
    bool managedClosing = false;
    RequestChannel channel;
    const std::string pluginId;
    const InstanceId instance;
    const Epoch epoch;
    mutable std::mutex mutex;
    NativeInstanceState state = NativeInstanceState::Created;
    CordisHandle native = 0;
    CordisRequestId startRequest = 0;
    CordisStatus startReturn = CORDIS_PENDING;
    std::optional<Task<NativeOutcome>> startWire;
    bool startIssued = false, startSettled = false, stopIssued = false;
    bool closing = false, uncertain = false, blocked = false;
    bool callbacksClosing = false, destroying = false;
    std::size_t hostCalls = 0, logBytes = 0;
    std::vector<NativeLogRecord> logs;
    std::optional<ErrorInfo> failure;
    ModuleLease instanceLease;
    Promise<void> started, callbacksEmpty;
    Promise<CleanupReport> stopped;
    bool ownerThread() const { return root->ownerThread(); }
    void quarantine(ErrorInfo error) noexcept;
    void watch(const Task<void>& receipt);
    template <class T, class F> void observe(const Task<T>& task, F action)
    {
        auto target = root->scheduler.lock();
        if (!target || !target->isOpen()) {
            quarantine(hostError(ErrorCode::SchedulerClosed, "native scheduler unavailable"));
            return;
        }
        auto self = shared_from_this();
        auto receipt = task.thenOn(target, [self, action = std::move(action)](const Result<T>& value) {
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                if (self->uncertain)
                    return;
            }
            action(*self, value);
        });
        watch(receipt);
    }
    void advanceClose();
    void settleStart(std::optional<ErrorInfo> error);
    void closeResources();
    Task<NativeOutcome> invoke(NativeRequestKind kind, const char* label,
                               const std::function<CordisStatus(CordisRequestId)>& action);
    void callbackDrain();
    void destroy();
    Result<NativeInstanceSnapshot> snapshot() const;
};

} // namespace cordis::module::detail
