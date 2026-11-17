/* Optional synchronous state extension. C11, natural Windows x64 layout.
 * Independent of CordisPluginApiV1/CordisHostV1; existing tables stay unchanged.
 */
#ifndef CORDIS_ABI_STATE_H
#define CORDIS_ABI_STATE_H
#include <cordis_abi/plugin.h>
#define CORDIS_STATE_ABI_MAJOR UINT32_C(1)
#define CORDIS_STATE_ABI_MINOR UINT32_C(0)
#define CORDIS_STATE_MAX_ID_BYTES UINT32_C(128)
#define CORDIS_STATE_MAX_JSON_BYTES UINT64_C(1048576)

#ifdef __cplusplus
extern "C" {
#endif

/* Host initializes struct_size and zeroes the rest. The plugin fills version,
 * id_size and id only. No terminator; visible ASCII, 1..128 bytes; version > 0.
 */
typedef struct CordisStateSchemaV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t id_size;
    uint32_t reserved;
    uint8_t id[CORDIS_STATE_MAX_ID_BYTES];
} CordisStateSchemaV1;

/* Host owns data and provides one buffer of at most CORDIS_STATE_MAX_JSON_BYTES.
 * Only size and the bytes at data may be written. Success requires one nonempty
 * UTF-8 JSON value; every non-OK return requires size == 0. No retained pointers.
 */
typedef struct CordisStateBufferV1 {
    uint32_t struct_size;
    uint32_t reserved;
    uint8_t* data;
    uint64_t capacity;
    uint64_t size;
} CordisStateBufferV1;

/* All slots mandatory. Query/describe/validate create no business resources.
 * All calls run on owner, never throw, wait, pump events or return PENDING.
 * Capture returns a coherent checkpoint or BUSY without blocking a worker.
 * Restore runs on a newly created instance, before start; borrowed inputs must
 * be copied before return. Validation outputs the schema reported by describe.
 */
typedef struct CordisStateApiV1 {
    CordisTableHeaderV1 header;
    /* UNSUPPORTED means this actual plugin ID is stateless; zero schema output. */
    CordisStatus(CORDIS_CALL* describe)(CordisBytesV1 plugin_id, CordisStateSchemaV1* schema);
    CordisStatus(CORDIS_CALL* validate)(CordisBytesV1 plugin_id, const CordisStateSchemaV1* source_schema,
                                        CordisBytesV1 source_json, CordisStateBufferV1* target_json);
    CordisStatus(CORDIS_CALL* capture)(CordisHandle instance, CordisStateBufferV1* json);
    CordisStatus(CORDIS_CALL* restore)(CordisHandle instance, const CordisStateSchemaV1* schema, CordisBytesV1 json);
} CordisStateApiV1;

/* A missing symbol means no extension. An exported but rejected/malformed query
 * is an error, never permission to discard state. Output follows host capacity.
 */
typedef CordisStatus(CORDIS_CALL* CordisQueryStateFnV1)(uint32_t abi_major, CordisStateApiV1* api);
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_state_v1(uint32_t abi_major, CordisStateApiV1* api);

#ifdef __cplusplus
}
#endif
#endif
