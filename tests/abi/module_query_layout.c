/* C11 layout baseline; no C++/Qt/Ela headers. */
#include <cordis_abi/module_query.h>
#include <stddef.h>

_Static_assert(sizeof(CordisQueryStatus) == 4, "status");
_Static_assert(sizeof(CordisQueryHeaderV1) == 16, "header");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(CordisQueryBytesV1) == 16, "bytes");
_Static_assert(sizeof(CordisModuleQueryV1) == 32, "request");
_Static_assert(sizeof(CordisModuleDescriptorV1) == 80, "descriptor");
_Static_assert(_Alignof(CordisModuleDescriptorV1) == 8, "alignment");
_Static_assert(offsetof(CordisModuleDescriptorV1, module_id) == 32, "module id");
_Static_assert(offsetof(CordisModuleDescriptorV1, plugins) == 72, "plugins");
_Static_assert(sizeof(CordisModuleQueryFnV1) == 8, "calling entry");
#endif
extern int module_query_cpp_layout(void);
int main(void)
{
    return module_query_cpp_layout();
}
