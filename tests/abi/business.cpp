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

using namespace cordis;
using namespace cordis::module;
using namespace business_test;
namespace abi = cordis::module::detail;
extern "C" CordisStatus CORDIS_CALL cordis_business_fixture_finish(CordisHandle);
namespace {
std::function<void(CordisPluginApiV1&)> mutation;
unsigned queryCalls = 0;
CordisStatus queryStatus = CORDIS_OK;
bool throwQuery = false;
extern "C" CordisStatus CORDIS_CALL modifiedQuery(uint32_t major, const CordisHostV1* host, CordisPluginApiV1* api)
{
    ++queryCalls;
    if (throwQuery)
        throw 42;
    if (queryStatus != CORDIS_OK)
        return queryStatus;
    const auto result = cordis_query_v1(major, host, api);
    if (result == CORDIS_OK && mutation)
        mutation(*api);
    return result;
}
ModuleManifest manifest()
{
    return {"org.cordis.business", "1.0.0", "module.dll", {}, {"org.cordis.plugin"}};
}
void negotiation()
{
    for (const auto* host : {&baseHost, &fullHost}) {
        auto result = abi::queryBusiness(cordis_query_v1, *host, manifest());
        BIZ_CHECK(result.hasValue());
        BIZ_CHECK(result.value().table.capabilities == host->capabilities);
        BIZ_CHECK(result.value().moduleId == "org.cordis.business");
        BIZ_CHECK(result.value().plugins == manifest().plugins);
    }
    static const std::array<CordisHostV1, 2> optionalHosts{makeHost(CORDIS_CAP_DISPATCH), makeHost(CORDIS_CAP_EFFECTS)};
    for (const auto& host : optionalHosts) {
        BIZ_CHECK(abi::queryBusiness(cordis_query_v1, host, manifest()).hasValue());
    }
    struct ExtendedHost {
        CordisHostV1 host;
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
    mutation = [](auto& api) { api.required_host_capabilities = CORDIS_CAP_ALL; };
    BIZ_CHECK(abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    BIZ_CHECK(trace.logs.empty() && trace.pending.empty() && trace.completions.empty());
}
void hosts()
{
    const std::vector<std::function<void(CordisHostV1&)>> invalid{
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
        [](auto& h) { h.capabilities &= ~CORDIS_CAP_EFFECTS; }};
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
    BIZ_CHECK(!abi::validateHostApi(makeHost(CORDIS_CAP_SERVICES)).hasValue());
}
void plugins()
{
    const std::vector<std::function<void(CordisPluginApiV1&)>> invalid{
        [](auto& p) { --p.header.struct_size; },
        [](auto& p) { p.header.abi_major = 2; },
        [](auto& p) { p.header.reserved = 1; },
        [](auto& p) { p.reserved = 1; },
        [](auto& p) { p.capabilities |= UINT64_C(8); },
        [](auto& p) { p.required_host_capabilities = UINT64_C(8); },
        [](auto& p) { p.capabilities &= ~CORDIS_CAP_EFFECTS; },
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
            static const CordisBytesV1 ids[] = {{nullptr, 17}};
            p.plugins = ids;
        },
        [](auto& p) {
            static const CordisBytesV1 ids[] = {{reinterpret_cast<const uint8_t*>("unknown"), 7}};
            p.plugins = ids;
        }};
    for (const auto& change : invalid) {
        mutation = change;
        BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    }
    mutation = [](auto& p) {
        static const CordisBytesV1 ids[] = {{reinterpret_cast<const uint8_t*>("org.cordis.plugin"), 17},
                                            {reinterpret_cast<const uint8_t*>("org.cordis.plugin"), 17}};
        p.plugin_count = 2;
        p.plugins = ids;
    };
    auto two = manifest();
    two.plugins.push_back("org.cordis.second");
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, two).hasValue());
    mutation = [](auto& p) { p.capabilities = CORDIS_CAP_DISPATCH; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    mutation = [](auto& p) { p.dispatch = nullptr; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, fullHost, manifest()).hasValue());
    const auto available = abi::queryBusiness(cordis_query_v1, fullHost, manifest()).value().table;
    mutation = [available](auto& p) { p.dispatch = available.dispatch; };
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    mutation = {};
    for (auto status : {CORDIS_PENDING, INT32_C(2), -INT32_C(99)}) {
        queryStatus = status;
        auto result = abi::queryBusiness(modifiedQuery, baseHost, manifest());
        BIZ_CHECK(!result.hasValue() && result.error().code == ErrorCode::AbiProtocol);
    }
    queryStatus = CORDIS_E_UNSUPPORTED;
    BIZ_CHECK(!abi::queryBusiness(modifiedQuery, baseHost, manifest()).hasValue());
    queryStatus = CORDIS_OK;
    throwQuery = true;
    BIZ_CHECK(abi::queryBusiness(modifiedQuery, baseHost, manifest()).error().code == ErrorCode::AbiProtocol);
    BIZ_CHECK(!abi::queryBusiness(nullptr, baseHost, manifest()).hasValue());
}
void capacity()
{
    for (uint32_t size : {0U, 15U, 16U, 151U, 152U, 168U}) {
        struct Storage {
            CordisPluginApiV1 api;
            std::array<uint8_t, 40> tail;
        } output;
        std::memset(&output, 0xa5, sizeof(output));
        output.api.header.struct_size = size;
        auto* first = reinterpret_cast<const uint8_t*>(&output);
        const std::vector<uint8_t> before(first, first + sizeof(output));
        auto* api = &output.api;
        const auto status = cordis_query_v1(CORDIS_ABI_MAJOR, &baseHost, api);
        const auto written = size < sizeof(CordisTableHeaderV1) ? 0U : std::min(size, uint32_t(sizeof(*api)));
        BIZ_CHECK(std::equal(first + written, first + sizeof(output), before.begin() + written));
        BIZ_CHECK((status == CORDIS_OK) == (size >= sizeof(*api)));
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
            const auto api = abi::queryBusiness(cordis_query_v1, fullHost, manifest()).value();
            exercise(api.table, cordis_business_fixture_finish);
        } else
            throw std::runtime_error("unknown business test");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
