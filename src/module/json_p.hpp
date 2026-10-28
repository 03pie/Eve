/** @file json_p.hpp
 * @brief ABI JSON 的有界语法/Unicode 检查；不构造 DOM 或改变数字原文。
 */
#pragma once
#include <string_view>

namespace cordis::module::detail {
/** @brief 接受单个 UTF-8 JSON 值，最多 64 层容器，转义必须为 Unicode 标量。
 * 保留重复成员名及原始文本；业务 schema 在其所属桥中另验。空文本不属于 JSON。
 */
bool validAbiJson(std::string_view text) noexcept;
} // namespace cordis::module::detail
