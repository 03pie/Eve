/** @file plugin.h
 * @brief Windows x64 C11 业务 ABI。宿主与模块以自然布局的固定宽度函数表传递句柄、借用字节和请求终态。
 */
#ifndef EVE_ABI_PLUGIN_H
#define EVE_ABI_PLUGIN_H
#include <stdint.h>

#if defined(_WIN32)
#define EVE_CALL __cdecl
#if defined(EVE_ABI_EXPORTS)
#define EVE_EXPORT __declspec(dllexport)
#else
#define EVE_EXPORT
#endif
#else
#define EVE_CALL
#define EVE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t EveHandle;
typedef uint64_t EveRequestId;
typedef int32_t EveStatus;
#define EVE_ABI_MAJOR UINT32_C(1)
#define EVE_ABI_MINOR UINT32_C(0)
#define EVE_OK INT32_C(0)
#define EVE_PENDING INT32_C(1)
#define EVE_E_INVALID_ARGUMENT (-INT32_C(1))
#define EVE_E_UNSUPPORTED (-INT32_C(2))
#define EVE_E_NOT_FOUND (-INT32_C(3))
#define EVE_E_CLOSED (-INT32_C(4))
#define EVE_E_WRONG_THREAD (-INT32_C(5))
#define EVE_E_CANCELLED (-INT32_C(6))
#define EVE_E_ABI_PROTOCOL (-INT32_C(7))
#define EVE_E_LIMIT (-INT32_C(8))
#define EVE_E_FAILED (-INT32_C(9))
#define EVE_E_BUSY (-INT32_C(10))

#define EVE_CAP_DISPATCH UINT64_C(1)
#define EVE_CAP_SERVICES UINT64_C(2)
#define EVE_CAP_EFFECTS UINT64_C(4)
#define EVE_CAP_ALL (EVE_CAP_DISPATCH | EVE_CAP_SERVICES | EVE_CAP_EFFECTS)
/* SERVICES requires EFFECTS: every registered provider belongs to an effect. */
#define EVE_MAX_ID_BYTES UINT64_C(128)
#define EVE_MAX_VERSION_BYTES UINT64_C(64)
#define EVE_MAX_LABEL_BYTES UINT64_C(256)
#define EVE_MAX_LOG_BYTES UINT64_C(65536)
#define EVE_MAX_JSON_BYTES UINT64_C(1048576)
#define EVE_MAX_PLUGINS UINT32_C(64)
#define EVE_LOG_TRACE UINT32_C(0)
#define EVE_LOG_DEBUG UINT32_C(1)
#define EVE_LOG_INFO UINT32_C(2)
#define EVE_LOG_WARNING UINT32_C(3)
#define EVE_LOG_ERROR UINT32_C(4)

/** @brief 宿主用 struct_size 提供可写容量，生产方报告自身表长，调用方按已协商版本访问已知前缀。 */
typedef struct EveTableHeaderV1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
} EveTableHeaderV1;

/** @brief 由发送方持有、按 size 读取的借用字节。size 大于零时 data 必须有效，无需终止符。
 * 输入只在调用期间有效，接收方需保留时须在返回前复制。JSON 使用 UTF-8 编码。
 * 对于结果字段，零字节表示没有结果，JSON 的 null 须显式编码。V1 没有共享缓冲区所有权协议。
 */
typedef struct EveBytesV1 {
    const uint8_t* data;
    uint64_t size;
} EveBytesV1;

typedef struct EveServiceKeyV1 {
    EveBytesV1 name;
    uint32_t interface_major;
    uint32_t reserved;
    EveHandle realm;
} EveServiceKeyV1;

typedef struct EveServiceMetadataV1 {
    uint32_t struct_size;
    uint32_t minor;
    EveBytesV1 capabilities_json;
} EveServiceMetadataV1;

/** @brief 宿主持有不可变的函数表，并保持其有效直至所有模块实例、回调和在途请求均已结束。
 * 非零句柄的作用域受上下文、模块代次和实例 epoch 限制。complete 和 log 可在任意线程复制输入，
 * 调用期间不得内联修改注册表或执行回调。其余操作须在宿主所属线程调用。
 * 失败时输出句柄或请求标识为零。所有入口均禁止异常越过 ABI 边界。
 */
typedef struct EveHostV1 {
    EveTableHeaderV1 header;
    uint64_t capabilities;
    uint64_t reserved;
    /* Final status only (OK or a defined negative error), exactly once for a
     * host-issued PENDING operation. Cancellation does not waive completion.
     */
    EveStatus(EVE_CALL* complete)(EveHandle context, EveRequestId request, EveStatus status,
                                        EveBytesV1 result_json);
    /** @brief 可从任意线程提交至多 EVE_MAX_LOG_BYTES 字节的 UTF-8 文本。
     * EVE_OK 表示宿主已将记录复制到该实例的有界内存日志。宿主配置接收端且日志已获准发布时才异步投递。
     * 磁盘归档取决于接收端的配置和写入结果。
     */
    EveStatus(EVE_CALL* log)(EveHandle context, uint32_t level, EveBytesV1 text);
    /* Cancels a plugin-originated host operation; completion still arrives. */
    EveStatus(EVE_CALL* request_cancel)(EveHandle context, EveRequestId request);
    /* Accepted host operations return PENDING with an ID. Delivery never runs
     * inline; request_complete is eventually called, even after cancellation.
     */
    EveStatus(EVE_CALL* dispatch)(EveHandle context, EveHandle callback, EveBytesV1 payload_json,
                                        EveRequestId* request);
    EveStatus(EVE_CALL* service_register)(EveHandle context, EveHandle effect,
                                                const EveServiceKeyV1* key, const EveServiceMetadataV1* metadata,
                                                EveHandle service_token, EveHandle* binding);
    EveStatus(EVE_CALL* service_withdraw)(EveHandle context, EveHandle binding, EveRequestId* request);
    EveStatus(EVE_CALL* service_invoke)(EveHandle context, const EveServiceKeyV1* key,
                                              uint32_t minimum_minor, EveBytesV1 required_capabilities_json,
                                              EveBytesV1 method, EveBytesV1 payload_json,
                                              EveRequestId* request);
    /* Register before creating resources. ready reports setup exactly once.
     * Cleanup callback runs via plugin.request_cleanup, never via a raw pointer.
     */
    EveStatus(EVE_CALL* effect_create)(EveHandle context, EveBytesV1 label, EveHandle cleanup_callback,
                                             EveHandle* effect);
    EveStatus(EVE_CALL* effect_ready)(EveHandle context, EveHandle effect, EveStatus status,
                                            EveBytesV1 error_json);
    EveStatus(EVE_CALL* effect_request_cleanup)(EveHandle context, EveHandle effect,
                                                      EveRequestId* request);
} EveHostV1;

/** @brief 模块必须提供基础生命周期和请求回调。在已协商的可选能力组内，每个槽位都必须有效，其余能力组的槽位均为 NULL。
 * 查询返回的元数据字节须在模块映射期间保持有效且不可变。函数均在宿主所属线程执行，禁止抛出异常。
 * 每个异步入口接收宿主分配的请求标识。返回 EVE_PENDING 的请求必须恰好调用一次 host.complete，
 * 完成通知可能早于入口返回。其他返回值均是终态，此时禁止再调用 host.complete。
 * 宿主先复制提前到达的结果，等入口返回后再核对并结算。
 */
typedef struct EvePluginApiV1 {
    EveTableHeaderV1 header;
    uint64_t capabilities;
    uint64_t required_host_capabilities;
    EveBytesV1 module_id;
    EveBytesV1 module_version;
    uint32_t plugin_count;
    uint32_t reserved;
    const EveBytesV1* plugins;
    /* Synchronous, no event-loop waits. Instance is nonzero only on OK.
     * One instance belongs to one activation context; config/plugin ID borrowed.
     */
    EveStatus(EVE_CALL* create)(EveHandle context, EveBytesV1 plugin_id, EveBytesV1 config_json,
                                      EveHandle* instance);
    EveStatus(EVE_CALL* start)(EveHandle instance, EveRequestId request);
    EveStatus(EVE_CALL* request_stop)(EveHandle instance, EveRequestId request);
    /* Synchronous, after stop, requests, callbacks and resources are drained.
     * Failure keeps the instance and module resident; never free on failure.
     */
    EveStatus(EVE_CALL* destroy)(EveHandle instance);
    /* Cancels host-initiated plugin work, without completing or destroying it. */
    EveStatus(EVE_CALL* cancel_request)(EveHandle instance, EveRequestId request);
    /* Final delivery for a plugin-originated host operation. Synchronous ACK;
     * no PENDING, no recursive complete, borrowed result copied if retained.
     */
    EveStatus(EVE_CALL* request_complete)(EveHandle instance, EveRequestId request, EveStatus status,
                                                EveBytesV1 result_json);
    EveStatus(EVE_CALL* dispatch)(EveHandle instance, EveHandle callback, EveBytesV1 payload_json,
                                        EveRequestId request);
    EveStatus(EVE_CALL* invoke_service)(EveHandle instance, EveHandle service_token, EveBytesV1 method,
                                              EveBytesV1 payload_json, EveRequestId request);
    EveStatus(EVE_CALL* request_cleanup)(EveHandle instance, EveHandle effect,
                                               EveHandle cleanup_callback, EveRequestId request);
} EvePluginApiV1;

/** @brief 入口仅协商函数表，调用期间禁止回调宿主、创建资源或执行生命周期操作。
 * host_abi 指定主版本，次版本、结构长度和能力位由表提供。宿主在 output.header.struct_size 中写入容量，
 * 模块至多写入该容量和自身表长的较小值，并报告自身表长。协商成功后模块才可保留 host。
 * 主版本相同、已知前缀完整且能力可识别时允许较新的次版本或尾部扩展。
 * 出现未知能力位或宿主缺少模块必需的能力时，必须拒绝协商。
 */
typedef EveStatus(EVE_CALL* EveQueryFnV1)(uint32_t host_abi, const EveHostV1* host,
                                                   EvePluginApiV1* output);
/** @brief 按 EveQueryFnV1 协商模块与宿主的业务函数表。宿主校验成功后才进入实例生命周期。 */
EVE_EXPORT EveStatus EVE_CALL eve_query_v1(uint32_t host_abi, const EveHostV1* host,
                                                       EvePluginApiV1* output);

#ifdef __cplusplus
}
#endif
#endif
