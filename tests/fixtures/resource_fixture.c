/* Real C plugin using the production dispatch/effect table. Test exports only
 * ask the plugin to use its borrowed HostTable; no private host entry is called. */
#include <cordis_abi/plugin.h>
#include <string.h>
#ifndef FIXTURE_ID
#define FIXTURE_ID "org.cordis.resources"
#endif

typedef struct ResourceInstance {
    CordisHandle context, id;
    CordisRequestId pending, latest;
    CordisStatus last_status;
    unsigned acks, calls, cleanups;
    CordisHandle order[256];
    int started, cancelled, finishable, setup_ready;
    CordisHandle service_effect, service_binding;
    CordisRequestId cleanup_host;
    CordisRequestId many[300];
    unsigned char many_cancelled[300];
    char config[128];
    CordisStatus ack_return;
    char data[1024];
    uint64_t size;
} ResourceInstance;
static const CordisHostV1* host;
static ResourceInstance instances[4];
static CordisHandle next_id = 1;
static unsigned queries;
static const CordisBytesV1 plugins[] = {{(const uint8_t*)"org.cordis.plugin", 17}};
static ResourceInstance* find(CordisHandle id)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        if (instances[i].id == id && id)
            return &instances[i];
    return 0;
}
static ResourceInstance* slot(unsigned index)
{
    return index < 4 && instances[index].id ? &instances[index] : 0;
}
static CordisStatus CORDIS_CALL create(CordisHandle context, CordisBytesV1 plugin, CordisBytesV1 config,
                                       CordisHandle* output)
{
    unsigned i;

    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context || plugin.size != 17 || !plugin.data || memcmp(plugin.data, "org.cordis.plugin", 17))
        return CORDIS_E_INVALID_ARGUMENT;
    if (!config.data || config.size >= sizeof(instances[0].config))
        return CORDIS_E_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i)
        if (!instances[i].id) {
            memset(&instances[i], 0, sizeof(instances[i]));
            instances[i].context = context;
            instances[i].id = next_id++;
            memcpy(instances[i].config, config.data, (size_t)config.size);
            *output = instances[i].id;
            return CORDIS_OK;
        }
    return CORDIS_E_LIMIT;
}
#ifdef FIXTURE_SERVICES
static const CordisServiceKeyV1 echo_key = {{(const uint8_t*)"native.echo", 11}, 1, 0, 1};
static const CordisServiceMetadataV1 echo_metadata = {
    sizeof(CordisServiceMetadataV1), 2, {(const uint8_t*)"[\"echo\",\"utf8\"]", 15}};
static CordisStatus provide_echo(ResourceInstance* value)
{
    static const CordisBytesV1 label = {(const uint8_t*)"service", 7};
    CordisStatus status = host->effect_create(value->context, label, 11, &value->service_effect);
    if (status != CORDIS_OK)
        return status;
    status = host->service_register(value->context, value->service_effect, &echo_key, &echo_metadata, 7,
                                    &value->service_binding);
    if (status != CORDIS_OK) {
        static const CordisBytesV1 none = {0, 0};
        (void)host->effect_ready(value->context, value->service_effect, status, none);
        return status;
    }
    if (strcmp(value->config, "\"setup-provider\"") != 0) {
        static const CordisBytesV1 none = {0, 0};
        status = host->effect_ready(value->context, value->service_effect, CORDIS_OK, none);
        if (status == CORDIS_OK)
            value->setup_ready = 1;
    }
    return status;
}
#endif
static CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    value->started = 1;
#ifdef FIXTURE_SERVICES
    if (strstr(value->config, "provider")) {
        CordisStatus status = provide_echo(value);
        return status != CORDIS_OK ? status
                                   : (strcmp(value->config, "\"failed-provider\"") == 0 ? CORDIS_E_FAILED : CORDIS_OK);
    }
    if (strcmp(value->config, "\"consumer-cleanup\"") == 0) {
        static const CordisBytesV1 label = {(const uint8_t*)"consumer", 8};
        static const CordisBytesV1 none = {0, 0};
        CordisStatus status = host->effect_create(value->context, label, 20, &value->service_effect);
        return status == CORDIS_OK ? host->effect_ready(value->context, value->service_effect, CORDIS_OK, none)
                                   : status;
    }
#endif
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL stop(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending)
        return CORDIS_E_BUSY;
#ifdef FIXTURE_SERVICES
    if (strcmp(value->config, "\"setup-provider\"") == 0 && value->service_effect && !value->setup_ready) {
        static const CordisBytesV1 none = {0, 0};
        (void)host->effect_ready(value->context, value->service_effect, CORDIS_E_CANCELLED, none);
    }
#endif
    {
        unsigned i;
        for (i = 0; i < 300; ++i)
            if (value->many[i])
                return CORDIS_E_BUSY;
    }
    value->started = 0;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL destroy(CordisHandle id)
{
    ResourceInstance* value = find(id);
    if (!value)
        return CORDIS_E_NOT_FOUND;
    if (value->pending || value->started)
        return CORDIS_E_BUSY;
    /* Keep host-side test diagnostics, but never expose the instance as alive. */
    value->id = 0;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL cancel(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    unsigned i;
    if (!value)
        return CORDIS_E_NOT_FOUND;
    if (value->pending == request) {
        value->cancelled = 1;
        return CORDIS_OK;
    }
    for (i = 0; i < 300; ++i)
        if (value->many[i] == request) {
            value->many_cancelled[i] = 1;
            return CORDIS_OK;
        }
    return CORDIS_E_NOT_FOUND;
}
static CordisStatus CORDIS_CALL ack(CordisHandle id, CordisRequestId request, CordisStatus status, CordisBytesV1 data)
{
    ResourceInstance* value = find(id);
    if (!value || !request || (data.size && !data.data) || data.size > sizeof(value->data))
        return CORDIS_E_INVALID_ARGUMENT;
    ++value->acks;
    value->latest = request;
    value->last_status = status;
    value->size = data.size;
    if (data.size)
        memcpy(value->data, data.data, (size_t)data.size);
#ifdef FIXTURE_SERVICES
    if (value->cleanup_host == request && value->pending) {
        CordisRequestId cleanup = value->pending;
        CordisStatus accepted;
        value->cleanup_host = 0;
        value->pending = 0;
        accepted = host->complete(value->context, cleanup, status, data);
        if (accepted != CORDIS_OK) {
            value->pending = cleanup;
            return accepted;
        }
    }
#endif
    return value->ack_return;
}
static CordisStatus CORDIS_CALL dispatch(CordisHandle id, CordisHandle callback, CordisBytesV1 data,
                                         CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !value->started || !request || (data.size && !data.data) || data.size > sizeof(value->data))
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending)
        return CORDIS_E_BUSY;
    ++value->calls;
    value->size = data.size;
    if (data.size)
        memcpy(value->data, data.data, (size_t)data.size);
    if (callback == 8)
        return CORDIS_OK;
    if (callback == 9) {
        if (host->complete(value->context, request, CORDIS_OK, data) != CORDIS_OK)
            return CORDIS_E_FAILED;
        return CORDIS_PENDING;
    }
    if (callback != 7)
        return CORDIS_E_NOT_FOUND;
    value->pending = request;
    value->finishable = 1;
    value->cancelled = 0;
    return CORDIS_PENDING;
}
static CordisStatus CORDIS_CALL cleanup(CordisHandle id, CordisHandle effect, CordisHandle callback,
                                        CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !effect || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending || value->cleanups >= 256)
        return CORDIS_E_BUSY;
    value->order[value->cleanups++] = callback;
#ifdef FIXTURE_SERVICES
    if (callback == 20) {
        static const CordisBytesV1 method = {(const uint8_t*)"echo", 4};
        static const CordisBytesV1 payload = {(const uint8_t*)"{\"cleanup\":true}", 16};
        static const CordisBytesV1 caps = {(const uint8_t*)"[]", 2};
        CordisStatus status =
            host->service_invoke(value->context, &echo_key, 2, caps, method, payload, &value->cleanup_host);
        if (status != CORDIS_PENDING)
            return status;
        value->pending = request;
        value->finishable = 0;
        return CORDIS_PENDING;
    }
#endif
    if (callback == 13)
        return CORDIS_E_FAILED;
    if (callback == 12) {
        value->pending = request;
        value->finishable = 1;
        value->size = 0;
        value->cancelled = 0;
        return CORDIS_PENDING;
    }
    return CORDIS_OK;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_resource_dispatch(unsigned index, CordisHandle callback,
                                                                CordisBytesV1 data, CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->dispatch(value->context, callback, data, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_resource_cancel(unsigned index, CordisRequestId request)
{
    ResourceInstance* value = slot(index);
    return value ? host->request_cancel(value->context, request) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_resource_effect(unsigned index, CordisHandle callback,
                                                              CordisHandle* output)
{
    static const CordisBytesV1 label = {(const uint8_t*)"native.resource", 15};
    ResourceInstance* value = slot(index);
    return value ? host->effect_create(value->context, label, callback, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_resource_ready(unsigned index, CordisHandle effect, CordisStatus status)
{
    static const CordisBytesV1 none = {0, 0};
    ResourceInstance* value = slot(index);
    CordisStatus result = value ? host->effect_ready(value->context, effect, status, none) : CORDIS_E_NOT_FOUND;
    if (value && effect == value->service_effect && result == CORDIS_OK)
        value->setup_ready = 1;
    return result;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_resource_close(unsigned index, CordisHandle effect,
                                                             CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->effect_request_cleanup(value->context, effect, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_finish_all(void)
{
    unsigned i, count = 0;
    for (i = 0; i < 4; ++i) {
        ResourceInstance* value = slot(i);
        if (value && value->pending && value->finishable) {
            CordisRequestId pending = value->pending;
            CordisBytesV1 data = {(const uint8_t*)value->data, value->size};
            CordisStatus status = value->cancelled ? CORDIS_E_CANCELLED : CORDIS_OK;
            value->pending = 0;
            if (host->complete(value->context, pending, status, data) != CORDIS_OK)
                value->pending = pending;
            else
                ++count;
        }
    }
    for (i = 0; i < 4; ++i) {
        ResourceInstance* value = slot(i);
        unsigned j;
        if (!value)
            continue;
        for (j = 0; j < 300; ++j)
            if (value->many[j]) {
                static const CordisBytesV1 data = {(const uint8_t*)"{}", 2};
                CordisRequestId pending = value->many[j];
                CordisStatus status = value->many_cancelled[j] ? CORDIS_E_CANCELLED : CORDIS_OK;
                value->many[j] = 0;
                if (host->complete(value->context, pending, status, data) != CORDIS_OK)
                    value->many[j] = pending;
                else
                    ++count;
            }
    }
    return count;
}
CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_queries(void)
{
    return queries;
}
CORDIS_EXPORT uint64_t CORDIS_CALL cordis_resource_stat(unsigned index, unsigned field)
{
    ResourceInstance* value = index < 4 ? &instances[index] : 0;
    if (!value)
        return 0;
    switch (field) {
    case 0:
        return value->acks;
    case 1:
        return value->calls;
    case 2:
        return value->cleanups;
    case 3:
        return (uint64_t)(int64_t)value->last_status;
    case 4:
        return value->pending;
    case 5:
        return value->latest;
    default:
        return field >= 10 && field < 266 ? value->order[field - 10] : 0;
    }
}
CORDIS_EXPORT void CORDIS_CALL cordis_resource_ack_status(unsigned index, CordisStatus status)
{
    ResourceInstance* value = slot(index);
    if (value)
        value->ack_return = status;
}
CORDIS_EXPORT CordisBytesV1 CORDIS_CALL cordis_resource_result(unsigned index)
{
    CordisBytesV1 value = {0, 0};
    if (index < 4) {
        value.data = (const uint8_t*)instances[index].data;
        value.size = instances[index].size;
    }
    return value;
}
#ifdef FIXTURE_SERVICES
static CordisStatus CORDIS_CALL invoke_echo(CordisHandle id, CordisHandle token, CordisBytesV1 method,
                                            CordisBytesV1 payload, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || token != 7 || !method.data)
        return CORDIS_E_NOT_FOUND;
    if (method.size == 5 && memcmp(method.data, "fatal", 5) == 0)
        return 0x7fff;
    if (value && method.size == 4 && method.data && memcmp(method.data, "hold", 4) == 0) {
        unsigned i;
        for (i = 0; i < 300; ++i)
            if (!value->many[i]) {
                ++value->calls;
                value->many[i] = request;
                value->many_cancelled[i] = 0;
                return CORDIS_PENDING;
            }
        return CORDIS_E_LIMIT;
    }
    if (token != 7 || !method.data)
        return CORDIS_E_NOT_FOUND;
    if (method.size == 4 && memcmp(method.data, "echo", 4) == 0)
        return dispatch(id, 7, payload, request);
    if (method.size == 5 && memcmp(method.data, "early", 5) == 0)
        return dispatch(id, 9, payload, request);
    return CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_invoke(unsigned index, uint32_t major, uint64_t realm,
                                                             uint32_t minor, CordisBytesV1 capabilities,
                                                             CordisBytesV1 method, CordisBytesV1 payload,
                                                             CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    CordisServiceKeyV1 key = echo_key;
    key.interface_major = major;
    key.realm = realm;
    return value ? host->service_invoke(value->context, &key, minor, capabilities, method, payload, output)
                 : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_withdraw(unsigned index, CordisHandle binding,
                                                               CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->service_withdraw(value->context, binding, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_invoke_named(unsigned index, CordisBytesV1 name,
                                                                   CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    CordisServiceKeyV1 key = echo_key;
    static const CordisBytesV1 caps = {(const uint8_t*)"[]", 2};
    static const CordisBytesV1 method = {(const uint8_t*)"echo", 4};
    static const CordisBytesV1 payload = {(const uint8_t*)"{}", 2};
    key.name = name;
    return value ? host->service_invoke(value->context, &key, 2, caps, method, payload, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisHandle CORDIS_CALL cordis_service_handle(unsigned index, unsigned effect)
{
    return index < 4 ? (effect ? instances[index].service_effect : instances[index].service_binding) : 0;
}
#endif
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(uint32_t major, const CordisHostV1* table,
                                                       CordisPluginApiV1* output)
{
    CordisPluginApiV1 api;
    uint32_t capacity;
    ++queries;
    if (major != CORDIS_ABI_MAJOR || !table || !output ||
        (table->capabilities & (CORDIS_CAP_DISPATCH | CORDIS_CAP_EFFECTS)) !=
            (CORDIS_CAP_DISPATCH | CORDIS_CAP_EFFECTS))
        return CORDIS_E_UNSUPPORTED;
    capacity = output->header.struct_size;
    memset(&api, 0, sizeof(api));
    api.header.struct_size = sizeof(api);
    api.header.abi_major = CORDIS_ABI_MAJOR;
    api.capabilities = CORDIS_CAP_DISPATCH | CORDIS_CAP_EFFECTS;
#ifdef FIXTURE_SERVICES
    api.capabilities |= CORDIS_CAP_SERVICES;
    api.invoke_service = invoke_echo;
#endif
    api.required_host_capabilities = api.capabilities;
    api.module_id.data = (const uint8_t*)FIXTURE_ID;
    api.module_id.size = sizeof(FIXTURE_ID) - 1;
    api.module_version.data = (const uint8_t*)"1.0.0";
    api.module_version.size = 5;
    api.plugins = plugins;
    api.plugin_count = 1;
    api.create = create;
    api.start = start;
    api.request_stop = stop;
    api.destroy = destroy;
    api.cancel_request = cancel;
    api.request_complete = ack;
    api.dispatch = dispatch;
    api.request_cleanup = cleanup;
    memcpy(output, &api, capacity < sizeof(api) ? capacity : sizeof(api));
    if (capacity < sizeof(api))
        return CORDIS_E_UNSUPPORTED;
    host = table;
    return CORDIS_OK;
}
