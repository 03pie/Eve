/* A small real C business module. It owns its instances and copied config.
 * No Kernel integration: tests supply a host and explicitly finish pending work.
 */
#include <cordis_abi/plugin.h>
#include <string.h>
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
} FixtureInstance;
static const CordisHostV1* active_host;
static CordisHandle next_id = 1;
static unsigned queries;
static FixtureInstance instances[4];
static const CordisBytesV1 plugin_ids[] = {{(const uint8_t*)"org.cordis.plugin", 17}};
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
    if (!instance)
        return CORDIS_E_INVALID_ARGUMENT;
    *instance = 0;
    if (!active_host || !context || !span_equal(plugin_id, "org.cordis.plugin") || !config.size || !config.data)
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
}
static CordisStatus CORDIS_CALL stop_instance(CordisHandle instance, CordisRequestId request)
{
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
