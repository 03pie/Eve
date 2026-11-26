/** @file plugin.h
 * @brief Windows x64 C11 业务 ABI。宿主与模块以自然布局的固定宽度函数表传递句柄、借用字节和请求终态。
 */
#ifndef CORDIS_ABI_PLUGIN_H
#define CORDIS_ABI_PLUGIN_H
#include <stdint.h>

#if defined(_WIN32)
#define CORDIS_CALL __cdecl
#if defined(CORDIS_ABI_EXPORTS)
#define CORDIS_EXPORT __declspec(dllexport)
#else
#define CORDIS_EXPORT
#endif
#else
#define CORDIS_CALL
#define CORDIS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t CordisHandle;
typedef uint64_t CordisRequestId;
typedef int32_t CordisStatus;
#define CORDIS_ABI_MAJOR UINT32_C(1)
#define CORDIS_ABI_MINOR UINT32_C(0)
#define CORDIS_OK INT32_C(0)
#define CORDIS_PENDING INT32_C(1)
#define CORDIS_E_INVALID_ARGUMENT (-INT32_C(1))
#define CORDIS_E_UNSUPPORTED (-INT32_C(2))
#define CORDIS_E_NOT_FOUND (-INT32_C(3))
#define CORDIS_E_CLOSED (-INT32_C(4))
#define CORDIS_E_WRONG_THREAD (-INT32_C(5))
#define CORDIS_E_CANCELLED (-INT32_C(6))
#define CORDIS_E_ABI_PROTOCOL (-INT32_C(7))
#define CORDIS_E_LIMIT (-INT32_C(8))
#define CORDIS_E_FAILED (-INT32_C(9))
#define CORDIS_E_BUSY (-INT32_C(10))

#define CORDIS_CAP_DISPATCH UINT64_C(1)
#define CORDIS_CAP_SERVICES UINT64_C(2)
#define CORDIS_CAP_EFFECTS UINT64_C(4)
#define CORDIS_CAP_ALL (CORDIS_CAP_DISPATCH | CORDIS_CAP_SERVICES | CORDIS_CAP_EFFECTS)
/* SERVICES requires EFFECTS: every registered provider belongs to an effect. */
#define CORDIS_MAX_ID_BYTES UINT64_C(128)
#define CORDIS_MAX_VERSION_BYTES UINT64_C(64)
#define CORDIS_MAX_LABEL_BYTES UINT64_C(256)
#define CORDIS_MAX_LOG_BYTES UINT64_C(65536)
#define CORDIS_MAX_JSON_BYTES UINT64_C(1048576)
#define CORDIS_MAX_PLUGINS UINT32_C(64)
#define CORDIS_LOG_TRACE UINT32_C(0)
#define CORDIS_LOG_DEBUG UINT32_C(1)
#define CORDIS_LOG_INFO UINT32_C(2)
#define CORDIS_LOG_WARNING UINT32_C(3)
#define CORDIS_LOG_ERROR UINT32_C(4)

/** @brief 宿主用 struct_size 提供可写容量，生产方报告自身表长，调用方按已协商版本访问已知前缀。 */
typedef struct CordisTableHeaderV1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
} CordisTableHeaderV1;

/** @brief 由发送方持有、按 size 读取的借用字节。size 大于零时 data 必须有效，无需终止符。
 * 输入只在调用期间有效，接收方需保留时须在返回前复制。JSON 使用 UTF-8 编码。
 * 对于结果字段，零字节表示没有结果，JSON 的 null 须显式编码。V1 没有共享缓冲区所有权协议。
 */
typedef struct CordisBytesV1 {
    const uint8_t* data;
    uint64_t size;
} CordisBytesV1;

typedef struct CordisServiceKeyV1 {
    CordisBytesV1 name;
    uint32_t interface_major;
    uint32_t reserved;
    CordisHandle realm;
} CordisServiceKeyV1;

typedef struct CordisServiceMetadataV1 {
    uint32_t struct_size;
    uint32_t minor;
    CordisBytesV1 capabilities_json;
} CordisServiceMetadataV1;

/** @brief 宿主持有不可变的函数表，并保持其有效直至所有模块实例、回调和在途请求均已结束。
 * 非零句柄的作用域受上下文、模块代次和实例 epoch 限制。complete 和 log 可在任意线程复制输入，
 * 调用期间不得内联修改注册表或执行回调。其余操作须在宿主所属线程调用。
 * 失败时输出句柄或请求标识为零。所有入口均禁止异常越过 ABI 边界。
 */
typedef struct CordisHostV1 {
    CordisTableHeaderV1 header;
    uint64_t capabilities;
    uint64_t reserved;
    /* Final status only (OK or a defined negative error), exactly once for a
     * host-issued PENDING operation. Cancellation does not waive completion.
     */
    CordisStatus(CORDIS_CALL* complete)(CordisHandle context, CordisRequestId request, CordisStatus status,
                                        CordisBytesV1 result_json);
    /** @brief 可从任意线程提交至多 CORDIS_MAX_LOG_BYTES 字节的 UTF-8 文本。
     * CORDIS_OK 表示宿主已将记录复制到该实例的有界内存日志。宿主配置接收端且日志已获准发布时才异步投递。
     * 磁盘归档取决于接收端的配置和写入结果。
     */
    CordisStatus(CORDIS_CALL* log)(CordisHandle context, uint32_t level, CordisBytesV1 text);
    /* Cancels a plugin-originated host operation; completion still arrives. */
    CordisStatus(CORDIS_CALL* request_cancel)(CordisHandle context, CordisRequestId request);
    /* Accepted host operations return PENDING with an ID. Delivery never runs
     * inline; request_complete is eventually called, even after cancellation.
     */
    CordisStatus(CORDIS_CALL* dispatch)(CordisHandle context, CordisHandle callback, CordisBytesV1 payload_json,
                                        CordisRequestId* request);
    CordisStatus(CORDIS_CALL* service_register)(CordisHandle context, CordisHandle effect,
                                                const CordisServiceKeyV1* key, const CordisServiceMetadataV1* metadata,
                                                CordisHandle service_token, CordisHandle* binding);
    CordisStatus(CORDIS_CALL* service_withdraw)(CordisHandle context, CordisHandle binding, CordisRequestId* request);
    CordisStatus(CORDIS_CALL* service_invoke)(CordisHandle context, const CordisServiceKeyV1* key,
                                              uint32_t minimum_minor, CordisBytesV1 required_capabilities_json,
                                              CordisBytesV1 method, CordisBytesV1 payload_json,
                                              CordisRequestId* request);
    /* Register before creating resources. ready reports setup exactly once.
     * Cleanup callback runs via plugin.request_cleanup, never via a raw pointer.
     */
    CordisStatus(CORDIS_CALL* effect_create)(CordisHandle context, CordisBytesV1 label, CordisHandle cleanup_callback,
                                             CordisHandle* effect);
    CordisStatus(CORDIS_CALL* effect_ready)(CordisHandle context, CordisHandle effect, CordisStatus status,
                                            CordisBytesV1 error_json);
    CordisStatus(CORDIS_CALL* effect_request_cleanup)(CordisHandle context, CordisHandle effect,
                                                      CordisRequestId* request);
} CordisHostV1;

/** @brief 模块必须提供基础生命周期和请求回调。在已协商的可选能力组内，每个槽位都必须有效，其余能力组的槽位均为 NULL。
 * 查询返回的元数据字节须在模块映射期间保持有效且不可变。函数均在宿主所属线程执行，禁止抛出异常。
 * 每个异步入口接收宿主分配的请求标识。返回 CORDIS_PENDING 的请求必须恰好调用一次 host.complete，
 * 完成通知可能早于入口返回。其他返回值均是终态，此时禁止再调用 host.complete。
 * 宿主先复制提前到达的结果，等入口返回后再核对并结算。
 */
typedef struct CordisPluginApiV1 {
    CordisTableHeaderV1 header;
    uint64_t capabilities;
    uint64_t required_host_capabilities;
    CordisBytesV1 module_id;
    CordisBytesV1 module_version;
    uint32_t plugin_count;
    uint32_t reserved;
    const CordisBytesV1* plugins;
    /* Synchronous, no event-loop waits. Instance is nonzero only on OK.
     * One instance belongs to one activation context; config/plugin ID borrowed.
     */
    CordisStatus(CORDIS_CALL* create)(CordisHandle context, CordisBytesV1 plugin_id, CordisBytesV1 config_json,
                                      CordisHandle* instance);
    CordisStatus(CORDIS_CALL* start)(CordisHandle instance, CordisRequestId request);
    CordisStatus(CORDIS_CALL* request_stop)(CordisHandle instance, CordisRequestId request);
    /* Synchronous, after stop, requests, callbacks and resources are drained.
     * Failure keeps the instance and module resident; never free on failure.
     */
    CordisStatus(CORDIS_CALL* destroy)(CordisHandle instance);
    /* Cancels host-initiated plugin work, without completing or destroying it. */
    CordisStatus(CORDIS_CALL* cancel_request)(CordisHandle instance, CordisRequestId request);
    /* Final delivery for a plugin-originated host operation. Synchronous ACK;
     * no PENDING, no recursive complete, borrowed result copied if retained.
     */
    CordisStatus(CORDIS_CALL* request_complete)(CordisHandle instance, CordisRequestId request, CordisStatus status,
                                                CordisBytesV1 result_json);
    CordisStatus(CORDIS_CALL* dispatch)(CordisHandle instance, CordisHandle callback, CordisBytesV1 payload_json,
                                        CordisRequestId request);
    CordisStatus(CORDIS_CALL* invoke_service)(CordisHandle instance, CordisHandle service_token, CordisBytesV1 method,
                                              CordisBytesV1 payload_json, CordisRequestId request);
    CordisStatus(CORDIS_CALL* request_cleanup)(CordisHandle instance, CordisHandle effect,
                                               CordisHandle cleanup_callback, CordisRequestId request);
} CordisPluginApiV1;

/** @brief 入口仅协商函数表，调用期间禁止回调宿主、创建资源或执行生命周期操作。
 * host_abi 指定主版本，次版本、结构长度和能力位由表提供。宿主在 output.header.struct_size 中写入容量，
 * 模块至多写入该容量和自身表长的较小值，并报告自身表长。协商成功后模块才可保留 host。
 * 主版本相同、已知前缀完整且能力可识别时允许较新的次版本或尾部扩展。
 * 出现未知能力位或宿主缺少模块必需的能力时，必须拒绝协商。
 */
typedef CordisStatus(CORDIS_CALL* CordisQueryFnV1)(uint32_t host_abi, const CordisHostV1* host,
                                                   CordisPluginApiV1* output);
/** @brief 按 CordisQueryFnV1 协商模块与宿主的业务函数表。宿主校验成功后才进入实例生命周期。 */
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(uint32_t host_abi, const CordisHostV1* host,
                                                       CordisPluginApiV1* output);

#ifdef __cplusplus
}
#endif
#endif
