/** @file brand_p.hpp
 * @brief 宿主内部的 Eve 品牌图标，不向插件 SDK 暴露资源或 Ela 类型。
 */
#pragma once
#include <QIcon>

namespace cordis::ui::detail {
/** @brief 从内嵌透明 PNG 返回原版多尺寸图标，不依赖外部文件或 SVG 插件。 */
QIcon eveApplicationIcon();
} // namespace cordis::ui::detail
