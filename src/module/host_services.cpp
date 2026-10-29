/** @file host_services.cpp
 * @brief 真实 Core 服务注册/撤回/版本能力/旧绑定清理及跨模块调用。
 */
#include "host_services_p.hpp"
#include "json_p.hpp"
#include <algorithm>

namespace cordis::module::detail {
CordisStatus nativeWireError(const ErrorInfo& error)
{
    switch (error.code) {
    case ErrorCode::WrongThread:
        return CORDIS_E_WRONG_THREAD;
    case ErrorCode::InvalidArgument:
    case ErrorCode::ConfigInvalid:
        return CORDIS_E_INVALID_ARGUMENT;
    case ErrorCode::DependencyMissing:
        return CORDIS_E_NOT_FOUND;
    case ErrorCode::DependencyVersion:
        return CORDIS_E_UNSUPPORTED;
    case ErrorCode::ServiceConflict:
        return CORDIS_E_BUSY;
    case ErrorCode::OperationCancelled:
        return CORDIS_E_CANCELLED;
    case ErrorCode::ContextClosing:
    case ErrorCode::StaleContext:
    case ErrorCode::ServiceRetiring:
    case ErrorCode::SchedulerClosed:
        return CORDIS_E_CLOSED;
    default:
        return CORDIS_E_FAILED;
    }
}
namespace {
template <class T> Task<T> rejected(ErrorInfo error)
{
    Promise<T> promise;
    promise.reject(std::move(error));
    return promise.task();
}
NativeServiceCall failedCall(ErrorInfo error)
{
    return {rejected<NativeServiceResult>(std::move(error)), [] { return Result<void>::success(); }};
}
CordisStatus text(CordisBytesV1 value, std::string& output, std::size_t limit, bool empty = false)
{
    if (value.size > limit)
        return CORDIS_E_LIMIT;
    if ((value.size && !value.data) || (!value.size && !empty))
        return CORDIS_E_INVALID_ARGUMENT;
    if (value.size)
        output.assign(reinterpret_cast<const char*>(value.data), static_cast<std::size_t>(value.size));
    return validAbiUtf8(output) ? CORDIS_OK : CORDIS_E_INVALID_ARGUMENT;
}
CordisStatus serviceKey(const CordisServiceKeyV1* source, ServiceKey& output)
{
    if (!source || source->reserved || !source->interface_major || !source->realm)
        return CORDIS_E_INVALID_ARGUMENT;
    const auto status = text(source->name, output.name, CORDIS_MAX_ID_BYTES);
    output.interfaceMajor = source->interface_major;
    output.realm = RealmId{source->realm};
    return status;
}
bool sameKey(const ServiceKey& a, const ServiceKey& b)
{
    return !(a < b) && !(b < a);
}
bool sameNames(std::vector<std::string> a, std::vector<std::string> b)
{
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}
struct ForwardedCall {
    std::optional<NativeServiceCall> native;
    Task<NativeServiceResult>::Snapshot result;
    Promise<NativeServiceResult> completed;
};
NativeServiceCall forward(const ServiceRef<NativeService>& ref, const std::optional<CleanupContext>& cleanup,
                          std::string method, std::string json)
{
    auto state = std::make_shared<ForwardedCall>();
    auto callback = [state, method = std::move(method), json = std::move(json)](NativeService& service) {
        state->native = service.invoke(method, json);
        Promise<void> finished;
        cordis::detail::TaskAccess::observe(state->native->result,
                                            [state, finished](Task<NativeServiceResult>::Snapshot value) {
                                                state->result = value;
                                                if (value->hasValue())
                                                    finished.resolve();
                                                else
                                                    finished.reject(value->error());
                                            });
        return finished.task();
    };
    auto held = cleanup ? cleanup->invokeService(ref, std::move(callback)) : ref.invoke(std::move(callback));
    cordis::detail::TaskAccess::observe(held, [state](Task<void>::Snapshot value) {
        if (!value->hasValue())
            state->completed.reject(value->error());
        else if (state->result)
            cordis::detail::TaskAccess::completeSnapshot(state->completed, state->result);
        else
            state->completed.reject(hostError(ErrorCode::AbiProtocol, "native service result missing"));
    });
    return {state->completed.task(), [state] {
                if (!state->completed.task().isReady() && state->native && state->native->cancel)
                    return state->native->cancel();
                return Result<void>::success();
            }};
}
} // namespace
struct NativeProvider {
    CordisHandle id, token;
    BindingId binding;
    bool alive = true;
    ModuleLease objectLease;
    std::optional<Task<void>> retired;
};
Result<void> NativeServices::bind(const Context& value, const NativePlugin& plugin)
{
    context = value;
    definition = plugin;
    for (const auto& dependency : plugin.dependencies) {
        auto consumed = context->consume<NativeService>(dependency.key);
        if (!consumed.hasValue())
            return Result<void>::failure(consumed.error());
        consumers.emplace(dependency.key, consumed.value());
    }
    return Result<void>::success();
}
CordisStatus NativeServices::provide(CordisHandle effect, const CordisServiceKeyV1* source,
                                     const CordisServiceMetadataV1* metadata, CordisHandle token, CordisHandle* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context)
        return CORDIS_E_UNSUPPORTED;
    auto self = owner.lock();
    auto identity = context->identity();
    if (!identity.hasValue())
        return nativeWireError(identity.error());
    if (!self->resources->canAttach(effect))
        return CORDIS_E_NOT_FOUND;
    if (!token || !metadata || metadata->struct_size < sizeof(CordisServiceMetadataV1))
        return CORDIS_E_INVALID_ARGUMENT;
    ServiceKey key;
    auto status = serviceKey(source, key);
    if (status != CORDIS_OK)
        return status;
    std::string capsJson;
    status = text(metadata->capabilities_json, capsJson, CORDIS_MAX_JSON_BYTES);
    if (status != CORDIS_OK)
        return status;
    auto capabilities = abiStringArray(capsJson);
    if (!capabilities)
        return CORDIS_E_INVALID_ARGUMENT;
    const auto declared = std::find_if(definition.outputs.begin(), definition.outputs.end(),
                                       [&](const NativeServiceOutput& item) { return sameKey(key, item.key); });
    if (declared == definition.outputs.end() || declared->metadata.minor != metadata->minor ||
        !sameNames(*capabilities, declared->metadata.capabilities))
        return CORDIS_E_UNSUPPORTED;
    if (providers.size() >= 128)
        return CORDIS_E_LIMIT;
    auto lease = self->root->source.ledger.acquire(ResourceKind::ServiceObject, key.name);
    if (!lease.hasValue())
        return CORDIS_E_LIMIT;
    auto provider = std::make_shared<NativeProvider>();
    provider->id = allocateNativeResourceHandle();
    provider->token = token;
    if (!provider->id)
        return CORDIS_E_LIMIT;
    provider->objectLease = std::move(lease.value());
    std::weak_ptr<NativeServices> weak = shared_from_this();
    auto object = std::make_shared<NativeService>([weak, provider](std::string method, std::string json) {
        auto services = weak.lock();
        return services ? services->call(provider, std::move(method), std::move(json))
                        : failedCall(hostError(ErrorCode::StaleContext, "native service owner expired"));
    });
    auto provided = context->provide<NativeService>(key, std::move(object));
    if (!provided.hasValue())
        return nativeWireError(provided.error());
    provider->binding = provided.value();
    providers.emplace(provider->id, provider);
    self->resources->attachService(
        effect,
        [weak, provider] {
            auto services = weak.lock();
            return services ? services->retire(provider)
                            : failedTask(hostError(ErrorCode::CleanupFailed, "service owner expired"));
        },
        [weak, provider] {
            provider->alive = false;
            provider->objectLease.reset();
            if (auto services = weak.lock())
                services->providers.erase(provider->id);
        });
    *output = provider->id;
    return CORDIS_OK;
}
Task<void> NativeServices::retire(const std::shared_ptr<NativeProvider>& provider)
{
    if (provider->retired)
        return *provider->retired;
    auto self = owner.lock();
    if (self->managedClosing && self->cleanupAccess) {
        auto access = self->cleanupAccess->snapshot();
        if (!access.hasValue())
            return failedTask(access.error());
        // Core begins this cleanup only after all owned outputs' calls and
        // committed consumers have drained. It still owns their keys/objects.
        provider->retired = completedTask();
    } else {
        provider->retired = context->withdraw(provider->binding);
    }
    return *provider->retired;
}
CordisStatus NativeServices::withdraw(CordisHandle binding, CordisRequestId* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context)
        return CORDIS_E_UNSUPPORTED;
    const auto found = providers.find(binding);
    if (found == providers.end())
        return CORDIS_E_NOT_FOUND;
    auto provider = found->second;
    auto self = owner.lock();
    auto services = shared_from_this();
    return self->resources->serviceCall(
        false, {},
        [services, provider](const std::string&) {
            auto task = services->retire(provider);
            Promise<NativeServiceResult> done;
            cordis::detail::TaskAccess::observe(task, [done](Task<void>::Snapshot value) {
                if (value->hasValue())
                    done.resolve({CORDIS_OK, {}});
                else
                    done.reject(value->error());
            });
            return NativeServiceCall{done.task(), [] { return Result<void>::success(); }};
        },
        output);
}
NativeServiceCall NativeServices::call(const std::shared_ptr<NativeProvider>& provider, std::string method,
                                       std::string json)
{
    auto self = owner.lock();
    if (!self || !self->ownerThread())
        return failedCall(hostError(ErrorCode::WrongThread, "native service owner required"));
    {
        std::lock_guard<std::mutex> lock(self->root->mutex);
        if (self->root->closing || self->root->faulted)
            return failedCall(hostError(ErrorCode::ServiceRetiring, "native generation no longer accepts calls"));
    }
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (!provider->alive || self->closing || self->uncertain || !self->native)
            return failedCall(hostError(ErrorCode::ServiceRetiring, "native service closed"));
    }
    if (method.empty() || method.size() > CORDIS_MAX_ID_BYTES || !validAbiUtf8(method) ||
        json.size() > CORDIS_MAX_JSON_BYTES || !validAbiJson(json))
        return failedCall(hostError(ErrorCode::InvalidArgument, "invalid native service method/payload"));
    CordisRequestId request = 0;
    auto source = self->invoke(NativeRequestKind::Invoke, "native.service.invoke", [&](CordisRequestId id) {
        request = id;
        return self->root->record->api->table.invoke_service(self->native, provider->token, bytes(method), bytes(json),
                                                             id);
    });
    Promise<NativeServiceResult> result;
    self->observe(source, [result](NativeInstanceControl& instance, const Result<NativeOutcome>& value) {
        auto snapshot = instance.channel.snapshot();
        if (!value.hasValue() || !snapshot.hasValue() || snapshot.value().faulted) {
            auto error =
                value.hasValue() ? hostError(ErrorCode::AbiProtocol, "native service protocol fault") : value.error();
            instance.quarantine(error);
            result.reject(std::move(error));
        } else
            result.resolve({value.value().status, value.value().json});
    });
    std::weak_ptr<NativeInstanceControl> weak = self;
    return {result.task(), [weak, request, source] {
                auto instance = weak.lock();
                if (!instance || !instance->ownerThread())
                    return Result<void>::failure(
                        hostError(ErrorCode::WrongThread, "native cancellation owner required"));
                if (source.isReady())
                    return Result<void>::success();
                auto cancelled = instance->channel.cancel(request);
                if (!cancelled.hasValue())
                    return Result<void>::failure(cancelled.error());
                if (!cancelled.value())
                    return Result<void>::success();
                auto call = instance->root->source.ledger.acquire(ResourceKind::Call, "native.service.cancel");
                if (!call.hasValue())
                    return Result<void>::failure(call.error());
                CordisStatus status;
                try {
                    status = instance->root->record->api->table.cancel_request(instance->native, request);
                } catch (...) {
                    status = 0x7fff;
                }
                if (!finalStatus(status)) {
                    auto error = hostError(ErrorCode::AbiProtocol, "native service cancellation protocol fault");
                    instance->quarantine(error);
                    return Result<void>::failure(std::move(error));
                }
                return Result<void>::success();
            }};
}
CordisStatus NativeServices::invoke(const CordisServiceKeyV1* source, std::uint32_t minor, CordisBytesV1 caps,
                                    CordisBytesV1 method, CordisBytesV1 json, CordisRequestId* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context)
        return CORDIS_E_UNSUPPORTED;
    auto self = owner.lock();
    ServiceKey key;
    auto status = serviceKey(source, key);
    if (status != CORDIS_OK)
        return status;
    std::string methodName, payload, capsJson;
    status = text(method, methodName, CORDIS_MAX_ID_BYTES);
    if (status != CORDIS_OK)
        return status;
    status = text(json, payload, CORDIS_MAX_JSON_BYTES);
    if (status != CORDIS_OK || !validAbiJson(payload))
        return status == CORDIS_OK ? CORDIS_E_INVALID_ARGUMENT : status;
    status = text(caps, capsJson, CORDIS_MAX_JSON_BYTES);
    if (status != CORDIS_OK)
        return status;
    auto capabilities = abiStringArray(capsJson);
    if (!capabilities)
        return CORDIS_E_INVALID_ARGUMENT;
    std::optional<CleanupContext> access;
    if (self->managedClosing && self->cleanupAccess) {
        auto snapshot = self->cleanupAccess->snapshot();
        if (!snapshot.hasValue())
            return nativeWireError(snapshot.error());
        access = self->cleanupAccess;
    } else {
        auto identity = context->identity();
        if (!identity.hasValue())
            return nativeWireError(identity.error());
    }
    auto found = consumers.find(key);
    if (found == consumers.end()) {
        if (access)
            return CORDIS_E_NOT_FOUND;
        if (consumers.size() >= 128)
            return CORDIS_E_LIMIT;
        auto consumed = context->consume<NativeService>(key, minor, *capabilities);
        if (!consumed.hasValue())
            return nativeWireError(consumed.error());
        found = consumers.emplace(key, consumed.value()).first;
    }
    auto snapshot = found->second.snapshot();
    if (!snapshot.hasValue())
        return nativeWireError(snapshot.error());
    if (snapshot.value().metadata.minor < minor)
        return CORDIS_E_UNSUPPORTED;
    for (const auto& capability : *capabilities)
        if (std::find(snapshot.value().metadata.capabilities.begin(), snapshot.value().metadata.capabilities.end(),
                      capability) == snapshot.value().metadata.capabilities.end())
            return CORDIS_E_UNSUPPORTED;
    auto ref = found->second;
    return self->resources->serviceCall(
        !access, std::move(payload),
        [ref, access, methodName = std::move(methodName)](const std::string& data) {
            return forward(ref, access, methodName, data);
        },
        output);
}
} // namespace cordis::module::detail
