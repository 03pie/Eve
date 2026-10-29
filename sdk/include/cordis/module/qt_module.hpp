/** @file qt_module.hpp
 * @brief Qt 模块映像与 loader root 的进程驻留装载；不授予业务对象或页面能力。
 */
#pragma once
#include <cordis/module/manager.hpp>
namespace cordis::module {
/** @brief 应用 owner 上重新固定来源并复用进程目录；错误装载保守驻留。
 * root 归 QPluginLoader，不能手动删除。当前仅装载 root，descriptorAccepted/businessAccepted
 * 仍为 false；业务工厂协商及实例装配另行完成，不调用 unload。
 */
Result<ModuleLoadReport> loadQtModule(const std::string& pluginRoot, const std::string& manifestPath,
                                      PreflightLimits limits = {});
} // namespace cordis::module
