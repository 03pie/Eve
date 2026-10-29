/* Real C plugin using the production dispatch/effect table. Test exports only
 * ask the plugin to use its borrowed HostTable; no private host entry is called. */
#include <cordis_abi/plugin.h>
#include <string.h>

typedef struct ResourceInstance {
    CordisHandle context, id;
    CordisRequestId pending, latest, target_request;
    CordisStatus last_status;
    unsigned acks, calls, cleanups;
    CordisHandle order[256];
    int started, cancelled;
    CordisStatus ack_return;
    char data[1024];
    uint64_t size;
    unsigned stops;
#ifdef FIXTURE_SERVICES
    CordisRequestId held[300];
    unsigned char held_cancelled[300];
    unsigned held_count;
    CordisRequestId cleanup_waiting, cleanup_operation;
    CordisStatus cleanup_result;
    int cleanup_ready, start_pending, start_fail;
#endif
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
#ifndef FIXTURE_SERVICES
    (void)config;
#endif
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context || plugin.size != 17 || !plugin.data || memcmp(plugin.data, "org.cordis.plugin", 17))
        return CORDIS_E_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i)
        if (!instances[i].id) {
            memset(&instances[i], 0, sizeof(instances[i]));
            instances[i].context = context;
            instances[i].id = next_id++;
#ifdef FIXTURE_SERVICES
            instances[i].start_pending =
                config.size == 15 && config.data && memcmp(config.data, "\"start-pending\"", 15) == 0;
            instances[i].start_fail =
                config.size == 12 && config.data && memcmp(config.data, "\"start-fail\"", 12) == 0;
#endif
            *output = instances[i].id;
            return CORDIS_OK;
        }
    return CORDIS_E_LIMIT;
}
static CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    value->started = 1;
#ifdef FIXTURE_SERVICES
    if (value->start_fail)
        return CORDIS_E_FAILED;
    if (value->start_pending) {
        value->pending = request;
        return CORDIS_PENDING;
    }
#endif
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL stop(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending
#ifdef FIXTURE_SERVICES
        || value->held_count
#endif
    )
        return CORDIS_E_BUSY;
    ++value->stops;
    value->started = 0;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL destroy(CordisHandle id)
{
    ResourceInstance* value = find(id);
    if (!value)
        return CORDIS_E_NOT_FOUND;
    if (value->pending || value->started
#ifdef FIXTURE_SERVICES
        || value->held_count
#endif
    )
        return CORDIS_E_BUSY;
    /* Keep host-side test diagnostics, but never expose the instance as alive. */
    value->id = 0;
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL cancel(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
#ifdef FIXTURE_SERVICES
    unsigned i;
    if (value)
        for (i = 0; i < value->held_count; ++i)
            if (value->held[i] == request) {
                value->held_cancelled[i] = 1;
                return CORDIS_OK;
            }
#endif
    if (!value || value->pending != request)
        return CORDIS_E_NOT_FOUND;
#ifdef FIXTURE_SERVICES
    if (value->ack_return == CORDIS_PENDING)
        return CORDIS_PENDING;
#endif
    value->cancelled = 1;
    return CORDIS_OK;
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
    if (request == value->cleanup_operation) {
        value->cleanup_result = status;
        value->cleanup_ready = 1;
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
    value->target_request = request;
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
    if (callback == 15) {
        const CordisServiceKeyV1 key = {{(const uint8_t*)"test.service", 12}, 1, 0, 1};
        const CordisBytesV1 caps = {(const uint8_t*)"[]", 2}, method = {(const uint8_t*)"cleanup", 7},
                            payload = {(const uint8_t*)"{}", 2};
        value->cleanup_waiting = request;
        if (host->service_invoke(value->context, &key, 0, caps, method, payload, &value->cleanup_operation) !=
            CORDIS_PENDING)
            return CORDIS_E_FAILED;
        return CORDIS_PENDING;
    }
    if (callback == 16) {
        const CordisServiceKeyV1 key = {{(const uint8_t*)"test.unbound", 12}, 1, 0, 1};
        const CordisBytesV1 caps = {(const uint8_t*)"[]", 2}, method = {(const uint8_t*)"cleanup", 7},
                            payload = {(const uint8_t*)"{}", 2};
        CordisRequestId output = 77;
        return host->service_invoke(value->context, &key, 0, caps, method, payload, &output) == CORDIS_E_CLOSED &&
                       !output
                   ? CORDIS_OK
                   : CORDIS_E_FAILED;
    }
#endif
    if (callback == 13)
        return CORDIS_E_FAILED;
    if (callback == 12) {
        value->pending = request;
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
    return value ? host->effect_ready(value->context, effect, status, none) : CORDIS_E_NOT_FOUND;
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
#ifdef FIXTURE_SERVICES
        if (value && value->held_count) {
            unsigned j;
            const CordisBytesV1 empty = {0, 0};
            for (j = 0; j < value->held_count; ++j)
                if (value->held[j] &&
                    host->complete(value->context, value->held[j],
                                   value->held_cancelled[j] ? CORDIS_E_CANCELLED : CORDIS_OK, empty) == CORDIS_OK) {
                    value->held[j] = 0;
                    ++count;
                }
            while (value->held_count && !value->held[value->held_count - 1])
                --value->held_count;
        }
        if (value && value->cleanup_waiting && value->cleanup_ready) {
            const CordisBytesV1 empty = {0, 0};
            if (host->complete(value->context, value->cleanup_waiting, value->cleanup_result, empty) == CORDIS_OK) {
                value->cleanup_waiting = 0;
                value->cleanup_ready = 0;
                ++count;
            }
        }
#endif
        if (value && value->pending) {
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
    case 7:
        return value->stops;
    case 6:
        return value->target_request;
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
static CordisStatus CORDIS_CALL invoke_service(CordisHandle id, CordisHandle token, CordisBytesV1 method,
                                               CordisBytesV1 payload, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!method.size || !method.data)
        return CORDIS_E_INVALID_ARGUMENT;
    if (token == 10) {
        if (!value || !value->started || !request)
            return CORDIS_E_CLOSED;
        if (value->held_count >= 300)
            return CORDIS_E_LIMIT;
        value->held_cancelled[value->held_count] = 0;
        value->held[value->held_count++] = request;
        ++value->calls;
        return CORDIS_PENDING;
    }
    return dispatch(id, token, payload, request);
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_register(unsigned index, CordisHandle effect,
                                                               const CordisServiceKeyV1* key,
                                                               const CordisServiceMetadataV1* metadata,
                                                               CordisHandle token, CordisHandle* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->service_register(value->context, effect, key, metadata, token, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_withdraw(unsigned index, CordisHandle binding,
                                                               CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->service_withdraw(value->context, binding, output) : CORDIS_E_NOT_FOUND;
}
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_service_invoke(unsigned index, const CordisServiceKeyV1* key,
                                                             uint32_t minor, CordisBytesV1 caps, CordisBytesV1 method,
                                                             CordisBytesV1 payload, CordisRequestId* output)
{
    ResourceInstance* value = slot(index);
    return value ? host->service_invoke(value->context, key, minor, caps, method, payload, output) : CORDIS_E_NOT_FOUND;
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
    api.required_host_capabilities = api.capabilities;
#ifdef FIXTURE_SERVICES
    api.capabilities = CORDIS_CAP_ALL;
    api.required_host_capabilities = CORDIS_CAP_ALL;
    api.invoke_service = invoke_service;
    api.module_id.data = (const uint8_t*)FIXTURE_ID;
    api.module_id.size = sizeof(FIXTURE_ID) - 1;
#else
    api.module_id.data = (const uint8_t*)"org.cordis.resources";
    api.module_id.size = 20;
#endif
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
