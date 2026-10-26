/** @file connection.cpp
 * @brief 原生直接桥只登记宿主令牌，业务在 owner 进入；关闭不依赖业务队列继续开放。
 */
#include <cordis/qt/connection.hpp>
#include <cordis/qt/object.hpp>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <set>

namespace cordis::qt::detail {
/** @brief 在途记录仅包含宿主任务值，完成源可在任意线程写入 result。 */
struct ConnectionInvocation {
    bool returned = false;
    Task<void>::Snapshot result;
};
/** @brief 原生线程只访问锁下的值；插件函数及捕获始终在 owner 调用/释放。 */
struct ConnectionControl : std::enable_shared_from_this<ConnectionControl> {
    std::shared_ptr<QtScheduler> scheduler;
    ContextIdentity owner;
    std::function<Result<void>()> guard;
    std::shared_ptr<std::function<Task<void>()>> callback;
    std::optional<QObjectResource> resource;
    QMetaObject::Connection native;
    mutable std::mutex mutex;
    QObject* host = nullptr;
    CancellationToken cancellation;
    std::set<std::uint64_t> pending;
    std::map<std::uint64_t, std::shared_ptr<ConnectionInvocation>> active;
    std::uint64_t sequence = 0;
    // 仅 owner 修改；原生通知可重入关闭，最后一个断连栈返回前不能释放插件捕获。
    std::size_t disconnectDepth = 0;
    std::size_t maximumPending = 256, accepted = 0, executed = 0, discarded = 0, rejected = 0, errors = 0;
    bool registering = true, attached = false, armed = false, closing = false, closed = false, nativeRetired = true;
    bool nativeDestroyed = false, deleting = false, draining = false, drainAgain = false, releasing = false,
         wakePending = false, blocked = false;
    std::optional<ErrorInfo> lastError;
    Promise<void> stopped;
    std::shared_ptr<ConnectionControl> retained;
    /** @brief 调用方持锁；只投递宿主弱通知，不能内联用户逻辑或与 QObject 销毁竞争。 */
    void wakeLocked() noexcept
    {
        if (wakePending || !host)
            return;
        wakePending = true;
        std::weak_ptr<ConnectionControl> weak = shared_from_this();
        try {
            if (QMetaObject::invokeMethod(
                    host,
                    [weak] {
                        if (auto state = weak.lock())
                            state->drain();
                    },
                    Qt::QueuedConnection))
                return;
        } catch (...) {
        }
        wakePending = false;
        blocked = true;
    }
    /** @brief 任意线程丢弃宿主票据，业务载荷仍由 owner 持有。 */
    void drop(std::uint64_t id, bool deliveryFailure) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!pending.erase(id))
            return;
        ++discarded;
        if (deliveryFailure)
            blocked = true;
        wakeLocked();
    }
    /** @brief 原生调用永不进入插件；有界票据在 owner 再检查代次和取消。 */
    void signal() noexcept
    {
        try {
            std::uint64_t id;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!armed || closing || cancellation.isCancellationRequested()) {
                    ++rejected;
                    return;
                }
                if (pending.size() + active.size() >= maximumPending ||
                    sequence == std::numeric_limits<std::uint64_t>::max()) {
                    ++rejected;
                    return;
                }
                id = ++sequence;
                pending.insert(id);
                ++accepted;
            }
            auto self = shared_from_this();
            try {
                if (scheduler->post({[self, id] { self->enter(id); }, [self, id] { self->drop(id, true); }}))
                    return;
            } catch (...) {
            }
            drop(id, true);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            blocked = true;
            wakeLocked();
        }
    }
    /** @brief 原生闭包最后一个快照释放后才允许删除接收宿主。 */
    void nativeEnded() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        nativeRetired = true;
        wakeLocked();
    }
    /** @brief 业务入口只在 scheduler owner 运行，回调栈返回之前不发布在途完成。 */
    void enter(std::uint64_t id)
    {
        auto invocation = std::make_shared<ConnectionInvocation>();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!pending.erase(id))
                return;
            if (closing || cancellation.isCancellationRequested()) {
                ++discarded;
                return;
            }
            // pending 转为 active 必须在同一锁内，发射线程不能利用入口间隙突破总预算。
            active.emplace(id, invocation);
        }
        auto allowed = guard();
        if (!allowed.hasValue()) {
            std::lock_guard<std::mutex> lock(mutex);
            active.erase(id);
            ++discarded;
            lastError = allowed.error();
            wakeLocked();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++executed;
        }
        auto function = callback;
        std::weak_ptr<ConnectionControl> weak = shared_from_this();
        try {
            auto task = (*function)();
            if (cordis::detail::TaskAccess::sameTask(task, stopped.task())) {
                invocation->result = std::make_shared<const Result<void>>(Result<void>::failure(
                    makeError(ErrorCode::CleanupCycle, "connection callback returned its own close task")));
            } else
                cordis::detail::TaskAccess::observe(task, [weak, invocation](Task<void>::Snapshot result) {
                    if (auto state = weak.lock()) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        invocation->result = std::move(result);
                        state->wakeLocked();
                    }
                });
        } catch (const std::exception& error) {
            invocation->result = std::make_shared<const Result<void>>(
                Result<void>::failure(makeError(ErrorCode::CallbackFailed, error.what())));
        } catch (...) {
            invocation->result = std::make_shared<const Result<void>>(
                Result<void>::failure(makeError(ErrorCode::CallbackFailed, "Qt connection callback threw")));
        }
        function.reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            invocation->returned = true;
            wakeLocked();
        }
    }
    /** @brief 原生 functor 可先于 disconnectNotify 释放；断连栈也必须阻止业务捕获提前析构。 */
    void disconnectNative()
    {
        ++disconnectDepth;
        struct Frame {
            std::size_t& depth;
            /** @brief 通知抛错时仍恢复深度，不把重入栈永久留在资源账目中。 */
            ~Frame() { --depth; }
        } frame{disconnectDepth};
        auto connection = std::exchange(native, {});
        QObject::disconnect(connection);
        connection = {};
    }
    /** @brief 同步关门和移除未进入的票据；断连和插件捕获析构不得在锁内执行。 */
    Task<void> stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            closing = true;
            armed = false;
            discarded += pending.size();
            pending.clear();
        }
        disconnectNative();
        drain();
        return stopped.task();
    }
    /** @brief owner 汇总完成任务，关闭等所有原生快照/回调及捕获释放后再删除宿主。 */
    void drain()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            wakePending = false;
        }
        if (draining) {
            drainAgain = true;
            return;
        }
        draining = true;
        struct Reset {
            ConnectionControl& state;
            /** @brief 嵌套推进只登记后续一次检查，不丢失完成通知或在嵌套循环中忙等。 */
            ~Reset()
            {
                state.draining = false;
                if (std::exchange(state.drainAgain, false)) {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.wakeLocked();
                }
            }
        } reset{*this};
        bool ended;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = active.begin(); it != active.end();) {
                const auto& invocation = *it->second;
                if (!invocation.returned || !invocation.result) {
                    ++it;
                    continue;
                }
                if (!invocation.result->hasValue()) {
                    ++errors;
                    lastError = invocation.result->error();
                }
                it = active.erase(it);
            }
            ended = nativeRetired && !registering && attached;
            if (blocked && !lastError)
                lastError = makeError(ErrorCode::SchedulerClosed, "Qt connection notification delivery blocked");
        }
        if (ended && !closing) {
            stop();
        }
        bool dispose;
        {
            std::lock_guard<std::mutex> lock(mutex);
            dispose = closing && !registering && disconnectDepth == 0 && active.empty() && nativeRetired && !deleting;
        }
        if (!dispose)
            return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            releasing = true;
        }
        callback.reset(); // 捕获析构可重入 close/Qt 循环；draining 仍为 true。
        guard = {};
        {
            std::lock_guard<std::mutex> lock(mutex);
            releasing = false;
            deleting = true;
        }
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(resource->close(), [self](Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                self->retained = self;
                self->stopped.reject(result->error());
                return;
            }
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                self->nativeDestroyed = true;
                self->closed = true;
            }
            self->stopped.resolve();
        });
    }
    /** @brief 应用过早退出只做 owner 关门；无法证明静止时保留控制块并报告失败。 */
    void hostGone()
    {
        bool planned;
        {
            std::lock_guard<std::mutex> lock(mutex);
            host = nullptr;
            planned = deleting;
        }
        if (planned)
            return;
        {
            std::lock_guard<std::mutex> lock(mutex);
            closing = true;
            armed = false;
            discarded += pending.size();
            pending.clear();
        }
        disconnectNative();
        retained = shared_from_this();
        stopped.reject(makeError(ErrorCode::CleanupFailed, "application ended before managed connection drained"));
    }
};
/** @brief 原生 Qt functor 可以在发射线程销毁；只更新宿主租约，不释放插件函数。 */
struct NativeConnectionLease {
    std::shared_ptr<ConnectionControl> control;
    /** @brief 最后一个 Qt 调用快照释放只登记宿主通知。 */
    ~NativeConnectionLease() { control->nativeEnded(); }
};
/** @brief 通过 QObjectResource 等完整 delete 返回，避免把 destroyed 信号当成删除完成。 */
class ConnectionHost final : public QObject {
public:
    /** @brief 应用线程创建，所有原生回调仅携带宿主租约。 */
    explicit ConnectionHost(std::shared_ptr<ConnectionControl> state) : state_(std::move(state)) {}
    /** @brief 正常关闭和应用退出走不同的确认路径。 */
    ~ConnectionHost() override { state_->hostGone(); }

private:
    std::shared_ptr<ConnectionControl> state_;
};
SignalBridge::SignalBridge(std::shared_ptr<ConnectionControl> control)
{
    lease_ = std::make_shared<NativeConnectionLease>();
    lease_->control = control;
    {
        std::lock_guard<std::mutex> lock(control->mutex);
        control->nativeRetired = false;
    }
}
void SignalBridge::operator()() const noexcept
{
    lease_->control->signal();
}
} // namespace cordis::qt::detail

namespace cordis::qt {
/** @brief effect 清理先登记，外部 connectNotify 重入停止时也不会漏记资源。 */
Result<QtConnection> QtConnection::create(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                          std::function<Task<void>()> callback, std::string label,
                                          std::size_t maximumPending, Installer install)
{
    if (!scheduler || !scheduler->isCurrentThread())
        return Result<QtConnection>::failure(makeError(ErrorCode::WrongThread, "Qt connection requires owner thread"));
    if (!callback || maximumPending == 0 || maximumPending > 65536)
        return Result<QtConnection>::failure(
            makeError(ErrorCode::InvalidArgument, "invalid connection callback or queue limit"));
    auto guard = owner.eventGuard(scheduler);
    if (!guard.hasValue())
        return Result<QtConnection>::failure(guard.error());
    auto state = std::make_shared<detail::ConnectionControl>();
    state->scheduler = scheduler;
    state->owner = owner.identity().value();
    state->guard = guard.value();
    state->maximumPending = maximumPending;
    state->callback = std::make_shared<std::function<Task<void>()>>(std::move(callback));
    std::unique_ptr<QObject> host = std::make_unique<detail::ConnectionHost>(state);
    state->host = host.get();
    state->resource.emplace(host, scheduler);
    auto effect = owner.effect(
        [state](EffectSetupContext setup) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->cancellation = setup.cancellation();
            }
            auto cleanup = setup.addCleanup([state] { return state->stop(); });
            if (!cleanup.hasValue())
                return failedTask(cleanup.error());
            state->attached = true;
            return completedTask();
        },
        std::move(label));
    if (!effect.hasValue()) {
        state->registering = false;
        state->stop();
        return Result<QtConnection>::failure(effect.error());
    }
    std::optional<ErrorInfo> failure;
    if (!state->attached)
        failure = makeError(ErrorCode::ActivationFailed, "connection cleanup registration failed");
    else {
        try {
            state->native = install(state->host, detail::SignalBridge(state));
        } catch (const std::exception& error) {
            failure = makeError(ErrorCode::CallbackFailed, error.what());
        } catch (...) {
            failure = makeError(ErrorCode::CallbackFailed, "Qt connection installation threw");
        }
        if (!failure && !state->native)
            failure = makeError(ErrorCode::InvalidArgument, "Qt signal connection failed");
        auto allowed = state->guard();
        if (!failure && !allowed.hasValue())
            failure = allowed.error();
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->registering = false;
        if (!failure && (state->closing || state->cancellation.isCancellationRequested()))
            failure = makeError(ErrorCode::ContextClosing, "connection owner closed during registration");
        if (!failure)
            state->armed = true;
    }
    if (failure) {
        state->stop();
        effect.value().close();
        return Result<QtConnection>::failure(*failure);
    }
    state->drain();
    return Result<QtConnection>::success(QtConnection(state, effect.value()));
}
/** @brief 初始化和资源关闭分别由 effect 和原生资源证明。 */
Task<void> QtConnection::ready() const
{
    return effect_.ready();
}
/** @brief effect 取消先同步阻止新业务，之后按清理协议排空。 */
Task<void> QtConnection::close() const
{
    auto state = control_;
    auto effect = effect_;
    if (!state->scheduler->isCurrentThread())
        return failedTask(makeError(ErrorCode::WrongThread, "connection close requires owner thread"));
    effect.close();
    return state->stop();
}
/** @brief 所有跨线程账目在同一锁下复制。 */
Result<ConnectionSnapshot> QtConnection::snapshot() const
{
    auto state = control_;
    if (!state->scheduler->isCurrentThread())
        return Result<ConnectionSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "connection snapshot requires owner thread"));
    std::lock_guard<std::mutex> lock(state->mutex);
    ConnectionSnapshot value;
    value.owner = state->owner;
    value.open = state->armed && !state->closing && !state->cancellation.isCancellationRequested();
    value.closing = state->closing;
    value.closed = state->closed;
    value.nativeRetired = state->nativeRetired;
    value.nativeDestroyed = state->nativeDestroyed;
    value.deliveryBlocked = state->blocked;
    value.queued = state->pending.size();
    value.running = state->active.size() + (state->releasing ? 1u : 0u);
    value.accepted = state->accepted;
    value.executed = state->executed;
    value.discarded = state->discarded;
    value.rejected = state->rejected;
    value.callbackErrors = state->errors;
    value.lastError = state->lastError;
    return Result<ConnectionSnapshot>::success(std::move(value));
}
} // namespace cordis::qt
