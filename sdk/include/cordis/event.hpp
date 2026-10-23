/** @file event.hpp
 * @brief 提供 effect 所有的类型化事件、同步中间件与异步分发屏障。
 */
#pragma once
#include <cordis/instance.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cordis {
/** @brief 每个完整事件键固定一种分发协议，同一个总线的 T 固定参数与停止值类型。 */
enum class EventMode { Emit, Bail, Serial, Parallel, Waterfall, Observer };
/** @brief 事件按名称和精确域匹配，不跨域回退。 */
struct EventKey {
    std::string name;
    RealmId realm{1};
    /** @brief 按完整键排序。 */
    friend bool operator<(const EventKey& a, const EventKey& b)
    { return a.name != b.name ? a.name < b.name : a.realm < b.realm; }
};
/** @brief 显式区分继续与停止；零、空字符串同样可以作为停止值。 */
template<class T> struct EventDecision {
    std::optional<T> stopped;
    /** @brief 继续调用后续监听器。 */
    static EventDecision Continue() { return {std::nullopt}; }
    /** @brief 使用指定值终止本次 bail/serial，或从 waterfall 返回结果。 */
    static EventDecision Stop(T value) { return {std::move(value)}; }
};
/** @brief 监听器注册顺序选项及参数过滤器；过滤器也受异常隔离。 */
template<class T> struct ListenerOptions {
    bool once = false;
    bool prepend = false;
    std::function<bool(const T&)> filter;
};
/** @brief 单个异步监听器结果，parallel 按编号而非完成顺序报告。 */
template<class T> struct EventOutcome {
    std::uint64_t listener;
    Result<EventDecision<T>> result;
};
/** @brief parallel 的全部已调用结果，失败与停止值不会取消其他监听器。 */
template<class T> struct EventReport { std::vector<EventOutcome<T>> listeners; };
/** @brief 总线残留诊断；投递失败会保守保留异步快照。 */
struct EventBusSnapshot {
    std::size_t liveListeners;
    std::size_t activeDispatches;
    bool deliveryBlocked;
};

/** @brief 仅在当前监听器同步调用栈内且至多一次有效的下游能力。 */
template<class T> class EventNext {
public:
    /** @brief 同线程、有效窗口内调用下游；重复或逃逸使用返回 NextInvalid。 */
    Result<EventDecision<T>> operator()(T value) const
    {
        if (!state_->scheduler->isCurrentThread()) return Result<EventDecision<T>>::failure(makeError(ErrorCode::WrongThread, "next requires owner thread"));
        if (!state_->active || state_->used) return Result<EventDecision<T>>::failure(makeError(ErrorCode::NextInvalid, "next expired or already used"));
        state_->used = true;
        return state_->run(std::move(value));
    }
private:
    template<class U> friend class EventBus;
    /** @brief 共享一次性调用窗口，窗口退出后释放下游闭包。 */
    struct State {
        std::shared_ptr<IScheduler> scheduler;
        bool active = true;
        bool used = false;
        std::function<Result<EventDecision<T>>(T)> run;
    };
    /** @brief 保存窗口状态，复制 Next 不会增加可调用次数。 */
    explicit EventNext(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

/** @brief owner 线程总线；所有监听器必须归属于 Context 或 EffectScope。 */
template<class T> class EventBus {
    static_assert(std::is_copy_constructible_v<T>, "event payload must be copyable");
    static_assert(std::is_move_constructible_v<T>, "event payload must be movable");
    using Decision = EventDecision<T>;
    using Sync = std::function<Result<Decision>(const T&)>;
    using Async = std::function<Task<Decision>(const T&)>;
    using Flow = std::function<Result<Decision>(const T&, EventNext<T>)>;
    /** @brief 回调及其清理屏障；快照保留数确保闭包释放先于 effect 静止。 */
    struct Record {
        std::uint64_t id = 0;
        bool alive = true;
        bool closing = false;
        std::size_t reservations = 0;
        ListenerOptions<T> options;
        std::shared_ptr<IScheduler> scheduler;
        CancellationToken cancellation;
        std::function<Result<void>()> guard;
        Sync sync;
        Async async;
        Flow flow;
        Promise<void> drained;
        /** @brief 关闭期间没有分发快照持有记录才确认回调资源静止。 */
        void checkDrained() { if (closing && reservations == 0) drained.resolve(); }
        /** @brief 清理关门并等待快照；effect 随后释放持有回调的清理闭包。 */
        Task<void> close() { alive = false; closing = true; checkDrained(); return drained.task(); }
    };
    /** @brief 一个事件的不可变协议及弱监听器索引。 */
    struct Descriptor { EventMode mode; std::vector<std::weak_ptr<Record>> listeners; };
    /** @brief 共享宿主索引，不拥有 effect 的生命周期。 */
    struct Control {
        std::shared_ptr<IScheduler> scheduler;
        std::map<EventKey, Descriptor> events;
        std::uint64_t next = 0;
        std::size_t dispatches = 0;
        std::atomic_bool blocked{false};
        /** @brief 检查 owner 与 scheduler，关闭后禁止新业务。 */
        Result<void> check() const
        {
            if (!scheduler->isCurrentThread()) return Result<void>::failure(makeError(ErrorCode::WrongThread, "events require owner thread"));
            if (!scheduler->isOpen()) return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "event scheduler closed"));
            return Result<void>::success();
        }
    };
    /** @brief 分发快照固定全部记录，释放时先销毁闭包引用再通知清理任务。 */
    struct Snapshot {
        std::shared_ptr<Control> owner;
        std::vector<std::shared_ptr<Record>> records;
        /** @brief 在 owner 释放快照保留；异步投递失败时快照不会被提前析构。 */
        ~Snapshot()
        {
            for (auto& record : records) {
                auto drained = record->drained;
                --record->reservations;
                const bool complete = record->closing && record->reservations == 0;
                record.reset();
                if (complete) drained.resolve();
            }
            --owner->dispatches;
        }
    };
    /** @brief 异步分发固定快照与参数，只有 owner 处理源结果才完成调用。 */
    struct Dispatch : std::enable_shared_from_this<Dispatch> {
        std::shared_ptr<Snapshot> snapshot;
        T value;
        bool parallel;
        bool launching = false;
        std::size_t index = 0;
        std::size_t pending = 0;
        std::map<std::uint64_t, Result<Decision>> outcomes;
        Promise<Decision> serialResult;
        Promise<EventReport<T>> parallelResult;
        std::shared_ptr<Dispatch> pin;
        /** @brief 保存拥有所有权的参数副本，不引用调用方栈。 */
        Dispatch(std::shared_ptr<Snapshot> view, T input, bool all)
            : snapshot(std::move(view)), value(std::move(input)), parallel(all) {}
        /** @brief 投递失败只发布原子诊断，保留快照及所有未完成任务。 */
        void watch(const Task<void>& receipt)
        {
            std::weak_ptr<Control> weak = snapshot->owner;
            detail::TaskAccess::observe(receipt, [weak](Task<void>::Snapshot result) {
                if (!result->hasValue()) if (auto control = weak.lock()) control->blocked.store(true);
            });
        }
        /** @brief serial 完成前释放分发快照，清理 continuation 仍统一排队。 */
        void finish(const Result<Decision>& result)
        {
            snapshot.reset();
            try {
                if (result.hasValue()) serialResult.resolve(result.value());
                else serialResult.reject(result.error());
            } catch (const std::exception& error) { serialResult.reject(makeError(ErrorCode::CallbackFailed, error.what())); }
            catch (...) { serialResult.reject(makeError(ErrorCode::CallbackFailed, "event result copy threw")); }
            pin.reset();
        }
        /** @brief parallel 等待所有已进入的监听器，并按注册编号整理结果。 */
        void finishParallel()
        {
            EventReport<T> report;
            try {
                for (const auto& entry : outcomes) report.listeners.push_back({entry.first, entry.second});
            } catch (const std::exception& error) {
                snapshot.reset();
                parallelResult.reject(makeError(ErrorCode::CallbackFailed, error.what()));
                pin.reset();
                return;
            } catch (...) {
                snapshot.reset();
                parallelResult.reject(makeError(ErrorCode::CallbackFailed, "event report copy threw"));
                pin.reset();
                return;
            }
            snapshot.reset();
            parallelResult.resolve(std::move(report));
            pin.reset();
        }
        /** @brief 插件抛错转换为 Task 失败，所有完成处理都经 owner 队列。 */
        Task<Decision> invoke(const std::shared_ptr<Record>& record)
        {
            try { return record->async(value); }
            catch (const std::exception& error) { return failed<Decision>(makeError(ErrorCode::CallbackFailed, error.what())); }
            catch (...) { return failed<Decision>(makeError(ErrorCode::CallbackFailed, "async event callback threw")); }
        }
        /** @brief 串行等待前项；并行先启动全部合格项再等待所有源结果。 */
        void advance()
        {
            auto self = this->shared_from_this();
            launching = true;
            while (index < snapshot->records.size()) {
                auto record = snapshot->records[index++];
                auto admitted = enter(record, value);
                if (!admitted.hasValue()) {
                    if (!parallel) { finish(Result<Decision>::failure(admitted.error())); return; }
                    outcomes.emplace(record->id, Result<Decision>::failure(admitted.error()));
                    continue;
                }
                if (!admitted.value()) continue;
                ++pending;
                watch(invoke(record).thenOn(snapshot->owner->scheduler, [self, id = record->id](const Result<Decision>& result) {
                    --self->pending;
                    if (self->parallel) {
                        try { self->outcomes.emplace(id, result); }
                        catch (const std::exception& error) { self->outcomes.emplace(id, Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, error.what()))); }
                        catch (...) { self->outcomes.emplace(id, Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, "event outcome copy threw"))); }
                        if (!self->launching && self->pending == 0) self->finishParallel();
                    } else if (!result.hasValue() || result.value().stopped) self->finish(result);
                    else self->advance();
                }));
                if (!parallel) { launching = false; return; }
            }
            launching = false;
            if (parallel) { if (pending == 0) finishParallel(); }
            else finish(Result<Decision>::success(Decision::Continue()));
        }
    };
public:
    /** @brief 构造开放 owner 线程上的独立总线；副本共享索引。 */
    explicit EventBus(std::shared_ptr<IScheduler> scheduler)
    {
        if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen()) throw std::invalid_argument("EventBus requires open owner-thread scheduler");
        control_ = std::make_shared<Control>();
        control_->scheduler = std::move(scheduler);
    }
    /** @brief 同键协议必须一致；总线类型 T 固定参数及返回值 schema。 */
    Result<void> declare(EventKey key, EventMode mode) const
    {
        auto allowed = control_->check();
        if (!allowed.hasValue()) return allowed;
        if (key.name.empty() || !key.realm.valid() || mode < EventMode::Emit || mode > EventMode::Observer)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "invalid event descriptor"));
        auto found = control_->events.find(key);
        if (found != control_->events.end() && found->second.mode != mode)
            return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "incompatible event protocol"));
        control_->events.emplace(std::move(key), Descriptor{mode, {}});
        return Result<void>::success();
    }
    /** @brief 注册同步 emit/bail/Observer 监听器，返回其资源关闭令牌。 */
    template<class Owner> Result<EffectToken> on(const Owner& owner, const EventKey& key, Sync callback, ListenerOptions<T> options = {}) const
    { return listen(owner, key, std::move(callback), {}, {}, std::move(options)); }
    /** @brief 注册 serial/parallel 异步监听器，返回任务完成前参数引用保持有效。 */
    template<class Owner> Result<EffectToken> onAsync(const Owner& owner, const EventKey& key, Async callback, ListenerOptions<T> options = {}) const
    { return listen(owner, key, {}, std::move(callback), {}, std::move(options)); }
    /** @brief 注册同步 waterfall 中间件，Next 禁止逃逸使用。 */
    template<class Owner> Result<EffectToken> onWaterfall(const Owner& owner, const EventKey& key, Flow callback, ListenerOptions<T> options = {}) const
    { return listen(owner, key, {}, {}, std::move(callback), std::move(options)); }
    /** @brief 按顺序通知所有合格监听器，Stop 不影响 emit，首个错误结束分发。 */
    Result<void> emit(const EventKey& key, const T& value) const
    {
        auto view = capture(key, EventMode::Emit);
        if (!view.hasValue()) return Result<void>::failure(view.error());
        for (const auto& record : view.value()->records) {
            auto result = call(record, value);
            if (!result.hasValue()) return Result<void>::failure(result.error());
        }
        return Result<void>::success();
    }
    /** @brief 首个显式 Stop 结束，全部继续时返回 Continue。 */
    Result<Decision> bail(const EventKey& key, const T& value) const
    {
        auto view = capture(key, EventMode::Bail);
        if (!view.hasValue()) return Result<Decision>::failure(view.error());
        for (const auto& record : view.value()->records) {
            auto result = call(record, value);
            if (!result.hasValue() || result.value().stopped) return result;
        }
        return Result<Decision>::success(Decision::Continue());
    }
    /** @brief 内部观察者错误逐个收集，不阻止其余观察者执行。 */
    Result<std::vector<ErrorInfo>> observe(const EventKey& key, const T& value) const
    {
        auto view = capture(key, EventMode::Observer);
        if (!view.hasValue()) return Result<std::vector<ErrorInfo>>::failure(view.error());
        std::vector<ErrorInfo> errors;
        for (const auto& record : view.value()->records) {
            auto result = call(record, value);
            if (!result.hasValue()) errors.push_back(result.error());
        }
        return Result<std::vector<ErrorInfo>>::success(std::move(errors));
    }
    /** @brief Next 显式调用下游，无监听器或跳过全部时返回 Continue。 */
    Result<Decision> waterfall(const EventKey& key, T value) const
    {
        auto view = capture(key, EventMode::Waterfall);
        return view.hasValue() ? flowAt(view.value(), 0, std::move(value)) : Result<Decision>::failure(view.error());
    }
    /** @brief 排队启动 serial，每项完成后再进入下一项，首错或首 Stop 结束。 */
    Task<Decision> serial(const EventKey& key, T value) const
    {
        auto view = capture(key, EventMode::Serial);
        if (!view.hasValue()) return failed<Decision>(view.error());
        auto dispatch = schedule(view.value(), std::move(value), false);
        return dispatch->serialResult.task();
    }
    /** @brief 排队启动所有合格项，等待全部结果并按 listener id 排序。 */
    Task<EventReport<T>> parallel(const EventKey& key, T value) const
    {
        auto view = capture(key, EventMode::Parallel);
        if (!view.hasValue()) return failed<EventReport<T>>(view.error());
        auto dispatch = schedule(view.value(), std::move(value), true);
        return dispatch->parallelResult.task();
    }
    /** @brief owner 可在 scheduler 关闭后检查仍保留的分发及投递阻断。 */
    Result<EventBusSnapshot> snapshot() const
    {
        if (!control_->scheduler->isCurrentThread()) return Result<EventBusSnapshot>::failure(makeError(ErrorCode::WrongThread, "snapshot requires owner thread"));
        std::size_t live = 0;
        for (const auto& event : control_->events) for (const auto& weak : event.second.listeners)
            if (auto record = weak.lock()) if (record->alive && !record->cancellation.isCancellationRequested()) ++live;
        return Result<EventBusSnapshot>::success({live, control_->dispatches, control_->blocked.load()});
    }
private:
    /** @brief 构造已失败的泛型任务，不调用插件。 */
    template<class U> static Task<U> failed(ErrorInfo error) { Promise<U> promise; promise.reject(std::move(error)); return promise.task(); }
    /** @brief 普通 EffectScope 的取消令牌已提供即时关门；额外门禁为空。 */
    Result<std::function<Result<void>()>> guardFor(const EffectScope& owner) const
    {
        using Output = Result<std::function<Result<void>()>>;
        if (!owner.usesScheduler(control_->scheduler)) return Output::failure(makeError(ErrorCode::InvalidArgument, "event scope requires same scheduler"));
        return Output::success({});
    }
    /** @brief Context 监听器每次分发检查实例业务能力及代次。 */
    Result<std::function<Result<void>()>> guardFor(const Context& owner) const { return owner.eventGuard(control_->scheduler); }
    /** @brief 检查协议后在 effect setup 内登记；所有权登记失败时不修改索引。 */
    template<class Owner> Result<EffectToken> listen(const Owner& owner, const EventKey& key, Sync sync, Async async, Flow flow, ListenerOptions<T> options) const
    {
        auto allowed = control_->check();
        if (!allowed.hasValue()) return Result<EffectToken>::failure(allowed.error());
        auto found = control_->events.find(key);
        if (found == control_->events.end() || control_->next == std::numeric_limits<std::uint64_t>::max())
            return Result<EffectToken>::failure(makeError(ErrorCode::InvalidArgument, "unknown event or listener ids exhausted"));
        const auto mode = found->second.mode;
        const bool compatible = (sync && (mode == EventMode::Emit || mode == EventMode::Bail || mode == EventMode::Observer)) ||
            (async && (mode == EventMode::Serial || mode == EventMode::Parallel)) || (flow && mode == EventMode::Waterfall);
        if (!compatible) return Result<EffectToken>::failure(makeError(ErrorCode::InvalidArgument, "empty or incompatible event callback"));
        auto guard = guardFor(owner);
        if (!guard.hasValue()) return Result<EffectToken>::failure(guard.error());
        auto record = std::make_shared<Record>();
        record->id = ++control_->next;
        record->scheduler = control_->scheduler;
        record->options = std::move(options);
        record->guard = guard.value();
        record->sync = std::move(sync);
        record->async = std::move(async);
        record->flow = std::move(flow);
        auto control = control_;
        return owner.effect([record, control, key](EffectSetupContext setup) {
            record->cancellation = setup.cancellation();
            auto added = setup.addCleanup([record] { return record->close(); });
            if (!added.hasValue()) return failedTask(added.error());
            auto& listeners = control->events.at(key).listeners;
            if (record->options.prepend) listeners.insert(listeners.begin(), record);
            else listeners.push_back(record);
            return completedTask();
        }, "event." + key.name);
    }
    /** @brief 固定注册时的快照；后续新增不加入当前分发，注销在进入时重查。 */
    Result<std::shared_ptr<Snapshot>> capture(const EventKey& key, EventMode mode) const
    {
        using Output = Result<std::shared_ptr<Snapshot>>;
        auto allowed = control_->check();
        if (!allowed.hasValue()) return Output::failure(allowed.error());
        auto found = control_->events.find(key);
        if (found == control_->events.end() || found->second.mode != mode) return Output::failure(makeError(ErrorCode::InvalidArgument, "unknown event or wrong dispatch protocol"));
        auto view = std::make_shared<Snapshot>();
        view->owner = control_;
        ++control_->dispatches;
        for (auto it = found->second.listeners.begin(); it != found->second.listeners.end();) {
            auto record = it->lock();
            if (!record || !record->alive) { it = found->second.listeners.erase(it); continue; }
            view->records.push_back(record);
            ++record->reservations;
            ++it;
        }
        return Output::success(std::move(view));
    }
    /** @brief 过滤器前后均重查权限，once 在调用用户回调之前失活。 */
    static Result<bool> enter(const std::shared_ptr<Record>& record, const T& value)
    {
        if (!record->scheduler->isOpen()) return Result<bool>::failure(makeError(ErrorCode::SchedulerClosed, "event scheduler closed during dispatch"));
        auto live = [&] { return record->alive && !record->cancellation.isCancellationRequested() && (!record->guard || record->guard().hasValue()); };
        if (!live()) return Result<bool>::success(false);
        try {
            if (record->options.filter && !record->options.filter(value)) return Result<bool>::success(false);
        } catch (const std::exception& error) { return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, error.what())); }
        catch (...) { return Result<bool>::failure(makeError(ErrorCode::CallbackFailed, "event filter threw")); }
        if (!record->scheduler->isOpen()) return Result<bool>::failure(makeError(ErrorCode::SchedulerClosed, "event scheduler closed by filter"));
        if (!live()) return Result<bool>::success(false);
        if (record->options.once) record->alive = false;
        return Result<bool>::success(true);
    }
    /** @brief 同步调用隔离异常，失活记录等价于 Continue。 */
    static Result<Decision> call(const std::shared_ptr<Record>& record, const T& value)
    {
        auto admitted = enter(record, value);
        if (!admitted.hasValue()) return Result<Decision>::failure(admitted.error());
        if (!admitted.value()) return Result<Decision>::success(Decision::Continue());
        try { return record->sync(value); }
        catch (const std::exception& error) { return Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, error.what())); }
        catch (...) { return Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, "event callback threw")); }
    }
    /** @brief 每层独立持有 Next 状态，异常与正常退出都撤销窗口及其闭包。 */
    static Result<Decision> flowAt(const std::shared_ptr<Snapshot>& view, std::size_t index, T value)
    {
        while (index < view->records.size()) {
            auto record = view->records[index++];
            auto admitted = enter(record, value);
            if (!admitted.hasValue()) return Result<Decision>::failure(admitted.error());
            if (!admitted.value()) continue;
            auto state = std::make_shared<typename EventNext<T>::State>();
            state->scheduler = view->owner->scheduler;
            state->run = [view, index](T input) { return flowAt(view, index, std::move(input)); };
            auto invoke = [&]() -> Result<Decision> {
                try { return record->flow(value, EventNext<T>(state)); }
                catch (const std::exception& error) { return Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, error.what())); }
                catch (...) { return Result<Decision>::failure(makeError(ErrorCode::CallbackFailed, "waterfall callback threw")); }
            };
            auto result = invoke();
            state->active = false; state->run = {};
            return result;
        }
        return Result<Decision>::success(Decision::Continue());
    }
    /** @brief 初次执行也排队；投递失败时自保留资源，供快照诊断。 */
    static std::shared_ptr<Dispatch> schedule(std::shared_ptr<Snapshot> view, T value, bool parallel)
    {
        auto dispatch = std::make_shared<Dispatch>(std::move(view), std::move(value), parallel);
        dispatch->pin = dispatch;
        dispatch->watch(completedTask().thenOn(dispatch->snapshot->owner->scheduler, [dispatch](const Result<void>&) { dispatch->advance(); }));
        return dispatch;
    }
    std::shared_ptr<Control> control_;
};
} // namespace cordis
