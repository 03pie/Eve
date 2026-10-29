/** @file json_p.hpp
 * @brief ABI JSON 的有界语法/Unicode 检查；不构造 DOM 或改变数字原文。
 */
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cordis::module::detail {
/** @brief 接受单个 UTF-8 JSON 值，最多 64 层容器，转义必须为 Unicode 标量。
 * 保留重复成员名及原始文本；业务 schema 在其所属桥中另验。空文本不属于 JSON。
 */
bool validAbiJson(std::string_view text) noexcept;
/** @brief 验证原始 UTF-8 文本的 Unicode 标量；ASCII 控制字符按长度保留。 */
bool validAbiUtf8(std::string_view text) noexcept;
/** @brief 严格解码能力名称数组，最多 128 项/每项 128 UTF-8 字节，拒绝空值和转义同名项。 */
std::optional<std::vector<std::string>> abiStringArray(std::string_view text);
} // namespace cordis::module::detail
