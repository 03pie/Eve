/** @file counter_state.hpp
 * @brief 计数器的可选同步状态协议；只读写原实例的 uint64，不创建业务资源。
 */
#pragma once
#include "support.hpp"
#if EVE_EXAMPLE_COUNTER_STATE
#include <charconv>
#include <eve_abi/state.h>
#include <cstddef>
#include <system_error>

namespace example::counter_state {
inline constexpr char pluginId[] = "example.counter", schemaId[] = "org.eve.example.counter";
inline constexpr std::uint32_t schemaVersion = 1;

/** @brief 输入只借用本调用；ASCII 十进制必须是唯一 canonical 写法并完整落入 uint64。 */
inline bool parse(EveBytesV1 json, std::uint64_t& value) noexcept
{
    if (!json.data || !json.size || json.size > 20 || json.data[0] < '0' || json.data[0] > '9' ||
        (json.size > 1 && json.data[0] == '0'))
        return false;
    const auto first = reinterpret_cast<const char*>(json.data);
    const auto last = first + static_cast<std::size_t>(json.size);
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last)
        return false;
    value = parsed;
    return true;
}
inline bool accepts(const EveStateSchemaV1* schema) noexcept
{
    return schema && schema->struct_size >= sizeof(*schema) && !schema->reserved && schema->version == schemaVersion &&
           schema->id_size == sizeof(schemaId) - 1 && std::memcmp(schema->id, schemaId, sizeof(schemaId) - 1) == 0;
}
/** @brief 短结构不能访问尾部 size；完整结构的所有失败都保持 size 为零。 */
inline EveStatus prepare(EveStateBufferV1* output) noexcept
{
    if (!output || output->struct_size < sizeof(*output))
        return EVE_E_INVALID_ARGUMENT;
    output->size = 0;
    if (output->reserved || output->capacity > EVE_STATE_MAX_JSON_BYTES || (output->capacity && !output->data))
        return EVE_E_INVALID_ARGUMENT;
    return EVE_OK;
}
/** @brief 调用方已核对完整 buffer；容量不足时不写任何 JSON 字节。 */
inline EveStatus write(EveStateBufferV1* output, std::uint64_t value) noexcept
{
    std::array<char, 20> json{};
    const auto result = std::to_chars(json.data(), json.data() + json.size(), value);
    if (result.ec != std::errc{})
        return EVE_E_FAILED;
    const auto size = static_cast<std::size_t>(result.ptr - json.data());
    if (output->capacity < size)
        return EVE_E_LIMIT;
    std::memcpy(output->data, json.data(), size);
    output->size = size;
    return EVE_OK;
}
inline EveStatus EVE_CALL describe(EveBytesV1 plugin, EveStateSchemaV1* output) noexcept
{
    if (!output || output->struct_size < sizeof(*output) || output->reserved)
        return EVE_E_INVALID_ARGUMENT;
    output->version = output->id_size = 0;
    std::memset(output->id, 0, sizeof(output->id));
    if (plugin.size && !plugin.data)
        return EVE_E_INVALID_ARGUMENT;
    if (!equals(plugin, pluginId))
        return EVE_E_NOT_FOUND;
    output->version = schemaVersion;
    output->id_size = static_cast<std::uint32_t>(sizeof(schemaId) - 1);
    std::memcpy(output->id, schemaId, sizeof(schemaId) - 1);
    return EVE_OK;
}
inline EveStatus EVE_CALL validate(EveBytesV1 plugin, const EveStateSchemaV1* schema, EveBytesV1 json,
                                         EveStateBufferV1* output) noexcept
{
    const auto prepared = prepare(output);
    if (prepared != EVE_OK)
        return prepared;
    if (plugin.size && !plugin.data)
        return EVE_E_INVALID_ARGUMENT;
    if (!equals(plugin, pluginId))
        return EVE_E_NOT_FOUND;
    std::uint64_t value = 0;
    if (!accepts(schema) || !parse(json, value))
        return EVE_E_INVALID_ARGUMENT;
    return write(output, value);
}
inline EveStatus EVE_CALL capture(EveHandle id, EveStateBufferV1* output) noexcept
{
    const auto prepared = prepare(output);
    if (prepared != EVE_OK)
        return prepared;
    const auto value = find(id);
    if (!value)
        return EVE_E_NOT_FOUND;
    if (!value->started)
        return EVE_E_CLOSED;
    return write(output, value->counter);
}
inline EveStatus EVE_CALL restore(EveHandle id, const EveStateSchemaV1* schema, EveBytesV1 json) noexcept
{
    const auto instance = find(id);
    if (!instance)
        return EVE_E_NOT_FOUND;
    if (instance->startedOnce)
        return EVE_E_CLOSED;
    std::uint64_t value = 0;
    if (!accepts(schema) || !parse(json, value))
        return EVE_E_INVALID_ARGUMENT;
    instance->counter = value;
    return EVE_OK;
}
inline EveStatus query(std::uint32_t abi, EveStateApiV1* output) noexcept
{
    if (abi != EVE_STATE_ABI_MAJOR)
        return EVE_E_UNSUPPORTED;
    if (!output || output->header.struct_size < sizeof(*output))
        return EVE_E_INVALID_ARGUMENT;
    const EveStateApiV1 table{
        {sizeof(table), EVE_STATE_ABI_MAJOR, EVE_STATE_ABI_MINOR, 0}, describe, validate, capture, restore};
    std::memcpy(output, &table, sizeof(table));
    return EVE_OK;
}
} // namespace example::counter_state
#endif
