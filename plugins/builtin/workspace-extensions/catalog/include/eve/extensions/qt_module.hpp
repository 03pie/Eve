/** @file qt_module.hpp
 * @brief Map a selected workspace extension after verifying its identity and digest.
 */
#pragma once
#include <eve/extensions/catalog.hpp>
#include <eve/module/qt_module.hpp>
#include <QString>

namespace eve::extensions {
Result<module::ModuleLoadReport> loadQtExtension(const QString& manifestPath, const QString& moduleId,
    const QString& pluginId, const QString& digest, ExtensionSource source = ExtensionSource::InstalledPackage);
}
