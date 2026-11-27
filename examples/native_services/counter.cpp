/** @file counter.cpp
 * @brief 每实例独立的计数服务；业务只使用 C ABI 值，代码默认驻留。
 */
#include "support.hpp"
#include <charconv>
#if EVE_EXAMPLE_COUNTER_STATE
#include "counter_state.hpp"
#endif

namespace {
using namespace example;
constexpr char moduleId[] = "org.eve.example.counter", pluginId[] = "example.counter";
EveStatus EVE_CALL createCounter(EveHandle context, EveBytesV1 plugin, EveBytesV1 config,
                                       EveHandle* output) noexcept
{
    return create(context, plugin, config, output, pluginId, "{\"failCleanup\":true}");
}
EveStatus EVE_CALL start(EveHandle id, EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request || value->started)
        return EVE_E_INVALID_ARGUMENT;
#if EVE_EXAMPLE_COUNTER_STATE
    value->startedOnce = true;
#endif
    value->started = true;
    return publish(*value, "example.counter", "[\"next\"]");
}
EveStatus EVE_CALL invoke(EveHandle id, EveHandle token, EveBytesV1 method, EveBytesV1 payload,
                                EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !value->started || token != 1 || !request || !equals(payload, "{}"))
        return EVE_E_INVALID_ARGUMENT;
    if (!equals(method, "next"))
        return EVE_E_NOT_FOUND;
    if (value->counter == std::numeric_limits<std::uint64_t>::max())
        return EVE_E_LIMIT;
    std::array<char, 64> result{};
    std::memcpy(result.data(), "{\"value\":", 9);
    auto converted = std::to_chars(result.data() + 9, result.data() + result.size() - 1, ++value->counter);
    if (converted.ec != std::errc{})
        return EVE_E_FAILED;
    *converted.ptr++ = '}';
    const auto accepted = host->complete(
        value->context, request, EVE_OK,
        {reinterpret_cast<const uint8_t*>(result.data()), static_cast<std::uint64_t>(converted.ptr - result.data())});
    return accepted == EVE_OK ? EVE_PENDING : EVE_E_FAILED;
}
EveStatus EVE_CALL cleanup(EveHandle id, EveHandle effect, EveHandle callback,
                                 EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || effect != value->effect || callback != 1 || !request)
        return EVE_E_INVALID_ARGUMENT;
    if (value->fail)
        return EVE_E_FAILED;
    value->effect = value->binding = 0;
    return EVE_OK;
}
EveStatus EVE_CALL cancel(EveHandle, EveRequestId) noexcept
{
    return EVE_E_NOT_FOUND;
}
EveStatus EVE_CALL ack(EveHandle, EveRequestId, EveStatus, EveBytesV1) noexcept
{
    return EVE_E_NOT_FOUND;
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
    result.create = createCounter;
    result.start = start;
    result.request_stop = example::stop;
    result.destroy = example::destroy;
    result.cancel_request = cancel;
    result.request_complete = ack;
    result.invoke_service = invoke;
    result.request_cleanup = cleanup;
    return example::negotiate(abi, input, output, moduleId, pluginId, result);
}
#if EVE_EXAMPLE_COUNTER_STATE
extern "C" EVE_EXPORT EveStatus EVE_CALL eve_query_state_v1(std::uint32_t abi, EveStateApiV1* output)
{
    return example::counter_state::query(abi, output);
}
#endif
