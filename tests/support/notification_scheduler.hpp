/** @file notification_scheduler.hpp
 * @brief 发布与定向通知测试共用原 FIFO、拒投及同步投递重入调度器。
 */
#pragma once
#include <cordis/testing/fake_scheduler.hpp>
#include <exception>
#include <functional>
#include <utility>

namespace cordis::test {
struct NotificationScheduler final : IScheduler {
    testing::FakeScheduler queue;
    bool reject = false;
    std::function<void()> onPost;
    std::exception_ptr hookFailure;
    bool post(ScheduledJob job) override
    {
        if (onPost) {
            try {
                const auto callback = onPost;
                callback();
            } catch (...) {
                if (!hookFailure)
                    hookFailure = std::current_exception();
            }
        }
        return !reject && queue.post(std::move(job));
    }
    bool isCurrentThread() const noexcept override { return queue.isCurrentThread(); }
    bool isOpen() const override { return queue.isOpen(); }
    void checkHook() const
    {
        if (hookFailure)
            std::rethrow_exception(hookFailure);
    }
};
} // namespace cordis::test
