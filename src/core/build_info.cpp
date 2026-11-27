/**
 * @file build_info.cpp
 * @brief 生成当前编译器和目标平台的构建信息。
 */
#include <eve/build_info.hpp>
#if defined(EVE_CORE_SHARED) && !defined(_DLL)
#error "Shared Core requires the dynamic MSVC CRT (/MD or /MDd)"
#endif

namespace eve {
/**
 * @brief 返回编译时确定的版本、编译器和指针宽度。
 * @return 当前构建信息。
 */
BuildInfo buildInfo() noexcept
{
#if defined(__clang__)
    constexpr auto compiler = "Clang " __clang_version__;
#elif defined(_MSC_VER)
    constexpr auto compiler = "MSVC";
#elif defined(__GNUC__)
    constexpr auto compiler = "GCC " __VERSION__;
#else
    constexpr auto compiler = "unknown";
#endif
    return {EVE_PROJECT_VERSION, compiler, sizeof(void*) * 8U};
}
const void* coreRuntimeIdentity() noexcept
{
    static unsigned char identity = 0;
    return &identity;
}
} // namespace eve
