/** @file counter.cpp
 * @brief 每实例独立的计数服务；业务只使用 C ABI 值，代码默认驻留。
 */
#include "support.hpp"
#include <charconv>

namespace {
using namespace example;
constexpr char moduleId[] = "org.cordis.example.counter", pluginId[] = "example.counter";
CordisStatus CORDIS_CALL createCounter(CordisHandle context, CordisBytesV1 plugin, CordisBytesV1 config,
                                       CordisHandle* output) noexcept
{
    return create(context, plugin, config, output, pluginId, "{\"failCleanup\":true}");
}
CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request || value->started)
        return CORDIS_E_INVALID_ARGUMENT;
    value->started = true;
    return publish(*value, "example.counter", "[\"next\"]");
}
CordisStatus CORDIS_CALL invoke(CordisHandle id, CordisHandle token, CordisBytesV1 method, CordisBytesV1 payload,
                                CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !value->started || token != 1 || !request || !equals(payload, "{}"))
        return CORDIS_E_INVALID_ARGUMENT;
    if (!equals(method, "next"))
        return CORDIS_E_NOT_FOUND;
    if (value->counter == std::numeric_limits<std::uint64_t>::max())
        return CORDIS_E_LIMIT;
    std::array<char, 64> result{};
    std::memcpy(result.data(), "{\"value\":", 9);
    auto converted = std::to_chars(result.data() + 9, result.data() + result.size() - 1, ++value->counter);
    if (converted.ec != std::errc{})
        return CORDIS_E_FAILED;
    *converted.ptr++ = '}';
    const auto accepted = host->complete(
        value->context, request, CORDIS_OK,
        {reinterpret_cast<const uint8_t*>(result.data()), static_cast<std::uint64_t>(converted.ptr - result.data())});
    return accepted == CORDIS_OK ? CORDIS_PENDING : CORDIS_E_FAILED;
}
CordisStatus CORDIS_CALL cleanup(CordisHandle id, CordisHandle effect, CordisHandle callback,
                                 CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || effect != value->effect || callback != 1 || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->fail)
        return CORDIS_E_FAILED;
    value->effect = value->binding = 0;
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL cancel(CordisHandle, CordisRequestId) noexcept
{
    return CORDIS_E_NOT_FOUND;
}
CordisStatus CORDIS_CALL ack(CordisHandle, CordisRequestId, CordisStatus, CordisBytesV1) noexcept
{
    return CORDIS_E_NOT_FOUND;
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
