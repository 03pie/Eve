/** @file abi_p.hpp
 * @brief 业务 ABI 的宿主私有校验；不授予 Kernel 能力或管理器的 DLL 指针访问。
 */
#pragma once
#include <eve/module/manifest.hpp>
#include <eve_abi/plugin.h>

namespace eve::module::detail {
/** @brief 元数据已复制；函数指针仍属 DLL，调用者必须持 Call/ModuleLease 及有效 HostTable。
 * 此私有结果不注册 Runtime、不拥有原生映射，也不证明请求/资源已经静止。
 */
struct BusinessApi {
    EvePluginApiV1 table;
    std::string moduleId;
    std::string moduleVersion;
    std::vector<std::string> plugins;
};
Result<void> validateHostApi(const EveHostV1& host);
Result<BusinessApi> queryBusiness(EveQueryFnV1 query, const EveHostV1& host, const ModuleManifest& manifest);
} // namespace eve::module::detail
