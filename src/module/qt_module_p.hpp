/** @file qt_module_p.hpp
 * @brief Qt 后端的驻留 root、已协商工厂和业务对象身份；不复制模块目录。
 */
#pragma once
#include "catalog_p.hpp"
#include <QPluginLoader>
#include <QPointer>
#include <cordis/module/qt_plugin.hpp>
#include <cordis/module/qt_state_plugin.hpp>
#include <map>
#include <set>
namespace cordis::module::detail {
struct QtResidency final : BackendResidency {
    explicit QtResidency(const std::string& path) : loader(QString::fromUtf8(path.c_str())) {}
    QPluginLoader loader;
    QPointer<QObject> root;
    ModuleLease rootLease;
    IQtPluginV1* factory = nullptr;
    IQtStatePluginV1* stateFactory = nullptr;
    std::map<std::string, StateSchema> stateSchemas;
    bool negotiating = false;
    bool creating = false;
    bool stateCalling = false;
    std::set<QObject*> businessObjects;
};
Result<void> qtAvailable(const std::shared_ptr<Generation>&, const std::shared_ptr<QtResidency>&);
void qtBusinessFault(const std::shared_ptr<Generation>&, const ErrorInfo&);
} // namespace cordis::module::detail
