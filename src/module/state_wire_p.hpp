/** @file state_wire_p.hpp
 * @brief 两后端共用的有界纯值边界；调用方必须先建立真实 Call/对象存活屏障。
 */
#pragma once
#include <eve/migration_state.hpp>
#include <eve_abi/state.h>
#include <functional>
#include <optional>

namespace eve::module::detail {
Result<EveStateApiV1> queryStateApi(EveQueryStateFnV1);
Result<EveStateSchemaV1> encodeStateSchema(const StateSchema&);
Result<std::optional<StateSchema>> readStateSchema(const std::function<EveStatus(EveStateSchemaV1*)>&);
Result<std::string> readStateJson(const std::function<EveStatus(EveStateBufferV1*)>&);
Result<void> stateStatus(EveStatus);
} // namespace eve::module::detail
