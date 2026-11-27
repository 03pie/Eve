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
} // namespace eve::module::detail
