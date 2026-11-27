/** @file host_resources.cpp
 * @brief 排队原生回调、异步应答及真实 Core effect 清理，保留失败资源。
 */
#include "host_resources_p.hpp"
#include "host_services_p.hpp"
#include "json_p.hpp"
#include <atomic>
#include <limits>

namespace eve::module::detail {
namespace {
class ResourceScheduler final : public IScheduler {
public:
    ResourceScheduler(std::weak_ptr<NativeInstanceControl> value, std::weak_ptr<IScheduler> target)
        : instance(std::move(value)), scheduler(std::move(target))
    {
    }
    bool post(ScheduledJob job) override
    {
        auto weak = instance;
        auto discarded = [weak, original = std::move(job.discarded)] {
            if (original)
                original();
            if (auto self = weak.lock())
                self->quarantine(hostError(ErrorCode::SchedulerClosed, "native effect job discarded"));
        };
        job.discarded = discarded;
        try {
            if (auto target = scheduler.lock())
                if (target->post(std::move(job)))
                    return true;
        } catch (...) {
            discarded();
            throw;
        }
        discarded();
        return false;
    }
    bool isCurrentThread() const noexcept override { return ownerThread == std::this_thread::get_id(); }
    bool isOpen() const override
    {
        auto target = scheduler.lock();
        return target && target->isOpen();
    }

private:
    std::weak_ptr<NativeInstanceControl> instance;
    std::weak_ptr<IScheduler> scheduler;
    const std::thread::id ownerThread = std::this_thread::get_id();
};
EveStatus copySpan(EveBytesV1 source, std::string& target, bool json, std::size_t limit)
{
    if (source.size > limit)
        return EVE_E_LIMIT;
    if (source.size && !source.data)
        return EVE_E_INVALID_ARGUMENT;
    if (source.size)
        target.assign(reinterpret_cast<const char*>(source.data), static_cast<std::size_t>(source.size));
    return (json ? validAbiJson(target) : validAbiUtf8(target)) ? EVE_OK : EVE_E_INVALID_ARGUMENT;
}
EveHandle allocateEffectHandle()
{
    static std::atomic<EveHandle> next{1};
    auto value = next.load();
    do {
        if (value == std::numeric_limits<EveHandle>::max())
            return 0;
    } while (!next.compare_exchange_weak(value, value + 1));
    return value;
}
} // namespace
struct NativeEffect {
    EveHandle id, callback;
    Promise<void> setup;
    std::optional<EffectToken> token;
    bool ready = false, succeeded = false, closed = false, cleanupPending = false, withdrawing = false;
    ModuleLease setupLease, callbackLease;
    std::function<void()> retired;
    ~NativeEffect()
    {
        token.reset();
        setupLease.reset();
        callbackLease.reset();
        if (retired)
            retired();
    }
};
HostOperation::~HostOperation()
{
    source.reset();
    serviceCall.reset();
    endpoint.reset();
    std::string{}.swap(payload);
    std::string{}.swap(method);
    callbackLease.reset();
    payloadLease.reset();
    if (retired)
        retired();
}
NativeResources::NativeResources(std::weak_ptr<NativeInstanceControl> instance,
                                 const std::shared_ptr<IScheduler>& scheduler)
    : owner(instance), scope(std::make_shared<ResourceScheduler>(std::move(instance), scheduler))
{
    services = std::make_shared<NativeServices>(owner);
}
std::function<void()> NativeResources::retirement(std::size_t bytes)
{
    ++retainedCaptures;
    payloadBytes += bytes;
    std::weak_ptr<NativeResources> weak = shared_from_this();
    return [weak, bytes]() noexcept {
        auto resources = weak.lock();
        auto self = resources ? resources->owner.lock() : nullptr;
        if (!self)
            return;
        try {
            if (self->ownerThread())
                resources->retired(bytes);
            else
                self->observe(completedTask(), [resources, bytes](NativeInstanceControl&, const Result<void>&) {
                    resources->retired(bytes);
                });
        } catch (...) {
            try {
                self->quarantine(hostError(ErrorCode::SchedulerClosed, "native capture retirement failed"));
            } catch (...) {
            }
        }
    };
}
void NativeResources::retired(std::size_t bytes)
{
    --retainedCaptures;
    payloadBytes -= bytes;
    checkClosed();
}
EveStatus NativeResources::allowed(bool cleanup) const
{
    auto self = owner.lock();
    if (!self)
        return EVE_E_CLOSED;
    if (!self->ownerThread())
        return EVE_E_WRONG_THREAD;
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->uncertain || self->callbacksClosing || self->state == NativeInstanceState::Destroyed ||
            (!cleanup && (self->closing || closing)))
            return EVE_E_CLOSED;
    }
    {
        std::lock_guard<std::mutex> lock(self->root->mutex);
        if (!cleanup && (self->root->closing || self->root->faulted))
            return EVE_E_CLOSED;
    }
    if (!cleanup && self->managed && !self->managed->identity().hasValue())
        return EVE_E_CLOSED;
    auto target = self->root->scheduler.lock();
    return target && target->isOpen() ? EVE_OK : EVE_E_CLOSED;
}
Result<std::shared_ptr<HostOperation>> NativeResources::operation(bool business, std::string payload,
                                                                  std::string method)
{
    using Output = Result<std::shared_ptr<HostOperation>>;
    if (operations.size() >= 128 || payload.size() + method.size() > 8 * 1024 * 1024 - payloadBytes)
        return Output::failure(hostError(ErrorCode::ModulePinned, "native host operation budget exhausted"));
    auto self = owner.lock();
    auto callback = self->root->source.ledger.acquire(ResourceKind::Callback, "native.host.operation");
    if (!callback.hasValue())
        return Output::failure(callback.error());
    auto data = self->root->source.ledger.acquire(ResourceKind::QueuePayload, "native.host.payload");
    if (!data.hasValue())
        return Output::failure(data.error());
    auto value = std::make_shared<HostOperation>();
    value->id = allocateNativeRequestId();
    value->business = business;
    value->payload = std::move(payload);
    value->method = std::move(method);
    value->callbackLease = std::move(callback.value());
    value->payloadLease = std::move(data.value());
    value->retired = retirement(value->payload.size() + value->method.size());
    operations.emplace(value->id, value);
    return Output::success(std::move(value));
}
EveStatus NativeResources::dispatch(EveHandle callback, EveBytesV1 payload, EveRequestId* output)
{
    if (!output)
        return EVE_E_INVALID_ARGUMENT;
    *output = 0;
    const auto access = allowed(false);
    if (access != EVE_OK)
        return access;
    auto self = owner.lock();
    if (!callback || !self->startIssued || !self->native)
        return EVE_E_INVALID_ARGUMENT;
    std::string data;
    const auto copied = copySpan(payload, data, true, EVE_MAX_JSON_BYTES);
    if (copied != EVE_OK)
        return copied;
    auto accepted = operation(true, std::move(data));
    if (!accepted.hasValue())
        return EVE_E_LIMIT;
    auto op = accepted.value();
    auto resources = shared_from_this();
    self->observe(completedTask(), [resources, op, callback](NativeInstanceControl&, const Result<void>&) {
        resources->dispatchOne(op, callback);
    });
    *output = op->id;
    return EVE_PENDING;
}
void NativeResources::dispatchOne(const std::shared_ptr<HostOperation>& op, EveHandle callback)
{
    if (op->cancelled || allowed(false) != EVE_OK) {
        deliver(op, EVE_E_CANCELLED);
        return;
    }
    auto self = owner.lock();
    op->entered = true;
    op->targetOwner = self;
    auto source = self->invoke(NativeRequestKind::Callback, "native.dispatch", [&](EveRequestId request) {
        op->target = request;
        return self->root->record->api->table.dispatch(self->native, callback, bytes(op->payload), request);
    });
    op->source = source;
    auto resources = shared_from_this();
    self->observe(source, [resources, op](NativeInstanceControl& instance, const Result<NativeOutcome>& outcome) {
        if (!outcome.hasValue()) {
            instance.quarantine(outcome.error());
            return;
        }
        const auto state = instance.channel.snapshot();
        if (!state.hasValue() || state.value().faulted) {
            instance.quarantine(hostError(ErrorCode::AbiProtocol, "native dispatch protocol fault"));
            return;
        }
        resources->deliver(op, outcome.value().status, outcome.value().json);
    });
}
void NativeResources::deliver(const std::shared_ptr<HostOperation>& op, EveStatus status, std::string json)
{
    if (op->delivering)
        return;
    op->delivering = true;
    auto self = owner.lock();
    // This synchronous ACK has no request of its own. The real call and final op capture still fence retirement.
    auto call = self->root->source.ledger.acquire(ResourceKind::Call, "native.request_complete");
    if (!call.hasValue()) {
        self->quarantine(call.error());
        return;
    }
    EveStatus returned;
    try {
        returned = self->root->record->api->table.request_complete(self->native, op->id, status, bytes(json));
    } catch (...) {
        returned = 0x7fff;
    }
    call.value().reset();
    const auto state = self->channel.snapshot();
    {
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->uncertain)
            return;
    }
    if (returned != EVE_OK || !state.hasValue() || state.value().faulted) {
        self->quarantine(hostError(ErrorCode::AbiProtocol, "native operation ACK rejected"));
        return;
    }
    operations.erase(op->id);
    if (failure) {
        self->quarantine(*failure);
        return;
    }
    checkClosed();
    self->advanceClose();
}
EveStatus NativeResources::cancel(EveRequestId request)
{
    const auto access = allowed(true);
    if (access != EVE_OK)
        return access;
    const auto found = operations.find(request);
    if (found == operations.end())
        return EVE_E_NOT_FOUND;
    auto op = found->second;
    if (op->delivering || op->cancelled || (op->source && op->source->isReady()))
        return EVE_OK;
    op->cancelled = true;
    if (!op->entered || !op->target)
        return EVE_OK;
    auto self = owner.lock();
    auto target = op->targetOwner.lock();
    if (!target) {
        self->quarantine(hostError(ErrorCode::StaleContext, "native target disappeared"));
        return EVE_E_CLOSED;
    }
    auto requested = target->channel.cancel(op->target);
    if (!requested.hasValue()) {
        target->quarantine(requested.error());
        self->quarantine(requested.error());
        return EVE_E_ABI_PROTOCOL;
    }
    if (!requested.value())
        return EVE_OK;
    auto call = target->root->source.ledger.acquire(ResourceKind::Call, "native.operation.cancel");
    if (!call.hasValue()) {
        self->quarantine(call.error());
        return EVE_E_LIMIT;
    }
    EveStatus status;
    try {
        status = target->root->record->api->table.cancel_request(target->native, op->target);
    } catch (...) {
        status = 0x7fff;
    }
    if (!finalStatus(status)) {
        auto error = hostError(ErrorCode::AbiProtocol, "native operation cancel protocol fault");
        target->quarantine(error);
        self->quarantine(std::move(error));
        return EVE_E_ABI_PROTOCOL;
    }
    return status;
}
bool NativeResources::businessBusy() const
{
    if (services->busy())
        return true;
    if (!operations.empty())
        return true;
    for (const auto& pair : effects)
        if (pair.second->cleanupPending)
            return true;
    return false;
}
bool NativeResources::empty() const
{
    return effects.empty() && operations.empty() && services->empty();
}
void NativeResources::cancelBusiness()
{
    services->retireAll();
    std::vector<EveRequestId> requests;
    for (const auto& pair : operations)
        if (pair.second->business)
            requests.push_back(pair.first);
    for (auto request : requests)
        (void)cancel(request);
}
EveStatus NativeResources::effectCreate(EveBytesV1 label, EveHandle callback, EveHandle* output)
{
    if (!output)
        return EVE_E_INVALID_ARGUMENT;
    *output = 0;
    const auto access = allowed(false);
    if (access != EVE_OK)
        return access;
    if (!callback)
        return EVE_E_INVALID_ARGUMENT;
    if (effects.size() >= 128)
        return EVE_E_LIMIT;
    std::string name;
    auto copied = copySpan(label, name, false, EVE_MAX_LABEL_BYTES);
    if (copied != EVE_OK || name.empty())
        return copied == EVE_OK ? EVE_E_INVALID_ARGUMENT : copied;
    auto self = owner.lock();
    auto setup = self->root->source.ledger.acquire(ResourceKind::SetupTask, "native.effect.setup");
    auto cleanup = self->root->source.ledger.acquire(ResourceKind::Callback, "native.effect.cleanup");
    if (!setup.hasValue() || !cleanup.hasValue())
        return EVE_E_LIMIT;
    auto effect = std::make_shared<NativeEffect>();
    effect->id = allocateEffectHandle();
    effect->callback = callback;
    if (!effect->id)
        return EVE_E_LIMIT;
    effect->setupLease = std::move(setup.value());
    effect->callbackLease = std::move(cleanup.value());
    effect->retired = retirement();
    std::weak_ptr<NativeResources> weak = shared_from_this();
    auto token = scope.effect(
        [weak, effect](EffectSetupContext context) {
            auto added = context.addCleanup([weak, effect] {
                auto resources = weak.lock();
                return resources ? resources->cleanup(effect)
                                 : failedTask(hostError(ErrorCode::StaleContext, "native resource owner expired"));
            });
            return added.hasValue() ? effect->setup.task() : failedTask(added.error());
        },
        std::move(name));
    if (!token.hasValue())
        return EVE_E_CLOSED;
    effect->token = token.value();
    effects.emplace(effect->id, effect);
    self->observe(effect->token->ready(), [weak](NativeInstanceControl&, const Result<void>&) {
        if (auto resources = weak.lock())
            resources->services->publishReady();
    });
    *output = effect->id;
    return EVE_OK;
}
EveStatus NativeResources::effectReady(EveHandle id, EveStatus status, EveBytesV1 details)
{
    const auto access = allowed(true);
    if (access != EVE_OK)
        return access;
    const auto found = effects.find(id);
    if (found == effects.end())
        return EVE_E_NOT_FOUND;
    auto effect = found->second;
    if (effect->ready || !finalStatus(status)) {
        owner.lock()->quarantine(hostError(ErrorCode::AbiProtocol, "native effect setup reported twice or nonfinal"));
        return EVE_E_ABI_PROTOCOL;
    }
    std::string json;
    if (details.size) {
        const auto copied = copySpan(details, json, true, EVE_MAX_JSON_BYTES);
        if (copied != EVE_OK)
            return copied;
    }
    effect->ready = true;
    effect->succeeded = status == EVE_OK;
    effect->setupLease.reset();
    if (status == EVE_OK)
        effect->setup.resolve();
    else
        effect->setup.reject(operationError(status, false, json));
    return EVE_OK;
}
Task<void> NativeResources::cleanup(const std::shared_ptr<NativeEffect>& effect)
{
    auto self = owner.lock();
    if (!self || !self->native)
        return failedTask(hostError(ErrorCode::CleanupFailed, "native cleanup instance unavailable"));
    effect->cleanupPending = true;
    Promise<void> done;
    self->observe(services->retireEffect(effect->id), [done, effect](NativeInstanceControl& instance,
                                                                     const Result<void>& unused) {
        if (!unused.hasValue()) {
            instance.quarantine(unused.error());
            done.reject(unused.error());
            return;
        }
        auto task = instance.invoke(NativeRequestKind::Cleanup, "native.effect.cleanup", [&](EveRequestId request) {
            return instance.root->record->api->table.request_cleanup(instance.native, effect->id, effect->callback,
                                                                     request);
        });
        instance.observe(task, [done, effect](NativeInstanceControl& instance, const Result<NativeOutcome>& result) {
            const auto state = instance.channel.snapshot();
            if (!result.hasValue() || !state.hasValue() || state.value().faulted) {
                auto error = result.hasValue() ? hostError(ErrorCode::AbiProtocol, "native cleanup protocol fault")
                                               : result.error();
                instance.quarantine(error);
                done.reject(std::move(error));
            } else {
                effect->cleanupPending = false;
                if (result.value().status != EVE_OK)
                    done.reject(operationError(result.value().status, true, result.value().json));
                else
                    done.resolve();
                instance.advanceClose();
            }
        });
    });
    return done.task();
}
EveStatus NativeResources::effectClose(EveHandle id, EveRequestId* output)
{
    if (!output)
        return EVE_E_INVALID_ARGUMENT;
    *output = 0;
    const auto access = allowed(true);
    if (access != EVE_OK)
        return access;
    const auto found = effects.find(id);
    if (found == effects.end())
        return EVE_E_NOT_FOUND;
    auto servicesAllowed = services->retirementAccess(id);
    if (!servicesAllowed.hasValue())
        return statusOf(servicesAllowed.error());
    auto accepted = operation(false, {});
    if (!accepted.hasValue())
        return EVE_E_LIMIT;
    auto op = accepted.value();
    op->entered = true;
    auto resources = shared_from_this();
    auto effect = found->second;
    effect->withdrawing = true;
    (void)services->retireEffect(id);
    owner.lock()->observe(effect->token->close(),
                          [resources, effect, op](NativeInstanceControl&, const Result<CleanupReport>& result) {
                              if (result.hasValue() && result.value().quiescent) {
                                  effect->closed = true;
                                  resources->effects.erase(effect->id);
                                  resources->deliver(op, EVE_OK);
                              } else {
                                  auto error = hostError(ErrorCode::CleanupFailed, "native effect retained resources");
                                  if (result.hasValue())
                                      error.causes = result.value().errors;
                                  else
                                      error.causes.push_back(result.error());
                                  resources->failure = std::move(error);
                                  resources->deliver(op, EVE_E_FAILED);
                              }
                          });
    *output = op->id;
    return EVE_PENDING;
}
Task<void> NativeResources::close()
{
    if (!closing) {
        closing = true;
        nextEffect();
    }
    return stopped.task();
}
void NativeResources::nextEffect()
{
    if (!closing || closingEffect || failure)
        return;
    std::shared_ptr<NativeEffect> effect;
    for (auto it = effects.rbegin(); it != effects.rend(); ++it)
        if (!it->second->closed) {
            effect = it->second;
            break;
        }
    if (!effect) {
        effectsDone = true;
        checkClosed();
        return;
    }
    closingEffect = true;
    auto resources = shared_from_this();
    owner.lock()->observe(
        effect->token->close(), [resources, effect](NativeInstanceControl&, const Result<CleanupReport>& result) {
            resources->closingEffect = false;
            if (!result.hasValue() || !result.value().quiescent) {
                auto error = hostError(ErrorCode::CleanupFailed, "native resource scope not quiescent");
                if (result.hasValue())
                    error.causes = result.value().errors;
                else
                    error.causes.push_back(result.error());
                resources->failure = error;
                resources->stopped.reject(std::move(error));
                return;
            }
            effect->closed = true;
            resources->effects.erase(effect->id);
            resources->nextEffect();
        });
}
void NativeResources::checkClosed()
{
    if (closing && effectsDone && operations.empty() && retainedCaptures == 0) {
        services->releaseConsumers();
        effects.clear();
        stopped.resolve();
    }
}
Result<ScopeSnapshot> NativeResources::snapshot() const
{
    return scope.snapshot();
}
bool NativeResources::serviceEffectExists(EveHandle id) const
{
    const auto found = effects.find(id);
    return found != effects.end() && !found->second->closed && !found->second->withdrawing &&
           !found->second->cleanupPending;
}
bool NativeResources::serviceEffectReady(EveHandle id) const
{
    if (!serviceEffectExists(id))
        return false;
    auto effect = effects.at(id);
    auto result = effect->token->ready().tryResult();
    auto self = owner.lock();
    return effect->succeeded && result && result->hasValue() && self && !self->closing &&
           self->state == NativeInstanceState::Started;
}
bool NativeResources::cleanupServices() const
{
    auto self = owner.lock();
    if (self && self->stopIssued)
        return true;
    for (const auto& effect : effects)
        if (effect.second->cleanupPending)
            return true;
    return false;
}
Task<void> NativeResources::ready() const
{
    std::vector<Task<void>> tasks;
    for (const auto& value : effects)
        if (!value.second->closed && !value.second->withdrawing)
            tasks.push_back(value.second->token->ready());
    if (tasks.empty())
        return completedTask();
    Promise<void> done;
    auto remaining = std::make_shared<std::atomic_size_t>(tasks.size());
    for (const auto& task : tasks)
        eve::detail::TaskAccess::observe(task, [done, remaining](Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
            if (remaining->fetch_sub(1) == 1)
                done.resolve();
        });
    return done.task();
}
} // namespace eve::module::detail
