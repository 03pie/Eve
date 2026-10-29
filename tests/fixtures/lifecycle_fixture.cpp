/** @file lifecycle_fixture.cpp
 * @brief S06/S07 专用真实 DLL；控制导出仅用于夹具，不加入业务示例或 SDK。
 */
#include <array>
#include <cordis_abi/plugin.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

namespace {
const CordisHostV1* host = nullptr;
CordisHandle context = 0, effect = 0;
CordisRequestId pendingStop = 0, outgoing = 0;
bool alive = false, started = false, asynchronousStop = false, failCleanup = false, cancelled = false;
unsigned queries = 0, bodies = 0, destructors = 0, acks = 0, stops = 0, cancels = 0, destroys = 0, cleanups = 0;
CordisStatus ackStatus = CORDIS_E_FAILED;
std::function<void()> callback;
template <std::size_t N> CordisBytesV1 bytes(const char (&value)[N]) noexcept
{
    return {reinterpret_cast<const uint8_t*>(value), N - 1};
}
bool equals(CordisBytesV1 value, const char* expected) noexcept
{
    const auto size = std::strlen(expected);
    return value.size == size && value.data && std::memcmp(value.data, expected, size) == 0;
}
struct Capture {
    explicit Capture(CordisHandle value) : context(value) {}
    CordisHandle context;
    ~Capture()
    {
        ++destructors;
        (void)host->log(context, CORDIS_LOG_INFO, bytes("DLL capture destroyed"));
    }
};
CordisStatus CORDIS_CALL create(CordisHandle owner, CordisBytesV1 plugin, CordisBytesV1 config,
                                CordisHandle* output) noexcept
{
    if (!output)
        return CORDIS_E_INVALID_ARGUMENT;
    *output = 0;
    if (alive || !owner || !equals(plugin, "org.cordis.plugin") ||
        (!equals(config, "\"stop\"") && !equals(config, "\"capture\"") && !equals(config, "\"capture-fail\"")))
        return CORDIS_E_INVALID_ARGUMENT;
    context = owner;
    asynchronousStop = equals(config, "\"stop\"");
    failCleanup = equals(config, "\"capture-fail\"");
    alive = true;
    *output = 1;
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL start(CordisHandle id, CordisRequestId request) noexcept
{
    if (!alive || id != 1 || !request || started)
        return CORDIS_E_INVALID_ARGUMENT;
    started = true;
    if (asynchronousStop)
        return CORDIS_OK;
    if (!host->effect_create || !host->effect_ready)
        return CORDIS_E_UNSUPPORTED;
    auto status = host->effect_create(context, bytes("DLL capture"), 1, &effect);
    return status == CORDIS_OK ? host->effect_ready(context, effect, CORDIS_OK, {}) : status;
}
CordisStatus CORDIS_CALL stop(CordisHandle id, CordisRequestId request) noexcept
{
    if (!alive || id != 1 || !request || outgoing || pendingStop)
        return CORDIS_E_BUSY;
    ++stops;
    if (asynchronousStop) {
        pendingStop = request;
        return CORDIS_PENDING;
    }
    started = false;
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL cancel(CordisHandle id, CordisRequestId request) noexcept
{
    if (!alive || id != 1 || request != pendingStop || !request)
        return CORDIS_E_NOT_FOUND;
    ++cancels;
    cancelled = true;
    return CORDIS_OK; // Cancellation does not waive the later completion.
}
CordisStatus CORDIS_CALL destroy(CordisHandle id) noexcept
{
    if (!alive || id != 1 || started || pendingStop || outgoing || effect || callback)
        return CORDIS_E_BUSY;
    ++destroys;
    alive = false;
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL ack(CordisHandle id, CordisRequestId request, CordisStatus status, CordisBytesV1) noexcept
{
    if (!alive || id != 1 || request != outgoing || !request)
        return CORDIS_E_NOT_FOUND;
    outgoing = 0;
    ++acks;
    ackStatus = status;
    return CORDIS_OK; // The lambda and its captured object remain owned until cleanup.
}
CordisStatus CORDIS_CALL dispatch(CordisHandle id, CordisHandle token, CordisBytesV1, CordisRequestId request) noexcept
{
    if (!alive || !started || id != 1 || token != 7 || !request || !callback)
        return CORDIS_E_INVALID_ARGUMENT;
    callback();
    return CORDIS_OK;
}
CordisStatus CORDIS_CALL cleanup(CordisHandle id, CordisHandle ownedEffect, CordisHandle token,
                                 CordisRequestId request) noexcept
{
    if (!alive || id != 1 || ownedEffect != effect || !effect || token != 1 || !request || outgoing)
        return CORDIS_E_INVALID_ARGUMENT;
    ++cleanups;
    if (failCleanup)
        return CORDIS_E_FAILED;
    callback = {}; // std::function, lambda and final Capture destructor all execute inside this DLL.
    effect = 0;
    return CORDIS_OK;
}
} // namespace
extern "C" CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_queries() noexcept
{
    return queries;
}
extern "C" CORDIS_EXPORT unsigned CORDIS_CALL cordis_business_fixture_finish_all() noexcept
{
    if (!pendingStop)
        return 0;
    const auto request = pendingStop;
    pendingStop = 0;
    started = false;
    char json[] = "{\"cancelled\":false}";
    if (cancelled)
        std::memcpy(json, "{\"cancelled\":true}", sizeof("{\"cancelled\":true}"));
    const auto status =
        host->complete(context, request, CORDIS_OK, {reinterpret_cast<const uint8_t*>(json), std::strlen(json)});
    std::memset(json, 'x', sizeof(json)); // The production channel must already own its result copy.
    return status == CORDIS_OK ? 1u : 0u;
}
extern "C" CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_lifecycle_queue() noexcept
{
    try {
        if (!alive || !started || !effect || callback || outgoing)
            return CORDIS_E_CLOSED;
        callback = [capture = std::make_shared<Capture>(context)] {
            ++bodies;
            (void)host->log(capture->context, CORDIS_LOG_INFO, bytes("DLL callback ran"));
        };
        const auto status = host->dispatch(context, 7, bytes("{\"payload\":1}"), &outgoing);
        if (status != CORDIS_PENDING)
            callback = {};
        return status;
    } catch (...) {
        return CORDIS_E_FAILED;
    }
}
extern "C" CORDIS_EXPORT std::uint64_t CORDIS_CALL cordis_lifecycle_stat(unsigned field) noexcept
{
    const std::array<std::uint64_t, 10> values{
        bodies,   destructors, acks,          static_cast<std::uint64_t>(ackStatus), stops, pendingStop != 0, cancels,
        destroys, cleanups,    bool(callback)};
    return field < values.size() ? values[field] : 0;
}
extern "C" CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_query_v1(std::uint32_t abi, const CordisHostV1* input,
                                                                  CordisPluginApiV1* output)
{
    ++queries;
    if (!input || !output || abi != CORDIS_ABI_MAJOR || input->header.struct_size < sizeof(*input) ||
        input->header.abi_major != CORDIS_ABI_MAJOR || input->reserved || input->header.reserved || !input->complete ||
        !input->log || !input->request_cancel)
        return CORDIS_E_UNSUPPORTED;
    if (output->header.struct_size < sizeof(*output))
        return CORDIS_E_INVALID_ARGUMENT;
    CordisPluginApiV1 api{};
    static const CordisBytesV1 plugins[] = {bytes("org.cordis.plugin")};
    api.header = {sizeof(api), CORDIS_ABI_MAJOR, CORDIS_ABI_MINOR, 0};
    api.capabilities = input->capabilities & (CORDIS_CAP_DISPATCH | CORDIS_CAP_EFFECTS);
    api.required_host_capabilities = api.capabilities;
    api.module_id = bytes("org.cordis.lifecycle");
    api.module_version = bytes("1.0.0");
    api.plugin_count = 1;
    api.plugins = plugins;
    api.create = create;
    api.start = start;
    api.request_stop = stop;
    api.destroy = destroy;
    api.cancel_request = cancel;
    api.request_complete = ack;
    if (api.capabilities & CORDIS_CAP_DISPATCH)
        api.dispatch = dispatch;
    if (api.capabilities & CORDIS_CAP_EFFECTS)
        api.request_cleanup = cleanup;
    std::memcpy(output, &api, sizeof(api));
    host = input;
    return CORDIS_OK;
}
