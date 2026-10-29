/** @file support.hpp
 * @brief DLL 内部的有界实例与纯 C ABI 协商；不链接 Core 或 Qt。
 */
#pragma once
#include <array>
#include <cordis_abi/module_query.h>
#include <cordis_abi/plugin.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace example {
inline const CordisHostV1* host = nullptr;
struct Instance {
    CordisHandle id = 0, context = 0, effect = 0, binding = 0;
    CordisRequestId pending = 0, outgoing = 0;
    std::uint64_t counter = 0;
    bool started = false, cleanup = false, fail = false;
    std::array<char, 64> result{};
    std::size_t size = 0;
};
inline std::array<Instance, 32> instances;
inline CordisHandle next = 1;
template <std::size_t N> CordisBytesV1 bytes(const char (&text)[N]) noexcept
{
    return {reinterpret_cast<const uint8_t*>(text), N - 1};
}
inline bool equals(CordisBytesV1 value, const char* text) noexcept
{
    const auto size = std::strlen(text);
    return value.size == size && (!size || (value.data && std::memcmp(value.data, text, size) == 0));
}
inline Instance* find(CordisHandle id) noexcept
{
    for (auto& value : instances)
        if (id && value.id == id)
            return &value;
    return nullptr;
}
inline CordisStatus create(CordisHandle context, CordisBytesV1 plugin, CordisBytesV1 config, CordisHandle* output,
                           const char* expected, const char* failure) noexcept
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context || !equals(plugin, expected) || (!equals(config, "null") && !equals(config, failure)))
        return CORDIS_E_INVALID_ARGUMENT;
    if (next == std::numeric_limits<CordisHandle>::max())
        return CORDIS_E_LIMIT;
    for (auto& value : instances)
        if (!value.id) {
            value = {};
            value.id = next++;
            value.context = context;
            value.fail = equals(config, failure);
            *output = value.id;
            return CORDIS_OK;
        }
    return CORDIS_E_LIMIT;
}
inline CordisStatus stop(CordisHandle id, CordisRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request)
        return CORDIS_E_INVALID_ARGUMENT;
    if (value->pending || value->outgoing)
        return CORDIS_E_BUSY;
    value->started = false;
    return CORDIS_OK;
}
inline CordisStatus destroy(CordisHandle id) noexcept
{
    auto* value = find(id);
    if (!value)
        return CORDIS_E_NOT_FOUND;
    if (value->started || value->effect || value->pending || value->outgoing)
        return CORDIS_E_BUSY;
    *value = {};
    return CORDIS_OK;
}
inline CordisStatus publish(Instance& value, const char* name, const char* caps) noexcept
{
    CordisBytesV1 label{reinterpret_cast<const uint8_t*>(name), std::strlen(name)};
    auto status = host->effect_create(value.context, label, 1, &value.effect);
    if (status != CORDIS_OK)
        return status;
    const CordisServiceKeyV1 key{label, 1, 0, 1};
    const CordisServiceMetadataV1 metadata{
        sizeof(CordisServiceMetadataV1), 1, {reinterpret_cast<const uint8_t*>(caps), std::strlen(caps)}};
    status = host->service_register(value.context, value.effect, &key, &metadata, 1, &value.binding);
    const auto ready = host->effect_ready(value.context, value.effect, status, {});
    return status == CORDIS_OK ? ready : status;
}
template <std::size_t N> CordisStatus log(Instance& value, const char (&prefix)[N], CordisBytesV1 result) noexcept
{
    if (result.size > value.result.size() || (result.size && !result.data))
        return CORDIS_E_LIMIT;
    std::array<char, 128> text{};
    static_assert(N - 1 + 64 <= 128);
    std::memcpy(text.data(), prefix, N - 1);
    if (result.size)
        std::memcpy(text.data() + N - 1, result.data, static_cast<std::size_t>(result.size));
    return host->log(value.context, CORDIS_LOG_INFO,
                     {reinterpret_cast<const uint8_t*>(text.data()), N - 1 + result.size});
}
template <std::size_t M, std::size_t P>
CordisQueryStatus discover(const CordisModuleQueryV1* request, CordisModuleDescriptorV1* output,
                           const char (&module)[M], const char (&plugin)[P]) noexcept
{
    if (!request || !output || request->header.struct_size < sizeof(*request) || request->header.reserved ||
        request->header.query_major != 1 || request->runtime_abi_major != 1 || request->flags)
        return CORDIS_QUERY_UNSUPPORTED;
    const auto capacity = output->header.struct_size;
    if (capacity < sizeof(*output))
        return CORDIS_QUERY_INVALID_ARGUMENT;
    static const CordisQueryBytesV1 plugins[] = {{reinterpret_cast<const uint8_t*>(plugin), P - 1}};
    const CordisModuleDescriptorV1 result{{sizeof(result), 1, 0, 0},
                                          1,
                                          0,
                                          CORDIS_MODULE_RESIDENT,
                                          {reinterpret_cast<const uint8_t*>(module), M - 1},
                                          {reinterpret_cast<const uint8_t*>("1.0.0"), 5},
                                          1,
                                          0,
                                          plugins};
    std::memcpy(output, &result, sizeof(result));
    return CORDIS_QUERY_OK;
}
template <std::size_t M, std::size_t P>
CordisStatus negotiate(std::uint32_t abi, const CordisHostV1* input, CordisPluginApiV1* output, const char (&module)[M],
                       const char (&plugin)[P], CordisPluginApiV1 result) noexcept
{
    constexpr auto caps = CORDIS_CAP_SERVICES | CORDIS_CAP_EFFECTS;
    if (!input || !output || abi != CORDIS_ABI_MAJOR || input->header.struct_size < sizeof(*input) ||
        input->header.abi_major != CORDIS_ABI_MAJOR || input->header.reserved || input->reserved ||
        (input->capabilities & caps) != caps || !input->complete || !input->log || !input->request_cancel ||
        !input->service_register || !input->service_withdraw || !input->service_invoke || !input->effect_create ||
        !input->effect_ready || !input->effect_request_cleanup)
        return CORDIS_E_UNSUPPORTED;
    const auto capacity = output->header.struct_size;
    if (capacity < sizeof(*output))
        return CORDIS_E_INVALID_ARGUMENT;
    static const CordisBytesV1 plugins[] = {bytes(plugin)};
    result.header = {sizeof(result), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0};
    result.capabilities = result.required_host_capabilities = caps;
    result.module_id = bytes(module);
    result.module_version = bytes("1.0.0");
    result.plugin_count = 1;
    result.plugins = plugins;
    std::memcpy(output, &result, sizeof(result));
    host = input;
    return CORDIS_OK;
}
} // namespace example
