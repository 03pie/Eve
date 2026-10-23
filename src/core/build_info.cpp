#include <cordis/build_info.hpp>

namespace cordis {
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
