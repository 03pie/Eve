/** @file module_runtime_header.cpp
 * @brief 原生 Runtime 描述的公开头可在无 Qt 工具链独立包含。
 */
#include <cordis/module/runtime.hpp>
#include <type_traits>
static_assert(std::is_copy_constructible_v<cordis::module::NativePlugin>);
