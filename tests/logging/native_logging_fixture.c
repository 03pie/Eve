/** @brief Generic Native ABI fixture for log admission and finalization. */
#include <eve_abi/module_query.h>
#include <eve_abi/plugin.h>
#include <string.h>

#define SPAN(literal) {(const uint8_t*)(literal), sizeof(literal) - 1}
#define SLOT_COUNT 4
#define MODULE_ID "org.eve.test.native.logging"
#define MODULE_VERSION "1.0.0"
#define ALPHA "test.native.alpha"
#define BRAVO "test.native.bravo"

typedef struct LoggingInstance {
    EveHandle context;
    int started;
} LoggingInstance;

static const EveHostV1* log_host;
static LoggingInstance slots[SLOT_COUNT];
static EveHandle retired_context;
static const EveBytesV1 plugin_ids[] = {SPAN(ALPHA), SPAN(BRAVO)};
static const EveQueryBytesV1 descriptor_ids[] = {SPAN(ALPHA), SPAN(BRAVO)};

static LoggingInstance* find_instance(EveHandle id)
{
    if (!id || id > SLOT_COUNT || !slots[id - 1].context)
        return 0;
    return &slots[id - 1];
}

static EveStatus EVE_CALL create(EveHandle context, EveBytesV1 plugin, EveBytesV1 config,
                                       EveHandle* instance)
{
    unsigned slot;
    (void)plugin;
    (void)config;
    if (!context || !instance)
        return EVE_E_INVALID_ARGUMENT;
    *instance = 0;
    for (slot = 0; slot < SLOT_COUNT; ++slot) {
        if (!slots[slot].context) {
            slots[slot].context = context;
            *instance = slot + 1;
            return EVE_OK;
        }
    }
    return EVE_E_LIMIT;
}

static EveStatus EVE_CALL start(EveHandle id, EveRequestId request)
{
    LoggingInstance* value = find_instance(id);
    static const EveBytesV1 message = SPAN("native fixture started");
    if (!value || !request || value->started)
        return EVE_E_INVALID_ARGUMENT;
    value->started = 1;
    return log_host->log(value->context, EVE_LOG_INFO, message);
}

static EveStatus EVE_CALL stop(EveHandle id, EveRequestId request)
{
    LoggingInstance* value = find_instance(id);
    if (!value || !request)
        return EVE_E_INVALID_ARGUMENT;
    value->started = 0;
    return EVE_OK;
}

static EveStatus EVE_CALL destroy(EveHandle id)
{
    LoggingInstance* value = find_instance(id);
    static const EveBytesV1 message = SPAN("native fixture destroyed");
    EveStatus status;
    if (!value || value->started)
        return EVE_E_INVALID_ARGUMENT;
    status = log_host->log(value->context, EVE_LOG_WARNING, message);
    if (status != EVE_OK && status != EVE_E_LIMIT)
        return status;
    retired_context = value->context;
    memset(value, 0, sizeof(*value));
    return EVE_OK;
}

static EveStatus EVE_CALL cancel(EveHandle id, EveRequestId request)
{
    (void)id;
    (void)request;
    return EVE_E_NOT_FOUND;
}

static EveStatus EVE_CALL complete(EveHandle id, EveRequestId request, EveStatus status,
                                         EveBytesV1 result)
{
    (void)id;
    (void)request;
    (void)status;
    (void)result;
    return EVE_E_NOT_FOUND;
}

EVE_EXPORT EveStatus EVE_CALL eve_logging_fixture_emit(EveHandle slot, uint32_t level, EveBytesV1 text)
{
    const LoggingInstance* value = find_instance(slot);
    return log_host->log(value ? value->context : retired_context, level, text);
}

EveStatus EVE_CALL eve_query_v1(uint32_t major, const EveHostV1* host, EvePluginApiV1* output)
{
    const EvePluginApiV1 table = {
        {sizeof(EvePluginApiV1), EVE_ABI_MAJOR, EVE_ABI_MINOR, 0},
        0, 0, SPAN(MODULE_ID), SPAN(MODULE_VERSION), 2, 0, plugin_ids,
        create, start, stop, destroy, cancel, complete, 0, 0, 0
    };
    if (!host || !host->log || !output || major != EVE_ABI_MAJOR ||
        output->header.struct_size < sizeof(table))
        return EVE_E_UNSUPPORTED;
    *output = table;
    log_host = host;
    return EVE_OK;
}

EveQueryStatus EVE_QUERY_CALL eve_module_query_v1(const EveModuleQueryV1* request,
                                                         EveModuleDescriptorV1* output)
{
    const EveModuleDescriptorV1 descriptor = {
        {sizeof(EveModuleDescriptorV1), EVE_MODULE_QUERY_MAJOR, EVE_MODULE_QUERY_MINOR, 0},
        EVE_ABI_MAJOR, EVE_ABI_MINOR, EVE_MODULE_RESIDENT,
        SPAN(MODULE_ID), SPAN(MODULE_VERSION), 2, 0, descriptor_ids
    };
    if (!request || !output || request->header.query_major != EVE_MODULE_QUERY_MAJOR ||
        request->header.query_minor != EVE_MODULE_QUERY_MINOR ||
        request->header.struct_size < sizeof(*request) || output->header.struct_size < sizeof(descriptor))
        return EVE_QUERY_UNSUPPORTED;
    *output = descriptor;
    return EVE_QUERY_OK;
}
