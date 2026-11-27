/** @file factory_p.hpp
 * @brief Official desktop module. The resident Qt root creates one QObject for each kernel instance.
 */
#pragma once
#include "desktop_contract.hpp"
#include <eve/module/qt_plugin.hpp>

namespace eve::workbench {
class DesktopInstance final : public QObject, public module::IQtInstanceV1 {
    Q_OBJECT
    Q_INTERFACES(eve::module::IQtInstanceV1)
public:
    Task<void> activate(Context context) override { return activateDesktop(std::move(context)); }
};

class DesktopFactory final : public QObject, public module::IQtPluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.eve.IQtPluginV1/1.0" FILE "desktop_metadata.json")
    Q_INTERFACES(eve::module::IQtPluginV1)
public:
    QStringList pluginIds() const override { return {QString::fromLatin1(desktopId)}; }
    QObject* create(const QString& id) override
    {
        return id == QString::fromLatin1(desktopId) ? new DesktopInstance : nullptr;
    }
};
} // namespace eve::workbench
