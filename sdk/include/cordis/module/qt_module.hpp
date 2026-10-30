/** @file qt_module.hpp
 * @brief Qt 模块映像与 loader root 的进程驻留装载；不授予业务对象或页面能力。
 */
#pragma once
#include <cordis/instance.hpp>
#include <cordis/module/manager.hpp>
namespace cordis::qt {
class QtScheduler;
}
namespace cordis::module {
/** @brief 应用 owner 上重新固定来源并复用进程目录；错误装载保守驻留。
 * root 归 QPluginLoader，不能手动删除。仅装载 root，不据此标记业务可用；
 * 业务工厂由 qtRuntime 显式协商，不调用 unload。
 */
Result<ModuleLoadReport> loadQtModule(const std::string& pluginRoot, const std::string& manifestPath,
                                      PreflightLimits limits = {});
/** @brief 对当前 Qt 代显式暂存另一份产物；root 仍归 loader，不切换业务或卸载旧代。 */
Result<ModuleLoadReport> stageQtModule(const std::string& pluginRoot, const std::string& manifestPath,
                                       GenerationId predecessor, PreflightLimits limits = {});
/** @brief 宿主冻结的同工具链插件契约；不允许 DLL 在创建时扩张依赖或输出声明。 */
struct QtPlugin {
    std::string id;
    std::vector<ServiceRequirement> dependencies;
    std::vector<ServiceOutput> outputs;
};
/** @brief 显式协商真实工厂并创建宿主 Runtime；每次 epoch 激活都创建独立业务 QObject。
 * Runtime 及其 Core 实例须使用该 QtScheduler；root 始终驻留，错误清理保留对象和租约。
 */
Result<Runtime> qtRuntime(GenerationId generation, QtPlugin plugin, std::shared_ptr<qt::QtScheduler> scheduler);
} // namespace cordis::module
