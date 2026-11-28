/** @file qt_artifact_load_p.hpp
 * @brief Verify an artifact selected by the composition root before mapping its Qt module.
 */
#pragma once
#include <eve/module/qt_module.hpp>

namespace eve::module::detail {
struct BundledQtModule {
    std::string manifestPath; // Relative to the application directory.
    std::string libraryName;  // DLL basename alongside the manifest.
    std::string moduleId;
    std::string pluginId;
    std::string version;
};

Result<ModuleLoadReport> loadBundledQtModule(const BundledQtModule& expected);
/** Match catalogue identity and digest against the held file before any external code runs. */
Result<ModuleLoadReport> loadInstalledQtModule(const std::string& root, const std::string& moduleId,
    const std::string& pluginId, const std::string& digest);
} // namespace eve::module::detail
