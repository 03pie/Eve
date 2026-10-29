/** @file failure_p.hpp
 * @brief 目录与业务宿主共用的宿主错误信号，不引入装载器或外部指针。
 */
#pragma once
#include <cordis/error.hpp>
#include <mutex>
#include <optional>
namespace cordis::module::detail {
struct GenerationFailureSignal {
    std::mutex mutex;
    std::optional<ErrorInfo> error;
};
} // namespace cordis::module::detail
