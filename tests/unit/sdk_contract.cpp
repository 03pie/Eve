#include <cordis/build_info.hpp>
#include <cordis/identity.hpp>
#include <cordis/state.hpp>
#include <type_traits>

static_assert(!std::is_same_v<cordis::InstanceId, cordis::BindingId>);
static_assert(!std::is_convertible_v<cordis::InstanceId, cordis::BindingId>);
static_assert(!std::is_convertible_v<std::uint64_t, cordis::InstanceId>);
static_assert(!std::is_convertible_v<cordis::InstanceId, std::uint64_t>);
static_assert(!cordis::InstanceId{}.valid());
static_assert(std::is_trivially_copyable_v<cordis::InstanceId>);

bool sdkHeaderIsolation();

int main()
{
    const auto info = cordis::buildInfo();
    // Do not use assert(): these checks must remain active in Release.
    if (info.version.empty() || info.compiler.empty()) return 1;
    if (info.pointerBits != sizeof(void*) * 8U) return 2;
    return sdkHeaderIsolation() ? 0 : 3;
}
