/** @file business_flow.hpp
 * @brief 测试宿主仅记录已发请求及复制值，不实现 Kernel/服务/effect 桥。
 */
#pragma once
#include <eve_abi/plugin.h>
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
inline EveBytesV1 bytes(const std::string& text)
{
    return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
struct Completion {
    EveRequestId id;
    EveStatus status;
    std::string data;
    std::thread::id thread;
};
struct Trace {
    std::mutex mutex;
    std::map<EveRequestId, EveHandle> pending;
    std::vector<Completion> completions;
    std::vector<std::string> logs;
};
inline Trace trace;
inline EveStatus EVE_CALL complete(EveHandle context, EveRequestId request, EveStatus status,
                                         EveBytesV1 result) noexcept
{
    std::lock_guard<std::mutex> lock(trace.mutex);
    auto found = trace.pending.find(request);
    if (found == trace.pending.end() || found->second != context || status > EVE_OK || status < EVE_E_BUSY)
        return EVE_E_ABI_PROTOCOL;
    if ((result.size && !result.data) || result.size > EVE_MAX_JSON_BYTES)
        return EVE_E_INVALID_ARGUMENT;
    std::string data;
    if (result.size)
        data.assign(reinterpret_cast<const char*>(result.data), static_cast<std::size_t>(result.size));
    trace.completions.push_back({request, status, std::move(data), std::this_thread::get_id()});
    trace.pending.erase(found);
    return EVE_OK;
}
inline EveStatus EVE_CALL log(EveHandle context, uint32_t level, EveBytesV1 text) noexcept
{
    if (context != 42 || level > EVE_LOG_ERROR || !text.data || text.size > EVE_MAX_LOG_BYTES)
        return EVE_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(trace.mutex);
    trace.logs.emplace_back(reinterpret_cast<const char*>(text.data), static_cast<std::size_t>(text.size));
    return EVE_OK;
}
inline EveStatus EVE_CALL cancel(EveHandle, EveRequestId) noexcept
{
    return EVE_E_NOT_FOUND;
}
/* Negotiation-only sentinels: these slots are never used as a production host. */
template <class... Args> EveStatus EVE_CALL unavailable(Args...) noexcept
{
    return EVE_E_UNSUPPORTED;
}
inline EveHostV1 makeHost(uint64_t capabilities = 0)
{
    EveHostV1 host{};
    host.header = {sizeof(host), EVE_ABI_MAJOR, EVE_ABI_MINOR, 0};
    host.capabilities = capabilities;
    host.complete = complete;
    host.log = log;
    host.request_cancel = cancel;
    if (capabilities & EVE_CAP_DISPATCH)
        host.dispatch = unavailable<EveHandle, EveHandle, EveBytesV1, EveRequestId*>;
    if (capabilities & EVE_CAP_SERVICES) {
        host.service_register = unavailable<EveHandle, EveHandle, const EveServiceKeyV1*,
                                            const EveServiceMetadataV1*, EveHandle, EveHandle*>;
        host.service_withdraw = unavailable<EveHandle, EveHandle, EveRequestId*>;
        host.service_invoke = unavailable<EveHandle, const EveServiceKeyV1*, uint32_t, EveBytesV1,
                                          EveBytesV1, EveBytesV1, EveRequestId*>;
    }
    if (capabilities & EVE_CAP_EFFECTS) {
        host.effect_create = unavailable<EveHandle, EveBytesV1, EveHandle, EveHandle*>;
        host.effect_ready = unavailable<EveHandle, EveHandle, EveStatus, EveBytesV1>;
        host.effect_request_cleanup = unavailable<EveHandle, EveHandle, EveRequestId*>;
    }
    return host;
}
/* Process-lifetime immutable tables, including in native resident-DLL tests. */
inline const EveHostV1 baseHost = makeHost();
inline const EveHostV1 fullHost = makeHost(EVE_CAP_ALL);
using Finish = EveStatus(EVE_CALL*)(EveHandle);
inline void issue(EveRequestId id)
{
    std::lock_guard<std::mutex> lock(trace.mutex);
    BIZ_CHECK(trace.pending.emplace(id, 42).second);
}
inline void exercise(const EvePluginApiV1& api, Finish finish)
{
    std::string config = "{\"value\":1}";
    const auto original = config;
    EveHandle instance = 99;
    BIZ_CHECK(api.create(0, bytes("org.eve.plugin"), bytes(config), &instance) == EVE_E_INVALID_ARGUMENT);
    BIZ_CHECK(instance == 0);
    BIZ_CHECK(api.create(42, bytes("org.eve.plugin"), bytes(config), &instance) == EVE_OK && instance);
    config.assign(config.size(), 'x');
    issue(100);
    BIZ_CHECK(api.start(instance, 100) == EVE_PENDING);
    BIZ_CHECK(trace.logs.size() == 1 && trace.logs[0] == original);
    BIZ_CHECK(api.cancel_request(instance, 100) == EVE_OK);
    BIZ_CHECK(trace.pending.size() == 1 && trace.completions.empty());
    BIZ_CHECK(api.request_stop(instance, 101) == EVE_E_BUSY);
    BIZ_CHECK(api.destroy(instance) == EVE_E_BUSY);
    EveStatus finished = EVE_E_FAILED;
    std::thread worker([&] { finished = finish(instance); });
    worker.join();
    BIZ_CHECK(finished == EVE_OK && trace.pending.empty() && trace.completions.size() == 1);
    BIZ_CHECK(trace.completions[0].status == EVE_E_CANCELLED && trace.completions[0].data == original);
    BIZ_CHECK(trace.completions[0].thread != std::this_thread::get_id());
    BIZ_CHECK(finish(instance) == EVE_E_NOT_FOUND);
    BIZ_CHECK(baseHost.complete(42, 100, EVE_OK, {}) == EVE_E_ABI_PROTOCOL);
    if (api.capabilities & EVE_CAP_SERVICES) {
        std::string payload = "{\"echo\":2}";
        const auto sent = payload;
        issue(102);
        BIZ_CHECK(api.invoke_service(instance, 7, bytes("echo"), bytes(payload), 102) == EVE_PENDING);
        payload.assign(payload.size(), 'y');
        BIZ_CHECK(finish(instance) == EVE_OK && trace.completions.back().data == sent);
    }
    if (api.capabilities & EVE_CAP_DISPATCH) {
        issue(103);
        BIZ_CHECK(api.dispatch(instance, 7, bytes("[3]"), 103) == EVE_PENDING);
        BIZ_CHECK(finish(instance) == EVE_OK && trace.completions.back().data == "[3]");
    }
    if (api.capabilities & EVE_CAP_EFFECTS)
        BIZ_CHECK(api.request_cleanup(instance, 1, 7, 104) == EVE_E_NOT_FOUND);
    BIZ_CHECK(api.request_complete(instance, 999, EVE_OK, {}) == EVE_E_NOT_FOUND);
    BIZ_CHECK(api.request_stop(instance, 105) == EVE_OK);
    BIZ_CHECK(api.destroy(instance) == EVE_OK);
    BIZ_CHECK(api.destroy(instance) == EVE_E_NOT_FOUND);
    BIZ_CHECK(trace.pending.empty());
    BIZ_CHECK(trace.completions[0].data == original);
    EveHandle next = 0;
    BIZ_CHECK(api.create(42, bytes("org.eve.plugin"), bytes("{}"), &next) == EVE_OK && next != instance);
    BIZ_CHECK(api.destroy(next) == EVE_OK);
}
} // namespace business_test
