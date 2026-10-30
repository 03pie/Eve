/** @file manager.hpp
 * @brief Windows 原生模块的只读握手、进程驻留和宿主诊断；不暴露加载器或 ABI 指针。
 */
#pragma once
#include <cordis/module/ledger.hpp>
#include <cordis/module/manifest.hpp>
#include <optional>
#include <thread>

namespace cordis::module {
/** @brief Loaded 为发现通过，Serving 为业务宿主会话开放；均不表示 Kernel 已注册或插件已激活。 */
enum class ModuleState { Validated, Loaded, Serving, Draining, Resident, Failed };
/** @brief 目录选中的当前代与显式暂存的候选代；装载状态不代表已经切换业务。 */
enum class GenerationRole { Current, Candidate };
/** @brief 装载完成回执；相同模块内容重复请求复用一代，不再次调用 DLL。 */
struct ModuleLoadReport {
    GenerationId generation;
    bool reused;
};
/** @brief 一代模块的宿主纯值快照；nativeMapped 指本目录取得的映射引用，不盘点外部加载器。
 * 失败也可能已映射，不能据失败推断已卸载。
 */
struct ModuleGenerationSnapshot {
    GenerationId generation;
    ModuleState state;
    ModuleManifest manifest;
    std::string manifestPath;
    std::string libraryPath;
    bool nativeMapped;
    bool nativePinned;
    bool descriptorAccepted;
    std::optional<ErrorInfo> failure;
    LedgerSnapshot resources;
    bool businessAccepted = false;
    std::optional<ErrorInfo> businessFailure;
    GenerationRole role = GenerationRole::Current;
    GenerationId predecessor{};
};
/** @brief 每个句柄绑定创建线程；进程内共享唯一驻留目录，最多 64 个已保留代次。
 * 默认驻留至进程退出，析构不调用 DLL 或 FreeLibrary；不提供业务或物理卸载能力。
 */
class ModuleManager {
public:
    ModuleManager();
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;
    /** @brief 重新预检并固定来源，受限搜索装载、只读描述握手；已映射失败保守驻留。 */
    Result<ModuleLoadReport> load(const std::string& pluginRoot, const std::string& manifestPath,
                                  PreflightLimits limits = {});
    /** @brief 对当前代显式暂存另一份兼容目录的 C ABI 产物；不切换运行实例。
     * 来源须已放在独立目录；仍重新固定文件、校验和驻留。相同候选复用，普通 load 不选择候选。
     */
    Result<ModuleLoadReport> stage(const std::string& pluginRoot, const std::string& manifestPath,
                                   GenerationId predecessor, PreflightLimits limits = {});
    /** @brief owner 线程复制进程内模块诊断；不持有插件指针或返回可调用函数表。 */
    Result<std::vector<ModuleGenerationSnapshot>> snapshot() const;
    /** @brief 停止该代接收引用；无执行引用后转为 Resident，不能再次装载为活动代。 */
    Result<void> drain(GenerationId generation);

private:
    Result<ModuleLoadReport> loadGeneration(const std::string& pluginRoot, const std::string& manifestPath,
                                            PreflightLimits limits, GenerationId predecessor);
    const std::thread::id owner_;
};
} // namespace cordis::module
