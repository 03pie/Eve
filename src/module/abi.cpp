/** @file abi.cpp
 * @brief 有界业务表协商，不调用生命周期或向 Kernel 注册资源。
 */
#include "abi_p.hpp"
#include <cstring>
#include <set>

namespace cordis::module::detail {
namespace {
ErrorInfo failure(const char* phase, std::string message, ErrorCode code = ErrorCode::AbiMismatch)
{
    auto error = makeError(code, std::move(message));
    error.phase = phase;
    return error;
}
bool has(std::uint64_t capabilities, std::uint64_t bit)
{
    return (capabilities & bit) != 0;
}
bool validCapabilities(std::uint64_t capabilities)
{
    return !(capabilities & ~CORDIS_CAP_ALL) &&
           (!has(capabilities, CORDIS_CAP_SERVICES) || has(capabilities, CORDIS_CAP_EFFECTS));
}
bool matches(CordisBytesV1 bytes, const std::string& text, std::uint64_t maximum)
{
    return bytes.data && bytes.size > 0 && bytes.size <= maximum && bytes.size == text.size() &&
           std::memcmp(bytes.data, text.data(), text.size()) == 0;
}
bool finalStatus(CordisStatus status)
{
    return status <= CORDIS_OK && status >= CORDIS_E_BUSY;
}
} // namespace

Result<void> validateHostApi(const CordisHostV1& host)
{
    if (host.header.struct_size < sizeof(host) || host.header.abi_major != CORDIS_ABI_MAJOR || host.header.reserved ||
        host.reserved || !validCapabilities(host.capabilities))
        return Result<void>::failure(failure("module.abi.host", "incompatible host header or capabilities"));
    if (!host.complete || !host.log || !host.request_cancel ||
        has(host.capabilities, CORDIS_CAP_DISPATCH) != (host.dispatch != nullptr) ||
        has(host.capabilities, CORDIS_CAP_SERVICES) != (host.service_register != nullptr) ||
        has(host.capabilities, CORDIS_CAP_SERVICES) != (host.service_withdraw != nullptr) ||
        has(host.capabilities, CORDIS_CAP_SERVICES) != (host.service_invoke != nullptr) ||
        has(host.capabilities, CORDIS_CAP_EFFECTS) != (host.effect_create != nullptr) ||
        has(host.capabilities, CORDIS_CAP_EFFECTS) != (host.effect_ready != nullptr) ||
        has(host.capabilities, CORDIS_CAP_EFFECTS) != (host.effect_request_cleanup != nullptr))
        return Result<void>::failure(failure("module.abi.host", "host functions do not match capabilities"));
    return Result<void>::success();
}

Result<BusinessApi> queryBusiness(CordisQueryFnV1 query, const CordisHostV1& host, const ModuleManifest& manifest)
{
    auto validHost = validateHostApi(host);
    if (!validHost.hasValue())
        return Result<BusinessApi>::failure(validHost.error());
    if (!query)
        return Result<BusinessApi>::failure(failure("module.abi.query", "missing cordis_query_v1 entry"));
    CordisPluginApiV1 table{};
    table.header.struct_size = sizeof(table);
    CordisStatus status;
    try {
        status = query(CORDIS_ABI_MAJOR, &host, &table);
    } catch (...) {
        return Result<BusinessApi>::failure(
            failure("module.abi.query", "exception escaped business query", ErrorCode::AbiProtocol));
    }
    if (status != CORDIS_OK)
        return Result<BusinessApi>::failure(
            failure("module.abi.query", "business query rejected negotiation, status " + std::to_string(status),
                    finalStatus(status) ? ErrorCode::AbiMismatch : ErrorCode::AbiProtocol));
    if (table.header.struct_size < sizeof(table) || table.header.abi_major != CORDIS_ABI_MAJOR ||
        table.header.reserved || table.reserved || !validCapabilities(table.capabilities) ||
        (table.capabilities & ~host.capabilities) || (table.required_host_capabilities & ~table.capabilities))
        return Result<BusinessApi>::failure(failure("module.abi.table", "incompatible plugin header or capabilities"));
    if (!table.create || !table.start || !table.request_stop || !table.destroy || !table.cancel_request ||
        !table.request_complete || has(table.capabilities, CORDIS_CAP_DISPATCH) != (table.dispatch != nullptr) ||
        has(table.capabilities, CORDIS_CAP_SERVICES) != (table.invoke_service != nullptr) ||
        has(table.capabilities, CORDIS_CAP_EFFECTS) != (table.request_cleanup != nullptr))
        return Result<BusinessApi>::failure(failure("module.abi.table", "plugin functions do not match capabilities"));
    if (!matches(table.module_id, manifest.moduleId, CORDIS_MAX_ID_BYTES) ||
        !matches(table.module_version, manifest.moduleVersion, CORDIS_MAX_VERSION_BYTES) || table.plugin_count == 0 ||
        table.plugin_count > CORDIS_MAX_PLUGINS || table.plugin_count != manifest.plugins.size() || !table.plugins)
        return Result<BusinessApi>::failure(failure("module.abi.identity", "business identity differs from manifest"));
    BusinessApi result{table, manifest.moduleId, manifest.moduleVersion, {}};
    std::set<std::string> remaining(manifest.plugins.begin(), manifest.plugins.end());
    for (std::uint32_t i = 0; i < table.plugin_count; ++i) {
        const auto bytes = table.plugins[i];
        if (!bytes.data || bytes.size == 0 || bytes.size > CORDIS_MAX_ID_BYTES)
            return Result<BusinessApi>::failure(failure("module.abi.identity", "invalid plugin identifier span"));
        std::string id(reinterpret_cast<const char*>(bytes.data), static_cast<std::size_t>(bytes.size));
        if (remaining.erase(id) != 1)
            return Result<BusinessApi>::failure(
                failure("module.abi.identity", "unknown or duplicate plugin identifier"));
        result.plugins.push_back(std::move(id));
    }
    return Result<BusinessApi>::success(std::move(result));
}
} // namespace cordis::module::detail
