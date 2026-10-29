/** @file business_flow.hpp
 * @brief 测试宿主仅记录已发请求及复制值，不实现 Kernel/服务/effect 桥。
 */
#pragma once
#include <cordis_abi/plugin.h>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace business_test {
inline void check(bool value, const char* expression)
{
    if (!value)
        throw std::runtime_error(expression);
}
#define BIZ_CHECK(x) ::business_test::check((x), #x)
inline CordisBytesV1 bytes(const std::string& text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
struct Completion {
    CordisRequestId id;
    CordisStatus status;
    std::string data;
    std::thread::id thread;
};
struct Trace {
    std::mutex mutex;
    std::map<CordisRequestId, CordisHandle> pending;
    std::vector<Completion> completions;
    std::vector<std::string> logs;
};
inline Trace trace;
inline CordisStatus CORDIS_CALL complete(CordisHandle context, CordisRequestId request, CordisStatus status,
                                         CordisBytesV1 result) noexcept
{
    std::lock_guard<std::mutex> lock(trace.mutex);
    auto found = trace.pending.find(request);
    if (found == trace.pending.end() || found->second != context || status > CORDIS_OK || status < CORDIS_E_BUSY)
        return CORDIS_E_ABI_PROTOCOL;
    if ((result.size && !result.data) || result.size > CORDIS_MAX_JSON_BYTES)
        return CORDIS_E_INVALID_ARGUMENT;
    std::string data;
    if (result.size)
        data.assign(reinterpret_cast<const char*>(result.data), static_cast<std::size_t>(result.size));
    trace.completions.push_back({request, status, std::move(data), std::this_thread::get_id()});
    trace.pending.erase(found);
    return CORDIS_OK;
}
inline CordisStatus CORDIS_CALL log(CordisHandle context, uint32_t level, CordisBytesV1 text) noexcept
{
    if (context != 42 || level > CORDIS_LOG_ERROR || !text.data || text.size > CORDIS_MAX_LOG_BYTES)
        return CORDIS_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(trace.mutex);
    trace.logs.emplace_back(reinterpret_cast<const char*>(text.data), static_cast<std::size_t>(text.size));
    return CORDIS_OK;
}
inline CordisStatus CORDIS_CALL cancel(CordisHandle, CordisRequestId) noexcept
{
    return CORDIS_E_NOT_FOUND;
}
/* Negotiation-only sentinels: these slots are never used as a production host. */
template <class... Args> CordisStatus CORDIS_CALL unavailable(Args...) noexcept
{
    return CORDIS_E_UNSUPPORTED;
}
inline CordisHostV1 makeHost(uint64_t capabilities = 0)
{
    CordisHostV1 host{};
    host.header = {sizeof(host), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0};
    host.capabilities = capabilities;
    host.complete = complete;
    host.log = log;
    host.request_cancel = cancel;
    if (capabilities & CORDIS_CAP_DISPATCH)
        host.dispatch = unavailable<CordisHandle, CordisHandle, CordisBytesV1, CordisRequestId*>;
    if (capabilities & CORDIS_CAP_SERVICES) {
        host.service_register = unavailable<CordisHandle, CordisHandle, const CordisServiceKeyV1*,
                                            const CordisServiceMetadataV1*, CordisHandle, CordisHandle*>;
        host.service_withdraw = unavailable<CordisHandle, CordisHandle, CordisRequestId*>;
        host.service_invoke = unavailable<CordisHandle, const CordisServiceKeyV1*, uint32_t, CordisBytesV1,
                                          CordisBytesV1, CordisBytesV1, CordisRequestId*>;
    }
    if (capabilities & CORDIS_CAP_EFFECTS) {
        host.effect_create = unavailable<CordisHandle, CordisBytesV1, CordisHandle, CordisHandle*>;
        host.effect_ready = unavailable<CordisHandle, CordisHandle, CordisStatus, CordisBytesV1>;
        host.effect_request_cleanup = unavailable<CordisHandle, CordisHandle, CordisRequestId*>;
    }
    return host;
}
/* Process-lifetime immutable tables, including in native resident-DLL tests. */
inline const CordisHostV1 baseHost = makeHost();
inline const CordisHostV1 fullHost = makeHost(CORDIS_CAP_ALL);
using Finish = CordisStatus(CORDIS_CALL*)(CordisHandle);
inline void issue(CordisRequestId id)
{
    std::lock_guard<std::mutex> lock(trace.mutex);
    BIZ_CHECK(trace.pending.emplace(id, 42).second);
}
inline void exercise(const CordisPluginApiV1& api, Finish finish)
{
    std::string config = "{\"value\":1}";
    const auto original = config;
    CordisHandle instance = 99;
    BIZ_CHECK(api.create(0, bytes("org.cordis.plugin"), bytes(config), &instance) == CORDIS_E_INVALID_ARGUMENT);
    BIZ_CHECK(instance == 0);
    BIZ_CHECK(api.create(42, bytes("org.cordis.plugin"), bytes(config), &instance) == CORDIS_OK && instance);
    config.assign(config.size(), 'x');
    issue(100);
    BIZ_CHECK(api.start(instance, 100) == CORDIS_PENDING);
    BIZ_CHECK(trace.logs.size() == 1 && trace.logs[0] == original);
    BIZ_CHECK(api.cancel_request(instance, 100) == CORDIS_OK);
    BIZ_CHECK(trace.pending.size() == 1 && trace.completions.empty());
    BIZ_CHECK(api.request_stop(instance, 101) == CORDIS_E_BUSY);
    BIZ_CHECK(api.destroy(instance) == CORDIS_E_BUSY);
    CordisStatus finished = CORDIS_E_FAILED;
    std::thread worker([&] { finished = finish(instance); });
    worker.join();
    BIZ_CHECK(finished == CORDIS_OK && trace.pending.empty() && trace.completions.size() == 1);
    BIZ_CHECK(trace.completions[0].status == CORDIS_E_CANCELLED && trace.completions[0].data == original);
    BIZ_CHECK(trace.completions[0].thread != std::this_thread::get_id());
    BIZ_CHECK(finish(instance) == CORDIS_E_NOT_FOUND);
    BIZ_CHECK(baseHost.complete(42, 100, CORDIS_OK, {}) == CORDIS_E_ABI_PROTOCOL);
    if (api.capabilities & CORDIS_CAP_SERVICES) {
        std::string payload = "{\"echo\":2}";
        const auto sent = payload;
        issue(102);
        BIZ_CHECK(api.invoke_service(instance, 7, bytes("echo"), bytes(payload), 102) == CORDIS_PENDING);
        payload.assign(payload.size(), 'y');
        BIZ_CHECK(finish(instance) == CORDIS_OK && trace.completions.back().data == sent);
    }
    if (api.capabilities & CORDIS_CAP_DISPATCH) {
        issue(103);
        BIZ_CHECK(api.dispatch(instance, 7, bytes("[3]"), 103) == CORDIS_PENDING);
        BIZ_CHECK(finish(instance) == CORDIS_OK && trace.completions.back().data == "[3]");
    }
    if (api.capabilities & CORDIS_CAP_EFFECTS)
        BIZ_CHECK(api.request_cleanup(instance, 1, 7, 104) == CORDIS_E_NOT_FOUND);
    BIZ_CHECK(api.request_complete(instance, 999, CORDIS_OK, {}) == CORDIS_E_NOT_FOUND);
    BIZ_CHECK(api.request_stop(instance, 105) == CORDIS_OK);
    BIZ_CHECK(api.destroy(instance) == CORDIS_OK);
    BIZ_CHECK(api.destroy(instance) == CORDIS_E_NOT_FOUND);
    BIZ_CHECK(trace.pending.empty());
    BIZ_CHECK(trace.completions[0].data == original);
    CordisHandle next = 0;
    BIZ_CHECK(api.create(42, bytes("org.cordis.plugin"), bytes("{}"), &next) == CORDIS_OK && next != instance);
    BIZ_CHECK(api.destroy(next) == CORDIS_OK);
}
} // namespace business_test
