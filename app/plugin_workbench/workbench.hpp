/** @file workbench.hpp
 * @brief 应用宿主只持有调度器、内核、工作区和日志。桌面由其内核实例托管。
 */
#pragma once
#include "desktop_contract.hpp"
#include <QString>
#include <optional>

namespace eve::workbench {
QString workbenchDataFile(const QString& applicationData, const QString& relativeFile);
class Workbench final {
public:
    Workbench();
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
    Task<void> openWorkspace(const std::optional<QString>& manifest, const QString& workspace);
    void closeSession();
    void closeConsumers();
    void closeKernel();
    void failCleanup(ErrorInfo error, const char* phase);
    std::shared_ptr<DesktopBootstrap> bootstrap_;
    RuntimeId desktopRuntime_;
    std::vector<ProviderHandle> providers_;
    std::string initial_;
    Promise<void> closed_;
    bool closing_ = false;
};
} // namespace eve::workbench
