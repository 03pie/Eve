/** @file runtime.hpp
 * @brief 宿主冻结的原生插件契约；C++ 描述仅用于宿主，不跨业务 C ABI。
 */
#pragma once
#include <cordis/instance.hpp>
#include <cordis/module/native.hpp>

namespace cordis::module {
struct NativeDependency {
    ServiceKey key;
    std::uint32_t minimumMinor = 0;
    std::vector<std::string> capabilities;
};
struct NativeOutput {
    ServiceKey key;
    bool required = true;
    ServiceMetadata metadata;
};
/** @brief 为宿主可选 inject 目录构造相同原生接口类型；结构约束由 Core/Loader 验证。 */
ServiceRequirement nativeRequirement(NativeDependency dependency);
/** @brief 托管实例只调用宿主声明或配置追加的类型化依赖；输出在 start 终结前登记。
 * effect ready 可以晚到，Core 等待初始 setup；低层 create 的动态消费/注册不变。
 */
struct NativePlugin {
    std::string id;
    std::vector<NativeDependency> dependencies;
    std::vector<NativeOutput> outputs;
};
} // namespace cordis::module
