/** @file native_catalog_p.hpp
 * @brief 受审档案在本构建生成的宿主纯值；不读取运行时文件或改变模块政策。
 */
#pragma once
#include <cordis/module/manager.hpp>

namespace notebook::detail {
cordis::module::NativeUnloadWhitelist productNativeApprovals();
} // namespace notebook::detail
