/* C11 layout baseline; no C++/Qt/Ela headers. */
#include <eve_abi/module_query.h>
#include <stddef.h>

_Static_assert(sizeof(EveQueryStatus) == 4, "status");
_Static_assert(sizeof(EveQueryHeaderV1) == 16, "header");
_Static_assert(EVE_MODULE_QUERY_MINOR == 0, "resident discovery remains 1.0");
_Static_assert(EVE_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR == 1, "explicit discovery is 1.1");
_Static_assert(EVE_MODULE_RESIDENT == 1 && EVE_MODULE_EXPLICIT_UNLOAD == 2, "policy flags remain distinct");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(EveQueryBytesV1) == 16, "bytes");
_Static_assert(sizeof(EveModuleQueryV1) == 32, "request");
_Static_assert(sizeof(EveModuleDescriptorV1) == 80, "descriptor");
_Static_assert(_Alignof(EveModuleDescriptorV1) == 8, "alignment");
_Static_assert(offsetof(EveModuleDescriptorV1, module_id) == 32, "module id");
_Static_assert(offsetof(EveModuleDescriptorV1, plugins) == 72, "plugins");
_Static_assert(sizeof(EveModuleQueryFnV1) == 8, "calling entry");
#endif
extern int module_query_cpp_layout(void);
int main(void)
{
    return module_query_cpp_layout();
}
