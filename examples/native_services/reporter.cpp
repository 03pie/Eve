/** @file reporter.cpp
 * @brief 异步读取计数服务，提供报告并在旧绑定清理期间再次读取。
 */
#include "support.hpp"

namespace {
using namespace example;
constexpr char moduleId[] = "org.eve.example.reporter", pluginId[] = "example.reporter";
EveStatus EVE_CALL createReporter(EveHandle context, EveBytesV1 plugin, EveBytesV1 config,
                                        EveHandle* output) noexcept
{
    return create(context, plugin, config, output, pluginId, "{\"failStart\":true}");
}
EveStatus readCounter(Instance& value, EveRequestId request) noexcept
{
    const EveServiceKeyV1 key{bytes("example.counter"), 1, 0, 1};
    const auto status =
        host->service_invoke(value.context, &key, 1, bytes("[\"next\"]"), bytes("next"), bytes("{}"), &value.outgoing);
    if (status == EVE_PENDING)
        value.pending = request;
    return status;
}
EveStatus EVE_CALL start(EveHandle id, EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request || value->started)
        return EVE_E_INVALID_ARGUMENT;
    if (value->fail)
        return EVE_E_FAILED;
    value->started = true;
    const auto published = publish(*value, "example.report", "[\"read\"]");
    return published == EVE_OK ? readCounter(*value, request) : published;
}
EveStatus EVE_CALL ack(EveHandle id, EveRequestId request, EveStatus status,
                             EveBytesV1 result) noexcept
{
    auto* value = find(id);
    if (!value || !request || request != value->outgoing || !value->pending)
        return EVE_E_NOT_FOUND;
    const auto pending = value->pending;
    value->outgoing = value->pending = 0;
    if (status == EVE_OK) {
        if (result.size > value->result.size() || (result.size && !result.data))
            status = EVE_E_LIMIT;
        else {
            value->size = static_cast<std::size_t>(result.size);
            if (value->size)
                std::memcpy(value->result.data(), result.data, value->size);
            status = value->cleanup ? log(*value, "cleanup result ", result) : log(*value, "start result ", result);
        }
    }
    if (value->cleanup && status == EVE_OK)
        value->effect = value->binding = 0;
    // Complete the host's original start/cleanup request, never the host-operation ACK itself.
    return host->complete(value->context, pending, status, {});
}
EveStatus EVE_CALL cleanup(EveHandle id, EveHandle effect, EveHandle callback,
                                 EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !effect || effect != value->effect || callback != 1 || !request || value->pending)
        return EVE_E_INVALID_ARGUMENT;
    value->cleanup = true;
    return readCounter(*value, request);
}
EveStatus EVE_CALL invoke(EveHandle id, EveHandle token, EveBytesV1 method, EveBytesV1 payload,
                                EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !value->started || token != 1 || !request || !equals(payload, "{}"))
        return EVE_E_INVALID_ARGUMENT;
    if (!equals(method, "read"))
        return EVE_E_NOT_FOUND;
    return host->complete(value->context, request, EVE_OK,
                          {reinterpret_cast<const uint8_t*>(value->result.data()), value->size}) == EVE_OK
               ? EVE_PENDING
               : EVE_E_FAILED;
}
EveStatus EVE_CALL cancel(EveHandle id, EveRequestId request) noexcept
{
    auto* value = find(id);
    return value && value->pending == request && value->outgoing ? host->request_cancel(value->context, value->outgoing)
                                                                 : EVE_E_NOT_FOUND;
}
} // namespace
extern "C" EVE_QUERY_EXPORT EveQueryStatus EVE_QUERY_CALL
eve_module_query_v1(const EveModuleQueryV1* request, EveModuleDescriptorV1* output)
{
    return example::discover(request, output, moduleId, pluginId);
}
extern "C" EVE_EXPORT EveStatus EVE_CALL eve_query_v1(std::uint32_t abi, const EveHostV1* input,
                                                                  EvePluginApiV1* output)
{
    EvePluginApiV1 result{};
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
