/** @file workspace.hpp
 * @brief 实际业务工作台的唯一宿主；装配与关闭沿同一资源拥有链。
 */
#pragma once
#include "service_module.hpp"
#include <atomic>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/qt_store.hpp>
#include <cordis/module/qt_module.hpp>
#include <cordis/replacement_operation.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace notebook {
namespace detail {
struct WorkspaceUpgradeExecution;
}

/** @brief 纯预览值，不授予安装/创建权；执行时重新核对完整来源和当前版本。 */
struct UpgradePreview {
    std::string alias, sourceRoot;
    cordis::GenerationId previous;
    cordis::RuntimeId previousRuntime;
    std::uint64_t desiredRevision;
    cordis::module::PreflightArtifact artifact;
    cordis::ReplacementClosure impact;
    /** @brief 展示值随执行重新预览核对，不能授予安装或替换权限。 */
    cordis::module::ModuleManifest previousManifest;
    std::vector<std::string> affectedPaths;
};
/** @brief 原事务结果与其后的物理清理分别保存；不持有任何执行资源。 */
struct WorkspaceUpgradeReport {
    std::string alias;
    cordis::GenerationId previous, candidate;
    /** @brief 退役或撤销候选的实际清理目标；不是目录 GenerationRole 的投影。 */
    cordis::GenerationId retired;
    std::optional<cordis::ReplacementReport> replacement;
    std::optional<cordis::ErrorInfo> failure;
    std::optional<cordis::Result<cordis::CleanupReport>> nativeClose;
    std::optional<cordis::Result<void>> drain, unload;
    bool retained = false, deliveryBlocked = false;
};
/** @brief 同步回收尝试的原结果，不改写业务替换报告或保留执行能力。 */
struct WorkspaceRetirementReport {
    cordis::GenerationId generation;
    std::optional<cordis::Result<void>> drain, unload;
};
struct WorkspaceUpgradeSnapshot {
    bool accepting = false, deliveryBlocked = false;
    std::optional<WorkspaceUpgradeReport> active;
    std::optional<cordis::ReplacementPhase> phase;
    std::vector<WorkspaceUpgradeReport> history;
    /** @brief 原操作已收到取消请求，不表示恢复或清理已经完成。 */
    bool cancellationRequested = false;
};
/** @brief 当前业务来源与已接受配置路径，不借出工厂或运行能力。 */
struct WorkspaceModuleEntry {
    std::string alias, plugin;
    cordis::GenerationId generation;
    cordis::RuntimeId runtime;
    std::vector<std::string> paths;
    /** @brief 现有运行契约支持升级；在途/候选/关闭准入仍由原入口判断。 */
    bool supportsUpgrade = false;
};
/** @brief 原目录与实际配置/关闭错误的纯值投影，关闭后仍可读取。 */
struct WorkspaceModulesSnapshot {
    bool open = false;
    std::vector<WorkspaceModuleEntry> current;
    std::vector<cordis::module::ModuleGenerationSnapshot> generations;
    std::vector<cordis::ErrorInfo> configurationErrors;
    std::uint64_t desiredRevision = 0;
    std::optional<std::uint64_t> persistedRevision;
    bool dirty = false, writing = false;
};
template <class T> T take(cordis::Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
inline void take(cordis::Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}
/** @brief 原阶段值回执；未装配/未执行为空，不持有模块、工厂或 Task。 */
struct WorkspaceCloseSnapshot {
    bool closing = false, closed = false, retained = false, deliveryBlocked = false;
    std::optional<cordis::Result<cordis::loader::SaveReport>> save;
    std::optional<cordis::Result<void>> session, shell, providers, counterDrain, reporterDrain;
    std::optional<cordis::Result<cordis::CleanupReport>> kernel, counter, reporter;
};
/** @brief 同一 Notebook 代次的实际工厂拥有者；不造 Qt 会话或卸载能力。 */
struct NotebookModule {
    explicit NotebookModule(cordis::GenerationId value) : generation(value) {}
    const cordis::GenerationId generation;
    std::optional<cordis::Runtime> runtime;
};
/** @brief caller 持有开放应用队列；正常退出显式等待 close，再关闭 caller 队列。 */
class Workspace : public std::enable_shared_from_this<Workspace> {
public:
    static cordis::Result<std::shared_ptr<Workspace>> open(const QString& root, const QString& file,
                                                           std::shared_ptr<cordis::qt::QtScheduler> scheduler);
    /** @brief 默认固定六种业务资格来源；已保存定位按原策略复核，生产卸载名单仍为空。 */
    static cordis::Result<std::shared_ptr<Workspace>>
    openQualification(const QString& sourceBin, const QString& storeRoot, const QString& file,
                      std::shared_ptr<cordis::qt::QtScheduler> scheduler);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    cordis::Result<cordis::GenerationId> generation();
    cordis::Result<cordis::Runtime> notebookRuntime();
    /** @brief 实际强能力视图，非纯历史；另持 Runtime 副本仍可能阻止关闭后的 drain。 */
    cordis::Result<std::shared_ptr<const native_services::CurrentModule>> counter();
    /** @brief 实际强能力视图，非纯历史；关闭不替调用方释放其工厂副本。 */
    cordis::Result<std::shared_ptr<const native_services::CurrentModule>> reporter();
    cordis::Result<UpgradePreview> previewUpgrade(const std::string& alias, const QString& sourceRoot,
                                                  const std::string& manifestPath);
    cordis::Result<cordis::Task<WorkspaceUpgradeReport>> upgrade(const UpgradePreview& preview);
    /** @brief 只向本次原操作请求取消，接纳不表示清理或恢复已完成。 */
    cordis::Result<void> cancelUpgrade(cordis::OperationId expectedOperationId);
    cordis::Result<WorkspaceUpgradeSnapshot> upgradeSnapshot();
    cordis::Result<WorkspaceModulesSnapshot> modulesSnapshot();
    /** @brief 仅尝试本工作台业务范围内的非 Current Native；真实授权和账目仍由原目录核对。 */
    cordis::Result<WorkspaceRetirementReport> retryRetirement(cordis::GenerationId expectedGeneration);
    /** @brief 显式保存当前已接受修订并返回原 Session/Store 回执，不应用编辑草稿。 */
    cordis::Result<cordis::Task<cordis::loader::SaveReport>> saveConfiguration(std::uint64_t expectedRevision);
    /** @brief 同步写栈保活并阻止破坏性关闭；不接管事务或异步回调。 */
    cordis::Result<void> withModuleView(std::function<void()> action);
    const std::string& pluginRoot() const noexcept { return pluginRoot_; }
    cordis::Result<WorkspaceCloseSnapshot> closeSnapshot() const;
    bool deliveryBlocked() const noexcept { return deliveryBlocked_->load(); }
    cordis::Task<void> ready() const;
    cordis::Result<cordis::loader::AcceptedTarget> apply(const std::string& text);
    cordis::Task<void> close();

    std::shared_ptr<cordis::qt::QtScheduler> scheduler;
    std::shared_ptr<cordis::Kernel> kernel;
    cordis::ui::ElaShell shell;
    std::shared_ptr<cordis::module::ModuleManager> manager;
    std::shared_ptr<cordis::loader::KernelReconcileDriver> driver;
    std::shared_ptr<cordis::loader::ConfigurationSession> session;
    std::shared_ptr<cordis::ui::IConfigurationManagement> configurationManagement;
    std::optional<cordis::ui::PageHandle> configurationPage, modulesPage;
    std::string initial;

private:
    friend struct detail::WorkspaceUpgradeExecution;
    explicit Workspace(std::shared_ptr<cordis::qt::QtScheduler> target);
    static cordis::Result<std::shared_ptr<Workspace>> create(const QString& root, const QString& store,
                                                             const QString& file,
                                                             std::shared_ptr<cordis::qt::QtScheduler> scheduler,
                                                             bool qualification);
    void initialize(const QString& root, const QString& store, const QString& file, bool qualification);
    void publishUi();
    cordis::loader::ConfiguredPlugin openNotebook(const cordis::loader::PluginModule& source, bool stateful);
    void initializeHost(const QString& file, cordis::loader::ConfigurationFileSnapshot baseline,
                        std::vector<cordis::loader::ConfiguredPlugin> plugins, bool qualification);
    void flushBeforeClose();
    void closeAfterUpgrade();
    void closeModuleViews();
    cordis::Result<void> synchronizeUpgrade();
    void closeSession();
    void closeKernel();
    void closeCounter();
    void closeReporter();
    void closeShell();
    void releaseOwners();
    void finishClose();
    void retainCloseFailure(cordis::ErrorInfo failure);
    void retryableCloseFailure(cordis::ErrorInfo failure);
    std::string pluginRoot_;
    std::shared_ptr<NotebookModule> notebook_;
    std::shared_ptr<native_services::CurrentModule> counter_, reporter_;
    std::shared_ptr<const cordis::loader::ConfigurationDefinitions> definitions_;
    cordis::module::NativeUnloadWhitelist approvals_;
    std::shared_ptr<detail::WorkspaceUpgradeExecution> upgrade_;
    std::deque<WorkspaceUpgradeReport> upgradeHistory_;
    bool moduleViewsOpen_ = true;
    std::size_t moduleViewDepth_ = 0;
    cordis::Promise<void> moduleViewsIdle_;
    std::vector<cordis::ProviderHandle> providers_;
    std::optional<cordis::Instance> host_;
    std::shared_ptr<Workspace> pin_;
    cordis::Promise<void> stopped_;
    std::optional<WorkspaceCloseSnapshot> closeState_{std::in_place};
    std::shared_ptr<std::atomic_bool> deliveryBlocked_ = std::make_shared<std::atomic_bool>(false);
    template <class T, class F> void watch(const cordis::Task<T>& task, F action)
    {
        const auto done = stopped_;
        const auto blocked = deliveryBlocked_;
        const auto receipt = task.thenOn(scheduler, std::move(action));
        // 原回执可能跨线程拒绝；只标记纯诊断并结算 Promise，不动 UI/拥有者。
        cordis::detail::TaskAccess::observe(receipt, [done, blocked](cordis::Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                blocked->store(true);
                done.reject(result->error());
            }
        });
    }
};
std::unique_ptr<QWidget> modulesPanel(std::weak_ptr<Workspace> workspace);
} // namespace notebook
