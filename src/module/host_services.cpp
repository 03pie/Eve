/** @file host_services.cpp
 * @brief 原生服务的真实 Core 注册、旧绑定租约、异步调用与逆序退出桥。
 */
#include "host_resources_p.hpp"
#include "host_services_p.hpp"
#include "json_p.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <algorithm>
#include <set>

namespace cordis::module::detail {
namespace {
CordisStatus statusOf(const ErrorInfo& error)
{
    switch (error.code) {
    case ErrorCode::WrongThread:
        return CORDIS_E_WRONG_THREAD;
    case ErrorCode::InvalidArgument:
        return CORDIS_E_INVALID_ARGUMENT;
    case ErrorCode::DependencyMissing:
        return CORDIS_E_NOT_FOUND;
    case ErrorCode::DependencyVersion:
        return CORDIS_E_UNSUPPORTED;
    case ErrorCode::ServiceConflict:
    case ErrorCode::DependencyCycle:
        return CORDIS_E_BUSY;
    case ErrorCode::ModulePinned:
        return CORDIS_E_LIMIT;
    case ErrorCode::StaleContext:
    case ErrorCode::ContextClosing:
    case ErrorCode::ServiceRetiring:
    case ErrorCode::SchedulerClosed:
        return CORDIS_E_CLOSED;
    default:
        return CORDIS_E_FAILED;
    }
}
CordisStatus copy(CordisBytesV1 value, std::string& text, std::size_t limit, bool json = false)
{
    if (value.size > limit)
        return CORDIS_E_LIMIT;
    if (value.size && !value.data)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value.size)
        text.assign(reinterpret_cast<const char*>(value.data), static_cast<std::size_t>(value.size));
    return (json ? validAbiJson(text) : validAbiUtf8(text)) ? CORDIS_OK : CORDIS_E_INVALID_ARGUMENT;
}
CordisStatus capabilities(CordisBytesV1 value, std::vector<std::string>& output)
{
    std::string json;
    auto copied = copy(value, json, 16384, true);
    if (copied != CORDIS_OK)
        return copied;
    const auto document = QJsonDocument::fromJson(QByteArray(json.data(), static_cast<int>(json.size())));
    if (!document.isArray())
        return CORDIS_E_INVALID_ARGUMENT;
    const auto array = document.array();
    if (array.size() > 64)
        return CORDIS_E_LIMIT;
    std::set<std::string> unique;
    for (const auto& item : array) {
        if (!item.isString())
            return CORDIS_E_INVALID_ARGUMENT;
        auto name = item.toString().toUtf8().toStdString();
        if (name.empty() || !unique.insert(name).second)
            return CORDIS_E_INVALID_ARGUMENT;
        if (name.size() > CORDIS_MAX_ID_BYTES)
            return CORDIS_E_LIMIT;
        output.push_back(std::move(name));
    }
    return CORDIS_OK;
}
CordisStatus key(const CordisServiceKeyV1* value, ServiceKey& output)
{
    if (!value || value->reserved || !value->interface_major || !value->realm)
        return CORDIS_E_INVALID_ARGUMENT;
    auto copied = copy(value->name, output.name, CORDIS_MAX_ID_BYTES);
    if (copied != CORDIS_OK)
        return copied;
    if (output.name.empty())
        return CORDIS_E_INVALID_ARGUMENT;
    output.interfaceMajor = value->interface_major;
    output.realm = RealmId{value->realm};
    return CORDIS_OK;
}
bool permits(const ServiceMetadata& available, std::uint32_t minor, const std::vector<std::string>& required)
{
    if (available.minor < minor)
        return false;
    for (const auto& name : required)
        if (std::find(available.capabilities.begin(), available.capabilities.end(), name) ==
            available.capabilities.end())
            return false;
    return true;
}
bool businessAvailable(const std::shared_ptr<NativeServiceEndpoint>& endpoint)
{
    auto provider = endpoint->owner.lock();
    if (!provider)
        return false;
    {
        std::lock_guard<std::mutex> lock(provider->mutex);
        if (provider->uncertain || provider->callbacksClosing || !provider->native)
            return false;
    }
    std::lock_guard<std::mutex> lock(provider->root->mutex);
    return !provider->root->closing && !provider->root->closed && !provider->root->faulted;
}
} // namespace
struct NativeBinding {
    std::optional<ProviderHandle> handle;
    std::shared_ptr<NativeServiceEndpoint> endpoint;
    CordisHandle id = 0;
    bool withdrawing = false;
    Promise<void> withdrawn;
};
struct NativeCommit {
    std::shared_ptr<cordis::detail::CommittedLease> lease;
    std::shared_ptr<NativeServiceEndpoint> endpoint;
    bool borrowed = false;
};
NativeServiceEndpoint::~NativeServiceEndpoint()
{
    detach();
}
void NativeServiceEndpoint::detach()
{
    owner.reset();
    token = effect = 0;
    lease.reset();
    auto notify = std::move(retired);
    if (notify)
        notify();
}
Result<void> NativeServices::bind(const Context& context)
{
    auto self = owner.lock();
    auto registry = context.managedRegistry();
    if (!registry.hasValue())
        return Result<void>::failure(registry.error());
    if (self->root->services && !same(*self->root->services, registry.value()))
        return Result<void>::failure(
            hostError(ErrorCode::InvalidArgument, "native index differs from owning Core registry"));
    auto inputs = context.inputs(typeid(NativeServiceEndpoint));
    if (!inputs.hasValue())
        return Result<void>::failure(inputs.error());
    if (inputs.value().size() > 128)
        return Result<void>::failure(hostError(ErrorCode::ModulePinned, "native input budget exhausted"));
    for (const auto& input : inputs.value()) {
        auto value = std::make_shared<NativeCommit>();
        value->lease = input.second;
        value->borrowed = true;
        auto obtained = cordis::detail::callCommitted(
            value->lease, typeid(NativeServiceEndpoint),
            [value](void* object) {
                value->endpoint = static_cast<NativeServiceEndpoint*>(object)->shared_from_this();
            },
            false);
        if (!obtained.hasValue())
            return obtained;
        consumers.emplace(input.first, std::move(value));
    }
    if (!self->root->services) {
        auto canonical = registry.value();
        canonical.publication_.reset();
        self->root->services = std::move(canonical);
    }
    managedIndex = registry.value();
    return Result<void>::success();
}
bool NativeServices::uses(const ServiceRegistry& registry, const std::shared_ptr<IScheduler>& scheduler)
{
    return registry.usesScheduler(scheduler);
}
bool NativeServices::same(const ServiceRegistry& first, const ServiceRegistry& second)
{
    return first.control_ == second.control_;
}
ServiceRegistry& NativeServices::index()
{
    if (managedIndex)
        return *managedIndex;
    auto self = owner.lock();
    if (!self->root->services)
        self->root->services.emplace(self->root->scheduler.lock());
    return *self->root->services;
}
CordisStatus NativeServices::add(CordisHandle effect, const CordisServiceKeyV1* wireKey,
                                 const CordisServiceMetadataV1* wireMetadata, CordisHandle token, CordisHandle* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    auto self = owner.lock();
    auto resources = self->resources;
    auto allowed = resources->allowed(false);
    if (allowed != CORDIS_OK)
        return allowed;
    if (!(self->root->record->api->table.capabilities & CORDIS_CAP_SERVICES))
        return CORDIS_E_UNSUPPORTED;
    if (self->managed && self->startSettled)
        return CORDIS_E_CLOSED;
    if (!resources->serviceEffectExists(effect) || !token || !wireMetadata ||
        wireMetadata->struct_size < sizeof(CordisServiceMetadataV1))
        return CORDIS_E_INVALID_ARGUMENT;
    if (providers.size() >= 128)
        return CORDIS_E_LIMIT;
    ServiceKey serviceKey;
    auto checked = key(wireKey, serviceKey);
    if (checked != CORDIS_OK)
        return checked;
    if (self->managed)
        serviceKey = self->managed->realms_.resolve(serviceKey);
    ServiceMetadata metadata;
    metadata.minor = wireMetadata->minor;
    checked = capabilities(wireMetadata->capabilities_json, metadata.capabilities);
    if (checked != CORDIS_OK)
        return checked;
    auto retained = self->root->source.ledger.acquire(ResourceKind::ServiceObject, "native.service.object");
    if (!retained.hasValue())
        return CORDIS_E_LIMIT;
    auto endpoint = std::make_shared<NativeServiceEndpoint>();
    endpoint->owner = self;
    endpoint->token = token;
    endpoint->effect = effect;
    endpoint->metadata = std::move(metadata);
    endpoint->lease = std::move(retained.value());
    endpoint->retired = resources->retirement();
    auto staged = index().stage(serviceKey, endpoint, endpoint->metadata);
    if (!staged.hasValue())
        return statusOf(staged.error());
    staged.value().setOwner(self->instance);
    if (self->managed) {
        auto adopted = self->managed->adoptOutput(staged.value(), typeid(NativeServiceEndpoint));
        if (!adopted.hasValue()) {
            (void)staged.value().retire();
            (void)staged.value().release();
            return statusOf(adopted.error());
        }
    }
    auto binding = std::make_shared<NativeBinding>();
    binding->handle = staged.value();
    binding->endpoint = endpoint;
    binding->id = staged.value().snapshot().value().id.value();
    providers.emplace(binding->id, binding);
    *output = binding->id;
    publishReady();
    return CORDIS_OK;
}
void NativeServices::publishReady()
{
    auto self = owner.lock();
    if (!self || self->resources->allowed(false) != CORDIS_OK)
        return;
    if (self->managed)
        return; // Core publishes only after the complete activation task settles.
    for (const auto& pair : providers) {
        const auto& binding = pair.second;
        if (!binding->withdrawing && binding->handle->snapshot().value().state == BindingState::Staged &&
            self->resources->serviceEffectReady(binding->endpoint->effect)) {
            auto published = binding->handle->publish();
            if (!published.hasValue()) {
                self->quarantine(published.error());
                return;
            }
        }
    }
}
void NativeServices::retire(const std::shared_ptr<NativeBinding>& binding)
{
    if (binding->withdrawing)
        return;
    binding->withdrawing = true;
    auto self = owner.lock();
    auto retired = binding->handle->retire();
    if (!retired.hasValue()) {
        self->quarantine(retired.error());
        binding->withdrawn.reject(retired.error());
        return;
    }
    auto services = shared_from_this();
    self->observe(binding->handle->whenUnused(),
                  [services, binding](NativeInstanceControl& instance, const Result<void>& result) {
                      if (!result.hasValue()) {
                          instance.quarantine(result.error());
                          binding->withdrawn.reject(result.error());
                          return;
                      }
                      auto released = instance.managed ? Result<void>::success() : binding->handle->release();
                      if (!released.hasValue()) {
                          instance.quarantine(released.error());
                          binding->withdrawn.reject(released.error());
                          return;
                      }
                      if (instance.managed)
                          binding->endpoint->detach();
                      services->providers.erase(binding->id);
                      binding->withdrawn.resolve();
                      instance.advanceClose();
                  });
}
void NativeServices::retireAll()
{
    std::vector<std::shared_ptr<NativeBinding>> values;
    for (const auto& pair : providers)
        values.push_back(pair.second);
    for (const auto& binding : values)
        retire(binding);
}
Task<void> NativeServices::retireEffect(CordisHandle effect)
{
    std::vector<std::shared_ptr<NativeBinding>> values;
    for (const auto& pair : providers)
        if (pair.second->endpoint->effect == effect)
            values.push_back(pair.second);
    if (values.empty())
        return completedTask();
    Promise<void> done;
    auto remaining = std::make_shared<std::size_t>(values.size());
    for (const auto& binding : values) {
        retire(binding);
        owner.lock()->observe(binding->withdrawn.task(),
                              [done, remaining](NativeInstanceControl&, const Result<void>& result) {
                                  if (!result.hasValue())
                                      done.reject(result.error());
                                  if (--*remaining == 0)
                                      done.resolve();
                              });
    }
    return done.task();
}
CordisStatus NativeServices::withdraw(CordisHandle id, CordisRequestId* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    auto self = owner.lock();
    auto resources = self->resources;
    const auto allowed = resources->allowed(true);
    if (allowed != CORDIS_OK)
        return allowed;
    if (!(self->root->record->api->table.capabilities & CORDIS_CAP_SERVICES))
        return CORDIS_E_UNSUPPORTED;
    auto found = providers.find(id);
    if (found == providers.end())
        return CORDIS_E_NOT_FOUND;
    auto accepted = resources->operation(false, {});
    if (!accepted.hasValue())
        return CORDIS_E_LIMIT;
    auto operation = accepted.value();
    operation->entered = true;
    auto binding = found->second;
    retire(binding);
    self->observe(binding->withdrawn.task(),
                  [resources, operation](NativeInstanceControl&, const Result<void>& result) {
                      resources->deliver(operation, result.hasValue() ? CORDIS_OK : CORDIS_E_FAILED);
                  });
    *output = operation->id;
    return CORDIS_PENDING;
}
CordisStatus NativeServices::invoke(const CordisServiceKeyV1* wireKey, std::uint32_t minor,
                                    CordisBytesV1 requiredCapabilities, CordisBytesV1 method, CordisBytesV1 payload,
                                    CordisRequestId* output)
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    auto self = owner.lock();
    auto resources = self->resources;
    const bool cleanup = resources->cleanupServices();
    const auto allowed = resources->allowed(cleanup);
    if (allowed != CORDIS_OK)
        return allowed;
    if (!(self->root->record->api->table.capabilities & CORDIS_CAP_SERVICES))
        return CORDIS_E_UNSUPPORTED;
    if (!self->native || !self->startIssued || released)
        return CORDIS_E_CLOSED;
    ServiceKey serviceKey;
    auto checked = key(wireKey, serviceKey);
    if (checked != CORDIS_OK)
        return checked;
    if (self->managed)
        serviceKey = self->managed->realms_.resolve(serviceKey);
    std::vector<std::string> required;
    checked = capabilities(requiredCapabilities, required);
    if (checked != CORDIS_OK)
        return checked;
    std::string name, data;
    checked = copy(method, name, CORDIS_MAX_ID_BYTES);
    if (checked != CORDIS_OK)
        return checked;
    if (name.empty())
        return CORDIS_E_INVALID_ARGUMENT;
    checked = copy(payload, data, CORDIS_MAX_JSON_BYTES, true);
    if (checked != CORDIS_OK)
        return checked;
    std::shared_ptr<NativeCommit> committed;
    auto existing = consumers.find(serviceKey);
    if (existing != consumers.end())
        committed = existing->second;
    else {
        if (self->managed)
            return cleanup ? CORDIS_E_CLOSED : CORDIS_E_NOT_FOUND;
        if (cleanup)
            return CORDIS_E_CLOSED;
        if (consumers.size() >= 128)
            return CORDIS_E_LIMIT;
        auto found = index().findErased(serviceKey, typeid(NativeServiceEndpoint), minor, required);
        if (!found.hasValue())
            return statusOf(found.error());
        std::weak_ptr<NativeInstanceControl> weak = self;
        auto retained = index().commitErased(
            serviceKey, typeid(NativeServiceEndpoint), self->root->scheduler.lock(), self->instance, self->epoch,
            [weak] {
                auto instance = weak.lock();
                return instance && instance->resources->allowed(false) == CORDIS_OK
                           ? Result<void>::success()
                           : Result<void>::failure(hostError(ErrorCode::ContextClosing, "native consumer closing"));
            },
            [weak] {
                if (auto instance = weak.lock())
                    instance->observe(completedTask(), [](NativeInstanceControl& value, const Result<void>&) {
                        (void)NativeInstance(value.shared_from_this()).close();
                    });
            });
        if (!retained.hasValue())
            return statusOf(retained.error());
        committed = std::make_shared<NativeCommit>();
        committed->lease = retained.value();
        auto obtained = cordis::detail::callCommitted(
            committed->lease, typeid(NativeServiceEndpoint),
            [committed](void* pointer) {
                committed->endpoint = static_cast<NativeServiceEndpoint*>(pointer)->shared_from_this();
            },
            false);
        if (!obtained.hasValue()) {
            cordis::detail::releaseCommitted(committed->lease);
            return statusOf(obtained.error());
        }
    }
    const bool newCommit = existing == consumers.end();
    const auto rejected = [&](CordisStatus status) {
        if (newCommit && !committed->borrowed)
            cordis::detail::releaseCommitted(committed->lease);
        return status;
    };
    if (!cleanup && !businessAvailable(committed->endpoint))
        return rejected(CORDIS_E_CLOSED);
    if (!permits(committed->endpoint->metadata, minor, required))
        return rejected(CORDIS_E_UNSUPPORTED);
    auto available = cordis::detail::callCommitted(
        committed->lease, typeid(NativeServiceEndpoint), [](void*) {}, cleanup, self->instance, self->epoch);
    if (!available.hasValue())
        return rejected(statusOf(available.error()));
    auto accepted = resources->operation(!cleanup, std::move(data), std::move(name));
    if (!accepted.hasValue())
        return rejected(CORDIS_E_LIMIT);
    if (newCommit)
        consumers.emplace(serviceKey, committed);
    auto operation = accepted.value();
    operation->endpoint = committed->endpoint;
    auto services = shared_from_this();
    auto called = cordis::detail::invokeService(
        {}, committed->lease, true, typeid(NativeServiceEndpoint),
        [services, self, operation](void*) {
            self->observe(completedTask(), [services, operation](NativeInstanceControl&, const Result<void>&) {
                services->call(operation);
            });
            return operation->serviceDone.task();
        },
        cleanup, self->instance, self->epoch);
    operation->serviceCall = called;
    self->observe(called, [resources, operation](NativeInstanceControl&, const Result<void>& result) {
        if (!result.hasValue())
            resources->deliver(operation, statusOf(result.error()));
        else if (operation->outcome)
            resources->deliver(operation, operation->outcome->status, operation->outcome->json);
    });
    *output = operation->id;
    return CORDIS_PENDING;
}
void NativeServices::call(const std::shared_ptr<HostOperation>& operation)
{
    if (operation->cancelled || (operation->business && owner.lock()->resources->allowed(false) != CORDIS_OK)) {
        operation->outcome = NativeOutcome{CORDIS_E_CANCELLED, {}, true};
        operation->serviceDone.resolve();
        return;
    }
    auto self = owner.lock();
    if (operation->business && !businessAvailable(operation->endpoint)) {
        operation->outcome = NativeOutcome{CORDIS_E_CLOSED, {}, false};
        operation->serviceDone.resolve();
        return;
    }
    auto provider = operation->endpoint->owner.lock();
    if (!provider || provider->uncertain || !provider->native) {
        self->quarantine(hostError(ErrorCode::StaleContext, "native provider unavailable"));
        return;
    }
    operation->entered = true;
    operation->targetOwner = provider;
    // 已接受调用可在提供者撤回后进入；close 仍开放受控清理请求，不能用消费者编号冒充目标编号。
    auto source = provider->invoke(
        provider->closing || !operation->business ? NativeRequestKind::Cleanup : NativeRequestKind::Invoke,
        "native.service.invoke", [&](CordisRequestId request) {
            operation->target = request;
            return provider->root->record->api->table.invoke_service(provider->native, operation->endpoint->token,
                                                                     bytes(operation->method),
                                                                     bytes(operation->payload), request);
        });
    operation->source = source;
    provider->observe(source, [self, operation](NativeInstanceControl& target, const Result<NativeOutcome>& result) {
        auto state = target.channel.snapshot();
        if (!result.hasValue() || !state.hasValue() || state.value().faulted) {
            auto error =
                result.hasValue() ? hostError(ErrorCode::AbiProtocol, "native service protocol fault") : result.error();
            target.quarantine(error);
            self->quarantine(std::move(error));
            return;
        }
        operation->outcome = result.value();
        operation->serviceDone.resolve();
    });
}
bool NativeServices::busy() const
{
    for (const auto& pair : providers) {
        auto state = pair.second->handle->snapshot();
        if (!state.hasValue() || state.value().activeCalls || state.value().committedLeases)
            return true;
    }
    return false;
}
bool NativeServices::empty() const
{
    return providers.empty() && consumers.empty();
}
void NativeServices::releaseConsumers()
{
    if (released)
        return;
    released = true;
    for (const auto& pair : consumers)
        if (!pair.second->borrowed)
            cordis::detail::releaseCommitted(pair.second->lease);
    consumers.clear();
}
std::vector<BindingSnapshot> NativeServices::snapshot() const
{
    std::vector<BindingSnapshot> result;
    for (const auto& pair : providers) {
        auto state = pair.second->handle->snapshot();
        if (state.hasValue())
            result.push_back(state.value());
    }
    return result;
}
} // namespace cordis::module::detail
