/* Windows x64 business ABI V1. C11, natural layout; no C++/Qt types or packing. */
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

typedef struct CordisTableHeaderV1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t reserved;
} CordisTableHeaderV1;

/* Borrowed bytes, no terminator. size > 0 requires data != NULL. JSON is UTF-8.
 * Inputs live only through the call; receivers copy before returning.
 * Zero bytes means no result, never an implicit JSON null. No shared buffers V1.
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

/* Host-owned immutable table. It outlives all module instances, callbacks and
 * outstanding requests. Nonzero handles are scoped to context/generation/epoch.
 * All operations except complete/log require the owner thread. complete/log
 * copy data on any thread and never execute a registry mutation or callback inline.
 * Output handles/request IDs are zero on failure. No exception may escape.
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

/* Base lifecycle and request callbacks are mandatory. Optional family slots
 * are all nonnull iff their capability is negotiated; otherwise they are NULL.
 * Query metadata spans stay immutable for the mapped module lifetime.
 * Functions run on the owner thread and never throw. All async entries receive
 * a host-issued request: PENDING promises exactly one host.complete (which may
 * race with return). Any other return is final and forbids complete. The host
 * reconciles a copied early completion only after the entry has returned.
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

/* Pure negotiation: no callbacks, resource creation or lifecycle calls.
 * host_abi is the major; minor/size/capabilities are in the tables. Host sets
 * output.header.struct_size to capacity. Producer writes <= min(capacity, own
 * size), reports own size, and retains host only after a successful query.
 * Same major + known prefix/features permits a newer minor/tail. Unknown bits
 * or required features missing from host are rejected, never silently ignored.
 */
typedef CordisStatus(CORDIS_CALL* CordisQueryFnV1)(uint32_t host_abi, const CordisHostV1* host,
                                                   CordisPluginApiV1* output);
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(uint32_t host_abi, const CordisHostV1* host,
                                                       CordisPluginApiV1* output);

#ifdef __cplusplus
}
#endif
#endif
