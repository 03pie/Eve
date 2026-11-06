/** @file target_commit_p.hpp
 * @brief 宿主整体提交的目标账目准备槽；不是普通接受目标或绕过运行驱动的公开入口。
 */
#pragma once
#include <cordis/loader/coordinator.hpp>
#include <cordis/loader/store.hpp>
#include <thread>

namespace cordis::loader::detail {
struct TargetCommitAccess;
enum class TargetCommitMode { Advance, Preserve };
struct CoordinatorTargetSlot {
    CoordinatorTargetSlot() = default;
    CoordinatorTargetSlot(const CoordinatorTargetSlot&) = delete;
    CoordinatorTargetSlot& operator=(const CoordinatorTargetSlot&) = delete;
    std::shared_ptr<CoordinatorControl> owner;
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<const ValidatedCandidate> baseline;
    std::shared_ptr<const ValidatedCandidate> target;
    std::uint64_t previous = 0;
    std::uint64_t next = 0;
    std::thread::id thread;
    bool published = false;
    ~CoordinatorTargetSlot();
    bool ready() const noexcept;
    void publish() noexcept;
};
struct StoreTargetSlot {
    StoreTargetSlot() = default;
    StoreTargetSlot(const StoreTargetSlot&) = delete;
    StoreTargetSlot& operator=(const StoreTargetSlot&) = delete;
    std::shared_ptr<StoreControl> owner;
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<const ValidatedCandidate> baseline;
    std::shared_ptr<const ValidatedCandidate> target;
    std::uint64_t previous = 0;
    std::uint64_t next = 0;
    std::thread::id thread;
    bool published = false;
    ~StoreTargetSlot();
    bool ready() const noexcept;
    void publish() noexcept;
};
/** @brief owner 同步准备窗口；必须在模块提交锁退出后销毁，析构可能恢复关闭泵送。
 * 上层先 ready，再发布 Core/Module，随后 publish；三步之间不能执行回调或排队等待。
 * 不持有 schema、工厂或插件回调，也不发布计划完成通知。
 */
class PreparedTargetCommit {
public:
    PreparedTargetCommit(const PreparedTargetCommit&) = delete;
    PreparedTargetCommit& operator=(const PreparedTargetCommit&) = delete;
    PreparedTargetCommit(PreparedTargetCommit&&) noexcept = default;
    PreparedTargetCommit& operator=(PreparedTargetCommit&&) = delete;
    bool ready() const noexcept;
    void publish() noexcept;
    std::uint64_t revision() const noexcept;

private:
    friend struct TargetCommitAccess;
    PreparedTargetCommit(std::unique_ptr<CoordinatorTargetSlot> coordinator, std::unique_ptr<StoreTargetSlot> store)
        : coordinator_(std::move(coordinator)), store_(std::move(store))
    {
    }
    std::unique_ptr<CoordinatorTargetSlot> coordinator_;
    std::unique_ptr<StoreTargetSlot> store_;
};
struct TargetCommitAccess {
    static Result<PreparedTargetCommit> prepare(ConfigurationCoordinator& coordinator, ConfigurationStore& store,
                                                std::shared_ptr<const ValidatedCandidate> target,
                                                std::uint64_t expected,
                                                TargetCommitMode mode = TargetCommitMode::Advance);

private:
    static Result<std::unique_ptr<CoordinatorTargetSlot>> prepareCoordinator(ConfigurationCoordinator&,
                                                                             std::shared_ptr<const ValidatedCandidate>,
                                                                             std::uint64_t, TargetCommitMode);
    static Result<std::unique_ptr<StoreTargetSlot>>
    prepareStore(ConfigurationStore&, std::shared_ptr<const ValidatedCandidate>, std::uint64_t, TargetCommitMode);
};
} // namespace cordis::loader::detail
