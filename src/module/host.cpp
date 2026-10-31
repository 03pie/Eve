/** @file host.cpp
 * @brief 稳定基础 HostTable、真实业务生命周期、资源入口与保守失败驻留。
 */
#include "admission_p.hpp"
#include "host_p.hpp"
#include "host_resources_p.hpp"
#include "host_services_p.hpp"
#include "json_p.hpp"
#include "native_source_p.hpp"
#include "request_p.hpp"
#include <algorithm>
#include <atomic>
#include <cordis/module/manager.hpp>
#include <cordis/module/native.hpp>
#include <map>
#include <mutex>
#include <thread>

namespace cordis::module::detail {
ErrorInfo hostError(ErrorCode code, const char* message)
{
    auto error = makeError(code, message);
    error.phase = "module.host";
    return error;
}
bool finalStatus(CordisStatus status)
{
    return status <= CORDIS_OK && status >= CORDIS_E_BUSY;
}
namespace {
Task<CleanupReport> failedCleanupTask(ErrorInfo error)
{
    Promise<CleanupReport> promise;
    promise.reject(std::move(error));
    return promise.task();
}
} // namespace
CordisBytesV1 bytes(const std::string& value)
{
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
namespace {
struct HostRegistry {
    std::mutex mutex;
    std::map<GenerationId, std::shared_ptr<BusinessRecord>> modules;
    std::map<CordisHandle, std::weak_ptr<NativeInstanceControl>> contexts;
};
HostRegistry& registry()
{
    static auto* value = new HostRegistry;
    return *value;
}
} // namespace

namespace {
std::shared_ptr<NativeInstanceControl> lookup(CordisHandle context)
{
    auto& directory = registry();
    std::lock_guard<std::mutex> lock(directory.mutex);
    const auto found = directory.contexts.find(context);
    return found == directory.contexts.end() ? nullptr : found->second.lock();
}
void discardUncreated(const std::shared_ptr<NativeInstanceControl>& self)
{
    (void)self->channel.seal();
    {
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        directory.contexts.erase(self->channel.context());
    }
    {
        std::lock_guard<std::mutex> lock(self->root->mutex);
        self->root->instances.erase(self->channel.context());
    }
    self->instanceLease.reset();
}
struct HostCall {
    std::shared_ptr<NativeInstanceControl> self;
    ModuleLease lease;
    HostCall(std::shared_ptr<NativeInstanceControl> value, ModuleLease reference)
        : self(std::move(value)), lease(std::move(reference))
    {
    }
    HostCall(HostCall&&) = default;
    ~HostCall()
    {
        if (!self)
            return;
        lease.reset();
        bool drained;
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            --self->hostCalls;
            drained = self->callbacksClosing && self->hostCalls == 0;
        }
        if (drained)
            self->callbacksEmpty.resolve();
    }
};
std::optional<HostCall> enter(const std::shared_ptr<NativeInstanceControl>& self, bool log)
{
    std::lock_guard<std::mutex> lock(self->mutex);
    if (self->state == NativeInstanceState::Destroyed ||
        (self->callbacksClosing && !(log && self->destroying && self->ownerThread())))
        return {};
    auto call = self->root->source.ledger.acquire(ResourceKind::Call, "native.host.callback");
    if (!call.hasValue())
        return {};
    ++self->hostCalls;
    return std::optional<HostCall>(std::in_place, self, std::move(call.value()));
}
CordisStatus CORDIS_CALL complete(CordisHandle context, CordisRequestId request, CordisStatus status,
                                  CordisBytesV1 json) noexcept
{
    try {
        auto self = lookup(context);
        if (!self)
            return CORDIS_E_CLOSED;
        auto call = enter(self, false);
        if (!call) {
            self->quarantine(hostError(ErrorCode::AbiProtocol, "late native completion after callback gate closed"));
            return CORDIS_E_CLOSED;
        }
        const auto accepted = self->channel.complete(context, request, status, json);
        if (accepted == CORDIS_E_ABI_PROTOCOL || accepted == CORDIS_E_INVALID_ARGUMENT || accepted == CORDIS_E_FAILED)
            self->quarantine(hostError(ErrorCode::AbiProtocol, "native completion violated host protocol"));
        return accepted;
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
CordisStatus CORDIS_CALL log(CordisHandle context, std::uint32_t level, CordisBytesV1 text) noexcept
{
    try {
        auto self = lookup(context);
        if (!self)
            return CORDIS_E_CLOSED;
        auto call = enter(self, true);
        if (!call) {
            self->quarantine(hostError(ErrorCode::AbiProtocol, "late native log after callback gate closed"));
            return CORDIS_E_CLOSED;
        }
        if (level > CORDIS_LOG_ERROR || (text.size && !text.data))
            return CORDIS_E_INVALID_ARGUMENT;
        if (text.size > CORDIS_MAX_LOG_BYTES)
            return CORDIS_E_LIMIT;
        std::string value;
        if (text.size)
            value.assign(reinterpret_cast<const char*>(text.data), static_cast<std::size_t>(text.size));
        if (!validAbiUtf8(value))
            return CORDIS_E_INVALID_ARGUMENT;
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->logs.size() >= 128 || value.size() > 256 * 1024 - self->logBytes)
            return CORDIS_E_LIMIT;
        self->logBytes += value.size();
        self->logs.push_back({level, std::move(value)});
        return CORDIS_OK;
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
CordisStatus CORDIS_CALL cancelHost(CordisHandle context, CordisRequestId request) noexcept
{
    try {
        auto self = lookup(context);
        if (!self)
            return CORDIS_E_CLOSED;
        auto call = enter(self, false);
        if (!call) {
            self->quarantine(hostError(ErrorCode::AbiProtocol, "late native host request after callback gate closed"));
            return CORDIS_E_CLOSED;
        }
        return self->resources->cancel(request);
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
template <class F> CordisStatus withResources(CordisHandle context, F action) noexcept
{
    try {
        auto self = lookup(context);
        if (!self)
            return CORDIS_E_CLOSED;
        auto call = enter(self, false);
        if (!call) {
            self->quarantine(hostError(ErrorCode::AbiProtocol, "late native resource call after callback gate closed"));
            return CORDIS_E_CLOSED;
        }
        return action(*self->resources);
    } catch (...) {
        if (auto self = lookup(context))
            self->quarantine(hostError(ErrorCode::CallbackFailed, "native resource host failure"));
        return CORDIS_E_FAILED;
    }
}
CordisStatus CORDIS_CALL dispatchHost(CordisHandle context, CordisHandle callback, CordisBytesV1 payload,
                                      CordisRequestId* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context,
                         [&](NativeResources& resources) { return resources.dispatch(callback, payload, output); });
}
CordisStatus CORDIS_CALL createEffectHost(CordisHandle context, CordisBytesV1 label, CordisHandle callback,
                                          CordisHandle* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context,
                         [&](NativeResources& resources) { return resources.effectCreate(label, callback, output); });
}
CordisStatus CORDIS_CALL readyEffectHost(CordisHandle context, CordisHandle effect, CordisStatus status,
                                         CordisBytesV1 details) noexcept
{
    return withResources(context,
                         [&](NativeResources& resources) { return resources.effectReady(effect, status, details); });
}
CordisStatus CORDIS_CALL closeEffectHost(CordisHandle context, CordisHandle effect, CordisRequestId* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context, [&](NativeResources& resources) { return resources.effectClose(effect, output); });
}
CordisStatus CORDIS_CALL registerServiceHost(CordisHandle context, CordisHandle effect, const CordisServiceKeyV1* key,
                                             const CordisServiceMetadataV1* metadata, CordisHandle token,
                                             CordisHandle* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context, [&](NativeResources& resources) {
        return resources.services->add(effect, key, metadata, token, output);
    });
}
CordisStatus CORDIS_CALL withdrawServiceHost(CordisHandle context, CordisHandle binding,
                                             CordisRequestId* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context,
                         [&](NativeResources& resources) { return resources.services->withdraw(binding, output); });
}
CordisStatus CORDIS_CALL invokeServiceHost(CordisHandle context, const CordisServiceKeyV1* key, std::uint32_t minor,
                                           CordisBytesV1 capabilities, CordisBytesV1 method, CordisBytesV1 payload,
                                           CordisRequestId* output) noexcept
{
    if (output)
        *output = 0;
    return withResources(context, [&](NativeResources& resources) {
        return resources.services->invoke(key, minor, capabilities, method, payload, output);
    });
}
const CordisHostV1 hostTable{{sizeof(CordisHostV1), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0},
                             CORDIS_CAP_ALL,
                             0,
                             complete,
                             log,
                             cancelHost,
                             dispatchHost,
                             registerServiceHost,
                             withdrawServiceHost,
                             invokeServiceHost,
                             createEffectHost,
                             readyEffectHost,
                             closeEffectHost};
} // namespace
ErrorInfo operationError(CordisStatus status, bool stopping, const std::string& details)
{
    auto error =
        hostError(status == CORDIS_E_CANCELLED ? ErrorCode::OperationCancelled
                                               : (stopping ? ErrorCode::CleanupFailed : ErrorCode::ActivationFailed),
                  stopping ? "native stop rejected" : "native start rejected");
    error.message += ", status " + std::to_string(status);
    if (!details.empty()) {
        auto cause = makeError(error.code, details);
        cause.phase = "module.host.details";
        error.causes.push_back(std::move(cause));
    }
    return error;
}

void NativeInstanceControl::quarantine(ErrorInfo error) noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state == NativeInstanceState::Destroyed || uncertain)
                return;
            uncertain = true;
            blocked = error.code == ErrorCode::SchedulerClosed;
            if (ownerThread()) {
                closing = true;
                state = NativeInstanceState::Failed;
            }
            failure = error;
        }
        (void)root->source.ledger.pin("nativeInstanceUncertain");
        {
            std::lock_guard<std::mutex> lock(root->mutex);
            root->faulted = true;
        }
        {
            std::lock_guard<std::mutex> lock(root->source.failureSignal->mutex);
            if (!root->source.failureSignal->error)
                root->source.failureSignal->error = error;
        }
        if (ownerThread())
            recordNativePhase(root->source.generation, ModuleState::Failed, error);
        started.reject(error);
        stopped.resolve({false, {std::move(error)}});
    } catch (...) {
    }
}
void NativeInstanceControl::watch(const Task<void>& receipt)
{
    std::weak_ptr<NativeInstanceControl> weak = shared_from_this();
    cordis::detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot value) {
        if (!value->hasValue())
            if (auto self = weak.lock())
                self->quarantine(value->error());
    });
}
void NativeInstanceControl::advanceClose()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (uncertain || stopIssued || !closing || (startIssued && !startSettled) || resources->businessBusy())
            return;
        stopIssued = true;
    }
    auto ticket = channel.begin(NativeRequestKind::Stop, "native.stop");
    if (!ticket.hasValue()) {
        quarantine(ticket.error());
        return;
    }
    CordisStatus status;
    try {
        status = root->record->api->table.request_stop(native, ticket.value().id);
    } catch (...) {
        status = 0x7fff;
    }
    auto returned = channel.returned(ticket.value().id, status);
    if (!returned.hasValue()) {
        quarantine(returned.error());
        return;
    }
    observe(ticket.value().result, [](NativeInstanceControl& self, const Result<NativeOutcome>& value) {
        if (!value.hasValue()) {
            self.quarantine(value.error());
            return;
        }
        if (value.value().status != CORDIS_OK) {
            self.quarantine(operationError(value.value().status, true, value.value().json));
            return;
        }
        const auto current = self.channel.snapshot();
        if (!current.hasValue() || current.value().faulted) {
            self.quarantine(hostError(ErrorCode::AbiProtocol, "native stop protocol fault"));
            return;
        }
        self.closeResources();
    });
}
void NativeInstanceControl::closeResources()
{
    observe(resources->close(), [](NativeInstanceControl& self, const Result<void>& result) {
        if (!result.hasValue()) {
            self.quarantine(result.error());
            return;
        }
        auto sealed = self.channel.seal();
        if (!sealed.hasValue()) {
            self.quarantine(sealed.error());
            return;
        }
        self.observe(self.channel.drained(), [](NativeInstanceControl& instance, const Result<void>& drained) {
            if (!drained.hasValue())
                instance.quarantine(drained.error());
            else
                instance.callbackDrain();
        });
    });
}
Task<NativeOutcome> NativeInstanceControl::invoke(NativeRequestKind kind, const char* label,
                                                  const std::function<CordisStatus(CordisRequestId)>& action)
{
    auto ticket = channel.begin(kind, label);
    if (!ticket.hasValue()) {
        Promise<NativeOutcome> failed;
        if ((kind == NativeRequestKind::Invoke || kind == NativeRequestKind::Callback) &&
            ticket.error().code == ErrorCode::ModulePinned)
            failed.resolve({CORDIS_E_LIMIT, {}, false});
        else {
            quarantine(ticket.error());
            failed.reject(ticket.error());
        }
        return failed.task();
    }
    CordisStatus status;
    try {
        status = action(ticket.value().id);
    } catch (...) {
        status = 0x7fff;
    }
    auto returned = channel.returned(ticket.value().id, status);
    if (!returned.hasValue())
        quarantine(returned.error());
    return ticket.value().result;
}
void NativeInstanceControl::callbackDrain()
{
    bool empty;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (uncertain)
            return;
        callbacksClosing = true;
        empty = hostCalls == 0;
    }
    if (empty)
        callbacksEmpty.resolve();
    observe(callbacksEmpty.task(), [](NativeInstanceControl& self, const Result<void>& value) {
        if (!value.hasValue())
            self.quarantine(value.error());
        else
            self.destroy();
    });
}
void NativeInstanceControl::destroy()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (uncertain)
            return;
        destroying = true;
    }
    auto call = root->source.ledger.acquire(ResourceKind::Call, "native.destroy");
    if (!call.hasValue()) {
        quarantine(call.error());
        return;
    }
    CordisStatus status;
    try {
        status = root->record->api->table.destroy(native);
    } catch (...) {
        status = 0x7fff;
    }
    call.value().reset();
    {
        std::lock_guard<std::mutex> lock(mutex);
        destroying = false;
        if (uncertain)
            return;
    }
    if (status != CORDIS_OK) {
        quarantine(hostError(finalStatus(status) ? ErrorCode::CleanupFailed : ErrorCode::AbiProtocol,
                             "native destroy failed; instance retained"));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        native = 0;
        state = NativeInstanceState::Destroyed;
    }
    {
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        directory.contexts.erase(channel.context());
    }
    instanceLease.reset();
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        root->instances.erase(channel.context());
    }
    stopped.resolve({true, {}});
}
Result<NativeInstanceSnapshot> NativeInstanceControl::snapshot() const
{
    if (!ownerThread())
        return Result<NativeInstanceSnapshot>::failure(
            hostError(ErrorCode::WrongThread, "native instance owner required"));
    auto requests = channel.snapshot();
    if (!requests.hasValue())
        return Result<NativeInstanceSnapshot>::failure(requests.error());
    auto effects = resources->snapshot();
    if (!effects.hasValue())
        return Result<NativeInstanceSnapshot>::failure(effects.error());
    std::lock_guard<std::mutex> lock(mutex);
    return Result<NativeInstanceSnapshot>::success(
        {root->source.generation, instance, epoch, pluginId, uncertain ? NativeInstanceState::Failed : state,
         native != 0, closing || uncertain, uncertain, blocked, requests.value().requests.size(), hostCalls, failure,
         logs, resources->pending(), effects.value(), resources->services->snapshot(),
         resources->services->committed()});
}
void NativeModuleControl::finishOne(const Result<CleanupReport>& value)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value.hasValue()) {
            report.quiescent = report.quiescent && value.value().quiescent;
            report.errors.insert(report.errors.end(), value.value().errors.begin(), value.value().errors.end());
        } else {
            report.quiescent = false;
            report.errors.push_back(value.error());
        }
        --waiting;
    }
    finish();
}
void NativeModuleControl::finish()
{
    CleanupReport result;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!closing || waiting)
            return;
        result = report;
        if (!instances.empty())
            result.quiescent = false;
        closed = result.quiescent;
    }
    if (result.quiescent) {
        recordNativePhase(source.generation, ModuleState::Loaded);
        source.session.reset();
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        if (record->active.get() == this)
            record->active.reset();
    }
    stopped.resolve(std::move(result));
}
} // namespace cordis::module::detail

namespace cordis::module {
using namespace detail;
Result<NativeModule> NativeModule::open(ModuleManager& manager, GenerationId id, std::shared_ptr<IScheduler> scheduler,
                                        std::optional<ServiceRegistry> services)
{
    auto managerView = manager.snapshot();
    if (!managerView.hasValue())
        return Result<NativeModule>::failure(managerView.error());
    if (!scheduler)
        return Result<NativeModule>::failure(hostError(ErrorCode::InvalidArgument, "native host scheduler is missing"));
    if (!scheduler->isCurrentThread())
        return Result<NativeModule>::failure(hostError(ErrorCode::WrongThread, "open native host on scheduler owner"));
    if (!scheduler->isOpen())
        return Result<NativeModule>::failure(hostError(ErrorCode::SchedulerClosed, "native host scheduler is closed"));
    if (services && !NativeServices::uses(*services, scheduler))
        return Result<NativeModule>::failure(
            hostError(ErrorCode::InvalidArgument, "native services require same scheduler"));
    auto admission = admitGeneration(id);
    if (!admission.hasValue())
        return Result<NativeModule>::failure(admission.error());
    auto source = acquireNativeSource(id);
    if (!source.hasValue())
        return Result<NativeModule>::failure(source.error());
    std::shared_ptr<BusinessRecord> record;
    {
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        auto& slot = directory.modules[id];
        if (!slot)
            slot = std::make_shared<BusinessRecord>();
        record = slot;
        if (record->negotiating)
            return Result<NativeModule>::failure(
                hostError(ErrorCode::ContextClosing, "business negotiation in progress"));
        if (record->failure)
            return Result<NativeModule>::failure(*record->failure);
        if (record->active) {
            auto root = record->active;
            if (!root->ownerThread() || root->scheduler.lock() != scheduler)
                return Result<NativeModule>::failure(
                    hostError(ErrorCode::WrongThread, "generation already bound to another scheduler"));
            std::lock_guard<std::mutex> rootLock(root->mutex);
            if (root->closing || root->faulted)
                return Result<NativeModule>::failure(hostError(ErrorCode::ContextClosing, "native host is closing"));
            if (services) {
                if (root->services && !NativeServices::same(*root->services, *services))
                    return Result<NativeModule>::failure(
                        hostError(ErrorCode::ServiceConflict, "generation already bound to another service registry"));
                if (!root->services)
                    root->services = std::move(services);
            }
            return Result<NativeModule>::success(NativeModule(std::move(root)));
        }
        record->negotiating = true;
    }
    if (!record->api) {
        auto call = source.value().ledger.acquire(ResourceKind::Call, "native.business.query");
        if (!call.hasValue()) {
            auto& directory = registry();
            std::lock_guard<std::mutex> lock(directory.mutex);
            record->negotiating = false;
            return Result<NativeModule>::failure(call.error());
        }
        auto queried = queryBusiness(source.value().query, hostTable, source.value().manifest);
        call.value().reset();
        if (!queried.hasValue()) {
            recordNativeBusiness(id, queried.error());
            auto& directory = registry();
            std::lock_guard<std::mutex> lock(directory.mutex);
            record->failure = queried.error();
            record->negotiating = false;
            return Result<NativeModule>::failure(queried.error());
        }
        record->api = std::move(queried.value());
        recordNativeBusiness(id, {});
    }
    auto root =
        std::make_shared<NativeModuleControl>(std::move(source.value()), record, scheduler, std::move(services));
    {
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        record->active = root;
        record->negotiating = false;
    }
    recordNativePhase(id, ModuleState::Serving);
    return Result<NativeModule>::success(NativeModule(std::move(root)));
}
Result<NativeInstance> NativeModule::create(std::string plugin, std::string config, InstanceId id, Epoch epoch) const
{
    return createBound(std::move(plugin), std::move(config), id, epoch, nullptr);
}
Result<NativeInstance> NativeModule::createBound(std::string plugin, std::string config, InstanceId id, Epoch epoch,
                                                 const Context* context) const
{
    auto root = control_;
    if (!root->ownerThread())
        return Result<NativeInstance>::failure(hostError(ErrorCode::WrongThread, "native host owner required"));
    auto scheduler = root->scheduler.lock();
    if (!scheduler || !scheduler->isOpen())
        return Result<NativeInstance>::failure(hostError(ErrorCode::SchedulerClosed, "native scheduler is closed"));
    if (context) {
        auto guard = context->eventGuard(scheduler);
        if (!guard.hasValue())
            return Result<NativeInstance>::failure(guard.error());
    }
    if (!id.valid() || epoch == 0 || config.size() > CORDIS_MAX_JSON_BYTES || !validAbiJson(config) ||
        std::find(root->record->api->plugins.begin(), root->record->api->plugins.end(), plugin) ==
            root->record->api->plugins.end())
        return Result<NativeInstance>::failure(
            hostError(ErrorCode::InvalidArgument, "invalid native plugin/configuration/identity"));
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        if (root->closing || root->faulted)
            return Result<NativeInstance>::failure(hostError(ErrorCode::ContextClosing, "native host is closing"));
        if (root->instances.size() >= 128)
            return Result<NativeInstance>::failure(
                hostError(ErrorCode::ModulePinned, "native instance budget exhausted"));
        for (const auto& pair : root->instances)
            if (pair.second->instance == id && pair.second->epoch == epoch)
                return Result<NativeInstance>::failure(
                    hostError(ErrorCode::InvalidArgument, "native activation identity already exists"));
    }
    auto admission = admitGeneration(root->source.generation);
    if (!admission.hasValue())
        return Result<NativeInstance>::failure(admission.error());
    auto lease = root->source.ledger.acquire(ResourceKind::Instance, plugin);
    if (!lease.hasValue())
        return Result<NativeInstance>::failure(lease.error());
    auto self = std::make_shared<NativeInstanceControl>(root, scheduler, std::move(plugin), id, epoch,
                                                        std::move(lease.value()));
    self->resources = std::make_shared<NativeResources>(self, scheduler);
    if (context)
        self->managed = *context;
    {
        auto& directory = registry();
        std::lock_guard<std::mutex> lock(directory.mutex);
        if (directory.contexts.size() >= 4096)
            return Result<NativeInstance>::failure(
                hostError(ErrorCode::ModulePinned, "native context budget exhausted"));
        directory.contexts.emplace(self->channel.context(), self);
    }
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        root->instances.emplace(self->channel.context(), self);
    }
    if (context) {
        auto bound = self->resources->services->bind(*context);
        if (!bound.hasValue()) {
            discardUncreated(self);
            return Result<NativeInstance>::failure(bound.error());
        }
        std::weak_ptr<NativeInstanceControl> weak = self;
        auto cleanup = context->onCleanup(
            [weak](CleanupContext) {
                auto value = weak.lock();
                if (!value)
                    return completedTask();
                value->managedClosing = true;
                auto closed = NativeInstance(value).close();
                Promise<void> done;
                cordis::detail::TaskAccess::observe(closed, [done](Task<CleanupReport>::Snapshot result) {
                    if (result->hasValue() && result->value().quiescent)
                        done.resolve();
                    else {
                        auto error = hostError(ErrorCode::CleanupFailed, "managed native instance did not quiesce");
                        if (result->hasValue())
                            error.causes = result->value().errors;
                        else
                            error.causes.push_back(result->error());
                        done.reject(std::move(error));
                    }
                });
                return done.task();
            },
            "native.instance");
        if (!cleanup.hasValue()) {
            discardUncreated(self);
            return Result<NativeInstance>::failure(cleanup.error());
        }
    }
    auto call = root->source.ledger.acquire(ResourceKind::Call, "native.create");
    if (!call.hasValue()) {
        discardUncreated(self);
        return Result<NativeInstance>::failure(call.error());
    }
    CordisStatus status;
    try {
        status = root->record->api->table.create(self->channel.context(), bytes(self->pluginId), bytes(config),
                                                 &self->native);
    } catch (...) {
        status = 0x7fff;
    }
    call.value().reset();
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->uncertain)
            return Result<NativeInstance>::failure(*self->failure);
    }
    auto requestState = self->channel.snapshot();
    if (!finalStatus(status) || (status == CORDIS_OK) != (self->native != 0) || !requestState.hasValue() ||
        requestState.value().faulted) {
        auto error = hostError(ErrorCode::AbiProtocol, "native create violated synchronous handle contract");
        self->quarantine(error);
        return Result<NativeInstance>::failure(std::move(error));
    }
    if (status != CORDIS_OK) {
        // Core owns these precommitted inputs; they are not resources retained by a failed foreign create.
        if (self->managed)
            self->resources->services->releaseConsumers();
        if (!self->resources->empty()) {
            auto error = hostError(ErrorCode::AbiProtocol, "failed native create retained registered resources");
            self->quarantine(error);
            return Result<NativeInstance>::failure(std::move(error));
        }
        auto error = operationError(status, false);
        discardUncreated(self);
        return Result<NativeInstance>::failure(std::move(error));
    }
    return Result<NativeInstance>::success(NativeInstance(std::move(self)));
}
Task<void> NativeInstance::start() const
{
    auto self = control_;
    if (!self->ownerThread())
        return failedTask(hostError(ErrorCode::WrongThread, "native instance owner required"));
    auto target = self->root->scheduler.lock();
    if (!target || !target->isOpen()) {
        self->quarantine(hostError(ErrorCode::SchedulerClosed, "native scheduler is closed"));
        return self->started.task();
    }
    {
        std::lock_guard<std::mutex> lock(self->root->mutex);
        if (self->root->closing || self->root->faulted)
            return failedTask(hostError(ErrorCode::ContextClosing, "native host is closing"));
    }
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->uncertain)
            return self->started.task();
        if (self->closing || self->state == NativeInstanceState::Destroyed)
            return failedTask(hostError(ErrorCode::ContextClosing, "native instance is closing"));
        if (self->startIssued)
            return self->started.task();
        self->startIssued = true;
        self->state = NativeInstanceState::Starting;
    }
    auto ticket = self->channel.begin(NativeRequestKind::Start, "native.start");
    if (!ticket.hasValue()) {
        self->quarantine(ticket.error());
        return self->started.task();
    }
    self->startRequest = ticket.value().id;
    self->startWire = ticket.value().result;
    CordisStatus status;
    try {
        status = self->root->record->api->table.start(self->native, ticket.value().id);
    } catch (...) {
        status = 0x7fff;
    }
    self->startReturn = status;
    auto returned = self->channel.returned(ticket.value().id, status);
    if (!returned.hasValue()) {
        self->quarantine(returned.error());
        return self->started.task();
    }
    self->observe(ticket.value().result, [](NativeInstanceControl& instance, const Result<NativeOutcome>& value) {
        if (!value.hasValue()) {
            instance.quarantine(value.error());
            return;
        }
        const auto snapshot = instance.channel.snapshot();
        if (!snapshot.hasValue() || snapshot.value().faulted) {
            instance.quarantine(hostError(ErrorCode::AbiProtocol, "native start protocol fault"));
            return;
        }
        std::optional<ErrorInfo> error;
        if (value.value().status != CORDIS_OK)
            error = operationError(value.value().status, false, value.value().json);
        bool closing;
        {
            std::lock_guard<std::mutex> lock(instance.mutex);
            instance.startSettled = true;
            instance.startWire.reset();
            closing = instance.closing;
        }
        if (instance.managed && !error) {
            instance.observe(instance.resources->ready(), [](NativeInstanceControl& value, const Result<void>& ready) {
                value.settleStart(ready.hasValue() ? std::optional<ErrorInfo>{}
                                                   : std::optional<ErrorInfo>{ready.error()});
            });
        } else {
            instance.settleStart(error);
        }
        if (closing)
            instance.advanceClose();
    });
    return self->started.task();
}
void NativeInstanceControl::settleStart(std::optional<ErrorInfo> error)
{
    bool stop;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (uncertain || state == NativeInstanceState::Destroyed)
            return;
        stop = closing;
        if (!closing) {
            state = error ? NativeInstanceState::Failed : NativeInstanceState::Started;
            if (error)
                failure = *error;
        }
    }
    if (stop && managed)
        started.reject(hostError(ErrorCode::OperationCancelled, "native start closing"));
    else if (error)
        started.reject(*error);
    else {
        resources->services->publishReady();
        started.resolve();
    }
    if (stop)
        advanceClose();
}
Task<CleanupReport> NativeInstance::close() const
{
    auto self = control_;
    if (!self->ownerThread())
        return failedCleanupTask(hostError(ErrorCode::WrongThread, "native instance owner required"));
    if (self->managed && !self->managedClosing)
        return failedCleanupTask(hostError(ErrorCode::ContextClosing, "close the owning Core instance first"));
    auto scheduler = self->root->scheduler.lock();
    if (!scheduler || !scheduler->isOpen()) {
        self->quarantine(hostError(ErrorCode::SchedulerClosed, "native scheduler is closed"));
        return self->stopped.task();
    }
    bool pending;
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->uncertain)
            return self->stopped.task();
        if (self->closing || self->state == NativeInstanceState::Destroyed)
            return self->stopped.task();
        self->closing = true;
        self->state = NativeInstanceState::Stopping;
        pending = self->startIssued && !self->startSettled && self->startReturn == CORDIS_PENDING && self->startWire &&
                  !self->startWire->isReady();
    }
    if (self->managed)
        self->started.reject(hostError(ErrorCode::OperationCancelled, "managed native instance closing"));
    self->resources->cancelBusiness();
    auto closed = self->channel.close();
    if (!closed.hasValue()) {
        self->quarantine(closed.error());
        return self->stopped.task();
    }
    if (pending) {
        auto requested = self->channel.cancel(self->startRequest);
        if (!requested.hasValue()) {
            self->quarantine(requested.error());
            return self->stopped.task();
        }
        const auto state = self->channel.snapshot();
        bool inFlight = false;
        if (state.hasValue())
            for (const auto& entry : state.value().requests)
                if (entry.id == self->startRequest)
                    inFlight = !entry.completed && !entry.deliveryBlocked;
        if (requested.value() && inFlight) {
            auto call = self->root->source.ledger.acquire(ResourceKind::Call, "native.cancel");
            if (!call.hasValue()) {
                self->quarantine(call.error());
                return self->stopped.task();
            }
            CordisStatus status;
            try {
                status = self->root->record->api->table.cancel_request(self->native, self->startRequest);
            } catch (...) {
                status = 0x7fff;
            }
            call.value().reset();
            if (!finalStatus(status))
                self->quarantine(hostError(ErrorCode::AbiProtocol, "invalid native cancellation response"));
        }
    }
    self->advanceClose();
    return self->stopped.task();
}
Result<NativeInstanceSnapshot> NativeInstance::snapshot() const
{
    return control_->snapshot();
}
Task<CleanupReport> NativeInstance::closed() const
{
    return control_->stopped.task();
}
Task<CleanupReport> NativeModule::close() const
{
    auto root = control_;
    if (!root->ownerThread())
        return failedCleanupTask(hostError(ErrorCode::WrongThread, "native host owner required"));
    std::vector<std::shared_ptr<NativeInstanceControl>> instances;
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        if (root->closing)
            return root->stopped.task();
        for (const auto& pair : root->instances)
            if (pair.second->managed && !pair.second->managedClosing)
                return failedCleanupTask(hostError(ErrorCode::ContextClosing, "close owning Core instances first"));
        root->closing = true;
        for (const auto& pair : root->instances)
            instances.push_back(pair.second);
        root->waiting = instances.size();
    }
    recordNativePhase(root->source.generation, ModuleState::Draining);
    if (instances.empty())
        root->finish();
    for (const auto& instance : instances) {
        NativeInstance wrapper(instance);
        auto task = wrapper.close();
        cordis::detail::TaskAccess::observe(task,
                                            [root](Task<CleanupReport>::Snapshot value) { root->finishOne(*value); });
    }
    return root->stopped.task();
}
Result<NativeModuleSnapshot> NativeModule::snapshot() const
{
    auto root = control_;
    if (!root->ownerThread())
        return Result<NativeModuleSnapshot>::failure(hostError(ErrorCode::WrongThread, "native host owner required"));
    NativeModuleSnapshot result;
    std::vector<std::shared_ptr<NativeInstanceControl>> instances;
    {
        std::lock_guard<std::mutex> lock(root->mutex);
        result = {root->source.generation, root->closing, root->closed, root->faulted, {}};
        for (const auto& pair : root->instances)
            instances.push_back(pair.second);
    }
    for (const auto& instance : instances) {
        auto snapshot = instance->snapshot();
        if (!snapshot.hasValue())
            return Result<NativeModuleSnapshot>::failure(snapshot.error());
        result.instances.push_back(std::move(snapshot.value()));
    }
    return Result<NativeModuleSnapshot>::success(std::move(result));
}
} // namespace cordis::module
