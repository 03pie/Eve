/** @file request.cpp
 * @brief 一次完成与入口返回仲裁、owner 投递及保守账目；不调用插件函数。
 */
#include "json_p.hpp"
#include "request_p.hpp"
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace cordis::module::detail {
namespace {
ErrorInfo requestError(ErrorCode code, const char* message)
{
    auto error = makeError(code, message);
    error.phase = "module.request";
    return error;
}
std::uint64_t allocate(std::atomic<std::uint64_t>& next)
{
    auto value = next.load(std::memory_order_relaxed);
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("native request/context identifiers exhausted");
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return value;
    }
}
std::atomic<std::uint64_t> contexts{1}, requests{1};
bool finalStatus(CordisStatus value)
{
    return value <= CORDIS_OK && value >= CORDIS_E_BUSY;
}
bool cleanup(NativeRequestKind kind)
{
    return kind == NativeRequestKind::Stop || kind == NativeRequestKind::Cleanup;
}
ResourceKind resourceKind(NativeRequestKind kind)
{
    if (kind == NativeRequestKind::Start)
        return ResourceKind::SetupTask;
    if (cleanup(kind))
        return ResourceKind::CleanupTask;
    if (kind == NativeRequestKind::Callback)
        return ResourceKind::Callback;
    return ResourceKind::Call;
}
struct RequestEntry {
    CordisRequestId id;
    NativeRequestKind kind;
    bool returned = false, attempted = false, receiving = false, completed = false;
    bool cancelled = false, queued = false, unsafe = false, blocked = false;
    CordisStatus returnStatus = CORDIS_PENDING, status = CORDIS_OK;
    std::size_t reserved = 0;
    std::string json;
    std::optional<ErrorInfo> error;
    ModuleLease entryLease, operationLease, payloadLease;
    Promise<NativeOutcome> result;
};
enum class Ready { Waiting, Queue, Blocked };
} // namespace
struct RequestControl {
    RequestControl(std::shared_ptr<IScheduler> target, GenerationLedger resources, InstanceId instanceId,
                   Epoch activation, RequestLimits budget)
        : scheduler(target), ledger(resources), owner(std::this_thread::get_id()), context(allocate(contexts)),
          instance(instanceId), epoch(activation), limits(budget)
    {
    }
    std::mutex mutex;
    std::weak_ptr<IScheduler> scheduler;
    GenerationLedger ledger;
    const std::thread::id owner;
    const CordisHandle context;
    const InstanceId instance;
    const Epoch epoch;
    const RequestLimits limits;
    bool closing = false, sealed = false, faulted = false;
    std::uint64_t protocolErrors = 0;
    std::size_t buffered = 0;
    std::map<CordisRequestId, std::shared_ptr<RequestEntry>> entries;
    // 未完成或投递失败时，不能因最后一个包装句柄销毁而丢弃原生引用。
    std::shared_ptr<RequestControl> retained;
    Promise<void> drained;
};
namespace {
bool owner(const RequestControl& state)
{
    return state.owner == std::this_thread::get_id();
}
void protocol(RequestControl& state)
{
    state.faulted = true;
    if (state.protocolErrors != std::numeric_limits<std::uint64_t>::max())
        ++state.protocolErrors;
    (void)state.ledger.pin("abiProtocol");
}
Ready prepare(RequestControl& state, RequestEntry& entry)
{
    if (entry.queued || !entry.returned || entry.receiving)
        return Ready::Waiting;
    if (entry.returnStatus == CORDIS_PENDING && !entry.completed)
        return Ready::Waiting;
    if (entry.returnStatus != CORDIS_PENDING && (!finalStatus(entry.returnStatus) || entry.attempted)) {
        entry.unsafe = true;
        entry.error =
            requestError(ErrorCode::AbiProtocol, "invalid entry return or synchronous return plus completion");
        protocol(state);
    } else if (entry.returnStatus != CORDIS_PENDING) {
        entry.status = entry.returnStatus;
    }
    if (!entry.payloadLease) {
        auto lease = state.ledger.acquire(ResourceKind::QueuePayload, "native.result");
        if (!lease.hasValue())
            return Ready::Blocked;
        entry.payloadLease = std::move(lease.value());
    }
    entry.queued = true;
    return Ready::Queue;
}
void deliveryFailed(const std::shared_ptr<RequestControl>& state, const std::shared_ptr<RequestEntry>& entry) noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (entry->blocked)
                return;
            entry->blocked = true;
            state->faulted = true;
        }
        (void)state->ledger.pin("requestDeliveryBlocked");
        entry->result.reject(requestError(ErrorCode::SchedulerClosed, "native request delivery/retirement blocked"));
    } catch (...) {
        // 保留控制块和账目，异常不能通过 C complete 边界逃逸。
    }
}
void retire(const std::shared_ptr<RequestControl>& state, const std::shared_ptr<RequestEntry>& entry)
{
    if (!owner(*state)) {
        deliveryFailed(state, entry);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (entry->unsafe || entry->blocked)
            return;
    }
    // run 已返回，Promise 的通知投递已结束；这里只析构宿主值，没有 DLL 捕获。
    entry->entryLease.reset();
    entry->operationLease.reset();
    entry->payloadLease.reset();
    std::shared_ptr<RequestControl> retained;
    bool drained;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->buffered -= entry->reserved;
        entry->reserved = 0;
        state->entries.erase(entry->id);
        drained = state->sealed && state->entries.empty();
        if (state->entries.empty())
            retained.swap(state->retained);
    }
    if (drained)
        state->drained.resolve();
}
struct Delivery {
    std::shared_ptr<RequestControl> state;
    std::shared_ptr<RequestEntry> entry;
    std::atomic_bool ran{false};
    Delivery(std::shared_ptr<RequestControl> source, std::shared_ptr<RequestEntry> request)
        : state(std::move(source)), entry(std::move(request))
    {
    }
    ~Delivery()
    {
        try {
            if (!ran.load(std::memory_order_acquire)) {
                deliveryFailed(state, entry);
                return;
            }
            if (owner(*state)) {
                retire(state, entry);
                return;
            }
            // 合法 scheduler 可在 post 返回前已运行/销毁队列中的副本。
            // 若最后一个投递捕获在生产者线程退役，将账目交回 owner。
            auto target = state->scheduler.lock();
            ScheduledJob job{[state = state, entry = entry] { retire(state, entry); },
                             [state = state, entry = entry] { deliveryFailed(state, entry); }};
            if (!target || !target->post(std::move(job)))
                deliveryFailed(state, entry);
        } catch (...) {
            deliveryFailed(state, entry);
        }
    }
    void run()
    {
        if (!owner(*state)) {
            deliveryFailed(state, entry);
            return;
        }
        std::optional<ErrorInfo> error;
        NativeOutcome outcome;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (entry->blocked)
                return;
            error = entry->error;
            outcome = {entry->status, std::move(entry->json), entry->cancelled};
        }
        if (error)
            entry->result.reject(*error);
        else
            entry->result.resolve(std::move(outcome));
        ran.store(true, std::memory_order_release);
    }
};
void dispatch(const std::shared_ptr<RequestControl>& state, const std::shared_ptr<RequestEntry>& entry,
              Ready ready) noexcept
{
    if (ready == Ready::Waiting)
        return;
    if (ready == Ready::Blocked) {
        deliveryFailed(state, entry);
        return;
    }
    try {
        auto target = state->scheduler.lock();
        auto delivery = std::make_shared<Delivery>(state, entry);
        ScheduledJob job{[delivery] { delivery->run(); }, [state, entry] { deliveryFailed(state, entry); }};
        delivery.reset();
        if (!target || !target->post(std::move(job)))
            deliveryFailed(state, entry);
    } catch (...) {
        deliveryFailed(state, entry);
    }
}
} // namespace

RequestChannel::RequestChannel(std::shared_ptr<IScheduler> scheduler, GenerationLedger ledger, InstanceId instance,
                               Epoch epoch, RequestLimits limits)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() || !instance.valid() || epoch == 0 ||
        limits.maximumRequests == 0 || limits.maximumRequests > 1024 || limits.maximumJsonBytes == 0 ||
        limits.maximumJsonBytes > CORDIS_MAX_JSON_BYTES || limits.maximumBufferedBytes < limits.maximumJsonBytes ||
        limits.maximumBufferedBytes > 64 * 1024 * 1024)
        throw std::invalid_argument("invalid native request channel owner, identity or limits");
    control_ = std::make_shared<RequestControl>(std::move(scheduler), ledger, instance, epoch, limits);
}
RequestChannel::~RequestChannel()
{
    try {
        std::lock_guard<std::mutex> lock(control_->mutex);
        if (!control_->entries.empty())
            (void)control_->ledger.pin("abandonedRequests");
    } catch (...) {
    }
}
CordisHandle RequestChannel::context() const noexcept
{
    return control_->context;
}
Result<NativeTicket> RequestChannel::begin(NativeRequestKind kind, std::string label)
{
    auto state = control_;
    if (!owner(*state))
        return Result<NativeTicket>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    auto target = state->scheduler.lock();
    if (!target || !target->isOpen())
        return Result<NativeTicket>::failure(requestError(ErrorCode::SchedulerClosed, "request scheduler is closed"));
    std::lock_guard<std::mutex> lock(state->mutex);
    if (kind < NativeRequestKind::Start || kind > NativeRequestKind::Cleanup)
        return Result<NativeTicket>::failure(requestError(ErrorCode::InvalidArgument, "unknown native request kind"));
    if (state->sealed || ((state->closing || state->faulted) && !cleanup(kind)))
        return Result<NativeTicket>::failure(
            requestError(ErrorCode::ContextClosing, "native context no longer accepts business"));
    if (state->entries.size() >= state->limits.maximumRequests)
        return Result<NativeTicket>::failure(requestError(ErrorCode::ModulePinned, "native request budget exhausted"));
    auto call = state->ledger.acquire(ResourceKind::Call, label);
    if (!call.hasValue())
        return Result<NativeTicket>::failure(call.error());
    auto operation = state->ledger.acquire(resourceKind(kind), std::move(label));
    if (!operation.hasValue())
        return Result<NativeTicket>::failure(operation.error());
    auto entry = std::make_shared<RequestEntry>();
    entry->id = allocate(requests);
    entry->kind = kind;
    entry->entryLease = std::move(call.value());
    entry->operationLease = std::move(operation.value());
    state->entries.emplace(entry->id, entry);
    state->retained = state;
    return Result<NativeTicket>::success({entry->id, entry->result.task()});
}
Result<void> RequestChannel::returned(CordisRequestId id, CordisStatus status)
{
    auto state = control_;
    if (!owner(*state))
        return Result<void>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    std::shared_ptr<RequestEntry> entry;
    Ready ready;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto found = state->entries.find(id);
        if (found == state->entries.end() || found->second->returned) {
            protocol(*state);
            return Result<void>::failure(requestError(ErrorCode::AbiProtocol, "unknown or duplicate entry return"));
        }
        entry = found->second;
        entry->returned = true;
        entry->returnStatus = status;
        entry->entryLease.reset();
        ready = prepare(*state, *entry);
    }
    dispatch(state, entry, ready);
    return Result<void>::success();
}
CordisStatus RequestChannel::complete(CordisHandle contextId, CordisRequestId id, CordisStatus status,
                                      CordisBytesV1 bytes) noexcept
{
    auto state = control_;
    std::shared_ptr<RequestEntry> entry;
    bool receiving = false;
    try {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto found = state->entries.find(id);
            if (contextId != state->context || found == state->entries.end()) {
                protocol(*state);
                return CORDIS_E_ABI_PROTOCOL;
            }
            entry = found->second;
            if (entry->receiving || entry->completed || entry->queued || entry->blocked ||
                (entry->returned && entry->returnStatus != CORDIS_PENDING)) {
                protocol(*state);
                return CORDIS_E_ABI_PROTOCOL;
            }
            entry->attempted = true;
            if (!finalStatus(status)) {
                protocol(*state);
                return CORDIS_E_ABI_PROTOCOL;
            }
            if (bytes.size && !bytes.data) {
                protocol(*state);
                return CORDIS_E_INVALID_ARGUMENT;
            }
            if (bytes.size > state->limits.maximumJsonBytes ||
                bytes.size > state->limits.maximumBufferedBytes - state->buffered)
                return CORDIS_E_LIMIT;
            auto payload = state->ledger.acquire(ResourceKind::QueuePayload, "native.result");
            if (!payload.hasValue())
                return CORDIS_E_LIMIT;
            entry->payloadLease = std::move(payload.value());
            entry->receiving = true;
            entry->reserved = static_cast<std::size_t>(bytes.size);
            state->buffered += entry->reserved;
            receiving = true;
        }
        std::string json;
        if (bytes.size)
            json.assign(reinterpret_cast<const char*>(bytes.data), static_cast<std::size_t>(bytes.size));
        const bool valid = json.empty() || validAbiJson(json);
        Ready ready;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            entry->receiving = false;
            receiving = false;
            if (valid) {
                entry->completed = true;
                entry->status = status;
                entry->json = std::move(json);
            } else {
                state->buffered -= entry->reserved;
                entry->reserved = 0;
                entry->payloadLease.reset();
                protocol(*state);
            }
            ready = prepare(*state, *entry);
        }
        dispatch(state, entry, ready);
        return valid ? CORDIS_OK : CORDIS_E_INVALID_ARGUMENT;
    } catch (...) {
        try {
            Ready ready = Ready::Waiting;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (receiving) {
                    entry->receiving = false;
                    state->buffered -= entry->reserved;
                    entry->reserved = 0;
                    entry->payloadLease.reset();
                    ready = prepare(*state, *entry);
                }
                (void)state->ledger.pin("completionCopyFailed");
            }
            if (entry)
                dispatch(state, entry, ready);
        } catch (...) {
        }
        return CORDIS_E_FAILED;
    }
}
Result<bool> RequestChannel::cancel(CordisRequestId id)
{
    auto state = control_;
    if (!owner(*state))
        return Result<bool>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    std::lock_guard<std::mutex> lock(state->mutex);
    auto found = state->entries.find(id);
    if (found == state->entries.end())
        return Result<bool>::failure(requestError(ErrorCode::AbiProtocol, "unknown request cancellation"));
    if (found->second->result.task().isReady())
        return Result<bool>::success(false);
    const bool changed = !found->second->cancelled;
    found->second->cancelled = true;
    return Result<bool>::success(changed);
}
Result<void> RequestChannel::close()
{
    auto state = control_;
    if (!owner(*state))
        return Result<void>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    std::lock_guard<std::mutex> lock(state->mutex);
    state->closing = true;
    return Result<void>::success();
}
Result<void> RequestChannel::seal()
{
    auto state = control_;
    if (!owner(*state))
        return Result<void>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    bool drained;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->closing = true;
        state->sealed = true;
        drained = state->entries.empty();
    }
    if (drained)
        state->drained.resolve();
    return Result<void>::success();
}
Task<void> RequestChannel::drained() const
{
    return control_->drained.task();
}
Result<NativeRequestSnapshot> RequestChannel::snapshot() const
{
    auto state = control_;
    if (!owner(*state))
        return Result<NativeRequestSnapshot>::failure(requestError(ErrorCode::WrongThread, "request owner required"));
    std::lock_guard<std::mutex> lock(state->mutex);
    NativeRequestSnapshot result{state->context,        state->instance, state->epoch,
                                 state->closing,        state->sealed,   state->faulted,
                                 state->protocolErrors, state->buffered, {}};
    for (const auto& pair : state->entries) {
        const auto& e = *pair.second;
        result.requests.push_back({e.id, e.kind, e.returned, e.completed, e.cancelled, e.queued, e.unsafe, e.blocked});
    }
    return Result<NativeRequestSnapshot>::success(std::move(result));
}
} // namespace cordis::module::detail
