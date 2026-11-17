/** @file state_wire_p.hpp
 * @brief 两后端共用的有界纯值边界；调用方必须先建立真实 Call/对象存活屏障。
 */
#pragma once
#include <cordis/migration_state.hpp>
#include <cordis_abi/state.h>
#include <functional>
#include <optional>

namespace cordis::module::detail {
Result<CordisStateApiV1> queryStateApi(CordisQueryStateFnV1);
Result<CordisStateSchemaV1> encodeStateSchema(const StateSchema&);
Result<std::optional<StateSchema>> readStateSchema(const std::function<CordisStatus(CordisStateSchemaV1*)>&);
Result<std::string> readStateJson(const std::function<CordisStatus(CordisStateBufferV1*)>&);
Result<void> stateStatus(CordisStatus);
} // namespace cordis::module::detail
