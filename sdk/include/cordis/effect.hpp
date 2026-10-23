/**
 * @file effect.hpp
 * @brief 声明 effect 作用域、生命周期句柄及清理报告。
 */
#pragma once

#include <cordis/cancellation.hpp>
#include <cordis/state.hpp>
#include <cordis/task.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace cordis {
namespace detail { struct EffectControl; struct ScopeControl; }

/**
 * @brief 汇总清理是否已静止以及清理过程中产生的错误。
 */
struct CleanupReport {
    bool quiescent = false;
    std::vector<ErrorInfo> errors;
};

/**
 * @brief 单个 effect 的只读诊断快照。
 */
struct EffectSnapshot {
    EffectId id;
    std::string label;
    EffectState state;
    bool setupPending;
    bool cleanupPending;
    bool deliveryBlocked;
    std::size_t remainingCleanups;
    std::size_t retainedCleanups;
    std::vector<ErrorInfo> errors;
};

/**
 * @brief 作用域及其 effect 的只读诊断快照。
 */
struct ScopeSnapshot {
    bool closing;
    bool deliveryBlocked;
    std::vector<EffectSnapshot> effects;
    std::vector<ErrorInfo> errors;
};

/** @brief 返回异步完成任务的清理函数类型。 */
using CleanupFunction = std::function<Task<void>()>;
class EffectToken;

/**
 * @brief effect 初始化期间用于注册资源清理和取消令牌的上下文。
 */
class EffectSetupContext {
public:
    /**
     * @brief 注册关闭时执行的清理函数。
     * @param cleanup 清理函数；所有清理函数按注册顺序逆序执行。
     * @return 注册成功时返回成功结果，否则返回上下文或线程错误。
     */
    Result<void> addCleanup(CleanupFunction cleanup) const;
    /**
     * @brief 将子 effect 的关闭纳入当前 effect 的清理过程。
     * @param child 要由当前 effect 接管关闭的子 effect。
     * @return 接管成功时返回成功结果；检测到无效上下文、线程错误或循环时返回错误。
     */
    Result<void> adopt(const EffectToken& child) const;
    /**
     * @brief 获取此 effect 的协作式取消令牌。
     * @return 当前 effect 的取消令牌；上下文过期时返回空令牌。
     */
    [[nodiscard]] CancellationToken cancellation() const;

private:
    friend struct detail::EffectControl;
    /**
     * @brief 使用 effect 控制状态创建初始化上下文。
     * @param control 当前 effect 的弱引用。
     */
    explicit EffectSetupContext(std::weak_ptr<detail::EffectControl> control) : control_(std::move(control)) {}
    std::weak_ptr<detail::EffectControl> control_;
};

/** @brief 初始化 effect 并返回表示初始化结果的异步任务。 */
using EffectSetup = std::function<Task<void>(EffectSetupContext)>;

/**
 * @brief 持有单个 effect 的生命周期操作句柄。
 */
class EffectToken {
public:
    /**
     * @brief 获取 effect 的初始化结果。
     * @return 初始化成功、失败或初始化期间取消的任务。
     */
    [[nodiscard]] Task<void> ready() const;
    /**
     * @brief 请求关闭 effect。
     * @return 报告清理结果的任务。
     */
    Task<CleanupReport> close() const;
    /**
     * @brief 在调度器所属线程读取 effect 状态。
     * @return 成功时返回状态快照；非所属线程调用时返回错误。
     */
    [[nodiscard]] Result<EffectSnapshot> snapshot() const;

private:
    friend class EffectScope;
    friend class EffectSetupContext;
    /**
     * @brief 使用 effect 控制对象创建生命周期令牌。
     * @param control effect 的共享控制状态。
     */
    explicit EffectToken(std::shared_ptr<detail::EffectControl> control) : control_(std::move(control)) {}
    std::shared_ptr<detail::EffectControl> control_;
};

/**
 * @brief 管理一组 effect，并协调取消、清理和资源释放。
 */
class EffectScope {
public:
    /**
     * @brief 使用指定的开放调度器创建作用域。
     * @param scheduler 必须处于开放状态，且当前线程必须是其所属线程。
     * @throws std::invalid_argument 调度器为空、已关闭或调用线程不匹配时抛出。
     */
    explicit EffectScope(std::shared_ptr<IScheduler> scheduler);
    /**
     * @brief 在调度器所属线程注册并启动 effect。
     * @param setup effect 初始化函数。
     * @param label 用于诊断的 effect 标签。
     * @return 注册成功时返回 effect 句柄；失败时返回错误。
     */
    Result<EffectToken> effect(EffectSetup setup, std::string label = "anonymous") const;
    /**
     * @brief 请求关闭作用域中的所有 effect。
     * @return 报告所有清理结果的任务。
     */
    Task<CleanupReport> close() const;
    /**
     * @brief 在调度器所属线程读取作用域状态。
     * @return 成功时返回状态快照；非所属线程调用时返回错误。
     */
    [[nodiscard]] Result<ScopeSnapshot> snapshot() const;

private:
    std::shared_ptr<detail::ScopeControl> control_;
};
} // namespace cordis
