/**
 * @file build_info.hpp
 * @brief 声明用于查询当前构建元数据的类型和函数。
 */
#pragma once

#include <cstddef>
#include <string_view>

namespace cordis {
/**
 * @brief 描述当前构建的版本、编译器和指针宽度。
 */
struct BuildInfo {
    std::string_view version;
    std::string_view compiler;
    std::size_t pointerBits;
};

/**
 * @brief 返回编译时确定的构建信息。
 * @return 当前构建的版本、编译器和指针宽度。
 */
[[nodiscard]] BuildInfo buildInfo() noexcept;
/** @brief 当前 Core 实现的进程内只读身份；比较身份不能代替 SDK/工具链兼容性校验。 */
[[nodiscard]] const void* coreRuntimeIdentity() noexcept;
} // namespace cordis
