/** @file service.hpp
 * @brief 宿主侧原生 JSON 服务接口与实例契约；本头不属于 C ABI。
 */
#pragma once
#include <cordis/instance.hpp>
#include <stdexcept>

namespace cordis::module {
struct NativeServiceResult {
    std::int32_t status;
    std::string json;
};
/** @brief 取消仅请求协作，result 仍须真实完成；cancel 只在 owner 调用。 */
struct NativeServiceCall {
    Task<NativeServiceResult> result;
    std::function<Result<void>()> cancel;
};
/** @brief 同工具链宿主包装层；对象及 C++ 函数不跨 DLL 的业务 C ABI。 */
class NativeService {
public:
    using Invoke = std::function<NativeServiceCall(std::string, std::string)>;
    explicit NativeService(Invoke invoke) : invoke_(std::move(invoke))
    {
        if (!invoke_)
            throw std::invalid_argument("native service requires an invocation");
    }
    NativeServiceCall invoke(std::string method, std::string json) const
    {
        return invoke_(std::move(method), std::move(json));
    }

private:
    Invoke invoke_;
};
struct NativeServiceRequirement {
    ServiceKey key;
    std::uint32_t minimumMinor = 0;
    std::vector<std::string> capabilities;
};
struct NativeServiceOutput {
    ServiceKey key;
    bool required = true;
    ServiceMetadata metadata;
};
/** @brief 已验证模块目录内的插件及宿主声明的类型化服务契约，不加载新模块。 */
struct NativePlugin {
    std::string pluginId;
    std::vector<NativeServiceRequirement> dependencies;
    std::vector<NativeServiceOutput> outputs;
};
} // namespace cordis::module
