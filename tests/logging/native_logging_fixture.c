/** @brief Generic Native ABI fixture for log admission and finalization. */
#include <cordis_abi/module_query.h>
#include <cordis_abi/plugin.h>
#include <string.h>

#define SPAN(literal) {(const uint8_t*)(literal), sizeof(literal) - 1}
#define SLOT_COUNT 4
#define MODULE_ID "org.cordis.test.native.logging"
#define MODULE_VERSION "1.0.0"
#define ALPHA "test.native.alpha"
#define BRAVO "test.native.bravo"

typedef struct LoggingInstance {
    CordisHandle context;
    int started;
} LoggingInstance;

static const CordisHostV1* log_host;
static LoggingInstance slots[SLOT_COUNT];
static CordisHandle retired_context;
static const CordisBytesV1 plugin_ids[] = {SPAN(ALPHA), SPAN(BRAVO)};
static const CordisQueryBytesV1 descriptor_ids[] = {SPAN(ALPHA), SPAN(BRAVO)};

static LoggingInstance* find_instance(CordisHandle id)
{
    if (!id || id > SLOT_COUNT || !slots[id - 1].context)
        return 0;
    return &slots[id - 1];
}

static CordisStatus CORDIS_CALL create(CordisHandle context, CordisBytesV1 plugin, CordisBytesV1 config,
                                       CordisHandle* instance)
{
    unsigned slot;
    (void)plugin;
    (void)config;
    if (!context || !instance)
        return CORDIS_E_INVALID_ARGUMENT;
    *instance = 0;
    for (slot = 0; slot < SLOT_COUNT; ++slot) {
        if (!slots[slot].context) {
            slots[slot].context = context;
            *instance = slot + 1;
            return CORDIS_OK;
        }
    }
    return CORDIS_E_LIMIT;
}

static CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request)
{
    LoggingInstance* value = find_instance(id);
    static const CordisBytesV1 message = SPAN("native fixture started");
    if (!value || !request || value->started)
        return CORDIS_E_INVALID_ARGUMENT;
    value->started = 1;
    return log_host->log(value->context, CORDIS_LOG_INFO, message);
}

static CordisStatus CORDIS_CALL stop(CordisHandle id, CordisRequestId request)
{
    LoggingInstance* value = find_instance(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    value->started = 0;
    return CORDIS_OK;
}

static CordisStatus CORDIS_CALL destroy(CordisHandle id)
{
    LoggingInstance* value = find_instance(id);
    static const CordisBytesV1 message = SPAN("native fixture destroyed");
    CordisStatus status;
    if (!value || value->started)
        return CORDIS_E_INVALID_ARGUMENT;
    status = log_host->log(value->context, CORDIS_LOG_WARNING, message);
    if (status != CORDIS_OK && status != CORDIS_E_LIMIT)
        return status;
    retired_context = value->context;
    memset(value, 0, sizeof(*value));
    return CORDIS_OK;
}

static CordisStatus CORDIS_CALL cancel(CordisHandle id, CordisRequestId request)
{
    (void)id;
    (void)request;
    return CORDIS_E_NOT_FOUND;
}

static CordisStatus CORDIS_CALL complete(CordisHandle id, CordisRequestId request, CordisStatus status,
                                         CordisBytesV1 result)
{
    (void)id;
    (void)request;
    (void)status;
    (void)result;
    return CORDIS_E_NOT_FOUND;
}

CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_logging_fixture_emit(CordisHandle slot, uint32_t level, CordisBytesV1 text)
{
    const LoggingInstance* value = find_instance(slot);
    return log_host->log(value ? value->context : retired_context, level, text);
}

CordisStatus CORDIS_CALL cordis_query_v1(uint32_t major, const CordisHostV1* host, CordisPluginApiV1* output)
{
    const CordisPluginApiV1 table = {
        {sizeof(CordisPluginApiV1), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0},
        0, 0, SPAN(MODULE_ID), SPAN(MODULE_VERSION), 2, 0, plugin_ids,
        create, start, stop, destroy, cancel, complete, 0, 0, 0
    };
    if (!host || !host->log || !output || major != CORDIS_ABI_MAJOR ||
        output->header.struct_size < sizeof(table))
        return CORDIS_E_UNSUPPORTED;
    *output = table;
    log_host = host;
    return CORDIS_OK;
}

CordisQueryStatus CORDIS_QUERY_CALL cordis_module_query_v1(const CordisModuleQueryV1* request,
                                                         CordisModuleDescriptorV1* output)
{
    const CordisModuleDescriptorV1 descriptor = {
        {sizeof(CordisModuleDescriptorV1), CORDIS_MODULE_QUERY_MAJOR, CORDIS_MODULE_QUERY_MINOR, 0},
        CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, CORDIS_MODULE_RESIDENT,
        SPAN(MODULE_ID), SPAN(MODULE_VERSION), 2, 0, descriptor_ids
    };
    if (!request || !output || request->header.query_major != CORDIS_MODULE_QUERY_MAJOR ||
        request->header.query_minor != CORDIS_MODULE_QUERY_MINOR ||
        request->header.struct_size < sizeof(*request) || output->header.struct_size < sizeof(descriptor))
        return CORDIS_QUERY_UNSUPPORTED;
    *output = descriptor;
    return CORDIS_QUERY_OK;
}
