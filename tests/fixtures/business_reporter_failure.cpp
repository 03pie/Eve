/** @file business_reporter_failure.cpp
 * @brief Tests-only real Reporter start failure; reuse its original ABI, state table and monotonic IDs.
 */
#if defined(EVE_ABI_EXPORTS)
#error The delegated Reporter query must remain private to this translation unit.
#endif
#if !defined(EVE_EXAMPLE_REPORTER_FAIL_RECOVERY)
#error The failure policy must be explicit for each test artifact.
#endif

#define eve_query_v1 originalReporterQuery
#include "../../examples/native_services/reporter.cpp"
#undef eve_query_v1

namespace {
EveStatus EVE_CALL failureStart(EveHandle id, EveRequestId request) noexcept
{
    const auto* value = example::find(id);
    if (!value || !request || value->started)
        return EVE_E_INVALID_ARGUMENT;
    if (id == 2 || (EVE_EXAMPLE_REPORTER_FAIL_RECOVERY && id > 2))
        return EVE_E_FAILED;
    return start(id, request);
}
} // namespace

extern "C" __declspec(dllexport) EveStatus EVE_CALL eve_query_v1(std::uint32_t abi, const EveHostV1* input,
                                                                          EvePluginApiV1* output)
{
    const auto status = originalReporterQuery(abi, input, output);
    if (status == EVE_OK)
        output->start = failureStart;
    return status;
}
