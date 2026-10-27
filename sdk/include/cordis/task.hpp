/**
 * @file task.hpp
 * @brief 提供异步任务、完成生产者及调度器回调接口。
 */
#pragma once

#include <cordis/scheduler.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace cordis {
/**
 * @brief 表示一个可能稍后完成的异步结果。
 * @tparam T 任务完成时的值类型。
 */
template<class T> class Task;

namespace detail {
struct TaskAccess;
/**
 * @brief 管理任务结果和观察者的共享内部状态。
 * @tparam T 任务结果的值类型。
 */
template<class T> struct TaskState {
    using Snapshot = std::shared_ptr<const Result<T>>;
    using Observer = std::function<void(Snapshot)>;
    mutable std::mutex mutex;
    Snapshot result;
    std::vector<Observer> observers;

    /**
     * @brief 首次设置结果并通知已注册的观察者。
     * @param candidate 要发布的不可变结果快照。
     * @return 此调用首次完成任务时返回 true。
     */
    bool finish(Snapshot candidate)
    {
        std::vector<Observer> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (result) return false;
            result = std::move(candidate);
            pending.swap(observers);
        }
        // 持锁期间不执行用户回调，也不调用调度器。
        for (auto& observer : pending) observer(result);
        return true;
    }

    /**
     * @brief 获取当前任务结果快照。
     * @return 任务未完成时返回空指针，否则返回不可变结果。
     */
    Snapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return result;
    }

    /**
     * @brief 注册结果观察者；若任务已完成则立即通知。
     * @param observer 接收任务结果快照的观察者。
     */
    void observe(Observer observer)
    {
        Snapshot ready;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = result;
            if (!ready) { observers.push_back(std::move(observer)); return; }
        }
        observer(std::move(ready));
    }
};

/**
 * @brief 持有任务状态，并在最后一个生产者销毁时处理未完成任务。
 * @tparam T 任务结果的值类型。
 */
template<class T> struct Producer {
    std::shared_ptr<TaskState<T>> state = std::make_shared<TaskState<T>>();
    /**
     * @brief 若任务仍未完成，则以 BrokenPromise 错误完成任务。
     */
    ~Producer()
    {
        if (!state->snapshot()) {
            state->finish(std::make_shared<const Result<T>>(Result<T>::failure(
                makeError(ErrorCode::BrokenPromise, "last Promise released before completion"))));
        }
    }
};
} // namespace detail

/**
 * @brief 用于完成共享异步任务的生产者句柄。
 * @tparam T 任务结果的值类型。
 */
template<class T> class Promise {
public:
    /**
     * @brief 创建新的未完成任务生产者。
     */
    Promise() : producer_(std::make_shared<detail::Producer<T>>()) {}
    /**
     * @brief 获取此生产者关联的任务句柄。
     * @return 与此生产者共享完成状态的任务。
     */
    [[nodiscard]] Task<T> task() const;
    /**
     * @brief 使用指定值成功完成任务。
     * @tparam U 用于启用非 void 重载的类型参数。
     * @param value 任务结果。
     * @return 此调用首次完成任务时返回 true。
     */
    template<class U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    bool resolve(U value) const
    {
        return producer_->state->finish(std::make_shared<const Result<T>>(Result<T>::success(std::move(value))));
    }
    /**
     * @brief 成功完成 void 任务。
     * @tparam U 用于启用 void 重载的类型参数。
     * @return 此调用首次完成任务时返回 true。
     */
    template<class U = T, std::enable_if_t<std::is_void_v<U>, int> = 0>
    bool resolve() const
    {
        return producer_->state->finish(std::make_shared<const Result<T>>(Result<T>::success()));
    }
    /**
     * @brief 使用指定错误完成任务。
     * @param error 任务失败原因。
     * @return 此调用首次完成任务时返回 true。
     */
    bool reject(ErrorInfo error) const
    {
        return producer_->state->finish(std::make_shared<const Result<T>>(Result<T>::failure(std::move(error))));
    }

private:
    friend struct detail::TaskAccess;
    std::shared_ptr<detail::Producer<T>> producer_;
};

/**
 * @brief 可共享、不可变观察的异步结果句柄。
 * @tparam T 任务结果的值类型。
 */
template<class T> class Task {
public:
    using Snapshot = std::shared_ptr<const Result<T>>;
    /**
     * @brief 非阻塞地读取任务结果。
     * @return 任务未完成时返回空指针，否则返回拥有所有权的不可变快照。
     */
    [[nodiscard]] Snapshot tryResult() const { return state_->snapshot(); }
    /**
     * @brief 查询任务是否已完成。
     * @return 任务已有成功或失败结果时返回 true。
     */
    [[nodiscard]] bool isReady() const { return static_cast<bool>(tryResult()); }

    /**
     * @brief 在指定调度器上投递结果回调。
     *
     * 回调始终在调度器上运行，并通过 Result 接收成功值或错误。
     * 返回任务报告投递结果，包括调度器关闭或回调异常。
     * @param scheduler 用于执行回调的调度器。
     * @param callback 接收任务结果的回调。
     * @return 报告回调投递成功或失败的任务。
     */
    Task<void> thenOn(std::shared_ptr<IScheduler> scheduler,
                      std::function<void(const Result<T>&)> callback) const;

private:
    friend class Promise<T>;
    friend struct detail::TaskAccess;
    /** @brief 使用共享内部状态构造任务句柄。 */
    explicit Task(std::shared_ptr<detail::TaskState<T>> state) : state_(std::move(state)) {}
    std::shared_ptr<detail::TaskState<T>> state_;
};

namespace detail {
/**
 * @brief 提供宿主内部观察任务状态的受信访问接口。
 *
 * 此接口不得用于调用插件或用户回调。
 */
struct TaskAccess {
    /** @brief 宿主转发已冻结结果；不复制或移动业务值，沿用完成仲裁及观察者协议。
     * @param result 非空不可变快照，发布后由任务及调用方共同管理其数据寿命。
     */
    template <class T> static bool completeSnapshot(const Promise<T>& promise, typename Task<T>::Snapshot result)
    {
        if (!result)
            return promise.reject(makeError(ErrorCode::InvalidArgument, "task result snapshot is empty"));
        return promise.producer_->state->finish(std::move(result));
    }
    /** @brief 宿主识别同一任务状态，用于拒绝资源回调直接返回自身关闭屏障的等待环。 */
    template <class T> static bool sameTask(const Task<T>& left, const Task<T>& right) noexcept
    {
        return left.state_ == right.state_;
    }
    /**
     * @brief 在任务完成时调用内部观察者。
     * @tparam T 任务结果的值类型。
     * @tparam F 观察者函数类型。
     * @param task 要观察的任务。
     * @param callback 接收任务结果快照的内部回调。
     */
    template<class T, class F> static void observe(const Task<T>& task, F&& callback)
    {
        task.state_->observe(std::forward<F>(callback));
    }
};
} // namespace detail

/**
 * @brief 创建已成功完成的 void 任务。
 * @return 已成功完成的任务。
 */
inline Task<void> completedTask()
{
    Promise<void> promise;
    promise.resolve();
    return promise.task();
}

/**
 * @brief 创建已失败的 void 任务。
 * @param error 任务失败原因。
 * @return 已失败的任务。
 */
inline Task<void> failedTask(ErrorInfo error)
{
    Promise<void> promise;
    promise.reject(std::move(error));
    return promise.task();
}

/**
 * @brief 获取 Promise 关联任务的句柄。
 * @tparam T 任务结果的值类型。
 * @return 与生产者共享完成状态的任务。
 */
template<class T> Task<T> Promise<T>::task() const { return Task<T>(producer_->state); }

/**
 * @brief 将任务结果回调投递到指定调度器。
 * @tparam T 任务结果的值类型。
 * @param scheduler 用于执行回调的调度器。
 * @param callback 接收任务结果的回调。
 * @return 报告回调投递成功或失败的任务。
 */
template<class T>
Task<void> Task<T>::thenOn(std::shared_ptr<IScheduler> scheduler,
                         std::function<void(const Result<T>&)> callback) const
{
    Promise<void> delivered;
    auto receipt = delivered.task();
    if (!callback) {
        delivered.reject(makeError(ErrorCode::InvalidArgument, "empty continuation"));
        return receipt;
    }
    if (!scheduler || !scheduler->isOpen()) {
        delivered.reject(makeError(ErrorCode::SchedulerClosed, "scheduler unavailable"));
        return receipt;
    }
    std::weak_ptr<IScheduler> weak = scheduler;
    state_->observe([weak, callback = std::move(callback), delivered](Snapshot result) mutable {
        auto target = weak.lock();
        auto reject = [delivered] {
            delivered.reject(makeError(ErrorCode::SchedulerClosed, "continuation was not delivered"));
        };
        if (!target) { reject(); return; }
        ScheduledJob job;
        job.discarded = reject;
        job.run = [result = std::move(result), callback = std::move(callback), delivered] {
            try {
                callback(*result);
                delivered.resolve();
            } catch (const std::exception& error) {
                delivered.reject(makeError(ErrorCode::CallbackFailed, error.what()));
            } catch (...) {
                delivered.reject(makeError(ErrorCode::CallbackFailed, "unknown continuation exception"));
            }
        };
        // Adapters must contain platform failures; post throws are still observable.
        try {
            if (!target->post(std::move(job))) reject();
        } catch (const std::exception& error) {
            delivered.reject(makeError(ErrorCode::SchedulerClosed, error.what()));
        } catch (...) { reject(); }
    });
    return receipt;
}
} // namespace cordis
