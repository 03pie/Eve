/** @file draft_state.hpp
 * @brief 笔记草稿的纯值状态协议；工厂与实例共用同一 canonical JSON 和 Unicode 检查。
 */
#pragma once
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cordis_abi/state.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace notebook::draft_state {
inline constexpr char pluginId[] = "example.notebook", schemaId[] = "org.cordis.example.notebook.draft";
inline constexpr std::uint32_t schemaVersion = 1;
inline constexpr int maximumTextBytes = 65536;

inline bool samePlugin(CordisBytesV1 plugin) noexcept
{
    return plugin.data && plugin.size == sizeof(pluginId) - 1 &&
           std::memcmp(plugin.data, pluginId, sizeof(pluginId) - 1) == 0;
}
inline bool accepts(const CordisStateSchemaV1* schema) noexcept
{
    return schema && schema->struct_size >= sizeof(*schema) && !schema->reserved && schema->version == schemaVersion &&
           schema->id_size == sizeof(schemaId) - 1 && std::memcmp(schema->id, schemaId, sizeof(schemaId) - 1) == 0;
}
/** @brief 短结构不访问尾部 size；完整可写结构的所有后续失败保持 size 为零。 */
inline CordisStatus prepare(CordisStateBufferV1* output) noexcept
{
    if (!output || output->struct_size < sizeof(*output))
        return CORDIS_E_INVALID_ARGUMENT;
    output->size = 0;
    if (output->reserved || output->capacity > CORDIS_STATE_MAX_JSON_BYTES || (output->capacity && !output->data))
        return CORDIS_E_INVALID_ARGUMENT;
    return CORDIS_OK;
}
inline CordisStatus describe(CordisBytesV1 plugin, CordisStateSchemaV1* output) noexcept
{
    if (!output || output->struct_size < sizeof(*output) || output->reserved)
        return CORDIS_E_INVALID_ARGUMENT;
    output->version = output->id_size = 0;
    std::memset(output->id, 0, sizeof(output->id));
    if (plugin.size && !plugin.data)
        return CORDIS_E_INVALID_ARGUMENT;
    if (!samePlugin(plugin))
        return CORDIS_E_NOT_FOUND;
    output->version = schemaVersion;
    output->id_size = static_cast<std::uint32_t>(sizeof(schemaId) - 1);
    std::memcpy(output->id, schemaId, sizeof(schemaId) - 1);
    return CORDIS_OK;
}
/** @brief 编码之前先限制 UTF-16 长度，超大正常编辑也不会触发无界状态副本。 */
inline CordisStatus encode(const QString& draft, QByteArray& json)
{
    if (draft.size() > maximumTextBytes)
        return CORDIS_E_LIMIT;
    const auto utf8 = draft.toUtf8();
    if (QString::fromUtf8(utf8.constData(), utf8.size()) != draft)
        return CORDIS_E_INVALID_ARGUMENT;
    if (utf8.size() > maximumTextBytes)
        return CORDIS_E_LIMIT;
    json = QJsonDocument(QJsonObject{{QStringLiteral("draft"), draft}}).toJson(QJsonDocument::Compact);
    return CORDIS_OK;
}
/** @brief 只接受本 codec 的精确输出，拒绝重复键、额外字段和非规范空白/转义。 */
inline CordisStatus decode(const CordisStateSchemaV1* schema, CordisBytesV1 input, QString& draft,
                           QByteArray& canonical)
{
    if (!accepts(schema) || !input.data || !input.size)
        return CORDIS_E_INVALID_ARGUMENT;
    if (input.size > CORDIS_STATE_MAX_JSON_BYTES)
        return CORDIS_E_LIMIT;
    const QByteArray json(reinterpret_cast<const char*>(input.data), static_cast<int>(input.size));
    if (QString::fromUtf8(json.constData(), json.size()).toUtf8() != json)
        return CORDIS_E_INVALID_ARGUMENT;
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return CORDIS_E_INVALID_ARGUMENT;
    const auto object = document.object();
    const auto found = object.constFind(QStringLiteral("draft"));
    if (object.size() != 1 || found == object.constEnd() || !found.value().isString())
        return CORDIS_E_INVALID_ARGUMENT;
    auto value = found.value().toString();
    const auto status = encode(value, canonical);
    if (status != CORDIS_OK)
        return status;
    if (canonical != json)
        return CORDIS_E_INVALID_ARGUMENT;
    draft = std::move(value);
    return CORDIS_OK;
}
/** @brief 调用方已验证完整 buffer；编码完整完成后才写入调用方内存。 */
inline CordisStatus write(const QByteArray& json, CordisStateBufferV1* output) noexcept
{
    const auto size = static_cast<std::uint64_t>(json.size());
    if (output->capacity < size)
        return CORDIS_E_LIMIT;
    std::memcpy(output->data, json.constData(), static_cast<std::size_t>(size));
    output->size = size;
    return CORDIS_OK;
}
inline CordisStatus capture(const QString& draft, CordisStateBufferV1* output) noexcept
{
    const auto prepared = prepare(output);
    if (prepared != CORDIS_OK)
        return prepared;
    try {
        QByteArray json;
        const auto status = encode(draft, json);
        return status == CORDIS_OK ? write(json, output) : status;
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
inline CordisStatus validate(CordisBytesV1 plugin, const CordisStateSchemaV1* schema, CordisBytesV1 input,
                             CordisStateBufferV1* output) noexcept
{
    const auto prepared = prepare(output);
    if (prepared != CORDIS_OK)
        return prepared;
    if (plugin.size && !plugin.data)
        return CORDIS_E_INVALID_ARGUMENT;
    if (!samePlugin(plugin))
        return CORDIS_E_NOT_FOUND;
    try {
        QString draft;
        QByteArray canonical;
        const auto status = decode(schema, input, draft, canonical);
        return status == CORDIS_OK ? write(canonical, output) : status;
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
} // namespace notebook::draft_state
