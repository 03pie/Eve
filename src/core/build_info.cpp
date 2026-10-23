/**
 * @file build_info.cpp
 * @brief 生成当前编译器和目标平台的构建信息。
 */
#include <cordis/build_info.hpp>

namespace cordis {
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
    return {CORDIS_PROJECT_VERSION, compiler, sizeof(void*) * 8U};
}
} // namespace cordis
