/**
 * @file cancellation.hpp
 * @brief 提供共享的协作式取消状态。
 */
#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace cordis {
class CancellationSource;

/**
 * @brief 可跨线程查询的协作式取消状态。
 *
 * 默认构造的令牌不关联取消源，查询时返回 false。
 */
class CancellationToken {
public:
    /**
     * @brief 创建不关联取消源的令牌。
     */
    CancellationToken() = default;
    /**
     * @brief 查询是否已请求取消。
     * @return 关联的取消源已请求取消时返回 true，否则返回 false。
     */
    [[nodiscard]] bool isCancellationRequested() const noexcept
    {
        return state_ && state_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;
    /**
     * @brief 创建共享指定取消状态的令牌。
     * @param state 取消源维护的原子状态。
     */
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state) : state_(std::move(state)) {}
    std::shared_ptr<std::atomic_bool> state_;
};

/**
 * @brief 管理可复制共享的协作式取消状态。
 */
class CancellationSource {
public:
    /**
     * @brief 创建初始状态为未取消的取消源。
     */
    CancellationSource() : state_(std::make_shared<std::atomic_bool>(false)) {}
    /**
     * @brief 创建共享当前取消状态的令牌。
     * @return 可跨线程查询取消状态的令牌。
     */
    [[nodiscard]] CancellationToken token() const { return CancellationToken(state_); }
    /**
     * @brief 请求取消；此操作只发布状态，不会自动完成关联任务。
     * @return 本次调用首次设置取消状态时返回 true。
     */
    bool requestCancel() const noexcept
    {
        return !state_->exchange(true, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};
} // namespace cordis
