/** @file manager.hpp
 * @brief Windows 原生模块的首次装载策略、只读握手和宿主诊断；不暴露加载器或 ABI 指针。
 */
#pragma once
#include <eve/module/ledger.hpp>
#include <eve/module/manifest.hpp>
#include <eve/module/replacement.hpp>
#include <optional>
#include <thread>

namespace eve {
class ReplacementScope;
}
namespace eve::module {
/** @brief Loaded 为发现通过，Serving 为业务宿主会话开放；均不表示 Kernel 已注册或插件已激活。 */
enum class ModuleState { Validated, Loaded, Serving, Draining, Resident, Failed, Unloading, Unloaded };
/** @brief 目录选中的当前代与显式暂存的候选代；装载状态不代表已经切换业务。 */
enum class GenerationRole { Current, Candidate, Retired };
/** @brief 首次映射前固定的实际策略；ApprovedNative 仍须完整资源证明，不能直接卸载。 */
enum class ModuleLoadPolicy { Resident, ApprovedNative };
/** @brief 宿主明确批准的 C ABI 产物；按重新固定的模块身份和 SHA256 精确匹配。 */
struct NativeUnloadApproval {
    std::string moduleId;
    std::string sha256;
};
using NativeUnloadWhitelist = std::vector<NativeUnloadApproval>;
/** @brief 装载完成回执；相同模块内容重复请求复用一代，不再次调用 DLL。 */
struct ModuleLoadReport {
    GenerationId generation;
    bool reused;
};
/** @brief 实际后端工厂的来源历史值；不含工厂引用，也不授予创建或换代权限。 */
struct ModuleRuntimeOrigin {
    RuntimeId runtime;
    GenerationId generation;
    std::string moduleId;
    std::string pluginId;
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
    std::optional<ModuleReplacementSnapshot> replacement{};
    /** @brief 从真实代次固定策略派生，nativePinned=false 单独不构成卸载许可。 */
    ModuleLoadPolicy loadPolicy = ModuleLoadPolicy::Resident;
    /** @brief 独立物理终结诊断，不改写业务协商或替换提交结果。 */
    std::optional<ErrorInfo> unloadFailure;
};
/** @brief 每个句柄绑定创建线程；进程共享唯一目录，最多 64 个重代次和 128 条纯卸载历史。
 * 默认驻留至进程退出；只有精确批准的原生代次可显式卸载，析构不调用 DLL 或 FreeLibrary。
 */
class ModuleManager {
public:
    ModuleManager();
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;
    /** @brief 重新固定来源、选择首次策略并进行受限装载/发现；显式声明须精确白名单批准。
     * resident 声明不被白名单升级；已映射失败保守保留，不自动释放。
     */
    Result<ModuleLoadReport> load(const std::string& pluginRoot, const std::string& manifestPath,
                                  PreflightLimits limits = {}, const NativeUnloadWhitelist& whitelist = {});
    /** @brief 对当前代显式暂存另一份兼容目录的 C ABI 产物；不切换运行实例。
     * 来源须已放在独立目录；仍重新固定文件、校验和驻留。相同候选复用，普通 load 不选择候选。
     */
    Result<ModuleLoadReport> stage(const std::string& pluginRoot, const std::string& manifestPath,
                                   GenerationId predecessor, PreflightLimits limits = {},
                                   const NativeUnloadWhitelist& whitelist = {});
    /** @brief owner 线程复制进程内模块诊断；不持有插件指针或返回可调用函数表。 */
    Result<std::vector<ModuleGenerationSnapshot>> snapshot() const;
    /** @brief owner 线程查询仍被实际 Runtime/实例持有的工厂来源；未知或已释放返回空。 */
    Result<std::optional<ModuleRuntimeOrigin>> runtimeOrigin(RuntimeId runtime) const;
    /** @brief 停止该代接收引用；无执行引用后转为 Resident，不能再次装载为活动代。 */
    Result<void> drain(GenerationId generation);
    /** @brief 首次装载线程显式终结已批准原生代次；先由原所有者关闭会话并释放全部执行引用。
     * 不主动停止业务、释放别人租约或撤销 pin；成功后仅保留有界历史，再次装载分配新 ID。
     */
    Result<void> tryUnload(GenerationId generation);
    /** @brief 在唯一进程目录预约同模块的当前/候选代；拒绝其他预约及入场中的创建。 */
    Result<ReplacementReservation> reserveReplacement(GenerationId current, GenerationId candidate);
    /** @brief 将预约关联到真实 Reserved Core 作用域；只允许其真实候选上下文创建候选代。 */
    Result<ReplacementReservation> reserveReplacement(GenerationId current, GenerationId candidate,
                                                      const ReplacementScope& scope);

private:
    bool ownerThread() const;
    Result<ModuleLoadReport> loadGeneration(const std::string& pluginRoot, const std::string& manifestPath,
                                            PreflightLimits limits, GenerationId predecessor,
                                            const NativeUnloadWhitelist& whitelist);
    const std::thread::id owner_;
    const std::uint64_t ownerIdentity_;
};
} // namespace eve::module
