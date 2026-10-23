/**
 * @file header_isolation.cpp
 * @brief 验证 SDK 头文件能够独立包含其直接依赖。
 */
// 故意先包含 error.hpp：SDK 头文件必须自行包含其直接依赖。
#include <cordis/error.hpp>
#include <cordis/state.hpp>

/**
 * @brief 验证错误类型头文件隔离及实例标识字段。
 * @return 标识有效且错误因果链正确时返回 true。
 */
bool sdkHeaderIsolation()
{
    cordis::ErrorInfo error;
    error.instance = cordis::InstanceId{1};
    error.causes.push_back(cordis::ErrorInfo{});
    return error.instance->valid() && error.causes.size() == 1;
}
