/**
 * @file observation.hpp
 * @brief 提供带超时的异步任务观察操作。
 */
#pragma once

#include <cordis/clock.hpp>
#include <cordis/task.hpp>

namespace cordis {
/** @brief 表示观察到的任务是在截止时间前完成，还是已超时。 */
enum class ObservationState { Completed, TimedOut };

/**
 * @brief 保存一次带超时观察的状态及任务结果。
 * @tparam T 被观察任务的结果类型。
 *
 * 超时时 result 为空；完成时 result 包含任务的成功或失败快照。
 */
template<class T> struct Observation {
    ObservationState state;
    typename Task<T>::Snapshot result;
};

/**
 * @brief 观察任务直至完成或超时。
 *
 * 若任务在截止时已实际完成，则不受回调队列延迟影响。
 * @tparam T 被观察任务的结果类型。
 * @param source 要观察的任务。
 * @param clock 用于测量超时的时钟。
 * @param timeout 最长观察时长。
 * @return 表示任务完成、超时或观察失败的任务。
 */
template<class T>
Task<Observation<T>> observeFor(const Task<T>& source, IClock& clock, std::chrono::milliseconds timeout)
{
    /** @brief 协调任务完成与超时事件的单次竞争状态。 */
    struct Race {
        std::atomic_bool won{false};
        Promise<Observation<T>> promise;
        TimerToken timer;
    };
    auto race = std::make_shared<Race>();
    auto output = race->promise.task();
    if (auto ready = source.tryResult()) {
        race->promise.resolve(Observation<T>{ObservationState::Completed, std::move(ready)});
        return output;
    }
    auto timer = clock.after(timeout, {
        [race, source] {
            if (!race->won.exchange(true)) {
                auto result = source.tryResult();
                const auto state = result ? ObservationState::Completed : ObservationState::TimedOut;
                race->promise.resolve(Observation<T>{state, std::move(result)});
            }
        },
        [race] {
            if (!race->won.exchange(true)) race->promise.reject(makeError(ErrorCode::ClockClosed, "observation clock closed"));
        }
    });
    if (!timer.hasValue()) { race->promise.reject(timer.error()); return output; }
    race->timer = timer.value();
    detail::TaskAccess::observe(source, [race](typename Task<T>::Snapshot result) {
        if (!race->won.exchange(true)) {
            race->timer.cancel();
            race->promise.resolve(Observation<T>{ObservationState::Completed, std::move(result)});
        }
    });
    return output;
}
} // namespace cordis
