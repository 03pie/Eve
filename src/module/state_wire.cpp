/** @file state_wire.cpp
 * @brief 宿主拥有缓冲区并复用原 JSON 检查；不访问模块目录或实例指针。
 */
#include "../core/state_value_p.hpp"
#include "json_p.hpp"
#include "state_wire_p.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace eve::module::detail {
static_assert(sizeof(EveStateSchemaV1{}.id) == maxStateSchemaBytes, "schema budgets must match");
static_assert(EVE_STATE_MAX_JSON_BYTES == maxStateJsonBytes, "payload budgets must match");
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "module.state";
    return value;
}
} // namespace
Result<void> stateStatus(EveStatus status)
{
    if (status == EVE_OK)
        return Result<void>::success();
    if (status > EVE_OK || status < EVE_E_BUSY || status == EVE_E_ABI_PROTOCOL)
        return Result<void>::failure(error(ErrorCode::AbiProtocol, "invalid synchronous state status"));
    const auto code = status == EVE_E_BUSY        ? ErrorCode::ConfigConflict
                      : status == EVE_E_CLOSED    ? ErrorCode::ContextClosing
                      : status == EVE_E_CANCELLED ? ErrorCode::OperationCancelled
                      : status == EVE_E_LIMIT     ? ErrorCode::InvalidArgument
                                                     : ErrorCode::ConfigInvalid;
    auto failure = error(code, "state operation rejected");
    failure.message += ", status " + std::to_string(status);
    return Result<void>::failure(std::move(failure));
}
Result<EveStateApiV1> queryStateApi(EveQueryStateFnV1 query)
{
    using Output = Result<EveStateApiV1>;
    if (!query)
        return Output::failure(error(ErrorCode::AbiMismatch, "missing state query"));
    EveStateApiV1 table{};
    table.header.struct_size = sizeof(table);
    EveStatus status;
    try {
        status = query(EVE_STATE_ABI_MAJOR, &table);
    } catch (...) {
        return Output::failure(error(ErrorCode::AbiProtocol, "exception escaped state query"));
    }
    auto accepted = stateStatus(status);
    if (!accepted.hasValue())
        return Output::failure(accepted.error());
    if (table.header.struct_size < sizeof(table) || table.header.abi_major != EVE_STATE_ABI_MAJOR ||
        table.header.reserved || !table.describe || !table.validate || !table.capture || !table.restore)
        return Output::failure(error(ErrorCode::AbiMismatch, "incompatible state table"));
    return Output::success(table);
}
Result<EveStateSchemaV1> encodeStateSchema(const StateSchema& schema)
{
    using Output = Result<EveStateSchemaV1>;
    if (!eve::detail::validStateSchema(schema))
        return Output::failure(error(ErrorCode::InvalidArgument, "invalid state schema"));
    EveStateSchemaV1 value{};
    value.struct_size = sizeof(value);
    value.version = schema.version;
    value.id_size = static_cast<std::uint32_t>(schema.id.size());
    std::memcpy(value.id, schema.id.data(), schema.id.size());
    return Output::success(value);
}
Result<std::optional<StateSchema>> readStateSchema(const std::function<EveStatus(EveStateSchemaV1*)>& describe)
{
    using Output = Result<std::optional<StateSchema>>;
    if (!describe)
        return Output::failure(error(ErrorCode::InvalidArgument, "empty state description"));
    EveStateSchemaV1 schema{};
    schema.struct_size = sizeof(schema);
    EveStatus status;
    try {
        status = describe(&schema);
    } catch (...) {
        return Output::failure(error(ErrorCode::AbiProtocol, "exception escaped state description"));
    }
    if (schema.struct_size != sizeof(schema) || schema.reserved)
        return Output::failure(error(ErrorCode::AbiProtocol, "state schema header changed"));
    if (status != EVE_OK) {
        if (schema.version || schema.id_size ||
            std::any_of(std::begin(schema.id), std::end(schema.id), [](std::uint8_t byte) { return byte != 0; }))
            return Output::failure(error(ErrorCode::AbiProtocol, "failed state description wrote a schema"));
        if (status == EVE_E_UNSUPPORTED)
            return Output::success({});
        return Output::failure(stateStatus(status).error());
    }
    if (schema.id_size > maxStateSchemaBytes)
        return Output::failure(error(ErrorCode::AbiProtocol, "state schema identifier exceeds budget"));
    StateSchema value{std::string(reinterpret_cast<const char*>(schema.id), schema.id_size), schema.version};
    if (!eve::detail::validStateSchema(value))
        return Output::failure(error(ErrorCode::AbiProtocol, "invalid state description"));
    return Output::success(std::move(value));
}
Result<std::string> readStateJson(const std::function<EveStatus(EveStateBufferV1*)>& callback)
{
    using Output = Result<std::string>;
    if (!callback)
        return Output::failure(error(ErrorCode::InvalidArgument, "empty state callback"));
    std::string json(maxStateJsonBytes, '\0');
    auto data = reinterpret_cast<std::uint8_t*>(json.data());
    EveStateBufferV1 buffer{sizeof(EveStateBufferV1), 0, data, maxStateJsonBytes, 0};
    EveStatus status;
    try {
        status = callback(&buffer);
    } catch (...) {
        return Output::failure(error(ErrorCode::AbiProtocol, "exception escaped state callback"));
    }
    if (buffer.struct_size != sizeof(buffer) || buffer.reserved || buffer.data != data ||
        buffer.capacity != maxStateJsonBytes || buffer.size > buffer.capacity || (status != EVE_OK && buffer.size))
        return Output::failure(error(ErrorCode::AbiProtocol, "state callback changed buffer contract"));
    auto accepted = stateStatus(status);
    if (!accepted.hasValue())
        return Output::failure(accepted.error());
    json.resize(static_cast<std::size_t>(buffer.size));
    if (!validAbiJson(json))
        return Output::failure(error(ErrorCode::AbiProtocol, "state output is not a UTF-8 JSON value"));
    return Output::success(std::move(json));
}
} // namespace eve::module::detail
