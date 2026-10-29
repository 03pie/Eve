/** @file abi_p.hpp
 * @brief 业务 ABI 的宿主私有校验；不授予 Kernel 能力或管理器的 DLL 指针访问。
 */
#pragma once
#include <cordis/module/manifest.hpp>
#include <cordis_abi/plugin.h>

namespace cordis::module::detail {
/** @brief 元数据已复制；函数指针仍属 DLL，调用者必须持 Call/ModuleLease 及有效 HostTable。
 * 此私有结果不注册 Runtime、不拥有原生映射，也不证明请求/资源已经静止。
 */
struct BusinessApi {
    CordisPluginApiV1 table;
    std::string moduleId;
    std::string moduleVersion;
    std::vector<std::string> plugins;
};
Result<void> validateHostApi(const CordisHostV1& host);
Result<BusinessApi> queryBusiness(CordisQueryFnV1 query, const CordisHostV1& host, const ModuleManifest& manifest);
} // namespace cordis::module::detail
