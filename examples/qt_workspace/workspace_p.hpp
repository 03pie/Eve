/** @file workspace_p.hpp
 * @brief 装配失败只搬运原 ErrorInfo；公共示例 take 不承担内部错误转换。
 */
#pragma once
#include "workspace.hpp"

namespace notebook::detail {
struct InitializationFailure {
    eve::ErrorInfo error;
};
template <class T> T require(eve::Result<T> result)
{
    if (!result.hasValue())
        throw InitializationFailure{result.error()};
    return std::move(result).value();
}
inline void require(eve::Result<void> result)
{
    if (!result.hasValue())
        throw InitializationFailure{result.error()};
}
} // namespace notebook::detail
