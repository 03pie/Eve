/** @file module_query_layout.cpp
 * @brief C++17 对照同一独立 C 头的布局，不引入 Qt/Ela 或 Core。
 */
#include <cordis_abi/module_query.h>
#include <cstddef>
static_assert(sizeof(CordisQueryHeaderV1) == 16, "header");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(CordisQueryBytesV1) == 16, "bytes");
static_assert(sizeof(CordisModuleQueryV1) == 32, "request");
static_assert(sizeof(CordisModuleDescriptorV1) == 80, "descriptor");
static_assert(alignof(CordisModuleDescriptorV1) == 8, "alignment");
static_assert(offsetof(CordisModuleDescriptorV1, module_id) == 32, "module id");
static_assert(offsetof(CordisModuleDescriptorV1, plugins) == 72, "plugins");
static_assert(sizeof(CordisModuleQueryFnV1) == 8, "calling entry");
#endif
extern "C" int module_query_cpp_layout(void)
{
    return 0;
}
