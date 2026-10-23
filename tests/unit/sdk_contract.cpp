/**
 * @file sdk_contract.cpp
 * @brief 验证 SDK 公共类型及构建信息契约。
 */
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

/**
 * @brief 验证 SDK 错误头文件能够独立包含。
 * @return 头文件隔离检查通过时返回 true。
 */
bool sdkHeaderIsolation();

/**
 * @brief 验证标识类型安全性和 SDK 头文件隔离。
 * @return 所有契约检查通过时返回 0，否则返回对应检查编号。
 */
int main()
{
    const auto info = cordis::buildInfo();
    // 不使用 assert()，确保这些契约检查在 Release 构建中仍会执行。
    if (info.version.empty() || info.compiler.empty()) return 1;
    if (info.pointerBits != sizeof(void*) * 8U) return 2;
    return sdkHeaderIsolation() ? 0 : 3;
}
