/** @file module_manager_header.cpp
 * @brief 无 Qt 构建也可独立包含原生管理器的源码 SDK 声明。
 */
#include <eve/module/manager.hpp>
static_assert(sizeof(eve::module::ModuleLoadReport) > 0, "manager SDK must be independent of Windows/Qt");
