/** @file workspace_extensions.hpp
 * @brief Connects the user installation catalog to one workspace's live configuration session.
 */
#pragma once
#include <eve/extensions/catalog.hpp>
#include <eve/loader/kernel_driver.hpp>
#include <eve/ui/extensions.hpp>
#include <map>

namespace eve::workbench {
class Workbench;
class WorkspaceExtensions final {
public:
    explicit WorkspaceExtensions(Workbench& host);
    std::shared_ptr<ui::ExtensionControls> controls() const { return controls_; }
    const extensions::Catalog& catalog() const { return catalog_; }
    std::vector<RuntimeId> prepare(const loader::ParsedConfiguration& target,
                                   const std::optional<extensions::Package>& developer);
    std::shared_ptr<loader::KernelReconcileDriver> driver() const { return driver_; }
private:
    enum class Activation { AfterInstall, Enable, Disable };
    std::vector<ui::ExtensionItem> inventory() const;
    Task<void> apply(ui::ExtensionAction action, const QString& id, const QString& path, const QString& digest,
                     std::optional<QString> expectedInstalledHash);
    loader::ConfiguredPlugin attach(const extensions::Package& package, const std::string& alias, bool pinned);
    Task<void> updateInstances(const extensions::Package& package, Activation action);
    Task<void> reconcile(const loader::ParsedConfiguration& desired);
    Workbench& host_;
    extensions::Catalog catalog_;
    std::shared_ptr<loader::KernelReconcileDriver> driver_;
    std::shared_ptr<ui::ExtensionControls> controls_;
    std::shared_ptr<const int> lifetime_ = std::make_shared<const int>(0);
    struct Resident { extensions::Package package; loader::ConfiguredPlugin definition; };
    std::map<QString, Resident> resident_;
    QString activeDesktopHash_;
    friend class Workbench;
};
}
