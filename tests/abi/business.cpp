/** @file business.cpp
 * @brief 无 Qt 的真实 C 表协商与有界值/生命周期夹具检查。
 */
#include "../../src/module/abi_p.hpp"
#include "business_flow.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>

using namespace eve;
using namespace eve::module;
using namespace business_test;
namespace abi = eve::module::detail;
extern "C" EveStatus EVE_CALL eve_business_fixture_finish(EveHandle);
namespace {
std::function<void(EvePluginApiV1&)> mutation;
unsigned queryCalls = 0;
EveStatus queryStatus = EVE_OK;
bool throwQuery = false;
extern "C" EveStatus EVE_CALL modifiedQuery(uint32_t major, const EveHostV1* host, EvePluginApiV1* api)
{
    ++queryCalls;
    if (throwQuery)
        throw 42;
    if (queryStatus != EVE_OK)
        return queryStatus;
    const auto result = eve_query_v1(major, host, api);
    if (result == EVE_OK && mutation)
        mutation(*api);
    return result;
}
ModuleManifest manifest()
{
    return {"org.eve.business", "1.0.0", "module.dll", {}, {"org.eve.plugin"}};
}
void negotiation()
{
    for (const auto* host : {&baseHost, &fullHost}) {
        auto result = abi::queryBusiness(eve_query_v1, *host, manifest());
        BIZ_CHECK(result.hasValue());
        BIZ_CHECK(result.value().table.capabilities == host->capabilities);
        BIZ_CHECK(result.value().moduleId == "org.eve.business");
        BIZ_CHECK(result.value().plugins == manifest().plugins);
    }
    static const std::array<EveHostV1, 2> optionalHosts{makeHost(EVE_CAP_DISPATCH), makeHost(EVE_CAP_EFFECTS)};
    for (const auto& host : optionalHosts) {
        BIZ_CHECK(abi::queryBusiness(eve_query_v1, host, manifest()).hasValue());
    }
    struct ExtendedHost {
        EveHostV1 host;
        uint64_t tail[2];
    };
    static const auto extended = [] {
        ExtendedHost value{baseHost, {}};
        value.host.header.struct_size = sizeof(value);
        value.host.header.abi_minor = 3;
        return value;
    }();
    mutation = [](auto& api) {
        api.header.abi_minor = 5;
        api.header.struct_size += 16;
    };
    BIZ_CHECK(abi::queryBusiness(modifiedQuery, extended.host, manifest()).hasValue());
    mutation = [](auto& api) { api.required_host_capabilities = EVE_CAP_ALL; };
    BIZ_CHECK(abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    BIZ_CHECK(trace.logs.empty() && trace.pending.empty() && trace.completions.empty());
}
void hosts()
{
    const std::vector<std::function<void(EveHostV1&)>> invalid{
        [](auto& h) { --h.header.struct_size; },
        [](auto& h) { h.header.abi_major = 2; },
        [](auto& h) { h.header.reserved = 1; },
        [](auto& h) { h.reserved = 1; },
        [](auto& h) { h.capabilities |= UINT64_C(8); },
        [](auto& h) { h.complete = nullptr; },
        [](auto& h) { h.log = nullptr; },
        [](auto& h) { h.request_cancel = nullptr; },
        [](auto& h) { h.dispatch = nullptr; },
        [](auto& h) { h.service_register = nullptr; },
        [](auto& h) { h.service_withdraw = nullptr; },
        [](auto& h) { h.service_invoke = nullptr; },
        [](auto& h) { h.effect_create = nullptr; },
        [](auto& h) { h.effect_ready = nullptr; },
        [](auto& h) { h.effect_request_cleanup = nullptr; },
        [](auto& h) { h.capabilities &= ~EVE_CAP_EFFECTS; }};
    for (const auto& change : invalid) {
        auto host = fullHost;
        change(host);
        const auto before = queryCalls;
        auto result = abi::queryBusiness(modifiedQuery, host, manifest());
        BIZ_CHECK(!result.hasValue() && result.error().phase == "module.abi.host" && queryCalls == before);
    }
    auto host = baseHost;
    host.dispatch = fullHost.dispatch;
    BIZ_CHECK(!abi::validateHostApi(host).hasValue());
    BIZ_CHECK(!abi::validateHostApi(makeHost(EVE_CAP_SERVICES)).hasValue());
}
void plugins()
{
    const std::vector<std::function<void(EvePluginApiV1&)>> invalid{
        [](auto& p) { --p.header.struct_size; },
        [](auto& p) { p.header.abi_major = 2; },
        [](auto& p) { p.header.reserved = 1; },
        [](auto& p) { p.reserved = 1; },
        [](auto& p) { p.capabilities |= UINT64_C(8); },
        [](auto& p) { p.required_host_capabilities = UINT64_C(8); },
        [](auto& p) { p.capabilities &= ~EVE_CAP_EFFECTS; },
        [](auto& p) { p.create = nullptr; },
        [](auto& p) { p.start = nullptr; },
        [](auto& p) { p.request_stop = nullptr; },
        [](auto& p) { p.destroy = nullptr; },
        [](auto& p) { p.cancel_request = nullptr; },
        [](auto& p) { p.request_complete = nullptr; },
        [](auto& p) { p.dispatch = nullptr; },
        [](auto& p) { p.invoke_service = nullptr; },
        [](auto& p) { p.request_cleanup = nullptr; },
        [](auto& p) { p.module_id.data = nullptr; },
        [](auto& p) { p.module_id.size = UINT64_MAX; },
        [](auto& p) { p.module_version.size = 1; },
        [](auto& p) { p.plugins = nullptr; },
        [](auto& p) { p.plugin_count = 0; },
        [](auto& p) { p.plugin_count = 65; },
        [](auto& p) {
            static const EveBytesV1 ids[] = {{nullptr, 17}};
            p.plugins = ids;
        },
        [](auto& p) {
            static const EveBytesV1 ids[] = {{reinterpret_cast<const uint8_t*>("unknown"), 7}};
            p.plugins = ids;
        }};
    for (const auto& change : invalid) {
        mutation = change;
        BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    }
    mutation = [](auto& p) {
        static const uint8_t pluginId[] = "org.eve.plugin";
        static const EveBytesV1 ids[] = {{pluginId, sizeof(pluginId) - 1}, {pluginId, sizeof(pluginId) - 1}};
        p.plugin_count = 2;
        p.plugins = ids;
    };
    auto two = manifest();
    two.plugins.push_back("org.eve.second");
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, two).hasValue());
    mutation = [](auto& p) { p.capabilities = EVE_CAP_DISPATCH; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    mutation = [](auto& p) { p.dispatch = nullptr; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    const auto available = abi::queryBusiness(eve_query_v1, fullHost, manifest()).value().table;
    mutation = [available](auto& p) { p.dispatch = available.dispatch; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    mutation = {};
    for (auto status : {EVE_PENDING, INT32_C(2), -INT32_C(99)}) {
        queryStatus = status;
        auto result = abi::queryBusiness(modifiedQuery, baseHost, manifest());
        BIZ_CHECK(!result.hasValue() && result.error().code == ErrorCode::AbiProtocol);
    }
    queryStatus = EVE_E_UNSUPPORTED;
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    queryStatus = EVE_OK;
    throwQuery = true;
    BIZ_CHECK(abi::queryBusiness(modifiedQuery, baseHost, manifest()).error().code == ErrorCode::AbiProtocol);
    BIZ_CHECK(!abi::queryBusiness(nullptr, baseHost, manifest()).hasValue());
}
void capacity()
{
    for (uint32_t size : {0U, 15U, 16U, 151U, 152U, 168U}) {
        struct Storage {
            EvePluginApiV1 api;
            std::array<uint8_t, 40> tail;
        } output;
        std::memset(&output, 0xa5, sizeof(output));
        output.api.header.struct_size = size;
        auto* first = reinterpret_cast<const uint8_t*>(&output);
        const std::vector<uint8_t> before(first, first + sizeof(output));
        auto* api = &output.api;
        const auto status = eve_query_v1(EVE_ABI_MAJOR, &baseHost, api);
        const auto written = size < sizeof(EveTableHeaderV1) ? 0U : std::min(size, uint32_t(sizeof(*api)));
        BIZ_CHECK(std::equal(first + written, first + sizeof(output), before.begin() + written));
        BIZ_CHECK((status == EVE_OK) == (size >= sizeof(*api)));
    }
}
} // namespace
int main(int argc, char** argv)
{
    try {
        BIZ_CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "negotiation")
            negotiation();
        else if (test == "hosts")
            hosts();
        else if (test == "plugins")
            plugins();
        else if (test == "capacity")
            capacity();
        else if (test == "lifecycle") {
            const auto api = abi::queryBusiness(eve_query_v1, fullHost, manifest()).value();
            exercise(api.table, eve_business_fixture_finish);
        } else
            throw std::runtime_error("unknown business test");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
