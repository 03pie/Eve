/** @file worker.cpp
 * @brief owner 轮询 Windows 原生线程信号，以零等待确认 TLS 退出后才释放业务捕获。
 */
#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <cordis/qt/object.hpp>
#include <cordis/qt/worker.hpp>
#include <exception>
#include <mutex>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#endif

namespace cordis::qt::detail {
/** @brief 只有 bodyEntered/bodyReturned/bodyResult 由 worker 在锁下写，其余生命周期仅在 owner 推进。 */
struct WorkerControl : std::enable_shared_from_this<WorkerControl> {
    std::shared_ptr<QtScheduler> scheduler;
    ContextIdentity owner;
    std::function<Result<void>()> guard;
    std::shared_ptr<std::function<Result<void>(WorkerCancellation)>> work;
    CancellationSource localCancel;
    CancellationToken ownerCancel;
    std::optional<WorkerCancellation> cancellation;
    mutable std::mutex mutex;
    Task<void>::Snapshot bodyResult;
    std::optional<ErrorInfo> lastError;
    std::optional<QObjectResource> resource;
    QTimer* poller = nullptr;
    void* native = nullptr;
    bool registering = true, attached = false, started = false;
    bool bodyEntered = false, bodyReturned = false;
    bool closing = false, joined = false, nativeHandleClosed = false;
    bool draining = false, releasing = false, capturesReleased = false, deleting = false;
    bool hostDestroyed = false, closed = false, quarantined = false;
    bool retirementExpected = false, retired = false, retirementBlocked = false;
    Promise<void> completed, stopped;
    std::shared_ptr<WorkerControl> retained;
    /** @brief 异常退出只报告失败并固定未知资源，不跨线程删除正在执行的工作。 */
    void quarantine(ErrorInfo error)
    {
        if (quarantined)
            return;
        localCancel.requestCancel();
        closing = true;
        quarantined = true;
        lastError = error;
        retained = shared_from_this();
        completed.reject(error);
        stopped.reject(std::move(error));
    }
    /** @brief 原生入口执行同步业务并只发布宿主值；闭包的最终所有权仍留在 owner。 */
    void run()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            bodyEntered = true;
        }
        Task<void>::Snapshot report;
        try {
            auto value = (*work)(*cancellation);
            report = std::make_shared<const Result<void>>(std::move(value));
        } catch (const std::exception& error) {
            report = std::make_shared<const Result<void>>(
                Result<void>::failure(makeError(ErrorCode::CallbackFailed, error.what())));
        } catch (...) {
            report = std::make_shared<const Result<void>>(
                Result<void>::failure(makeError(ErrorCode::CallbackFailed, "worker body threw")));
        }
        std::lock_guard<std::mutex> lock(mutex);
        bodyResult = std::move(report);
        bodyReturned = true;
    }
    /** @brief 业务延续在本资源的 effect 清理窗口结束后运行，不误伤仍有效的 Context。 */
    void publish()
    {
        if (!closed || completed.task().isReady())
            return;
        if (!lastError && retirementBlocked)
            lastError = makeError(ErrorCode::SchedulerClosed, "worker effect retirement was not delivered");
        if (!lastError) {
            auto allowed = guard();
            if (!allowed.hasValue())
                lastError = allowed.error();
        }
        guard = {};
        if (lastError)
            completed.reject(*lastError);
        else
            completed.resolve();
    }
    /** @brief 生命周期投递失败不回退原生证明；业务任务失败，Core 账目仍保持未知。 */
    void retirementLost()
    {
        if (retired || retirementBlocked)
            return;
        retirementBlocked = true;
        publish();
    }
    /** @brief 保留一个可被丢弃的宿主通知，避免 scheduler 先关闭时结果永久等待 Core 回执。 */
    void watchRetirement(EffectToken token)
    {
        if (retired || retirementBlocked)
            return;
        auto self = shared_from_this();
        try {
            if (scheduler->post({[self, token] {
                                     if (token.snapshot().value().deliveryBlocked)
                                         self->retirementLost();
                                     else
                                         self->watchRetirement(token);
                                 },
                                 [self] { self->retirementLost(); }}))
                return;
        } catch (...) {
        }
        retirementLost();
    }
    /** @brief 先排队退役，再由 Core 排队清理，已登记的 ready 延续先于本 effect 的清理窗口。 */
    void retire(EffectToken token)
    {
        auto self = shared_from_this();
        try {
            if (scheduler->post(
                    {[self, token] {
                         auto report = token.close();
                         cordis::detail::TaskAccess::observe(report, [self](Task<CleanupReport>::Snapshot result) {
                             self->retired = result->hasValue() && result->value().quiescent;
                             if (!self->retired) {
                                 self->retirementBlocked = true;
                                 if (!self->lastError) {
                                     auto error = makeError(ErrorCode::CleanupFailed, "worker effect did not retire");
                                     if (result->hasValue())
                                         error.causes = result->value().errors;
                                     else
                                         error.causes.push_back(result->error());
                                     self->lastError = std::move(error);
                                 }
                             }
                             self->publish();
                         });
                         self->watchRetirement(token);
                     },
                     [self] { self->retirementLost(); }}))
                return;
        } catch (...) {
        }
        retirementLost();
    }
    /** @brief 原生完成时先选定业务结果；内部退役取消不追溯结果，发布前仍重查 Context。 */
    void finish()
    {
        Task<void>::Snapshot report;
        {
            std::lock_guard<std::mutex> lock(mutex);
            report = bodyResult;
        }
        if (report && !report->hasValue())
            lastError = report->error();
        else if (localCancel.token().isCancellationRequested() || ownerCancel.isCancellationRequested())
            lastError = makeError(ErrorCode::OperationCancelled, "worker owner or resource cancelled");
        else if (!report)
            lastError = makeError(ErrorCode::CallbackFailed, "worker exited without a result");
        closed = true;
        stopped.resolve();
        if (!retirementExpected) {
            retired = true;
            publish();
        }
    }
    /** @brief 仅作零时长原生等待；函数返回不是线程/TLS 已结束的证明。 */
    void drain()
    {
        if (registering || draining || deleting || quarantined)
            return;
        draining = true;
        struct Reset {
            bool& flag;
            /** @brief 捕获析构泵送 Qt 时不允许第二个推进栈删除宿主。 */
            ~Reset() { flag = false; }
        } reset{draining};
        if (started && !joined) {
#if defined(_WIN32)
            const auto state = WaitForSingleObject(native, 0);
            if (state == WAIT_TIMEOUT)
                return;
            if (state != WAIT_OBJECT_0) {
                quarantine(makeError(ErrorCode::CleanupFailed, "native worker wait failed"));
                return;
            }
            joined = true;
            if (!CloseHandle(native)) {
                quarantine(makeError(ErrorCode::CleanupFailed, "native worker handle close failed"));
                return;
            }
            native = nullptr;
            nativeHandleClosed = true;
#endif
        }
        if (!started) {
            joined = true;
            nativeHandleClosed = true;
        }
        closing = true;
        poller->stop();
        releasing = true;
        work.reset();
        releasing = false;
        capturesReleased = true;
        if (quarantined)
            return;
        deleting = true;
        auto self = shared_from_this();
        cordis::detail::TaskAccess::observe(resource->close(), [self](Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                self->quarantine(result->error());
                return;
            }
            self->hostDestroyed = true;
            self->finish();
        });
    }
    /** @brief owner 同步请求取消；不阻塞 GUI，不使用 terminate 或 detach。 */
    Task<void> stop()
    {
        closing = true;
        localCancel.requestCancel();
        drain();
        return stopped.task();
    }
    /** @brief QObjectResource 的应用退出兜底不能代替原生线程 join。 */
    void hostGone()
    {
        poller = nullptr;
        if (!deleting)
            quarantine(makeError(ErrorCode::CleanupFailed, "application ended before worker joined"));
    }
};
/** @brief 私有 Qt 宿主只拥有轮询计时器，不能把尚未退出的原生线程作为 QObject 子对象删除。 */
class WorkerHost final : public QObject {
public:
    /** @brief 宿主拥有控制块直到正常删除，原生函数不需要在 worker 析构宿主对象。 */
    explicit WorkerHost(std::shared_ptr<WorkerControl> state) : state_(std::move(state))
    {
        state_->poller = new QTimer(this);
        state_->poller->setInterval(5);
        QObject::connect(state_->poller, &QTimer::timeout, this, [state = state_] { state->drain(); });
    }
    /** @brief 意外删除时固定无法证明已退出的资源，禁止伪报 Kernel 静止。 */
    ~WorkerHost() override { state_->hostGone(); }

private:
    std::shared_ptr<WorkerControl> state_;
};
#if defined(_WIN32)
/** @brief CRT 原生入口不传播 C++ 异常；未能发布结果由 owner 在实际 join 后报告失败。 */
unsigned __stdcall runNative(void* opaque) noexcept
{
    try {
        static_cast<WorkerControl*>(opaque)->run();
    } catch (...) {
    }
    return 0;
}
#endif
} // namespace cordis::qt::detail

namespace cordis::qt {
/** @brief 清理登记成功后才启动原生线程，启动失败也走宿主资源退役路径。 */
Result<QtWorker> QtWorker::start(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                 std::function<Result<void>(WorkerCancellation)> work, std::string label)
{
    if (!scheduler || !scheduler->isCurrentThread())
        return Result<QtWorker>::failure(makeError(ErrorCode::WrongThread, "worker requires owner thread"));
    if (!work)
        return Result<QtWorker>::failure(makeError(ErrorCode::InvalidArgument, "worker requires a body"));
    auto guard = owner.eventGuard(scheduler);
    if (!guard.hasValue())
        return Result<QtWorker>::failure(guard.error());
#if !defined(_WIN32)
    (void)label;
    return Result<QtWorker>::failure(makeError(ErrorCode::InvalidArgument, "managed worker backend requires Windows"));
#else
    auto state = std::make_shared<detail::WorkerControl>();
    state->scheduler = scheduler;
    state->owner = owner.identity().value();
    state->guard = guard.value();
    state->work = std::make_shared<std::function<Result<void>(WorkerCancellation)>>(std::move(work));
    std::unique_ptr<QObject> host = std::make_unique<detail::WorkerHost>(state);
    std::weak_ptr<detail::WorkerControl> weak = state;
    QObject::connect(
        QCoreApplication::instance(), &QObject::destroyed, host.get(),
        [weak] {
            if (auto current = weak.lock())
                if (!current->closed)
                    current->quarantine(
                        makeError(ErrorCode::CleanupFailed, "application destroyed before worker closed"));
        },
        Qt::DirectConnection);
    state->resource.emplace(host, scheduler);
    auto effect = owner.effect(
        [state](EffectSetupContext setup) {
            state->ownerCancel = setup.cancellation();
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
        return Result<QtWorker>::failure(effect.error());
    }
    // 本适配器的 setup 始终同步返回 completedTask，其确认任务先于退役通知进入同一 owner FIFO。
    state->retirementExpected = true;
    cordis::detail::TaskAccess::observe(
        state->stopped.task(), [state, token = effect.value()](Task<void>::Snapshot) { state->retire(token); });
    state->cancellation.emplace(WorkerCancellation(state->ownerCancel, state->localCancel.token()));
    std::optional<ErrorInfo> failure;
    auto allowed = state->guard();
    if (!state->attached || !allowed.hasValue())
        failure = !allowed.hasValue() ? allowed.error()
                                      : makeError(ErrorCode::ActivationFailed, "worker cleanup registration failed");
    else {
        const auto handle = _beginthreadex(nullptr, 0, detail::runNative, state.get(), 0, nullptr);
        if (!handle)
            failure = makeError(ErrorCode::ActivationFailed, "native worker could not start");
        else {
            state->native = reinterpret_cast<void*>(handle);
            state->started = true;
        }
    }
    state->registering = false;
    if (failure) {
        state->stop();
        effect.value().close();
        return Result<QtWorker>::failure(*failure);
    }
    state->poller->start();
    return Result<QtWorker>::success(QtWorker(state, effect.value()));
#endif
}
/** @brief 仅观察初始化，不阻塞等待工作函数。 */
Task<void> QtWorker::ready() const
{
    return effect_.ready();
}
/** @brief 正常业务结果在资源确认后终结；异常退出的失败回执不作静止证明。 */
Task<void> QtWorker::result() const
{
    return control_->completed.task();
}
/** @brief 本地取消只发布原子状态，不触碰 owner 的 QObject 和生命周期账目。 */
bool QtWorker::requestCancel() const noexcept
{
    return control_->localCancel.requestCancel();
}
/** @brief 在可重入的 effect/资源关闭调用前固定 facade 的控制块。 */
Task<void> QtWorker::close() const
{
    auto state = control_;
    auto effect = effect_;
    if (!state->scheduler->isCurrentThread())
        return failedTask(makeError(ErrorCode::WrongThread, "worker close requires owner thread"));
    effect.close();
    return state->stop();
}
/** @brief owner 复制工作线程的锁下完成值及本地生命周期状态。 */
Result<WorkerSnapshot> QtWorker::snapshot() const
{
    auto state = control_;
    if (!state->scheduler->isCurrentThread())
        return Result<WorkerSnapshot>::failure(
            makeError(ErrorCode::WrongThread, "worker snapshot requires owner thread"));
    std::lock_guard<std::mutex> lock(state->mutex);
    WorkerSnapshot value;
    value.owner = state->owner;
    value.started = state->started;
    value.bodyEntered = state->bodyEntered;
    value.bodyReturned = state->bodyReturned;
    value.cancellationRequested =
        state->localCancel.token().isCancellationRequested() || state->ownerCancel.isCancellationRequested();
    value.closing = state->closing;
    value.joined = state->joined;
    value.nativeHandleClosed = state->nativeHandleClosed;
    value.releasingCaptures = state->releasing;
    value.capturesReleased = state->capturesReleased;
    value.hostDestroyed = state->hostDestroyed;
    value.closed = state->closed;
    value.quarantined = state->quarantined;
    value.retired = state->retired;
    value.retirementBlocked = state->retirementBlocked;
    value.lastError = state->lastError;
    return Result<WorkerSnapshot>::success(std::move(value));
}
} // namespace cordis::qt
