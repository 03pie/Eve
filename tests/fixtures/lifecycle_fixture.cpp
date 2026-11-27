/** @file lifecycle_fixture.cpp
 * @brief S06/S07 专用真实 DLL；控制导出仅用于夹具，不加入业务示例或 SDK。
 */
#include <array>
#include <eve_abi/plugin.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

namespace {
const EveHostV1* host = nullptr;
EveHandle context = 0, effect = 0;
EveRequestId pendingStop = 0, outgoing = 0;
bool alive = false, started = false, asynchronousStop = false, failCleanup = false, cancelled = false;
unsigned queries = 0, bodies = 0, destructors = 0, acks = 0, stops = 0, cancels = 0, destroys = 0, cleanups = 0;
EveStatus ackStatus = EVE_E_FAILED;
std::function<void()> callback;
template <std::size_t N> EveBytesV1 bytes(const char (&value)[N]) noexcept
{
    return {reinterpret_cast<const uint8_t*>(value), N - 1};
}
bool equals(EveBytesV1 value, const char* expected) noexcept
{
    const auto size = std::strlen(expected);
    return value.size == size && value.data && std::memcmp(value.data, expected, size) == 0;
}
struct Capture {
    explicit Capture(EveHandle value) : context(value) {}
    EveHandle context;
    ~Capture()
    {
        ++destructors;
        (void)host->log(context, EVE_LOG_INFO, bytes("DLL capture destroyed"));
    }
};
EveStatus EVE_CALL create(EveHandle owner, EveBytesV1 plugin, EveBytesV1 config,
                                EveHandle* output) noexcept
{
    if (!output)
        return EVE_E_INVALID_ARGUMENT;
    *output = 0;
    if (alive || !owner || !equals(plugin, "org.eve.plugin") ||
        (!equals(config, "\"stop\"") && !equals(config, "\"capture\"") && !equals(config, "\"capture-fail\"")))
        return EVE_E_INVALID_ARGUMENT;
    context = owner;
    asynchronousStop = equals(config, "\"stop\"");
    failCleanup = equals(config, "\"capture-fail\"");
    alive = true;
    *output = 1;
    return EVE_OK;
}
EveStatus EVE_CALL start(EveHandle id, EveRequestId request) noexcept
{
    if (!alive || id != 1 || !request || started)
        return EVE_E_INVALID_ARGUMENT;
    started = true;
    if (asynchronousStop)
        return EVE_OK;
    if (!host->effect_create || !host->effect_ready)
        return EVE_E_UNSUPPORTED;
    auto status = host->effect_create(context, bytes("DLL capture"), 1, &effect);
    return status == EVE_OK ? host->effect_ready(context, effect, EVE_OK, {}) : status;
}
EveStatus EVE_CALL stop(EveHandle id, EveRequestId request) noexcept
{
    if (!alive || id != 1 || !request || outgoing || pendingStop)
        return EVE_E_BUSY;
    ++stops;
    if (asynchronousStop) {
        pendingStop = request;
        return EVE_PENDING;
    }
    started = false;
    return EVE_OK;
}
EveStatus EVE_CALL cancel(EveHandle id, EveRequestId request) noexcept
{
    if (!alive || id != 1 || request != pendingStop || !request)
        return EVE_E_NOT_FOUND;
    ++cancels;
    cancelled = true;
    return EVE_OK; // Cancellation does not waive the later completion.
}
EveStatus EVE_CALL destroy(EveHandle id) noexcept
{
    if (!alive || id != 1 || started || pendingStop || outgoing || effect || callback)
        return EVE_E_BUSY;
    ++destroys;
    alive = false;
    return EVE_OK;
}
EveStatus EVE_CALL ack(EveHandle id, EveRequestId request, EveStatus status, EveBytesV1) noexcept
{
    if (!alive || id != 1 || request != outgoing || !request)
        return EVE_E_NOT_FOUND;
    outgoing = 0;
    ++acks;
    ackStatus = status;
    return EVE_OK; // The lambda and its captured object remain owned until cleanup.
}
EveStatus EVE_CALL dispatch(EveHandle id, EveHandle token, EveBytesV1, EveRequestId request) noexcept
{
    if (!alive || !started || id != 1 || token != 7 || !request || !callback)
        return EVE_E_INVALID_ARGUMENT;
    callback();
    return EVE_OK;
}
EveStatus EVE_CALL cleanup(EveHandle id, EveHandle ownedEffect, EveHandle token,
                                 EveRequestId request) noexcept
{
    if (!alive || id != 1 || ownedEffect != effect || !effect || token != 1 || !request || outgoing)
        return EVE_E_INVALID_ARGUMENT;
    ++cleanups;
    if (failCleanup)
        return EVE_E_FAILED;
    callback = {}; // std::function, lambda and final Capture destructor all execute inside this DLL.
    effect = 0;
    return EVE_OK;
}
} // namespace
extern "C" EVE_EXPORT unsigned EVE_CALL eve_business_fixture_queries() noexcept
{
    return queries;
}
extern "C" EVE_EXPORT unsigned EVE_CALL eve_business_fixture_finish_all() noexcept
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
        host->complete(context, request, EVE_OK, {reinterpret_cast<const uint8_t*>(json), std::strlen(json)});
    std::memset(json, 'x', sizeof(json)); // The production channel must already own its result copy.
    return status == EVE_OK ? 1u : 0u;
}
extern "C" EVE_EXPORT EveStatus EVE_CALL eve_lifecycle_queue() noexcept
{
    try {
        if (!alive || !started || !effect || callback || outgoing)
            return EVE_E_CLOSED;
        callback = [capture = std::make_shared<Capture>(context)] {
            ++bodies;
            (void)host->log(capture->context, EVE_LOG_INFO, bytes("DLL callback ran"));
        };
        const auto status = host->dispatch(context, 7, bytes("{\"payload\":1}"), &outgoing);
        if (status != EVE_PENDING)
            callback = {};
        return status;
    } catch (...) {
        return EVE_E_FAILED;
    }
}
extern "C" EVE_EXPORT std::uint64_t EVE_CALL eve_lifecycle_stat(unsigned field) noexcept
{
    const std::array<std::uint64_t, 10> values{
        bodies,   destructors, acks,          static_cast<std::uint64_t>(ackStatus), stops, pendingStop != 0, cancels,
        destroys, cleanups,    bool(callback)};
    return field < values.size() ? values[field] : 0;
}
extern "C" EVE_EXPORT EveStatus EVE_CALL eve_query_v1(std::uint32_t abi, const EveHostV1* input,
                                                                  EvePluginApiV1* output)
{
    ++queries;
    if (!input || !output || abi != EVE_ABI_MAJOR || input->header.struct_size < sizeof(*input) ||
        input->header.abi_major != EVE_ABI_MAJOR || input->reserved || input->header.reserved || !input->complete ||
        !input->log || !input->request_cancel)
        return EVE_E_UNSUPPORTED;
    if (output->header.struct_size < sizeof(*output))
        return EVE_E_INVALID_ARGUMENT;
    EvePluginApiV1 api{};
    static const EveBytesV1 plugins[] = {bytes("org.eve.plugin")};
    api.header = {sizeof(api), EVE_ABI_MAJOR, EVE_ABI_MINOR, 0};
    api.capabilities = input->capabilities & (EVE_CAP_DISPATCH | EVE_CAP_EFFECTS);
    api.required_host_capabilities = api.capabilities;
    api.module_id = bytes("org.eve.lifecycle");
    api.module_version = bytes("1.0.0");
    api.plugin_count = 1;
    api.plugins = plugins;
    api.create = create;
    api.start = start;
    api.request_stop = stop;
    api.destroy = destroy;
    api.cancel_request = cancel;
    api.request_complete = ack;
    if (api.capabilities & EVE_CAP_DISPATCH)
        api.dispatch = dispatch;
    if (api.capabilities & EVE_CAP_EFFECTS)
        api.request_cleanup = cleanup;
    std::memcpy(output, &api, sizeof(api));
    host = input;
    return EVE_OK;
}
