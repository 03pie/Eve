/** @file workspace_extensions.hpp
 * @brief Connects the user installation catalog to one workspace's live configuration session.
 */
#pragma once
#include "desktop_contract.hpp"
#include <eve/extensions/catalog.hpp>
#include <eve/loader/kernel_driver.hpp>
#include <eve/ui/extensions.hpp>
#include <map>

namespace eve::workbench {
struct WorkspaceInputs {
    std::weak_ptr<Kernel> kernel;
    std::shared_ptr<qt::QtScheduler> scheduler;
    std::function<std::shared_ptr<DesktopView>()> desktop;
    std::function<bool()> closing;
    std::function<QString()> selected;
    std::function<Result<QString>()> requested;
    std::function<Result<void>(QString)> select;
    std::function<Result<void>()> restart;
};
class WorkspaceExtensions final : public std::enable_shared_from_this<WorkspaceExtensions> {
public:
    WorkspaceExtensions(Context owner, WorkspaceInputs inputs, ServiceRef<DesktopRunLogSink> log);
    std::shared_ptr<ui::ExtensionControls> controls() const { return controls_; }
    const extensions::Catalog& catalog() const { return catalog_; }
    std::vector<RuntimeId> prepare(const loader::ParsedConfiguration& target,
                                   const std::optional<extensions::Package>& developer);
    std::shared_ptr<loader::KernelReconcileDriver> driver() const { return driver_; }
    void bindSession(std::shared_ptr<loader::ConfigurationSession> session) { session_ = std::move(session); }
    void setActiveDesktopHash(QString hash) { activeDesktopHash_ = std::move(hash); }
private:
    enum class Activation { AfterInstall, Enable, Disable };
    std::vector<ui::ExtensionItem> inventory() const;
    Task<void> apply(ui::ExtensionAction action, const QString& id, const QString& path, const QString& digest,
                     std::optional<QString> expectedInstalledHash);
    loader::ConfiguredPlugin attach(const extensions::Package& package, const std::string& alias,
                                    extensions::ExtensionSource source = extensions::ExtensionSource::InstalledPackage);
    Task<void> updateInstances(const extensions::Package& package, Activation action);
    Task<void> reconcile(const loader::ParsedConfiguration& desired);
    void writeLog(std::string text, ui::OutputLevel level, ui::OutputDiagnostic diagnostic) const;
    Context owner_;
    WorkspaceInputs inputs_;
    ServiceRef<DesktopRunLogSink> log_;
    std::shared_ptr<loader::ConfigurationSession> session_;
    extensions::Catalog catalog_;
    std::shared_ptr<loader::KernelReconcileDriver> driver_;
    std::shared_ptr<ui::ExtensionControls> controls_;
    struct Resident { extensions::Package package; loader::ConfiguredPlugin definition; };
    std::map<QString, Resident> resident_;
    QString activeDesktopHash_;
};
Result<Runtime> registerWorkspaceExtensions(Kernel& kernel, WorkspaceInputs inputs,
    std::function<void(std::weak_ptr<WorkspaceExtensions>)> activated);
}
