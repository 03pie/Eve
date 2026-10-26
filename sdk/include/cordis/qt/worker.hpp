/** @file worker.hpp
 * @brief 将一次协作式后台工作接入 Context/epoch，并确认原生线程和捕获完整退出。
 */
#pragma once
#include <cordis/cancellation.hpp>
#include <cordis/instance.hpp>
#include <cordis/qt/scheduler.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace cordis::qt {
namespace detail {
struct WorkerControl;
}
/** @brief worker 只查询本地取消及所属 effect 取消，不访问 Context 或 owner 对象。 */
class WorkerCancellation {
public:
    /** @brief 任意工作线程查询；请求取消不代表函数、TLS 或线程已经退出。 */
    bool isCancellationRequested() const noexcept
    {
        return owner_.isCancellationRequested() || local_.isCancellationRequested();
    }

private:
    friend class QtWorker;
    /** @brief 只有资源工厂组合本地与所属代次的协作取消状态。 */
    WorkerCancellation(CancellationToken owner, CancellationToken local)
        : owner_(std::move(owner)), local_(std::move(local))
    {
    }
    CancellationToken owner_, local_;
};
/** @brief owner 诊断区分函数返回、原生退出、捕获/宿主释放及所属 effect 账目确认。 */
struct WorkerSnapshot {
    ContextIdentity owner;
    bool started = false, bodyEntered = false, bodyReturned = false;
    bool cancellationRequested = false, closing = false, joined = false;
    bool nativeHandleClosed = false, releasingCaptures = false, capturesReleased = false;
    bool hostDestroyed = false, closed = false, quarantined = false;
    bool retired = false, retirementBlocked = false;
    std::optional<ErrorInfo> lastError;
};
/** @brief 一次工作使用一个原生线程；释放 facade 不取消资源，所属 effect 承担退出责任。 */
class QtWorker {
public:
    /** @brief owner 登记并启动工作；当前原生退出后端支持 Windows，工作不得使用 Qt GUI/普通 Context。
     * @param work 同步返回该次工作结果；捕获须支持 worker 使用，捕获析构统一留在 owner。
     * @param label 所属 effect 的诊断标签。
     */
    static Result<QtWorker> start(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                  std::function<Result<void>(WorkerCancellation)> work,
                                  std::string label = "qt.worker");
    /** @brief effect 初始化回执，不代表后台工作已完成。 */
    Task<void> ready() const;
    /** @brief 正常路径在原生退出、捕获/宿主和本 effect 账目排空后交付业务结果；协议失败不证明静止。
     * 物理完成时选定工作错误/取消，发布时重查 Context；业务失败不阻止已排空资源关闭。
     */
    Task<void> result() const;
    /** @brief 任意线程请求本地协作取消；首次本地请求返回 true，不阻塞或强制终止线程。 */
    bool requestCancel() const noexcept;
    /** @brief owner 请求 effect 退役及协作取消，异步等原生退出；Kernel 静止仍由其自身屏障证明。 */
    Task<void> close() const;
    /** @brief owner 读取诊断，不泄漏原生线程句柄或工作闭包。 */
    Result<WorkerSnapshot> snapshot() const;

private:
    /** @brief 仅工厂交付已经登记清理的资源。 */
    QtWorker(std::shared_ptr<detail::WorkerControl> control, EffectToken effect)
        : control_(std::move(control)), effect_(std::move(effect))
    {
    }
    std::shared_ptr<detail::WorkerControl> control_;
    EffectToken effect_;
};
} // namespace cordis::qt
