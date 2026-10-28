/** @file module_manifest_header.cpp
 * @brief 即使关闭 Qt 构建，模块预检的公开头仍可独立包含。
 */
#include <cordis/module/manifest.hpp>
static_assert(sizeof(cordis::module::PreflightLimits) > 0, "module headers must remain independent of Qt");
