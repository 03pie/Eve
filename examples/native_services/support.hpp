/** @file support.hpp
 * @brief DLL 内部的有界实例与纯 C ABI 协商；不链接 Core 或 Qt。
 */
#pragma once
#include <array>
#include <eve_abi/module_query.h>
#include <eve_abi/plugin.h>
#include <cstdint>
#include <cstring>
#include <limits>

#ifndef EVE_EXAMPLE_MODULE_VERSION
#define EVE_EXAMPLE_MODULE_VERSION "1.0.0"
#endif
#ifndef EVE_EXAMPLE_EXPLICIT_UNLOAD
#define EVE_EXAMPLE_EXPLICIT_UNLOAD 0
#endif
#ifndef EVE_EXAMPLE_COUNTER_STATE
#define EVE_EXAMPLE_COUNTER_STATE 0
#endif

namespace example {
inline constexpr char moduleVersion[] = EVE_EXAMPLE_MODULE_VERSION;
static_assert(sizeof(moduleVersion) > 1 && sizeof(moduleVersion) - 1 <= EVE_MAX_VERSION_BYTES);
inline const EveHostV1* host = nullptr;
struct Instance {
    EveHandle id = 0, context = 0, effect = 0, binding = 0;
    EveRequestId pending = 0, outgoing = 0;
    std::uint64_t counter = 0;
    bool started = false, cleanup = false, fail = false;
#if EVE_EXAMPLE_COUNTER_STATE
    bool startedOnce = false;
#endif
    std::array<char, 64> result{};
    std::size_t size = 0;
};
inline std::array<Instance, 32> instances;
inline EveHandle next = 1;
template <std::size_t N> EveBytesV1 bytes(const char (&text)[N]) noexcept
{
    return {reinterpret_cast<const uint8_t*>(text), N - 1};
}
inline bool equals(EveBytesV1 value, const char* text) noexcept
{
    const auto size = std::strlen(text);
    return value.size == size && (!size || (value.data && std::memcmp(value.data, text, size) == 0));
}
inline Instance* find(EveHandle id) noexcept
{
    for (auto& value : instances)
        if (id && value.id == id)
            return &value;
    return nullptr;
}
inline EveStatus create(EveHandle context, EveBytesV1 plugin, EveBytesV1 config, EveHandle* output,
                           const char* expected, const char* failure) noexcept
{
    if (!output)
        return EVE_E_INVALID_ARGUMENT;
    *output = 0;
    if (!context || !equals(plugin, expected) || (!equals(config, "null") && !equals(config, failure)))
        return EVE_E_INVALID_ARGUMENT;
    if (next == std::numeric_limits<EveHandle>::max())
        return EVE_E_LIMIT;
    for (auto& value : instances)
        if (!value.id) {
            value = {};
            value.id = next++;
            value.context = context;
            value.fail = equals(config, failure);
            *output = value.id;
            return EVE_OK;
        }
    return EVE_E_LIMIT;
}
inline EveStatus stop(EveHandle id, EveRequestId request) noexcept
{
    auto* value = find(id);
    if (!value || !request)
        return EVE_E_INVALID_ARGUMENT;
    if (value->pending || value->outgoing)
        return EVE_E_BUSY;
    value->started = false;
    return EVE_OK;
}
inline EveStatus destroy(EveHandle id) noexcept
{
    auto* value = find(id);
    if (!value)
        return EVE_E_NOT_FOUND;
    if (value->started || value->effect || value->pending || value->outgoing)
        return EVE_E_BUSY;
    *value = {};
    return EVE_OK;
}
inline EveStatus publish(Instance& value, const char* name, const char* caps) noexcept
{
    EveBytesV1 label{reinterpret_cast<const uint8_t*>(name), std::strlen(name)};
    auto status = host->effect_create(value.context, label, 1, &value.effect);
    if (status != EVE_OK)
        return status;
    const EveServiceKeyV1 key{label, 1, 0, 1};
    const EveServiceMetadataV1 metadata{
        sizeof(EveServiceMetadataV1), 1, {reinterpret_cast<const uint8_t*>(caps), std::strlen(caps)}};
    status = host->service_register(value.context, value.effect, &key, &metadata, 1, &value.binding);
    const auto ready = host->effect_ready(value.context, value.effect, status, {});
    return status == EVE_OK ? ready : status;
}
template <std::size_t N> EveStatus log(Instance& value, const char (&prefix)[N], EveBytesV1 result) noexcept
{
    if (result.size > value.result.size() || (result.size && !result.data))
        return EVE_E_LIMIT;
    std::array<char, 128> text{};
    static_assert(N - 1 + 64 <= 128);
    std::memcpy(text.data(), prefix, N - 1);
    if (result.size)
        std::memcpy(text.data() + N - 1, result.data, static_cast<std::size_t>(result.size));
    return host->log(value.context, EVE_LOG_INFO,
                     {reinterpret_cast<const uint8_t*>(text.data()), N - 1 + result.size});
}
template <std::size_t M, std::size_t P>
EveQueryStatus discover(const EveModuleQueryV1* request, EveModuleDescriptorV1* output,
                           const char (&module)[M], const char (&plugin)[P]) noexcept
{
    if (!request || !output || request->header.struct_size < sizeof(*request) || request->header.reserved ||
        request->header.query_major != EVE_MODULE_QUERY_MAJOR || request->runtime_abi_major != EVE_ABI_MAJOR ||
        request->runtime_abi_minor != EVE_ABI_MINOR || request->flags)
        return EVE_QUERY_UNSUPPORTED;
    const auto minor = request->header.query_minor;
#if EVE_EXAMPLE_EXPLICIT_UNLOAD
    if (minor != EVE_MODULE_QUERY_MINOR && minor != EVE_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR)
#else
    if (minor != EVE_MODULE_QUERY_MINOR)
#endif
        return EVE_QUERY_UNSUPPORTED;
    const auto capacity = output->header.struct_size;
    if (capacity < sizeof(*output))
        return EVE_QUERY_INVALID_ARGUMENT;
    static const EveQueryBytesV1 plugins[] = {{reinterpret_cast<const uint8_t*>(plugin), P - 1}};
    const EveModuleDescriptorV1 result{
        {sizeof(result), EVE_MODULE_QUERY_MAJOR, minor, 0},
        EVE_ABI_MAJOR,
        EVE_ABI_MINOR,
        minor == EVE_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR ? EVE_MODULE_EXPLICIT_UNLOAD : EVE_MODULE_RESIDENT,
        {reinterpret_cast<const uint8_t*>(module), M - 1},
        {reinterpret_cast<const uint8_t*>(moduleVersion), sizeof(moduleVersion) - 1},
        1,
        0,
        plugins};
    std::memcpy(output, &result, sizeof(result));
    return EVE_QUERY_OK;
}
template <std::size_t M, std::size_t P>
EveStatus negotiate(std::uint32_t abi, const EveHostV1* input, EvePluginApiV1* output, const char (&module)[M],
                       const char (&plugin)[P], EvePluginApiV1 result) noexcept
{
    constexpr auto caps = EVE_CAP_SERVICES | EVE_CAP_EFFECTS;
    if (!input || !output || abi != EVE_ABI_MAJOR || input->header.struct_size < sizeof(*input) ||
        input->header.abi_major != EVE_ABI_MAJOR || input->header.reserved || input->reserved ||
        (input->capabilities & caps) != caps || !input->complete || !input->log || !input->request_cancel ||
        !input->service_register || !input->service_withdraw || !input->service_invoke || !input->effect_create ||
        !input->effect_ready || !input->effect_request_cleanup)
        return EVE_E_UNSUPPORTED;
    const auto capacity = output->header.struct_size;
    if (capacity < sizeof(*output))
        return EVE_E_INVALID_ARGUMENT;
    static const EveBytesV1 plugins[] = {bytes(plugin)};
    result.header = {sizeof(result), EVE_ABI_MAJOR, EVE_ABI_MINOR, 0};
    result.capabilities = result.required_host_capabilities = caps;
    result.module_id = bytes(module);
    result.module_version = bytes(moduleVersion);
    result.plugin_count = 1;
    result.plugins = plugins;
    std::memcpy(output, &result, sizeof(result));
    host = input;
    return EVE_OK;
}
} // namespace example
