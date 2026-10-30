/** @file module_manifest_header.cpp
 * @brief 即使关闭 Qt 构建，模块预检的公开头仍可独立包含。
 */
#include <cordis/module/qt_manifest.hpp>
static_assert(sizeof(cordis::module::PreflightLimits) > 0, "module headers must remain independent of Qt");

#include <cordis/module/install.hpp>
#include <cordis/module/qt_module.hpp>
#include <type_traits>
static_assert(std::is_copy_constructible_v<cordis::module::InstalledArtifact>, "installation guards are shareable");
static_assert(std::is_nothrow_move_constructible_v<cordis::module::InstalledArtifact>,
              "guards transfer without allocation");
