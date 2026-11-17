/* Test-only unload witness. No business table or instance registry. */
#include <Windows.h>
#include <cordis_abi/plugin.h>

static volatile LONG* dynamic_detaches;
CORDIS_EXPORT CordisStatus CORDIS_CALL cordis_fixture_unload_watch(volatile LONG* slot)
{
    if (!slot)
        return CORDIS_E_INVALID_ARGUMENT;
    dynamic_detaches = slot;
    return CORDIS_OK;
}
CORDIS_EXPORT void CORDIS_CALL cordis_fixture_unload_call(void(CORDIS_CALL* action)(void))
{
    if (action)
        action();
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    if (reason == DLL_PROCESS_DETACH && !reserved && dynamic_detaches)
        InterlockedIncrement(dynamic_detaches);
    return TRUE;
}
