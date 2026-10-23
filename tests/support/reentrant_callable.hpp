/** @file reentrant_callable.hpp
 * @brief 提供在移动和析构时重入调度器的可调用对象，覆盖标准库小对象存储路径。
 */
#pragma once
#include <cordis/scheduler.hpp>

namespace cordis::test {
/** @brief 仅持有测试作用域内的借用指针；每次生命周期回调都查询调度器锁。 */
class ReentrantCallable {
public:
    /** @brief 调度器和计数须存活到所有任务闭包释放之后。 */
    ReentrantCallable(IScheduler& scheduler, int& calls) : scheduler_(&scheduler), calls_(&calls) {}
    /** @brief 保持可复制，以便存入 std::function。 */
    ReentrantCallable(const ReentrantCallable&) = default;
    /** @brief MSVC 小对象移动会执行此函数，查询必须发生在调度器锁外。 */
    ReentrantCallable(ReentrantCallable&& other) noexcept : scheduler_(other.scheduler_), calls_(other.calls_)
    {
        (void)scheduler_->isOpen();
    }
    /** @brief 移动后对象的析构也可能重入，不能在持锁时清理 std::function。 */
    ~ReentrantCallable() { (void)scheduler_->isOpen(); }
    /** @brief 区分实际执行或丢弃调用与移动/析构。 */
    void operator()() const { ++*calls_; }
private:
    IScheduler* scheduler_;
    int* calls_;
};
} // namespace cordis::test
