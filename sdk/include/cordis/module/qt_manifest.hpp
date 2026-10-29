/** @file qt_manifest.hpp
 * @brief Qt 工具链与只读预检的宿主值；公开头不引入 Qt 或插件对象。
 */
#pragma once
#include <cordis/module/manifest.hpp>

namespace cordis::module {
struct QtToolchain {
    std::string sdkVersion, qtVersion, compiler, compilerVersion, runtime;
    std::uint32_t pointerBits = 0, cppStandard = 0, iteratorDebug = 0;
    bool debug = false;
};
/** @brief 由当前宿主实际编译配置构造，非调用方传入的兼容性覆盖值。 */
QtToolchain qtToolchain();
struct QtModuleInspection {
    PreflightArtifact artifact;
    QtToolchain toolchain;
};
/** @brief 固定文件并比较嵌入元数据；不装载映像、不调用工厂、不注册代次。
 * 返回一次快照，后续 Qt 装载必须重新固定/验证，不能复用为永久授权。
 */
Result<QtModuleInspection> inspectQtModule(const std::string& root, const std::string& manifest,
                                           PreflightLimits limits = {});
} // namespace cordis::module
