/** @file workbench.hpp
 * @brief 组装初始插件与工作区。启动器持有运行结束回执所需的日志写入句柄。
 */
#pragma once
#include "desktop_contract.hpp"
#include "desktop_selection.hpp"
#include <QString>
#include <optional>

namespace eve::workbench {
class WorkspaceExtensions;
QString workbenchDataFile(const QString& applicationData, const QString& relativeFile);
class Workbench final {
public:
    explicit Workbench(bool safeDesktop = false);
    ~Workbench();
    DesktopSelection& selection() { return selection_; }
    std::shared_ptr<ui::ExtensionControls> extensionControls() const;
    std::function<void()> restartRequested;
    /** @brief 应用线程启动日志和系统桌面。就绪回执包含桌面及配置页的激活结果。 */
    Task<void> open(const std::optional<QString>& manifestPath, const QString& workspaceDir);
    loader::AcceptedTarget applyInitial();
    Task<void> close();
    /** @brief 保存阶段失败后窗口仍可用于修正或重试。清理开始后不再接受操作。 */
    bool canRetryClose() const { return !closing_ && bool(desktop()); }
    std::shared_ptr<DesktopView> desktop() const { return bootstrap_->window.lock(); }
    Result<std::uint64_t> writeOutput(std::string text, ui::OutputLevel level = ui::OutputLevel::Info,
                                      ui::OutputDiagnostic diagnostic = {}) const;
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::shared_ptr<Kernel> kernel;
    std::shared_ptr<loader::ConfigurationSession> session;
private:
    Task<void> mountWorkspaceExtensions();
    void openDesktop(const std::optional<QString>& manifest, const QString& workspace, Promise<void> ready);
    Task<void> mountSelectedDesktop();
    Task<void> openWorkspace(const std::optional<QString>& manifest, const QString& workspace);
    void closeSession();
    void closeConsumers();
    void closeLayout();
    void closeDesktop();
    void closeKernel();
    void failCleanup(ErrorInfo error, const char* phase);
    std::shared_ptr<DesktopBootstrap> bootstrap_;
    RealmMap workbenchServices_;
    RuntimeId logRuntime_;
    std::shared_ptr<DesktopRunLogOwner> runLog_;
    RuntimeId desktopRuntime_;
    std::optional<Instance> desktopInstance_;
    std::optional<Instance> layout_;
    DesktopSelection selection_;
    bool safeDesktop_;
    RuntimeId extensionsRuntime_;
    std::weak_ptr<WorkspaceExtensions> extensions_;
    std::vector<ProviderHandle> providers_;
    std::string initial_;
    Promise<void> closed_;
    bool closing_ = false;
};
} // namespace eve::workbench
