/** @file unload_p.hpp
 * @brief 唯一目录终结与私有 Windows 调用边界，不对 SDK 或插件暴露。
 */
#pragma once
#include "catalog_p.hpp"

namespace eve::module::detail {
struct NativeUnloadSystem {
    BOOL(WINAPI* release)(HMODULE) = FreeLibrary;
    BOOL(WINAPI* probe)(DWORD, LPCWSTR, HMODULE*) = GetModuleHandleExW;
    SIZE_T(WINAPI* query)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) = VirtualQuery;
};
Result<void> unloadGeneration(GenerationId id, const NativeUnloadSystem& system = {});
} // namespace eve::module::detail
