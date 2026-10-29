/* Real C plugin using the production dispatch/effect table. Test exports only
 * ask the plugin to use its borrowed HostTable; no private host entry is called. */
#include <cordis_abi/plugin.h>
#include <string.h>

typedef struct ResourceInstance {
    CordisHandle context, id;
    CordisRequestId pending, latest;
    CordisStatus last_status;
    unsigned acks, calls, cleanups;
    CordisHandle order[256];
    int started, cancelled;
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
    (void)config;
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
    return CORDIS_OK;
}
static CordisStatus CORDIS_CALL stop(CordisHandle id, CordisRequestId request)
{
    ResourceInstance* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending)
        return CORDIS_E_BUSY;
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
    if (!value || value->pending != request)
        return CORDIS_E_NOT_FOUND;
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
    api.module_id.data = (const uint8_t*)"org.cordis.resources";
    api.module_id.size = 20;
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
