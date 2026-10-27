/** @file value_worker.hpp
 * @brief 在托管工作线程的原生退出和代次屏障之后交付不可变类型化结果。
 */
#pragma once
#include <cordis/qt/worker.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>

namespace cordis::qt {
namespace detail {
/** @brief worker 仅写入一次冻结快照；正常结果在原生退出后读取，失败不读取仍在运行的工作。
 * @tparam T 可移动的数据结果类型，不含借用的 Context、GUI 或线程约束服务。
 */
template <class T> struct WorkerValue {
    std::mutex mutex;
    typename Task<T>::Snapshot result;
};
} // namespace detail

/** @brief 一次类型化后台工作；资源寿命由 QtWorker 管理，数据快照寿命由 Task 及观察者管理。
 * @tparam T 非 void 可移动的数据类型；支持不可复制类型，析构须适合最后一个快照释放所在的线程。
 * 不携带 Qt 对象或插件可执行资源所有权，不提供模块驻留或跨 ABI 保证。
 */
template <class T> class QtValueWorker {
    static_assert(!std::is_void_v<T> && std::is_move_constructible_v<T>,
                  "QtValueWorker requires a movable non-void data type");
public:
    /** @brief owner 登记一次工作；work 在 worker 构造冻结结果，成功只在原生和 effect 屏障后发布。
     * @param work 同步覆盖全部后台活动，返回拥有数据的 Result<T>；异常转换沿用 QtWorker。
     * @param label 所属 effect 的诊断标签。
     */
    static Result<QtValueWorker> start(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                       std::function<Result<T>(WorkerCancellation)> work,
                                       std::string label = "qt.worker.value")
    {
        if (!work)
            return Result<QtValueWorker>::failure(makeError(ErrorCode::InvalidArgument, "worker requires a body"));
        auto value = std::make_shared<detail::WorkerValue<T>>();
        Promise<T> delivered;
        auto worker = QtWorker::start(
            owner, std::move(scheduler),
            [value, work = std::move(work)](WorkerCancellation cancellation) {
                auto result = std::make_shared<const Result<T>>(work(cancellation));
                auto status = result->hasValue() ? Result<void>::success() : Result<void>::failure(result->error());
                {
                    std::lock_guard<std::mutex> lock(value->mutex);
                    value->result = std::move(result);
                }
                return status;
            },
            std::move(label));
        if (!worker.hasValue())
            return Result<QtValueWorker>::failure(worker.error());
        cordis::detail::TaskAccess::observe(worker.value().result(), [value, delivered](Task<void>::Snapshot status) {
            if (!status->hasValue()) {
                // 退出协议失败时线程仍可能运行；不得读取或清空后台持有的值。
                delivered.reject(status->error());
                return;
            }
            typename Task<T>::Snapshot result;
            {
                std::lock_guard<std::mutex> lock(value->mutex);
                result = std::move(value->result);
            }
            cordis::detail::TaskAccess::completeSnapshot(delivered, std::move(result));
        });
        return Result<QtValueWorker>::success(QtValueWorker(worker.value(), delivered.task()));
    }
    /** @brief effect 初始化回执，不表示工作完成或数据可用。 */
    Task<void> ready() const { return worker_.ready(); }
    /** @brief 成功沿用 QtWorker 的完整屏障和 Context 门禁；协议失败不证明资源静止。
     * 数据快照可以比资源存活更久，不能将 close 当作所有结果对象均已析构的证明。
     */
    Task<T> result() const { return result_; }
    /** @brief 任意线程请求协作取消，不强制终止工作或销毁结果快照。 */
    bool requestCancel() const noexcept { return worker_.requestCancel(); }
    /** @brief owner 等待原生资源关闭；数据及 Kernel 静止各有独立的寿命边界。 */
    Task<void> close() const { return worker_.close(); }
    /** @brief owner 查询同一底层 QtWorker 的资源账目。 */
    Result<WorkerSnapshot> snapshot() const { return worker_.snapshot(); }

private:
    /** @brief 只有成功登记的底层资源及结果任务才能构造 facade。 */
    QtValueWorker(QtWorker worker, Task<T> result) : worker_(std::move(worker)), result_(std::move(result)) {}
    QtWorker worker_;
    Task<T> result_;
};
} // namespace cordis::qt
