/* Read-only module discovery ABI. This is separate from the future business HostTable. */
#ifndef CORDIS_ABI_MODULE_QUERY_H
#define CORDIS_ABI_MODULE_QUERY_H
#include <stdint.h>

#if defined(_WIN32)
#define CORDIS_QUERY_CALL __cdecl
#if defined(CORDIS_MODULE_EXPORTS)
#define CORDIS_QUERY_EXPORT __declspec(dllexport)
#else
#define CORDIS_QUERY_EXPORT
#endif
#else
#define CORDIS_QUERY_CALL
#define CORDIS_QUERY_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CORDIS_MODULE_QUERY_MAJOR UINT32_C(1)
#define CORDIS_MODULE_QUERY_MINOR UINT32_C(0)
#define CORDIS_MODULE_RESIDENT UINT64_C(1)
#define CORDIS_QUERY_OK INT32_C(0)
#define CORDIS_QUERY_UNSUPPORTED (-INT32_C(1))
#define CORDIS_QUERY_INVALID_ARGUMENT (-INT32_C(2))
#define CORDIS_QUERY_FAILED (-INT32_C(3))

typedef int32_t CordisQueryStatus;
typedef struct CordisQueryHeaderV1 {
    uint32_t struct_size;
    uint32_t query_major;
    uint32_t query_minor;
    uint32_t reserved;
} CordisQueryHeaderV1;

/* Borrowed bytes, no terminator required; nonempty spans require nonnull data. */
typedef struct CordisQueryBytesV1 {
    const uint8_t* data;
    uint64_t size;
} CordisQueryBytesV1;

/* Borrowed only during query. Flags and reserved fields must be zero in V1. */
typedef struct CordisModuleQueryV1 {
    CordisQueryHeaderV1 header;
    uint32_t runtime_abi_major;
    uint32_t runtime_abi_minor;
    uint64_t flags;
} CordisModuleQueryV1;

/* Returned spans/array stay immutable and valid for the mapped module lifetime.
 * IDs use the manifest's ASCII rules. Maximum: 128 bytes per ID, 64 bytes per
 * version, 64 unique plugin IDs. reserved must be zero. flags is RESIDENT only.
 */
typedef struct CordisModuleDescriptorV1 {
    CordisQueryHeaderV1 header;
    uint32_t runtime_abi_major;
    uint32_t runtime_abi_minor;
    uint64_t flags;
    CordisQueryBytesV1 module_id;
    CordisQueryBytesV1 module_version;
    uint32_t plugin_count;
    uint32_t reserved;
    const CordisQueryBytesV1* plugins;
} CordisModuleDescriptorV1;

/* Synchronous, no callbacks, resources, retained request pointers or exceptions.
 * Host initializes output.header.struct_size to its writable capacity; plugin
 * writes at most min(capacity, its descriptor size), reports its own size, and
 * returns UNSUPPORTED when versions cannot be negotiated. Output is read only
 * on OK. A successful query does not create/register/activate business plugins.
 */
typedef CordisQueryStatus(CORDIS_QUERY_CALL* CordisModuleQueryFnV1)(const CordisModuleQueryV1* request,
                                                                    CordisModuleDescriptorV1* output);
CORDIS_QUERY_EXPORT CordisQueryStatus CORDIS_QUERY_CALL cordis_module_query_v1(const CordisModuleQueryV1* request,
                                                                               CordisModuleDescriptorV1* output);

#ifdef __cplusplus
}
#endif
#endif
