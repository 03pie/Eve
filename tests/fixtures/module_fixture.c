/* C-only discovery DLL. Modes intentionally violate one contract at a time. */
#include <cordis_abi/module_query.h>
#include <string.h>
#ifdef FIXTURE_BLOCKING
#include <Windows.h>
#include <stdio.h>
static int await_release(void)
{
    char name[96];
    HANDLE entered, release;
    DWORD waited;
    (void)snprintf(name, sizeof(name), "Local\\CordisQueryEntered%lu", (unsigned long)GetCurrentProcessId());
    entered = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    (void)snprintf(name, sizeof(name), "Local\\CordisQueryRelease%lu", (unsigned long)GetCurrentProcessId());
    release = OpenEventA(SYNCHRONIZE, FALSE, name);
    if (!entered || !release) {
        if (entered)
            CloseHandle(entered);
        if (release)
            CloseHandle(release);
        return 0;
    }
    waited = SetEvent(entered) ? WaitForSingleObject(release, 10000) : WAIT_FAILED;
    CloseHandle(release);
    CloseHandle(entered);
    return waited == WAIT_OBJECT_0;
}
#endif
#ifndef FIXTURE_MODE
#define FIXTURE_MODE 0
#endif
#ifndef FIXTURE_ID
#define FIXTURE_ID "org.cordis.good"
#endif
#ifdef FIXTURE_DEPENDENT
__declspec(dllimport) int __cdecl cordis_fixture_dependency(void);
#endif
static int queries;
static const CordisQueryBytesV1 plugins[] = {{(const uint8_t*)"org.cordis.plugin", 17}};
__declspec(dllexport) int __cdecl cordis_fixture_queries(void)
{
    return queries;
}

CordisQueryStatus CORDIS_QUERY_CALL cordis_module_query_v1(const CordisModuleQueryV1* request,
                                                           CordisModuleDescriptorV1* output)
{
    CordisModuleDescriptorV1 result;
    uint32_t capacity;
    ++queries;
    if (!request || !output || request->header.struct_size < sizeof(*request) || request->header.query_major != 1 ||
        request->header.query_minor != 0 || request->header.reserved || request->runtime_abi_major != 1 ||
        request->runtime_abi_minor != 0 || request->flags)
        return CORDIS_QUERY_UNSUPPORTED;
    capacity = output->header.struct_size;
    if (capacity < sizeof(*output))
        return CORDIS_QUERY_INVALID_ARGUMENT;
#ifdef FIXTURE_BLOCKING
    if (!await_release())
        return CORDIS_QUERY_FAILED;
#endif
#ifdef FIXTURE_DEPENDENT
    if (cordis_fixture_dependency() != 42)
        return CORDIS_QUERY_FAILED;
#endif
    memset(&result, 0, sizeof(result));
    result.header.struct_size = sizeof(result);
    result.header.query_major = 1;
    result.runtime_abi_major = 1;
    result.flags = CORDIS_MODULE_RESIDENT;
    result.module_id.data = (const uint8_t*)FIXTURE_ID;
    result.module_id.size = sizeof(FIXTURE_ID) - 1;
    result.module_version.data = (const uint8_t*)"1.0.0";
    result.module_version.size = 5;
    result.plugin_count = 1;
    result.plugins = plugins;
#if FIXTURE_MODE == 1
    result.header.query_major = 2;
#elif FIXTURE_MODE == 2
    result.module_id.data = (const uint8_t*)"org.cordis.wrong";
    result.module_id.size = 16;
#elif FIXTURE_MODE == 3
    result.header.reserved = 1;
#elif FIXTURE_MODE == 4
    result.module_id.data = 0;
#elif FIXTURE_MODE == 6
    result.header.struct_size = sizeof(result) - 1;
#elif FIXTURE_MODE == 7
    result.runtime_abi_major = 2;
#elif FIXTURE_MODE == 8
    result.flags = UINT64_C(2);
#elif FIXTURE_MODE == 9
    {
        static const CordisQueryBytesV1 other[] = {{(const uint8_t*)"org.cordis.other", 16}};
        result.plugins = other;
    }
#elif FIXTURE_MODE == 10
    result.module_id.size = UINT64_MAX;
#endif
    memcpy(output, &result, capacity < sizeof(result) ? capacity : sizeof(result));
#if FIXTURE_MODE == 5
    return CORDIS_QUERY_FAILED;
#else
    return CORDIS_QUERY_OK;
#endif
}
