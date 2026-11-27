/** @file workspace.hpp
 * @brief 实际业务工作台的唯一宿主；装配与关闭沿同一资源拥有链。
 */
#pragma once
#include "service_module.hpp"
#include <atomic>
#include <eve/loader/kernel_driver.hpp>
#include <eve/loader/qt_store.hpp>
#include <eve/module/qt_module.hpp>
#include <eve/replacement_operation.hpp>
#include <eve/ui/configuration.hpp>
#include <eve/ui/ela_shell.hpp>
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
    eve::GenerationId previous;
    eve::RuntimeId previousRuntime;
    std::uint64_t desiredRevision;
    eve::module::PreflightArtifact artifact;
    eve::ReplacementClosure impact;
    /** @brief 展示值随执行重新预览核对，不能授予安装或替换权限。 */
    eve::module::ModuleManifest previousManifest;
    std::vector<std::string> affectedPaths;
};
/** @brief 原事务结果与其后的物理清理分别保存；不持有任何执行资源。 */
struct WorkspaceUpgradeReport {
    std::string alias;
    eve::GenerationId previous, candidate;
    /** @brief 退役或撤销候选的实际清理目标；不是目录 GenerationRole 的投影。 */
    eve::GenerationId retired;
    std::optional<eve::ReplacementReport> replacement;
    std::optional<eve::ErrorInfo> failure;
    std::optional<eve::Result<eve::CleanupReport>> nativeClose;
    std::optional<eve::Result<void>> drain, unload;
    bool retained = false, deliveryBlocked = false;
};
/** @brief 同步回收尝试的原结果，不改写业务替换报告或保留执行能力。 */
struct WorkspaceRetirementReport {
    eve::GenerationId generation;
    std::optional<eve::Result<void>> drain, unload;
};
struct WorkspaceUpgradeSnapshot {
    bool accepting = false, deliveryBlocked = false;
    std::optional<WorkspaceUpgradeReport> active;
    std::optional<eve::ReplacementPhase> phase;
    std::vector<WorkspaceUpgradeReport> history;
    /** @brief 原操作已收到取消请求，不表示恢复或清理已经完成。 */
    bool cancellationRequested = false;
};
/** @brief 当前业务来源与已接受配置路径，不借出工厂或运行能力。 */
struct WorkspaceModuleEntry {
    std::string alias, plugin;
    eve::GenerationId generation;
    eve::RuntimeId runtime;
    std::vector<std::string> paths;
    /** @brief 现有运行契约支持升级；在途/候选/关闭准入仍由原入口判断。 */
    bool supportsUpgrade = false;
};
/** @brief 原目录与实际配置/关闭错误的纯值投影，关闭后仍可读取。 */
struct WorkspaceModulesSnapshot {
    bool open = false;
    std::vector<WorkspaceModuleEntry> current;
    std::vector<eve::module::ModuleGenerationSnapshot> generations;
    std::vector<eve::ErrorInfo> configurationErrors;
    std::uint64_t desiredRevision = 0;
    std::optional<std::uint64_t> persistedRevision;
    bool dirty = false, writing = false;
};
template <class T> T take(eve::Result<T> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
inline void take(eve::Result<void> result)
{
    if (!result.hasValue())
        throw std::runtime_error(result.error().message);
}
/** @brief 原阶段值回执；未装配/未执行为空，不持有模块、工厂或 Task。 */
struct WorkspaceCloseSnapshot {
    bool closing = false, closed = false, retained = false, deliveryBlocked = false;
    std::optional<eve::Result<eve::loader::SaveReport>> save;
    std::optional<eve::Result<void>> session, shell, providers, counterDrain, reporterDrain;
    std::optional<eve::Result<eve::CleanupReport>> kernel, counter, reporter;
};
/** @brief 同一 Notebook 代次的实际工厂拥有者；不造 Qt 会话或卸载能力。 */
struct NotebookModule {
    explicit NotebookModule(eve::GenerationId value) : generation(value) {}
    const eve::GenerationId generation;
    std::optional<eve::Runtime> runtime;
};
/** @brief caller 持有开放应用队列；正常退出显式等待 close，再关闭 caller 队列。 */
class Workspace : public std::enable_shared_from_this<Workspace> {
public:
    static eve::Result<std::shared_ptr<Workspace>> open(const QString& root, const QString& file,
                                                           std::shared_ptr<eve::qt::QtScheduler> scheduler);
    /** @brief 固定六种实际业务来源的资格入口；仅临时批准该次固定 Native A/B 字节。 */
    static eve::Result<std::shared_ptr<Workspace>>
    openQualification(const QString& sourceBin, const QString& storeRoot, const QString& file,
                      std::shared_ptr<eve::qt::QtScheduler> scheduler);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    eve::Result<eve::GenerationId> generation();
    eve::Result<eve::Runtime> notebookRuntime();
    /** @brief 实际强能力视图，非纯历史；另持 Runtime 副本仍可能阻止关闭后的 drain。 */
    eve::Result<std::shared_ptr<const native_services::CurrentModule>> counter();
    /** @brief 实际强能力视图，非纯历史；关闭不替调用方释放其工厂副本。 */
    eve::Result<std::shared_ptr<const native_services::CurrentModule>> reporter();
    eve::Result<UpgradePreview> previewUpgrade(const std::string& alias, const QString& sourceRoot,
                                                  const std::string& manifestPath);
    eve::Result<eve::Task<WorkspaceUpgradeReport>> upgrade(const UpgradePreview& preview);
    /** @brief 只向本次原操作请求取消，接纳不表示清理或恢复已完成。 */
    eve::Result<void> cancelUpgrade(eve::OperationId expectedOperationId);
    eve::Result<WorkspaceUpgradeSnapshot> upgradeSnapshot();
    eve::Result<WorkspaceModulesSnapshot> modulesSnapshot();
    /** @brief 仅尝试本工作台业务范围内的非 Current Native；真实授权和账目仍由原目录核对。 */
    eve::Result<WorkspaceRetirementReport> retryRetirement(eve::GenerationId expectedGeneration);
    /** @brief 显式保存当前已接受修订并返回原 Session/Store 回执，不应用编辑草稿。 */
    eve::Result<eve::Task<eve::loader::SaveReport>> saveConfiguration(std::uint64_t expectedRevision);
    /** @brief 同步写栈保活并阻止破坏性关闭；不接管事务或异步回调。 */
    eve::Result<void> withModuleView(std::function<void()> action);
    const std::string& pluginRoot() const noexcept { return pluginRoot_; }
    eve::Result<WorkspaceCloseSnapshot> closeSnapshot() const;
    bool deliveryBlocked() const noexcept { return deliveryBlocked_->load(); }
    eve::Task<void> ready() const;
    eve::Result<eve::loader::AcceptedTarget> apply(const std::string& text);
    eve::Task<void> close();

    std::shared_ptr<eve::qt::QtScheduler> scheduler;
    std::shared_ptr<eve::Kernel> kernel;
    eve::ui::ElaShell shell;
    std::shared_ptr<eve::module::ModuleManager> manager;
    std::shared_ptr<eve::loader::KernelReconcileDriver> driver;
    std::shared_ptr<eve::loader::ConfigurationSession> session;
    std::shared_ptr<eve::ui::IConfigurationManagement> configurationManagement;
    std::optional<eve::ui::PageHandle> configurationPage, modulesPage;
    std::string initial;

private:
    friend struct detail::WorkspaceUpgradeExecution;
    enum class SourceMode { Notebook, Qualification };
    explicit Workspace(std::shared_ptr<eve::qt::QtScheduler> target);
    static eve::Result<std::shared_ptr<Workspace>> create(const QString& root, const QString& store,
                                                             const QString& file,
                                                             std::shared_ptr<eve::qt::QtScheduler> scheduler,
                                                             SourceMode mode);
    void initialize(const QString& root, const QString& store, const QString& file, SourceMode mode);
    void publishUi();
    eve::loader::ConfiguredPlugin openNotebook(const eve::loader::PluginModule& source, bool stateful);
    void registerQtOutputSource(eve::RuntimeId runtime, eve::GenerationId generation);
    void initializeHost(const QString& file, eve::loader::ConfigurationFileSnapshot baseline,
                        std::vector<eve::loader::ConfiguredPlugin> plugins, SourceMode mode);
    void flushBeforeClose();
    void closeAfterUpgrade();
    void closeModuleViews();
    eve::Result<void> synchronizeUpgrade();
    void closeSession();
    void closeKernel();
    void closeCounter();
    void closeReporter();
    void closeShell();
    void releaseOwners();
    void finishClose();
    void retainCloseFailure(eve::ErrorInfo failure);
    void retryableCloseFailure(eve::ErrorInfo failure);
    std::string pluginRoot_;
    std::shared_ptr<const eve::module::NativeLogSink> nativeLogSink_;
    std::shared_ptr<NotebookModule> notebook_;
    std::shared_ptr<native_services::CurrentModule> counter_, reporter_;
    std::shared_ptr<const eve::loader::ConfigurationDefinitions> definitions_;
    eve::module::NativeUnloadWhitelist approvals_;
    std::shared_ptr<detail::WorkspaceUpgradeExecution> upgrade_;
    std::deque<WorkspaceUpgradeReport> upgradeHistory_;
    bool moduleViewsOpen_ = true;
    std::size_t moduleViewDepth_ = 0;
    eve::Promise<void> moduleViewsIdle_;
    std::vector<eve::ProviderHandle> providers_;
    std::optional<eve::Instance> host_;
    std::shared_ptr<Workspace> pin_;
    eve::Promise<void> stopped_;
    std::optional<WorkspaceCloseSnapshot> closeState_{std::in_place};
    std::shared_ptr<std::atomic_bool> deliveryBlocked_ = std::make_shared<std::atomic_bool>(false);
    template <class T, class F> void watch(const eve::Task<T>& task, F action)
    {
        const auto done = stopped_;
        const auto blocked = deliveryBlocked_;
        const auto receipt = task.thenOn(scheduler, std::move(action));
        // 原回执可能跨线程拒绝；只标记纯诊断并结算 Promise，不动 UI/拥有者。
        eve::detail::TaskAccess::observe(receipt, [done, blocked](eve::Task<void>::Snapshot result) {
            if (!result->hasValue()) {
                blocked->store(true);
                done.reject(result->error());
            }
        });
    }
};
std::unique_ptr<QWidget> modulesPanel(std::weak_ptr<Workspace> workspace);
} // namespace notebook
