/** @file reporter.cpp
 * @brief 异步读取计数服务，提供报告并在旧绑定清理期间再次读取。
 */
#include "support.hpp"

namespace {
using namespace example;
constexpr char moduleId[] = "org.cordis.example.reporter", pluginId[] = "example.reporter";
CordisStatus CORDIS_CALL createReporter(CordisHandle context, CordisBytesV1 plugin, CordisBytesV1 config,
                                        CordisHandle* output) noexcept
{
    return create(context, plugin, config, output, pluginId, "{\"failStart\":true}");
}
CordisStatus readCounter(Instance& value, CordisRequestId request) noexcept
{
    const CordisServiceKeyV1 key{bytes("example.counter"), 1, 0, 1};
    const auto status =
        host->service_invoke(value.context, &key, 1, bytes("[\"next\"]"), bytes("next"), bytes("{}"), &value.outgoing);
    if (status == CORDIS_PENDING)
        value.pending = request;
    return status;
}
CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request || value->started)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->fail)
        return CORDIS_E_FAILED;
    value->started = true;
    const auto published = publish(*value, "example.report", "[\"read\"]");
    return published == CORDIS_OK ? readCounter(*value, request) : published;
}
CordisStatus CORDIS_CALL ack(CordisHandle id, CordisRequestId request, CordisStatus status,
                             CordisBytesV1 result) noexcept
{
    auto* value = find(id);
    if (!value || !request || request != value->outgoing || !value->pending)
        return CORDIS_E_NOT_FOUND;
    const auto pending = value->pending;
    value->outgoing = value->pending = 0;
    if (status == CORDIS_OK) {
        if (result.size > value->result.size() || (result.size && !result.data))
            status = CORDIS_E_LIMIT;
        else {
            value->size = static_cast<std::size_t>(result.size);
            if (value->size)
                std::memcpy(value->result.data(), result.data, value->size);
            status = value->cleanup ? log(*value, "cleanup result ", result) : log(*value, "start result ", result);
        }
    }
    if (value->cleanup && status == CORDIS_OK)
        value->effect = value->binding = 0;
    // Complete the host's original start/cleanup request, never the host-operation ACK itself.
    return host->complete(value->context, pending, status, {});
}
CordisStatus CORDIS_CALL cleanup(CordisHandle id, CordisHandle effect, CordisHandle callback,
                                 CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !effect || effect != value->effect || callback != 1 || !request || value->pending)
        return CORDIS_E_INVALID_ARGUMENT;
    value->cleanup = true;
    return readCounter(*value, request);
}
CordisStatus CORDIS_CALL invoke(CordisHandle id, CordisHandle token, CordisBytesV1 method, CordisBytesV1 payload,
                                CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !value->started || token != 1 || !request || !equals(payload, "{}"))
        return CORDIS_E_INVALID_ARGUMENT;
    if (!equals(method, "read"))
        return CORDIS_E_NOT_FOUND;
    return host->complete(value->context, request, CORDIS_OK,
                          {reinterpret_cast<const uint8_t*>(value->result.data()), value->size}) == CORDIS_OK
               ? CORDIS_PENDING
               : CORDIS_E_FAILED;
}
CordisStatus CORDIS_CALL cancel(CordisHandle id, CordisRequestId request) noexcept
{
    auto* value = find(id);
    return value && value->pending == request && value->outgoing ? host->request_cancel(value->context, value->outgoing)
                                                                 : CORDIS_E_NOT_FOUND;
}
} // namespace
extern "C" CORDIS_QUERY_EXPORT CordisQueryStatus CORDIS_QUERY_CALL
cordis_module_query_v1(const CordisModuleQueryV1* request, CordisModuleDescriptorV1* output)
{
    return example::discover(request, output, moduleId, pluginId);
}
extern "C" CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(std::uint32_t abi, const CordisHostV1* input,
                                                                  CordisPluginApiV1* output)
{
    CordisPluginApiV1 result{};
    result.create = createReporter;
    result.start = start;
    result.request_stop = example::stop;
    result.destroy = example::destroy;
    result.cancel_request = cancel;
    result.request_complete = ack;
    result.invoke_service = invoke;
    result.request_cleanup = cleanup;
    return example::negotiate(abi, input, output, moduleId, pluginId, result);
}
