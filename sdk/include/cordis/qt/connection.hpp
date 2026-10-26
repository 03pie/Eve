/** @file connection.hpp
 * @brief 将 Qt 信号作为无参数触发通知纳入当前插件代的 effect 和异步关闭屏障。
 */
#pragma once
#include <QObject>
#include <cordis/instance.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
namespace cordis::qt {
namespace detail {
struct ConnectionControl;
struct NativeConnectionLease;
/** @brief 原生 Qt 闭包只包含宿主租约，业务函数不进入 Qt 原生连接或消息队列。 */
class SignalBridge {
public:
    /** @brief 同一租约的 Qt 闭包副本共同证明原生调用快照是否已释放。 */
    explicit SignalBridge(std::shared_ptr<ConnectionControl> control);
    /** @brief 任意发射线程只登记宿主通知，忽略信号参数，不调用插件。 */
    void operator()() const noexcept;

private:
    std::shared_ptr<NativeConnectionLease> lease_;
};
} // namespace detail
/** @brief 只读连接账目；running 包括异步任务和回调栈，queued 不包含已取消的宿主空通知。 */
struct ConnectionSnapshot {
    ContextIdentity owner;
    bool open = false, closing = false, closed = false;
    bool nativeRetired = false, nativeDestroyed = false, deliveryBlocked = false;
    std::size_t queued = 0, running = 0;
    std::size_t accepted = 0, executed = 0, discarded = 0, rejected = 0, callbackErrors = 0;
    std::optional<ErrorInfo> lastError;
};
/** @brief 连接归属一个 Context/epoch；复制或释放句柄不取消资源，退出由 effect 排空。 */
class QtConnection {
public:
    /** @brief owner 登记信号触发器；callback 始终排队，返回 Task 必须覆盖异步业务完成。
     * @param sender 调用方保证其在本次注册返回前存活；注册后自动处理发送者销毁。
     * @param signal 信号成员指针；参数不转发或复制，回调可在 owner 查询当前宿主状态。
     * @param maximumPending 队列与在途业务总上限，溢出通知拒绝并记账。
     */
    template <class Sender, class Signal>
    static Result<QtConnection> observe(const Context& owner, std::shared_ptr<QtScheduler> scheduler, Sender* sender,
                                        Signal signal, std::function<Task<void>()> callback,
                                        std::string label = "qt.connection", std::size_t maximumPending = 256)
    {
        if (!sender || !signal)
            return Result<QtConnection>::failure(
                makeError(ErrorCode::InvalidArgument, "connection requires sender and signal"));
        return create(owner, std::move(scheduler), std::move(callback), std::move(label), maximumPending,
                      [sender, signal](QObject* receiver, detail::SignalBridge bridge) {
                          return QObject::connect(sender, signal, receiver, std::move(bridge), Qt::DirectConnection);
                      });
    }
    /** @brief effect 初始化回执，和所属插件激活结果分别观察。 */
    Task<void> ready() const;
    /** @brief owner 同步关门并请求 effect 退役，等待资源排空；Kernel 的整体清理由其自身屏障确认。 */
    Task<void> close() const;
    /** @brief owner 查询诊断，不获取插件回调或 QObject 所有权。 */
    Result<ConnectionSnapshot> snapshot() const;

private:
    using Installer = std::function<QMetaObject::Connection(QObject*, detail::SignalBridge)>;
    /** @brief 构造只交付已注册清理的托管资源。 */
    QtConnection(std::shared_ptr<detail::ConnectionControl> control, EffectToken effect)
        : control_(std::move(control)), effect_(std::move(effect))
    {
    }
    /** @brief 模板只做 Qt 类型检查，资源及所有业务调度代码编译在宿主 Qt 适配库。 */
    static Result<QtConnection> create(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                       std::function<Task<void>()> callback, std::string label,
                                       std::size_t maximumPending, Installer install);
    std::shared_ptr<detail::ConnectionControl> control_;
    EffectToken effect_;
};
} // namespace cordis::qt
