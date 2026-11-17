/** @file state_wire.cpp
 * @brief 真实宿主缓冲区的协议边界；只验证值层，不冒充后端实例集成。
 */
#include "../../src/module/state_wire_p.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace cordis;
using namespace cordis::module::detail;
extern "C" int stateWireLayout(void);
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
namespace {
CordisStatus CORDIS_CALL describe(CordisBytesV1, CordisStateSchemaV1*)
{
    return CORDIS_E_UNSUPPORTED;
}
CordisStatus CORDIS_CALL validate(CordisBytesV1, const CordisStateSchemaV1*, CordisBytesV1, CordisStateBufferV1*)
{
    return CORDIS_E_FAILED;
}
CordisStatus CORDIS_CALL capture(CordisHandle, CordisStateBufferV1*)
{
    return CORDIS_E_BUSY;
}
CordisStatus CORDIS_CALL restore(CordisHandle, const CordisStateSchemaV1*, CordisBytesV1)
{
    return CORDIS_OK;
}
int queryMode = 0;
CordisStatus CORDIS_CALL query(std::uint32_t major, CordisStateApiV1* api)
{
    CHECK(major == CORDIS_STATE_ABI_MAJOR && api->header.struct_size == sizeof(*api));
    if (queryMode == 1)
        return CORDIS_PENDING;
    if (queryMode == 2)
        throw std::runtime_error("query");
    if (queryMode == 3)
        return CORDIS_E_UNSUPPORTED;
    *api = {{sizeof(*api), CORDIS_STATE_ABI_MAJOR, CORDIS_STATE_ABI_MINOR, 0}, describe, validate, capture, restore};
    if (queryMode == 4)
        api->capture = nullptr;
    if (queryMode == 5)
        api->header.reserved = 1;
    if (queryMode == 6)
        api->header.struct_size = sizeof(CordisTableHeaderV1);
    if (queryMode == 7)
        api->header.abi_major = 2;
    return CORDIS_OK;
}
void tables()
{
    CHECK(stateWireLayout());
    auto api = queryStateApi(query).value();
    CHECK(api.capture == capture && api.restore == restore);
    CHECK(!queryStateApi(nullptr).hasValue());
    for (queryMode = 1; queryMode <= 7; ++queryMode) {
        const auto result = queryStateApi(query);
        CHECK(!result.hasValue());
        CHECK(result.error().code == (queryMode <= 2   ? ErrorCode::AbiProtocol
                                      : queryMode == 3 ? ErrorCode::ConfigInvalid
                                                       : ErrorCode::AbiMismatch));
    }
}
void schemas()
{
    StateSchema schema{std::string(maxStateSchemaBytes, 'a'), UINT32_MAX};
    const auto encoded = encodeStateSchema(schema).value();
    auto result = readStateSchema([&](CordisStateSchemaV1* output) {
                      *output = encoded;
                      return CORDIS_OK;
                  }).value();
    CHECK(result && result->id == schema.id && result->version == schema.version);
    CHECK(!readStateSchema([](CordisStateSchemaV1*) { return CORDIS_E_UNSUPPORTED; }).value());
    CHECK(encodeStateSchema({"", 1}).error().code == ErrorCode::InvalidArgument);
    CHECK(encodeStateSchema({"valid", 0}).error().code == ErrorCode::InvalidArgument);
    CHECK(encodeStateSchema({"has space", 1}).error().code == ErrorCode::InvalidArgument);
    CHECK(!readStateSchema({}).hasValue());
    for (int mode = 0; mode < 8; ++mode) {
        auto invalid = readStateSchema([&](CordisStateSchemaV1* output) -> CordisStatus {
            *output = encoded;
            if (mode == 0)
                output->reserved = 1;
            if (mode == 1)
                output->struct_size = 0;
            if (mode == 2)
                output->version = 0;
            if (mode == 3)
                output->id_size = maxStateSchemaBytes + 1;
            if (mode == 4)
                output->id[0] = 0;
            if (mode == 5)
                output->id[0] = 0xff;
            if (mode == 6)
                return CORDIS_E_UNSUPPORTED;
            if (mode == 7)
                throw std::runtime_error("describe");
            return CORDIS_OK;
        });
        CHECK(invalid.error().code == ErrorCode::AbiProtocol);
    }
}
void payloads()
{
    const std::string text = "{\"value\":9007199254740993,\"list\":[null,true]}";
    int calls = 0;
    auto result = readStateJson([&](CordisStateBufferV1* buffer) {
        ++calls;
        CHECK(buffer->capacity == maxStateJsonBytes && buffer->size == 0 && buffer->reserved == 0);
        std::memcpy(buffer->data, text.data(), text.size());
        buffer->size = text.size();
        return CORDIS_OK;
    });
    CHECK(calls == 1 && result.value() == text);
    const std::string maximum = "\"" + std::string(maxStateJsonBytes - 2, '0') + "\"";
    CHECK(readStateJson([&](CordisStateBufferV1* buffer) {
              std::memcpy(buffer->data, maximum.data(), maximum.size());
              buffer->size = maximum.size();
              return CORDIS_OK;
          }).value() == maximum);
    CHECK(!readStateJson({}).hasValue());
}
void faults()
{
    for (int mode = 0; mode < 12; ++mode) {
        auto result = readStateJson([&](CordisStateBufferV1* buffer) -> CordisStatus {
            std::memcpy(buffer->data, "null", 4);
            buffer->size = 4;
            if (mode == 0)
                buffer->data = nullptr;
            if (mode == 1)
                ++buffer->capacity;
            if (mode == 2)
                buffer->reserved = 1;
            if (mode == 3)
                buffer->struct_size = 0;
            if (mode == 4)
                buffer->size = buffer->capacity + 1;
            if (mode == 5)
                buffer->size = 0;
            if (mode == 6)
                buffer->data[0] = 0xff;
            if (mode == 7)
                buffer->data[0] = '{';
            if (mode == 8)
                return CORDIS_E_FAILED;
            if (mode == 9) {
                buffer->size = 0;
                return CORDIS_PENDING;
            }
            if (mode == 10) {
                buffer->size = 0;
                return -255;
            }
            if (mode == 11)
                throw std::runtime_error("capture");
            return CORDIS_OK;
        });
        CHECK(result.error().code == ErrorCode::AbiProtocol);
    }
}
void statuses()
{
    CHECK(stateStatus(CORDIS_OK).hasValue());
    const std::vector<std::pair<CordisStatus, ErrorCode>> examples = {
        {CORDIS_E_BUSY, ErrorCode::ConfigConflict},
        {CORDIS_E_CLOSED, ErrorCode::ContextClosing},
        {CORDIS_E_CANCELLED, ErrorCode::OperationCancelled},
        {CORDIS_E_LIMIT, ErrorCode::InvalidArgument},
        {CORDIS_E_FAILED, ErrorCode::ConfigInvalid},
        {CORDIS_E_ABI_PROTOCOL, ErrorCode::AbiProtocol},
        {CORDIS_PENDING, ErrorCode::AbiProtocol},
        {99, ErrorCode::AbiProtocol}};
    for (const auto& example : examples) {
        CHECK(stateStatus(example.first).error().code == example.second);
        CHECK(readStateJson([&](CordisStateBufferV1*) { return example.first; }).error().code == example.second);
    }
}
} // namespace
int main(int argc, char** argv)
{
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "tables")
            tables();
        else if (name == "schemas")
            schemas();
        else if (name == "payloads")
            payloads();
        else if (name == "faults")
            faults();
        else if (name == "statuses")
            statuses();
        else
            CHECK(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
