/** @file module_manifest_header.cpp
 * @brief 即使关闭 Qt 构建，模块预检的公开头仍可独立包含。
 */
#include <eve/module/qt_manifest.hpp>
static_assert(sizeof(eve::module::PreflightLimits) > 0, "module headers must remain independent of Qt");

#include <eve/module/install.hpp>
#include <eve/module/qt_module.hpp>
#include <type_traits>
static_assert(std::is_copy_constructible_v<eve::module::InstalledArtifact>, "installation guards are shareable");
static_assert(std::is_nothrow_move_constructible_v<eve::module::InstalledArtifact>,
              "guards transfer without allocation");
