/** @file module_native_header.cpp
 * @brief 原生业务 SDK 声明可在无 Qt/Windows 头的 GCC/Core 构建独立包含。
 */
#include <cordis/module/native.hpp>
static_assert(sizeof(cordis::module::NativeModuleSnapshot) > 0, "native SDK header isolation");
