/** @file connection.hpp
 * @brief 将 Qt 信号触发及受限类型化数据纳入当前插件代的 effect 和异步关闭屏障。
 */
#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <cordis/instance.hpp>
#include <cordis/qt/scheduler.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
namespace cordis::qt {
namespace detail {
struct ConnectionControl;
struct NativeConnectionLease;
struct ConnectionTicket;
using ConnectionPayload = std::shared_ptr<const void>;
using ConnectionCallback = std::function<Task<void>(ConnectionPayload)>;
/** @brief 原生 Qt 闭包只包含宿主租约，业务函数不进入 Qt 原生连接或消息队列。 */
class SignalBridge {
public:
    /** @brief 同一租约的 Qt 闭包副本共同证明原生调用快照是否已释放。 */
    explicit SignalBridge(std::shared_ptr<ConnectionControl> control);
    /** @brief 任意发射线程只登记宿主通知，忽略信号参数，不调用插件。 */
    void operator()() const noexcept;
    /** @brief 原生桥先占预算再复制；空票据表示关门、取消或预算拒绝。 */
    std::shared_ptr<ConnectionTicket> reserve() const noexcept;
    /** @brief 将已冻结的数据交给 owner；Qt 消息仅保留票据，不承载数据。 */
    void deliver(const std::shared_ptr<ConnectionTicket>& ticket, ConnectionPayload payload) const noexcept;
    /** @brief 复制失败或超过字节预算时终结票据，不使异常越过 Qt 激活栈。 */
    void payloadFailed(const std::shared_ptr<ConnectionTicket>& ticket, bool oversized) const noexcept;

private:
    std::shared_ptr<NativeConnectionLease> lease_;
};
/** @brief 类型化信号仅接纳拥有数据的标准值；不提供用户自定义 copier 或元类型扩展点。 */
template <class T> inline constexpr bool signalValue =
    std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string> ||
    std::is_same_v<T, QString> || std::is_same_v<T, QByteArray>;
/** @brief 估算冻结数据的内容字节数，不含容器和分配器固定开销。 */
template <class T> std::size_t signalValueBytes(const T& value) noexcept
{
    if constexpr (std::is_same_v<T, QString>)
        return static_cast<std::size_t>(value.size()) * sizeof(QChar);
    else if constexpr (std::is_same_v<T, QByteArray> || std::is_same_v<T, std::string>)
        return static_cast<std::size_t>(value.size());
    else {
        (void)value;
        return sizeof(T);
    }
}
/** @brief 强制 Qt 字符串拥有独立存储，避免隐式共享继续借用 fromRawData 缓冲区。 */
template <class T> T freezeSignalValue(const T& value)
{
    if constexpr (std::is_same_v<T, QString> || std::is_same_v<T, QByteArray>)
        return value.isNull() ? T{} : T(value.constData(), value.size());
    else
        return value;
}
/** @brief 原生线程仅冻结白名单值；插件回调和捕获不进入原生连接。 */
template <class... Args> class ValueSignalBridge {
    static_assert((signalValue<std::decay_t<Args>> && ...), "unsupported managed signal value type");
public:
    using Values = std::tuple<std::decay_t<Args>...>;
    /** @brief 同一资源租约覆盖冻结过程及原生 Qt 调用快照。 */
    ValueSignalBridge(SignalBridge bridge, std::size_t maximumBytes)
        : bridge_(std::move(bridge)), maximumBytes_(maximumBytes) {}
    /** @brief const 引用入口避免在预算检查之前复制信号的值参数。 */
    void operator()(const std::decay_t<Args>&... values) const noexcept
    {
        auto ticket = bridge_.reserve();
        if (!ticket)
            return;
        if constexpr (sizeof...(Args) > 0) {
            std::size_t remaining = maximumBytes_;
            const auto fits = [&remaining](const auto& value) {
                const auto bytes = signalValueBytes(value);
                if (bytes > remaining)
                    return false;
                remaining -= bytes;
                return true;
            };
            if (!(fits(values) && ...)) {
                bridge_.payloadFailed(ticket, true);
                return;
            }
        }
        try {
            bridge_.deliver(ticket, std::make_shared<const Values>(freezeSignalValue(values)...));
        } catch (...) {
            bridge_.payloadFailed(ticket, false);
        }
    }
private:
    SignalBridge bridge_;
    std::size_t maximumBytes_;
};
/** @brief 被明确选择的数据参数共用冻结桥、tuple 及 owner 回调，不为未选参数创建存储。 */
template <class... Args> struct SelectedSignalValues {
    using Arguments = std::tuple<std::decay_t<Args>...>;
    using Bridge = ValueSignalBridge<Args...>;
    using Callback = std::function<Task<void>(const std::decay_t<Args>&...)>;
    /** @brief 本次调用的参数由连接在途账目保留至业务 Task 及调用栈都已完成。 */
    static ConnectionCallback bind(Callback callback)
    {
        return [callback = std::move(callback)](ConnectionPayload payload) {
            auto values = std::static_pointer_cast<const typename Bridge::Values>(std::move(payload));
            return std::apply(callback, *values);
        };
    }
};
/** @brief 从信号成员指针提取数据参数，支持普通及 const/noexcept 信号。 */
template <class Signal> struct SignalValues;
/** @brief 完整信号沿用全部参数；前缀入口仅借助 Arguments 检查原始类型顺序。 */
template <class C, class... Args> struct SignalValues<void (C::*)(Args...)> : SelectedSignalValues<Args...> {};
/** @brief const 信号沿用相同的值冻结与投递规则。 */
template <class C, class... Args> struct SignalValues<void (C::*)(Args...) const> : SignalValues<void (C::*)(Args...)> {};
/** @brief noexcept 信号的复制异常仍由桥接层隔离。 */
template <class C, class... Args> struct SignalValues<void (C::*)(Args...) noexcept> : SignalValues<void (C::*)(Args...)> {};
/** @brief const noexcept 信号沿用相同的值冻结与投递规则。 */
template <class C, class... Args> struct SignalValues<void (C::*)(Args...) const noexcept> : SignalValues<void (C::*)(Args...)> {};
/** @brief 比较选中参数的精确值类型，不能以 Qt 允许隐式转换为依据执行发射端转换。 */
template <class Full, class Selected, std::size_t... I>
constexpr bool signalPrefixMatches(std::index_sequence<I...>)
{
    return (std::is_same_v<std::tuple_element_t<I, Full>, std::tuple_element_t<I, Selected>> && ...);
}
/** @brief 长度先检查再访问 tuple，过长前缀产生明确的约束诊断。 */
template <class Full, class Selected> constexpr bool signalPrefixMatches()
{
    if constexpr (std::tuple_size_v<Selected> > std::tuple_size_v<Full>)
        return false;
    else
        return signalPrefixMatches<Full, Selected>(std::make_index_sequence<std::tuple_size_v<Selected>>{});
}
} // namespace detail
/** @brief 只读连接账目；running 覆盖业务、Task 与释放栈，queued 排除已取消票据。
 * accepted 统计已保留票据，payloads 统计仍持有的数据份数，payloadErrors 统计冻结/字节预算错误。
 */
struct ConnectionSnapshot {
    ContextIdentity owner;
    bool open = false, closing = false, closed = false;
    bool nativeRetired = false, nativeDestroyed = false, deliveryBlocked = false;
    std::size_t queued = 0, running = 0;
    std::size_t accepted = 0, executed = 0, discarded = 0, rejected = 0, callbackErrors = 0;
    std::size_t payloads = 0, payloadErrors = 0;
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
        if (!sender || !signal || !callback)
            return Result<QtConnection>::failure(
                makeError(ErrorCode::InvalidArgument, "connection requires sender, signal and callback"));
        return create(owner, std::move(scheduler),
                      [callback = std::move(callback)](detail::ConnectionPayload) { return callback(); },
                      std::move(label), maximumPending,
                      [sender, signal](QObject* receiver, detail::SignalBridge bridge) {
                          return QObject::connect(sender, signal, receiver, std::move(bridge), Qt::DirectConnection);
                      });
    }
    /** @brief owner 登记类型化信号；受支持值在发射线程复制，业务始终排队并重查当前代。
     * @param sender 与 observe 相同，调用方保证发送者在注册返回前存活，不在 connectNotify 中销毁自身。
     * @param callback 以 const 引用接收冻结数据；存储覆盖回调栈和返回 Task 的寿命，之后不得借用引用。
     * @param maximumPayloadBytes 每次信号的内容字节预算，默认 1 MiB，上限 16 MiB；不含固定容器开销。
     * 只支持数值、枚举、std::string、QString、QByteArray，不接纳指针、视图或自定义元类型。
     */
    template <class Sender, class Signal, class Callback>
    static Result<QtConnection> observeValues(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                              Sender* sender, Signal signal, Callback&& callback,
                                              std::string label = "qt.connection.values",
                                              std::size_t maximumPending = 256,
                                              std::size_t maximumPayloadBytes = 1024 * 1024)
    {
        return observeSelectedValues<detail::SignalValues<Signal>>(
            owner, std::move(scheduler), sender, signal, std::forward<Callback>(callback),
            std::move(label), maximumPending, maximumPayloadBytes);
    }
    /** @brief 只冻结显式选择的前 N 个数据参数，忽略 Qt 私有尾标记等未选参数。
     * @tparam Values 非空连续前缀，去除 cv/ref 后须与原信号前 N 项类型完全相同；仍遵守数据白名单。
     * @param callback 参数寿命和所有关闭/预算语义与 observeValues 相同。
     * 不按名称识别或构造 QPrivateSignal，不允许隐式类型转换，也不访问或复制未选参数。
     */
    template <class... Values, class Sender, class Signal, class Callback>
    static Result<QtConnection> observePrefixValues(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                                    Sender* sender, Signal signal, Callback&& callback,
                                                    std::string label = "qt.connection.prefix",
                                                    std::size_t maximumPending = 256,
                                                    std::size_t maximumPayloadBytes = 1024 * 1024)
    {
        using Traits = detail::SelectedSignalValues<Values...>;
        static_assert(sizeof...(Values) > 0, "managed signal prefix must select at least one value");
        static_assert(detail::signalPrefixMatches<typename detail::SignalValues<Signal>::Arguments,
                                                   typename Traits::Arguments>(),
                      "managed signal prefix must match the leading signal values");
        return observeSelectedValues<Traits>(owner, std::move(scheduler), sender, signal,
                                             std::forward<Callback>(callback), std::move(label),
                                             maximumPending, maximumPayloadBytes);
    }
    /** @brief effect 初始化回执，和所属插件激活结果分别观察。 */
    Task<void> ready() const;
    /** @brief owner 同步关门并请求 effect 退役，等待资源排空；Kernel 的整体清理由其自身屏障确认。 */
    Task<void> close() const;
    /** @brief owner 查询诊断，不获取插件回调或 QObject 所有权。 */
    Result<ConnectionSnapshot> snapshot() const;

private:
    /** @brief 完整与前缀入口共享运行门禁、预算及资源登记，唯一差异是被选中的参数列表。 */
    template <class Traits, class Sender, class Signal, class Callback>
    static Result<QtConnection> observeSelectedValues(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                                      Sender* sender, Signal signal, Callback&& callback,
                                                      std::string label, std::size_t maximumPending,
                                                      std::size_t maximumPayloadBytes)
    {
        typename Traits::Callback function(std::forward<Callback>(callback));
        if (!sender || !signal || !function || maximumPayloadBytes == 0 || maximumPayloadBytes > 16 * 1024 * 1024)
            return Result<QtConnection>::failure(makeError(ErrorCode::InvalidArgument, "invalid typed connection"));
        return create(owner, std::move(scheduler), Traits::bind(std::move(function)), std::move(label), maximumPending,
                      [sender, signal, maximumPayloadBytes](QObject* receiver, detail::SignalBridge bridge) {
                          return QObject::connect(sender, signal, receiver,
                                                  typename Traits::Bridge(std::move(bridge), maximumPayloadBytes),
                                                  Qt::DirectConnection);
                      });
    }
    using Installer = std::function<QMetaObject::Connection(QObject*, detail::SignalBridge)>;
    /** @brief 构造只交付已注册清理的托管资源。 */
    QtConnection(std::shared_ptr<detail::ConnectionControl> control, EffectToken effect)
        : control_(std::move(control)), effect_(std::move(effect))
    {
    }
    /** @brief 模板负责 Qt 类型检查及受限值冻结，资源账目和业务调度在宿主 Qt 适配库。 */
    static Result<QtConnection> create(const Context& owner, std::shared_ptr<QtScheduler> scheduler,
                                       detail::ConnectionCallback callback, std::string label,
                                       std::size_t maximumPending, Installer install);
    std::shared_ptr<detail::ConnectionControl> control_;
    EffectToken effect_;
};
} // namespace cordis::qt
