/** @file module_throw.cpp
 * @brief 故意违反禁止异常跨边界的约束，验证宿主防御及失败驻留。
 */
#include <cordis_abi/module_query.h>
static int queries;
extern "C" __declspec(dllexport) int __cdecl cordis_fixture_queries()
{
    return queries;
}
CordisQueryStatus CORDIS_QUERY_CALL cordis_module_query_v1(const CordisModuleQueryV1* request,
                                                           CordisModuleDescriptorV1* output)
{
    (void)request;
    (void)output;
    ++queries;
    throw 42;
}
