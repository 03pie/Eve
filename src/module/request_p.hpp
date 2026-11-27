/** @file request_p.hpp
 * @brief ABI 请求的生产宿主仲裁；只管理宿主值和租约，不执行 DLL 或修改 Kernel。
 */
#pragma once
#include <eve/module/ledger.hpp>
#include <eve/task.hpp>
#include <eve_abi/plugin.h>

namespace eve::module::detail {
enum class NativeRequestKind { Start, Stop, Invoke, Callback, Cleanup };
struct NativeOutcome {
    EveStatus status;
    std::string json;
    bool cancelRequested;
};
struct NativeTicket {
    EveRequestId id;
    Task<NativeOutcome> result;
};
struct RequestLimits {
    std::size_t maximumRequests = 256;
    std::size_t maximumJsonBytes = 1024 * 1024;
    std::size_t maximumBufferedBytes = 8 * 1024 * 1024;
};
struct NativeRequestRecord {
    EveRequestId id;
    NativeRequestKind kind;
    bool returned;
    bool completed;
    bool cancelled;
    bool queued;
    bool unsafe;
    bool deliveryBlocked;
};
struct NativeRequestSnapshot {
    EveHandle context;
    InstanceId instance;
    Epoch epoch;
    bool closing;
    bool sealed;
    bool faulted;
    std::uint64_t protocolErrors;
    std::size_t bufferedBytes;
    std::vector<NativeRequestRecord> requests;
};
/** @brief 两个方向共用不复用请求编号，不混淆消费者和目标请求。 */
EveRequestId allocateNativeRequestId();
struct RequestControl;
/** @brief 每个激活上下文一个通道；除 complete 外须 owner 线程调用。
 * 必须在全部清理请求登记后 seal，并等待 drained 后销毁；失败账目保守自保留。
 */
class RequestChannel {
public:
    RequestChannel(std::shared_ptr<IScheduler> scheduler, GenerationLedger ledger, InstanceId instance, Epoch epoch,
                   RequestLimits limits = {});
    ~RequestChannel();
    RequestChannel(const RequestChannel&) = delete;
    RequestChannel& operator=(const RequestChannel&) = delete;
    EveHandle context() const noexcept;
    Result<NativeTicket> begin(NativeRequestKind kind, std::string label);
    /** @brief 在 DLL 入口实际退出后记录返回值；不在此处发布业务结果。 */
    Result<void> returned(EveRequestId request, EveStatus status);
    /** @brief 可在任意线程到达；返回前复制合法 JSON，仲裁一次，不内联推进状态机。 */
    EveStatus complete(EveHandle context, EveRequestId request, EveStatus status,
                          EveBytesV1 json) noexcept;
    /** @brief 登记取消意图；已发布结果或已请求取消时返回 false，不再向插件重复取消。 */
    Result<bool> cancel(EveRequestId request);
    /** @brief 关新业务，仍允许 Stop/Cleanup；不伪造现有请求的完成或自动发送取消。 */
    Result<void> close();
    /** @brief 最终禁止所有新请求，包括清理；只在此后发布不可逆的 drained 屏障。 */
    Result<void> seal();
    Task<void> drained() const;
    Result<NativeRequestSnapshot> snapshot() const;

private:
    std::shared_ptr<RequestControl> control_;
};
} // namespace eve::module::detail
