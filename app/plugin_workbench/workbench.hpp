#pragma once

#include <QString>
#include <cordis/kernel.hpp>
#include <cordis/loader/kernel_driver.hpp>
#include <cordis/loader/session.hpp>
#include <cordis/ui/configuration.hpp>
#include <cordis/ui/ela_shell.hpp>

namespace cordis::workbench {
// The application thread owns this object until close() settles. The caller owns its event loop.
class Workbench final {
public:
    Workbench();
    void open(const QString& manifestPath, const QString& workspaceDir);
    void publishUi();
    loader::AcceptedTarget applyInitial();
    Task<void> close();
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::shared_ptr<Kernel> kernel;
    ui::ElaShell shell;
    std::shared_ptr<loader::ConfigurationSession> session;
private:
    void closeSession();
    void closeKernel();
    void closeShell();
    std::shared_ptr<ui::IConfigurationManagement> management_;
    std::vector<ProviderHandle> providers_;
    std::optional<ui::PageHandle> configPage_;
    std::string initial_;
    Promise<void> closed_;
    bool closing_ = false;
    bool published_ = false;
};
} // namespace cordis::workbench
