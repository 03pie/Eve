/* A small real C business module. It owns its instances and copied config.
 * No Kernel integration: tests supply a host and explicitly finish pending work.
 */
#include <eve_abi/plugin.h>
#include <string.h>
#ifdef FIXTURE_STATE
#include <eve_abi/state.h>
#ifndef FIXTURE_STATE_VERSION
#define FIXTURE_STATE_VERSION 2
#endif
typedef void(EVE_CALL* StateFixtureHook)(uint32_t);
static StateFixtureHook state_hook;
static uint32_t state_mode;
static uint32_t state_counts[8]; /* describe, validate, restore, capture, start, stop, destroy, query */
void __cdecl eve_business_throw(void);
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "org.eve.business"
#endif
#ifndef FIXTURE_VERSION
#define FIXTURE_VERSION "1.0.0"
#endif
#ifdef FIXTURE_THROW_START
void __cdecl eve_business_throw(void);
#endif

typedef struct FixtureInstance {
    EveHandle context;
    EveHandle id;
    EveRequestId pending;
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
static const EveHostV1* active_host;
static EveHandle next_id = 1;
static unsigned queries;
static unsigned create_calls;
static FixtureInstance instances[4];
#ifndef FIXTURE_PLUGIN_ID
#define FIXTURE_PLUGIN_ID "org.eve.plugin"
#endif
static const EveBytesV1 plugin_ids[] = {{(const uint8_t*)FIXTURE_PLUGIN_ID, sizeof(FIXTURE_PLUGIN_ID) - 1}};
static FixtureInstance* find_instance(EveHandle id)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        if (instances[i].id == id && id)
            return &instances[i];
    return 0;
}
static int span_equal(EveBytesV1 bytes, const char* text)
{
    return bytes.data && bytes.size == strlen(text) && memcmp(bytes.data, text, (size_t)bytes.size) == 0;
}
static EveStatus EVE_CALL create_instance(EveHandle context, EveBytesV1 plugin_id, EveBytesV1 config,
                                                EveHandle* instance)
{
    unsigned i;
    ++create_calls;
    if (!instance)
        return EVE_E_INVALID_ARGUMENT;
    *instance = 0;
    if (!active_host || !context || !span_equal(plugin_id, FIXTURE_PLUGIN_ID) || !config.size || !config.data)
        return EVE_E_INVALID_ARGUMENT;
    if (config.size > sizeof(instances[0].data))
        return EVE_E_LIMIT;
    if (span_equal(config, "\"create-fail\""))
        return EVE_E_FAILED;
    if (span_equal(config, "\"create-null\""))
        return EVE_OK;
    if (span_equal(config, "\"create-pending\""))
        return EVE_PENDING;
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
                return EVE_E_FAILED;
            return EVE_OK;
        }
    }
    return EVE_E_LIMIT;
}
static EveStatus EVE_CALL start_instance(EveHandle instance, EveRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    EveBytesV1 text;
    EveStatus logged;
    if (!value || !request)
        return EVE_E_INVALID_ARGUMENT;
    if (value->started || value->pending)
        return EVE_E_BUSY;
    text.data = (const uint8_t*)value->data;
    text.size = value->size;
    logged = active_host->log(value->context, EVE_LOG_INFO, text);
    if (logged != EVE_OK)
        return logged;
#ifdef FIXTURE_STATE
    ++state_counts[4];
#if FIXTURE_STATE_VERSION >= 3
    if (state_hook)
        state_hook(4);
#endif
    if (state_mode == 18 || (state_mode == 19 && state_counts[4] == 2))
        return EVE_E_FAILED;
    if (span_equal(text, "\"need-restore\"") && !value->restored)
        return EVE_E_FAILED;
    text.data = (const uint8_t*)value->state_json;
    text.size = value->state_size;
    if (active_host->log(value->context, EVE_LOG_INFO, text) != EVE_OK)
        return EVE_E_FAILED;
    value->started = 1;
    return EVE_OK;
#else
    value->started = 1;
    if (span_equal(text, "\"start-fail\""))
        return EVE_E_FAILED;
#ifdef FIXTURE_THROW_START
    if (span_equal(text, "\"throw\""))
        eve_business_throw();
#endif
    if (span_equal(text, "\"sync\"") || span_equal(text, "\"stop-fail\"") || span_equal(text, "\"destroy-fail\""))
        return EVE_OK;
    value->pending = request;
    if (span_equal(text, "\"early\"") || span_equal(text, "\"duplicate\"") || span_equal(text, "\"start-protocol\"")) {
        value->pending = 0;
        if (active_host->complete(value->context, request, EVE_OK, text) != EVE_OK)
            return EVE_E_FAILED;
        if (span_equal(text, "\"duplicate\""))
            (void)active_host->complete(value->context, request, EVE_OK, text);
        return span_equal(text, "\"start-protocol\"") ? EVE_OK : EVE_PENDING;
    }
    return EVE_PENDING;
#endif
}
static EveStatus EVE_CALL stop_instance(EveHandle instance, EveRequestId request)
{
#ifdef FIXTURE_STATE
    ++state_counts[5];
#endif
    FixtureInstance* value = find_instance(instance);
    if (!value || !request)
        return EVE_E_INVALID_ARGUMENT;
    if (value->pending)
        return EVE_E_BUSY;
    {
        EveBytesV1 config = {(const uint8_t*)value->data, value->size};
        if (span_equal(config, "\"stop-fail\""))
            return EVE_E_FAILED;
    }
    value->started = 0;
    return EVE_OK;
}
static EveStatus EVE_CALL destroy_instance(EveHandle instance)
{
#ifdef FIXTURE_STATE
    ++state_counts[6];
#endif
    FixtureInstance* value = find_instance(instance);
    if (!value)
        return EVE_E_NOT_FOUND;
    if (value->pending || value->started)
        return EVE_E_BUSY;
    {
        EveBytesV1 config = {(const uint8_t*)value->data, value->size};
        if (span_equal(config, "\"destroy-fail\""))
            return EVE_E_FAILED;
    }
    memset(value, 0, sizeof(*value));
    return EVE_OK;
}
static EveStatus EVE_CALL cancel_request(EveHandle instance, EveRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    if (!value || !request || value->pending != request)
        return EVE_E_NOT_FOUND;
    value->cancelled = 1;
    return EVE_OK;
}
static EveStatus EVE_CALL request_complete(EveHandle instance, EveRequestId request, EveStatus status,
                                                 EveBytesV1 result)
{
    (void)instance;
    (void)request;
    (void)status;
    (void)result;
    /* This fixture never submits host operations; no such request can exist. */
    return EVE_E_NOT_FOUND;
}
static EveStatus EVE_CALL dispatch(EveHandle instance, EveHandle callback, EveBytesV1 payload,
                                         EveRequestId request)
{
    FixtureInstance* value = find_instance(instance);
    if (!value || !value->started || callback != 7 || !request || (payload.size && !payload.data))
        return EVE_E_INVALID_ARGUMENT;
    if (value->pending)
        return EVE_E_BUSY;
    if (payload.size > sizeof(value->data))
        return EVE_E_LIMIT;
    if (payload.size)
        memcpy(value->data, payload.data, (size_t)payload.size);
    value->size = payload.size;
    value->pending = request;
    value->cancelled = 0;
    return EVE_PENDING;
}
static EveStatus EVE_CALL invoke_service(EveHandle instance, EveHandle token, EveBytesV1 method,
                                               EveBytesV1 payload, EveRequestId request)
{
    if (!span_equal(method, "echo"))
        return EVE_E_NOT_FOUND;
    return dispatch(instance, token, payload, request);
}
static EveStatus EVE_CALL request_cleanup(EveHandle instance, EveHandle effect, EveHandle callback,
                                                EveRequestId request)
{
    (void)instance;
    (void)effect;
    (void)callback;
    (void)request;
    /* The echo fixture registers no effects. Unknown ownership is rejected. */
    return EVE_E_NOT_FOUND;
}
EVE_EXPORT EveStatus EVE_CALL eve_business_fixture_finish(EveHandle instance)
{
    FixtureInstance* value = find_instance(instance);
    EveBytesV1 data;
    EveStatus accepted;
    EveRequestId pending;
    int cancelled;
    if (!value || !value->pending)
        return EVE_E_NOT_FOUND;
    data.data = (const uint8_t*)value->data;
    data.size = value->size;
    pending = value->pending;
    cancelled = value->cancelled;
    value->pending = 0;
    value->cancelled = 0;
    accepted = active_host->complete(
        value->context, pending,
        cancelled ? EVE_E_CANCELLED : (span_equal(data, "\"async-fail\"") ? EVE_E_FAILED : EVE_OK), data);
    if (accepted != EVE_OK) {
        value->pending = pending;
        value->cancelled = cancelled;
    }
    return accepted;
}
EVE_EXPORT unsigned EVE_CALL eve_business_fixture_creates(void)
{
    return create_calls;
}
EVE_EXPORT unsigned EVE_CALL eve_business_fixture_queries(void)
{
    return queries;
}
EVE_EXPORT EveStatus EVE_CALL eve_business_fixture_log_one(void)
{
    unsigned i;
    static const EveBytesV1 text = {(const uint8_t*)"late", 4};
    for (i = 0; i < 4; ++i)
        if (instances[i].id)
            return active_host->log(instances[i].context, EVE_LOG_INFO, text);
    return EVE_E_NOT_FOUND;
}
EVE_EXPORT unsigned EVE_CALL eve_business_fixture_finish_all(void)
{
    unsigned i, completed = 0;
    for (i = 0; i < 4; ++i)
        if (instances[i].id && instances[i].pending && eve_business_fixture_finish(instances[i].id) == EVE_OK)
            ++completed;
    return completed;
}
EVE_EXPORT EveStatus EVE_CALL eve_query_v1(uint32_t host_abi, const EveHostV1* host,
                                                       EvePluginApiV1* output)
{
    EvePluginApiV1 table;
    uint32_t capacity;
    unsigned i;
    ++queries;
    if (!host || !output || host_abi != EVE_ABI_MAJOR || host->header.abi_major != EVE_ABI_MAJOR ||
        host->header.struct_size < sizeof(*host) || host->header.reserved || host->reserved ||
        (host->capabilities & ~EVE_CAP_ALL) || !host->complete || !host->log || !host->request_cancel)
        return EVE_E_UNSUPPORTED;
    if (((host->capabilities & EVE_CAP_SERVICES) && !(host->capabilities & EVE_CAP_EFFECTS)) ||
        (!!(host->capabilities & EVE_CAP_DISPATCH) != !!host->dispatch) ||
        (!!(host->capabilities & EVE_CAP_SERVICES) != !!host->service_register) ||
        (!!(host->capabilities & EVE_CAP_SERVICES) != !!host->service_withdraw) ||
        (!!(host->capabilities & EVE_CAP_SERVICES) != !!host->service_invoke) ||
        (!!(host->capabilities & EVE_CAP_EFFECTS) != !!host->effect_create) ||
        (!!(host->capabilities & EVE_CAP_EFFECTS) != !!host->effect_ready) ||
        (!!(host->capabilities & EVE_CAP_EFFECTS) != !!host->effect_request_cleanup))
        return EVE_E_UNSUPPORTED;
    for (i = 0; i < 4; ++i)
        if (instances[i].id)
            return EVE_E_BUSY;
    capacity = output->header.struct_size;
    if (capacity < sizeof(EveTableHeaderV1))
        return EVE_E_INVALID_ARGUMENT;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = sizeof(table);
    table.header.abi_major = EVE_ABI_MAJOR;
    table.capabilities = host->capabilities & EVE_CAP_ALL;
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
    if (table.capabilities & EVE_CAP_DISPATCH)
        table.dispatch = dispatch;
    if (table.capabilities & EVE_CAP_SERVICES)
        table.invoke_service = invoke_service;
    if (table.capabilities & EVE_CAP_EFFECTS)
        table.request_cleanup = request_cleanup;
    memcpy(output, &table, capacity < sizeof(table) ? capacity : sizeof(table));
    if (capacity < sizeof(table))
        return EVE_E_UNSUPPORTED;
    active_host = host;
    return EVE_OK;
}

#ifdef FIXTURE_STATE
EVE_EXPORT void EVE_CALL eve_state_set_mode(uint32_t mode)
{
    state_mode = mode;
}
EVE_EXPORT void EVE_CALL eve_state_set_hook(StateFixtureHook hook)
{
    state_hook = hook;
}
EVE_EXPORT uint32_t EVE_CALL eve_state_count(uint32_t index)
{
    return index < 8 ? state_counts[index] : 0;
}
static EveStatus EVE_CALL describe_state(EveBytesV1 plugin, EveStateSchemaV1* output)
{
    static const char schema[] = "counter";
    ++state_counts[0];
    if (!span_equal(plugin, FIXTURE_PLUGIN_ID) || !output || output->struct_size != sizeof(*output))
        return EVE_E_INVALID_ARGUMENT;
    if (state_mode == 14)
        return EVE_E_UNSUPPORTED;
    if (state_mode == 16)
        eve_business_throw();
    if (state_mode == 3) {
        output->reserved = 1;
        return EVE_OK;
    }
    output->id_size = sizeof(schema) - 1;
    memcpy(output->id, schema, sizeof(schema) - 1);
    output->version = FIXTURE_STATE_VERSION;
    return EVE_OK;
}
static EveStatus write_state(EveStateBufferV1* output, EveBytesV1 input)
{
    if (!output || output->struct_size != sizeof(*output) || output->capacity < input.size || !output->data)
        return EVE_E_INVALID_ARGUMENT;
    memcpy(output->data, input.data, (size_t)input.size);
    output->size = input.size;
    return EVE_OK;
}
/* Probes operate on the real fixture instances, with the caller holding the generation Call. */
EVE_EXPORT EveStatus EVE_CALL eve_state_set_value(uint32_t slot, EveBytesV1 input)
{
    FixtureInstance* value;
    if (slot >= 4 || !input.data || !input.size || input.size > sizeof(instances[0].state_json))
        return EVE_E_INVALID_ARGUMENT;
    value = &instances[slot];
    if (!value->id || !value->started)
        return EVE_E_CLOSED;
    memcpy(value->state_json, input.data, (size_t)input.size);
    value->state_size = input.size;
    return EVE_OK;
}
EVE_EXPORT EveStatus EVE_CALL eve_state_value(uint32_t slot, EveStateBufferV1* output)
{
    EveBytesV1 input;
    if (slot >= 4 || !instances[slot].id)
        return EVE_E_NOT_FOUND;
    input.data = (const uint8_t*)instances[slot].state_json;
    input.size = instances[slot].state_size;
    return write_state(output, input);
}
EVE_EXPORT uint32_t EVE_CALL eve_state_live(void)
{
    uint32_t i, live = 0;
    for (i = 0; i != 4; ++i)
        if (instances[i].id)
            ++live;
    return live;
}
static EveStatus EVE_CALL validate_state(EveBytesV1 plugin, const EveStateSchemaV1* schema,
                                               EveBytesV1 input, EveStateBufferV1* output)
{
    ++state_counts[1];
    if (state_hook)
        state_hook(1);
    if (state_mode == 4)
        return EVE_E_FAILED;
    if (state_mode == 5)
        return EVE_PENDING;
    if (state_mode == 6)
        eve_business_throw();
    if (!span_equal(plugin, FIXTURE_PLUGIN_ID) || !schema || schema->id_size != 7 || memcmp(schema->id, "counter", 7) ||
        (!schema->version || schema->version > FIXTURE_STATE_VERSION))
        return EVE_E_FAILED;
#if FIXTURE_STATE_VERSION >= 3
    if (schema->version < FIXTURE_STATE_VERSION) {
        if (!input.data || !input.size)
            return EVE_E_FAILED;
        uint64_t i = input.size && input.data[0] == '-' ? 1 : 0;
        if (i == input.size)
            return EVE_E_FAILED;
        for (; i < input.size; ++i)
            if (input.data[i] < '0' || input.data[i] > '9')
                return EVE_E_FAILED;
        if ((input.size == 1 && input.data[0] == '0') ||
            (input.size == 2 && input.data[0] == '-' && input.data[1] == '0'))
            return write_state(output, input);
        if (!output || output->struct_size != sizeof(*output) || !output->data || output->capacity <= input.size)
            return EVE_E_LIMIT;
        memcpy(output->data, input.data, (size_t)input.size);
        output->data[input.size] = '0';
        output->size = input.size + 1;
        return EVE_OK;
    }
#endif
    return write_state(output, input);
}
static EveStatus EVE_CALL capture_state(EveHandle instance, EveStateBufferV1* output)
{
    FixtureInstance* value = find_instance(instance);
    EveBytesV1 data;
    ++state_counts[3];
    if (state_hook)
        state_hook(3);
    if (!value || !value->id || !value->started)
        return EVE_E_CLOSED;
    if (state_mode == 7)
        return EVE_E_BUSY;
    if (state_mode == 8) {
        data.data = (const uint8_t*)"{";
        data.size = 1;
        return write_state(output, data);
    }
    if (state_mode == 9) {
        output->data = 0;
        return EVE_OK;
    }
    if (state_mode == 10)
        return EVE_PENDING;
    data.data = (const uint8_t*)value->state_json;
    data.size = value->state_size;
    return write_state(output, data);
}
static EveStatus EVE_CALL restore_state(EveHandle instance, const EveStateSchemaV1* schema,
                                              EveBytesV1 input)
{
    FixtureInstance* value = find_instance(instance);
    ++state_counts[2];
    if (state_hook)
        state_hook(2);
    if (state_mode == 11)
        return EVE_E_FAILED;
    if (state_mode == 12)
        return EVE_PENDING;
    if (state_mode == 13)
        eve_business_throw();
    if (!value || !value->id || value->started || !schema || schema->version != FIXTURE_STATE_VERSION ||
        input.size > sizeof(value->state_json))
        return EVE_E_INVALID_ARGUMENT;
    memcpy(value->state_json, input.data, (size_t)input.size);
    value->state_size = input.size;
    value->restored = 1;
    return EVE_OK;
}
EVE_EXPORT EveStatus EVE_CALL eve_query_state_v1(uint32_t major, EveStateApiV1* output)
{
    EveStateApiV1 table;
    uint32_t capacity;
    ++state_counts[7];
    if (state_mode == 1)
        return EVE_PENDING;
    if (state_mode == 15)
        eve_business_throw();
    if (state_mode == 17)
        return EVE_E_UNSUPPORTED;
    if (major != EVE_STATE_ABI_MAJOR || !output)
        return EVE_E_UNSUPPORTED;
    capacity = output->header.struct_size;
    if (capacity < sizeof(table))
        return EVE_E_INVALID_ARGUMENT;
    memset(&table, 0, sizeof(table));
    table.header.struct_size = sizeof(table);
    table.header.abi_major = EVE_STATE_ABI_MAJOR;
    table.describe = describe_state;
    table.validate = validate_state;
    table.capture = capture_state;
    table.restore = restore_state;
    if (state_mode == 2)
        table.capture = 0;
    memcpy(output, &table, sizeof(table));
    return EVE_OK;
}
#endif
