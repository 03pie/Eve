/** @file business_reporter_failure.cpp
 * @brief Tests-only real Reporter start failure; reuse its original ABI, state table and monotonic IDs.
 */
#if defined(CORDIS_ABI_EXPORTS)
#error The delegated Reporter query must remain private to this translation unit.
#endif
#if !defined(CORDIS_EXAMPLE_REPORTER_FAIL_RECOVERY)
#error The failure policy must be explicit for each test artifact.
#endif

#define cordis_query_v1 originalReporterQuery
#include "../../examples/native_services/reporter.cpp"
#undef cordis_query_v1

namespace {
CordisStatus CORDIS_CALL failureStart(CordisHandle id, CordisRequestId request) noexcept
{
    const auto* value = example::find(id);
    if (!value || !request || value->started)
        return CORDIS_E_INVALID_ARGUMENT;
    if (id == 2 || (CORDIS_EXAMPLE_REPORTER_FAIL_RECOVERY && id > 2))
        return CORDIS_E_FAILED;
    return start(id, request);
}
} // namespace

extern "C" __declspec(dllexport) CordisStatus CORDIS_CALL cordis_query_v1(std::uint32_t abi, const CordisHostV1* input,
                                                                          CordisPluginApiV1* output)
{
    const auto status = originalReporterQuery(abi, input, output);
    if (status == CORDIS_OK)
        output->start = failureStart;
    return status;
}
