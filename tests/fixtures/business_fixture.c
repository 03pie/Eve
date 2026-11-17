/* A small real C business module. It owns its instances and copied config.
 * No Kernel integration: tests supply a host and explicitly finish pending work.
 */
#include <cordis_abi/plugin.h>
#include <string.h>
#ifdef FIXTURE_STATE
#include <cordis_abi/state.h>
#ifndef FIXTURE_STATE_VERSION
#define FIXTURE_STATE_VERSION 2
#endif
typedef void(CORDIS_CALL* StateFixtureHook)(uint32_t);
static StateFixtureHook state_hook;
static uint32_t state_mode;
static uint32_t state_counts[8]; /* describe, validate, restore, capture, start, stop, destroy, query */
void __cdecl cordis_business_throw(void);
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "org.cordis.business"
#endif
#ifndef FIXTURE_VERSION
#define FIXTURE_VERSION "1.0.0"
#endif
#ifdef FIXTURE_THROW_START
void __cdecl cordis_business_throw(void);
#endif

typedef struct FixtureInstance {
    CordisHandle context;
    CordisHandle id;
    CordisRequestId pending;
    int started;
    int cancelled;
    char data[128];
    uint64_t size;
#ifdef FIXTURE_STATE
    char state_json[128];
    uint64_t state_size;
    int restored;
#endif
} FixtureInstance;
static const CordisHostV1* active_host;
static CordisHandle next_id = 1;
static unsigned queries;
static unsigned create_calls;
static FixtureInstance instances[4];
#ifndef FIXTURE_PLUGIN_ID
#define FIXTURE_PLUGIN_ID "org.cordis.plugin"
#endif
static const CordisBytesV1 plugin_ids[] = {{(const uint8_t*)FIXTURE_PLUGIN_ID, sizeof(FIXTURE_PLUGIN_ID) - 1}};
static FixtureInstance* find_instance(CordisHandle id)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        if (instances[i].id == id && id)
            return &instances[i];
    return 0;
}
static int span_equal(CordisBytesV1 bytes, const char* text)
{
    return bytes.data && bytes.size == strlen(text) && memcmp(bytes.data, text, (size_t)bytes.size) == 0;
}
static CordisStatus CORDIS_CALL create_instance(CordisHandle context, CordisBytesV1 plugin_id, CordisBytesV1 config,
                                                CordisHandle* instance)
{
    unsigned i;
    ++create_calls;
    if (!instance)
        return CORDIS_E_INVALID_ARGUMENT;
    *instance = 0;
    if (!active_host || !context || !span_equal(plugin_id, FIXTURE_PLUGIN_ID) || !config.size || !config.data)
        return CORDIS_E_INVALID_ARGUMENT;
    if (config.size > sizeof(instances[0].data))
        return CORDIS_E_LIMIT;
    if (span_equal(config, "\"create-fail\""))
        return CORDIS_E_FAILED;
    if (span_equal(config, "\"create-null\""))
        return CORDIS_OK;
    if (span_equal(config, "\"create-pending\""))
        return CORDIS_PENDING;
    for (i = 0; i < 4; ++i) {
        if (!instances[i].id) {
            FixtureInstance* value = &instances[i];
            memset(value, 0, sizeof(*value));
            value->id = next_id++;
            value->context = context;
#ifdef FIXTURE_STATE
            value->state_json[0] = '7';
            value->state_size = 1;
#endif
            memcpy(value->data, config.data, (size_t)config.size);
            value->size = config.size;
            *instance = value->id;
            if (span_equal(config, "\"create-live-error\""))
                return CORDIS_E_FAILED;
            return CORDIS_OK;
        }
    }
    return CORDIS_E_LIMIT;
}
static CordisStatus CORDIS_CALL start_instance(CordisHandle instance, CordisRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    CordisBytesV1 text;
    CordisStatus logged;
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->started || value->pending)
        return CORDIS_E_BUSY;
    text.data = (const uint8_t*)value->data;
    text.size = value->size;
    logged = active_host->log(value->context, CORDIS_LOG_INFO, text);
    if (logged != CORDIS_OK)
        return logged;
#ifdef FIXTURE_STATE
    ++state_counts[4];
#if FIXTURE_STATE_VERSION >= 3
    if (state_hook)
        state_hook(4);
#endif
    if (state_mode == 18 || (state_mode == 19 && state_counts[4] == 2))
        return CORDIS_E_FAILED;
    if (span_equal(text, "\"need-restore\"") && !value->restored)
        return CORDIS_E_FAILED;
    text.data = (const uint8_t*)value->state_json;
    text.size = value->state_size;
    if (active_host->log(value->context, CORDIS_LOG_INFO, text) != CORDIS_OK)
        return CORDIS_E_FAILED;
    value->started = 1;
    return CORDIS_OK;
#else
    value->started = 1;
    if (span_equal(text, "\"start-fail\""))
        return CORDIS_E_FAILED;
#ifdef FIXTURE_THROW_START
    if (span_equal(text, "\"throw\""))
        cordis_business_throw();
#endif
    if (span_equal(text, "\"sync\"") || span_equal(text, "\"stop-fail\"") || span_equal(text, "\"destroy-fail\""))
        return CORDIS_OK;
    value->pending = request;
    if (span_equal(text, "\"early\"") || span_equal(text, "\"duplicate\"") || span_equal(text, "\"start-protocol\"")) {
        value->pending = 0;
        if (active_host->complete(value->context, request, CORDIS_OK, text) != CORDIS_OK)
            return CORDIS_E_FAILED;
        if (span_equal(text, "\"duplicate\""))
            (void)active_host->complete(value->context, request, CORDIS_OK, text);
        return span_equal(text, "\"start-protocol\"") ? CORDIS_OK : CORDIS_PENDING;
    }
    return CORDIS_PENDING;
#endif
}
static CordisStatus CORDIS_CALL stop_instance(CordisHandle instance, CordisRequestId request)
{
#ifdef FIXTURE_STATE
    ++state_counts[5];
#endif
    FixtureInstance* value = find_instance(instance);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending)
        return CORDIS_E_BUSY;
    {
        CordisBytesV1 config = {(const uint8_t*)value->data, value->size};
        if (span_equal(config, "\"stop-fail\""))
            return CORDIS_E_FAILED;
    }
    value->started = 0;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL destroy_instance(CordisHandle instance)
{
#ifdef FIXTURE_STATE
    ++state_counts[6];
#endif
    FixtureInstance* value = find_instance(instance);
    if (!value)
        return CORDIS_E_NOT_FOUND;
    if (value->pending || value->started)
        return CORDIS_E_BUSY;
    {
        CordisBytesV1 config = {(const uint8_t*)value->data, value->size};
        if (span_equal(config, "\"destroy-fail\""))
            return CORDIS_E_FAILED;
    }
    memset(value, 0, sizeof(*value));
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL cancel_request(CordisHandle instance, CordisRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    if (!value || !request || value->pending != request)
        return CORDIS_E_NOT_FOUND;
    value->cancelled = 1;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL request_complete(CordisHandle instance, CordisRequestId request, CordisStatus status,
                                                 CordisBytesV1 result)
{
    (void)instance;
    (void)request;
    (void)status;
    (void)result;
    /* This fixture never submits host operations; no such request can exist. */
    return CORDIS_E_NOT_FOUND;
}
static CordisStatus CORDIS_CALL dispatch(CordisHandle instance, CordisHandle callback, CordisBytesV1 payload,
                                         CordisRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    if (!value || !value->started || callback != 7 || !request || (payload.size && !payload.data))
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending)
        return CORDIS_E_BUSY;
    if (payload.size > sizeof(value->data))
        return CORDIS_E_LIMIT;
    if (payload.size)
        memcpy(value->data, payload.data, (size_t)payload.size);
    value->size = payload.size;
    value->pending = request;
    value->cancelled = 0;
    return CORDIS_PENDING;
}
static CordisStatus CORDIS_CALL invoke_service(CordisHandle instance, CordisHandle token, CordisBytesV1 method,
                                               CordisBytesV1 payload, CordisRequestId request)
{
    if (!span_equal(method, "echo"))
        return CORDIS_E_NOT_FOUND;
    return dispatch(instance, token, payload, request);
}
static CordisStatus CORDIS_CALL request_cleanup(CordisHandle instance, CordisHandle effect, CordisHandle callback,
                                                CordisRequestId request)
{
    (void)instance;
    (void)effect;
    (void)callback;
    (void)request;
    /* The echo fixture registers no effects. Unknown ownership is rejected. */
    return CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_business_fixture_finish(CordisHandle instance)
{
    FixtureInstance* value = find_instance(instance);
    CordisBytesV1 data;
    CordisStatus accepted;
    CordisRequestId pending;
    int cancelled;
    if (!value || !value->pending)
        return CORDIS_E_NOT_FOUND;
    data.data = (const uint8_t*)value->data;
    data.size = value->size;
    pending = value->pending;
    cancelled = value->cancelled;
    value->pending = 0;
    value->cancelled = 0;
    accepted = active_host->complete(
        value->context, pending,
        cancelled ? CORDIS_E_CANCELLED : (span_equal(data, "\"async-fail\"") ? CORDIS_E_FAILED : CORDIS_OK), data);
    if (accepted != CORDIS_OK) {
        value->pending = pending;
        value->cancelled = cancelled;
    }
    return accepted;
}
CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_creates(void)
{
    return create_calls;
}
CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_queries(void)
{
    return queries;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_business_fixture_log_one(void)
{
    unsigned i;
    static const CordisBytesV1 text = {(const uint8_t*)"late", 4};
    for (i = 0; i < 4; ++i)
        if (instances[i].id)
            return active_host->log(instances[i].context, CORDIS_LOG_INFO, text);
    return CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_finish_all(void)
{
    unsigned i, completed = 0;
    for (i = 0; i < 4; ++i)
        if (instances[i].id && instances[i].pending && cordis_business_fixture_finish(instances[i].id) == CORDIS_OK)
            ++completed;
    return completed;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(uint32_t host_abi, const CordisHostV1* host,
                                                       CordisPluginApiV1* output)
{
    CordisPluginApiV1 table;
    uint32_t capacity;
    unsigned i;
    ++queries;
    if (!host || !output || host_abi != CORDIS_ABI_MAJOR || host->header.abi_major != CORDIS_ABI_MAJOR ||
        host->header.struct_size < sizeof(*host) || host->header.reserved || host->reserved ||
        (host->capabilities & ~CORDIS_CAP_ALL) || !host->complete || !host->log || !host->request_cancel)
        return CORDIS_E_UNSUPPORTED;
    if (((host->capabilities & CORDIS_CAP_SERVICES) && !(host->capabilities & CORDIS_CAP_EFFECTS)) ||
        (!!(host->capabilities & CORDIS_CAP_DISPATCH) != !!host->dispatch) ||
        (!!(host->capabilities & CORDIS_CAP_SERVICES) != !!host->service_register) ||
        (!!(host->capabilities & CORDIS_CAP_SERVICES) != !!host->service_withdraw) ||
        (!!(host->capabilities & CORDIS_CAP_SERVICES) != !!host->service_invoke) ||
        (!!(host->capabilities & CORDIS_CAP_EFFECTS) != !!host->effect_create) ||
        (!!(host->capabilities & CORDIS_CAP_EFFECTS) != !!host->effect_ready) ||
        (!!(host->capabilities & CORDIS_CAP_EFFECTS) != !!host->effect_request_cleanup))
        return CORDIS_E_UNSUPPORTED;
    for (i = 0; i < 4; ++i)
        if (instances[i].id)
            return CORDIS_E_BUSY;
    capacity = output->header.struct_size;
    if (capacity < sizeof(CordisTableHeaderV1))
        return CORDIS_E_INVALID_ARGUMENT;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = sizeof(table);
    table.header.abi_major = CORDIS_ABI_MAJOR;
    table.capabilities = host->capabilities & CORDIS_CAP_ALL;
    table.module_id.data = (const uint8_t*)FIXTURE_ID;
    table.module_id.size = sizeof(FIXTURE_ID) - 1;
    table.module_version.data = (const uint8_t*)FIXTURE_VERSION;
    table.module_version.size = sizeof(FIXTURE_VERSION) - 1;
    table.plugin_count = 1;
    table.plugins = plugin_ids;
    table.create = create_instance;
    table.start = start_instance;
    table.request_stop = stop_instance;
    table.destroy = destroy_instance;
    table.cancel_request = cancel_request;
    table.request_complete = request_complete;
    if (table.capabilities & CORDIS_CAP_DISPATCH)
        table.dispatch = dispatch;
    if (table.capabilities & CORDIS_CAP_SERVICES)
        table.invoke_service = invoke_service;
    if (table.capabilities & CORDIS_CAP_EFFECTS)
        table.request_cleanup = request_cleanup;
    memcpy(output, &table, capacity < sizeof(table) ? capacity : sizeof(table));
    if (capacity < sizeof(table))
        return CORDIS_E_UNSUPPORTED;
    active_host = host;
    return CORDIS_OK;
}

#ifdef FIXTURE_STATE
CORDIS_EXPORT void CORDIS_CALL cordis_state_set_mode(uint32_t mode)
{
    state_mode = mode;
}
CORDIS_EXPORT void CORDIS_CALL cordis_state_set_hook(StateFixtureHook hook)
{
    state_hook = hook;
}
CORDIS_EXPORT uint32_t CORDIS_CALL cordis_state_count(uint32_t index)
{
    return index < 8 ? state_counts[index] : 0;
}
static CordisStatus CORDIS_CALL describe_state(CordisBytesV1 plugin, CordisStateSchemaV1* output)
{
    static const char schema[] = "counter";
    ++state_counts[0];
    if (!span_equal(plugin, FIXTURE_PLUGIN_ID) || !output || output->struct_size != sizeof(*output))
        return CORDIS_E_INVALID_ARGUMENT;
    if (state_mode == 14)
        return CORDIS_E_UNSUPPORTED;
    if (state_mode == 16)
        cordis_business_throw();
    if (state_mode == 3) {
        output->reserved = 1;
        return CORDIS_OK;
    }
    output->id_size = sizeof(schema) - 1;
    memcpy(output->id, schema, sizeof(schema) - 1);
    output->version = FIXTURE_STATE_VERSION;
    return CORDIS_OK;
}
static CordisStatus write_state(CordisStateBufferV1* output, CordisBytesV1 input)
{
    if (!output || output->struct_size != sizeof(*output) || output->capacity < input.size || !output->data)
        return CORDIS_E_INVALID_ARGUMENT;
    memcpy(output->data, input.data, (size_t)input.size);
    output->size = input.size;
    return CORDIS_OK;
}
/* Probes operate on the real fixture instances, with the caller holding the generation Call. */
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_state_set_value(uint32_t slot, CordisBytesV1 input)
{
    FixtureInstance* value;
    if (slot >= 4 || !input.data || !input.size || input.size > sizeof(instances[0].state_json))
        return CORDIS_E_INVALID_ARGUMENT;
    value = &instances[slot];
    if (!value->id || !value->started)
        return CORDIS_E_CLOSED;
    memcpy(value->state_json, input.data, (size_t)input.size);
    value->state_size = input.size;
    return CORDIS_OK;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_state_value(uint32_t slot, CordisStateBufferV1* output)
{
    CordisBytesV1 input;
    if (slot >= 4 || !instances[slot].id)
        return CORDIS_E_NOT_FOUND;
    input.data = (const uint8_t*)instances[slot].state_json;
    input.size = instances[slot].state_size;
    return write_state(output, input);
}
CORDIS_EXPORT uint32_t CORDIS_CALL cordis_state_live(void)
{
    uint32_t i, live = 0;
    for (i = 0; i != 4; ++i)
        if (instances[i].id)
            ++live;
    return live;
}
static CordisStatus CORDIS_CALL validate_state(CordisBytesV1 plugin, const CordisStateSchemaV1* schema,
                                               CordisBytesV1 input, CordisStateBufferV1* output)
{
    ++state_counts[1];
    if (state_hook)
        state_hook(1);
    if (state_mode == 4)
        return CORDIS_E_FAILED;
    if (state_mode == 5)
        return CORDIS_PENDING;
    if (state_mode == 6)
        cordis_business_throw();
    if (!span_equal(plugin, FIXTURE_PLUGIN_ID) || !schema || schema->id_size != 7 || memcmp(schema->id, "counter", 7) ||
        (!schema->version || schema->version > FIXTURE_STATE_VERSION))
        return CORDIS_E_FAILED;
#if FIXTURE_STATE_VERSION >= 3
    if (schema->version < FIXTURE_STATE_VERSION) {
        if (!input.data || !input.size)
            return CORDIS_E_FAILED;
        uint64_t i = input.size && input.data[0] == '-' ? 1 : 0;
        if (i == input.size)
            return CORDIS_E_FAILED;
        for (; i < input.size; ++i)
            if (input.data[i] < '0' || input.data[i] > '9')
                return CORDIS_E_FAILED;
        if ((input.size == 1 && input.data[0] == '0') ||
            (input.size == 2 && input.data[0] == '-' && input.data[1] == '0'))
            return write_state(output, input);
        if (!output || output->struct_size != sizeof(*output) || !output->data || output->capacity <= input.size)
            return CORDIS_E_LIMIT;
        memcpy(output->data, input.data, (size_t)input.size);
        output->data[input.size] = '0';
        output->size = input.size + 1;
        return CORDIS_OK;
    }
#endif
    return write_state(output, input);
}
static CordisStatus CORDIS_CALL capture_state(CordisHandle instance, CordisStateBufferV1* output)
{
    FixtureInstance* value = find_instance(instance);
    CordisBytesV1 data;
    ++state_counts[3];
    if (state_hook)
        state_hook(3);
    if (!value || !value->id || !value->started)
        return CORDIS_E_CLOSED;
    if (state_mode == 7)
        return CORDIS_E_BUSY;
    if (state_mode == 8) {
        data.data = (const uint8_t*)"{";
        data.size = 1;
        return write_state(output, data);
    }
    if (state_mode == 9) {
        output->data = 0;
        return CORDIS_OK;
    }
    if (state_mode == 10)
        return CORDIS_PENDING;
    data.data = (const uint8_t*)value->state_json;
    data.size = value->state_size;
    return write_state(output, data);
}
static CordisStatus CORDIS_CALL restore_state(CordisHandle instance, const CordisStateSchemaV1* schema,
                                              CordisBytesV1 input)
{
    FixtureInstance* value = find_instance(instance);
    ++state_counts[2];
    if (state_hook)
        state_hook(2);
    if (state_mode == 11)
        return CORDIS_E_FAILED;
    if (state_mode == 12)
        return CORDIS_PENDING;
    if (state_mode == 13)
        cordis_business_throw();
    if (!value || !value->id || value->started || !schema || schema->version != FIXTURE_STATE_VERSION ||
        input.size > sizeof(value->state_json))
        return CORDIS_E_INVALID_ARGUMENT;
    memcpy(value->state_json, input.data, (size_t)input.size);
    value->state_size = input.size;
    value->restored = 1;
    return CORDIS_OK;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_state_v1(uint32_t major, CordisStateApiV1* output)
{
    CordisStateApiV1 table;
    uint32_t capacity;
    ++state_counts[7];
    if (state_mode == 1)
        return CORDIS_PENDING;
    if (state_mode == 15)
        cordis_business_throw();
    if (state_mode == 17)
        return CORDIS_E_UNSUPPORTED;
    if (major != CORDIS_STATE_ABI_MAJOR || !output)
        return CORDIS_E_UNSUPPORTED;
    capacity = output->header.struct_size;
    if (capacity < sizeof(table))
        return CORDIS_E_INVALID_ARGUMENT;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = sizeof(table);
    table.header.abi_major = CORDIS_STATE_ABI_MAJOR;
    table.describe = describe_state;
    table.validate = validate_state;
    table.capture = capture_state;
    table.restore = restore_state;
    if (state_mode == 2)
        table.capture = 0;
    memcpy(output, &table, sizeof(table));
    return CORDIS_OK;
}
#endif
