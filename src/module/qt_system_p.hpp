/** @file qt_system_p.hpp
 * @brief Workbench-only entry point for the fixed desktop in the application bundle.
 */
#pragma once
#include <eve/module/qt_module.hpp>

namespace eve::module::detail {
inline constexpr char systemDesktopModuleId[] = "org.eve.desktop";
inline constexpr char systemDesktopPluginId[] = "eve.desktop";

/** @brief Validate the pinned artifact's path and identities before mapping the module. */
Result<ModuleLoadReport> loadSystemDesktopModule();
/** Match catalogue identity and digest against the held file before any external code runs. */
Result<ModuleLoadReport> loadInstalledQtModule(const std::string& root, const std::string& moduleId,
    const std::string& pluginId, const std::string& digest);
} // namespace eve::module::detail
