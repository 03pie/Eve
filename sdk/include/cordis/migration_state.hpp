/** @file migration_state.hpp
 * @brief 迁移只传独立拥有的版本化值；不包含实例、服务或模块句柄。
 */
#pragma once
#include <cordis/result.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cordis {
inline constexpr std::size_t maxStateSchemaBytes = 128;
inline constexpr std::size_t maxStateJsonBytes = 1024 * 1024;
struct StateSchema {
    /** @brief 1–128 个可见 ASCII 字节，不允许空白；标识与配置 schema 无关。 */
    std::string id;
    std::uint32_t version = 0;
};
struct StateSnapshot {
    StateSchema schema;
    /** @brief 独立拥有的 UTF-8 JSON，非空且至多 1 MiB；JSON/业务语义由上层验证。 */
    std::string json;
};
struct StateContract {
    StateSchema schema;
    /** @brief 无业务副作用的有界校验/转换，返回本契约版本的数据；同版本也必须校验。 */
    std::function<Result<std::string>(const StateSnapshot&)> validate;
};
struct StateParticipant {
    /** @brief owner 线程的一致检查点；不能等待 worker、泵事件或调用其他业务服务。 */
    std::function<Result<std::string>()> capture;
    /** @brief 接收已校验的本契约版本数据，须在业务 start/activate 前完成；输入只在调用期间有效。 */
    std::function<Result<void>(std::string_view)> restore;
};
} // namespace cordis
