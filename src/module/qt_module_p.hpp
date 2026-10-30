/** @file qt_module_p.hpp
 * @brief Qt 后端的驻留 root、已协商工厂和业务对象身份；不复制模块目录。
 */
#pragma once
#include "catalog_p.hpp"
#include <QPluginLoader>
#include <QPointer>
#include <cordis/module/qt_plugin.hpp>
#include <set>
namespace cordis::module::detail {
struct QtResidency final : BackendResidency {
    explicit QtResidency(const std::string& path) : loader(QString::fromUtf8(path.c_str())) {}
    QPluginLoader loader;
    QPointer<QObject> root;
    ModuleLease rootLease;
    IQtPluginV1* factory = nullptr;
    bool negotiating = false;
    bool creating = false;
    std::set<QObject*> businessObjects;
};
} // namespace cordis::module::detail
