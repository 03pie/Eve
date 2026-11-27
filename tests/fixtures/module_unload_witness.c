/* Test-only unload witness. No business table or instance registry. */
#include <Windows.h>
#include <eve_abi/plugin.h>

static volatile LONG* dynamic_detaches;
EVE_EXPORT EveStatus EVE_CALL eve_fixture_unload_watch(volatile LONG* slot)
{
    if (!slot)
        return EVE_E_INVALID_ARGUMENT;
    dynamic_detaches = slot;
    return EVE_OK;
}
EVE_EXPORT void EVE_CALL eve_fixture_unload_call(void(EVE_CALL* action)(void))
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
