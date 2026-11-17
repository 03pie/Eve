/** @file native_state_p.hpp
 * @brief 原生状态适配；描述属于原业务记录，参与者只持有真实原生实例。
 */
#pragma once
#include "state_wire_p.hpp"
#include <map>
#include <memory>
#include <vector>

namespace cordis::module {
class NativeInstance;
namespace detail {
struct NativeModuleControl;
struct NativeStateApi {
    CordisStateApiV1 table;
    std::map<std::string, StateSchema> schemas;
};
/** @brief 仅在 open 的实际 Call/negotiating 窗口中调用；缺失可选符号返回空值。 */
Result<std::optional<NativeStateApi>> queryNativeState(CordisQueryStateFnV1, const std::vector<std::string>&);
Result<std::string> validateNativeState(const std::shared_ptr<NativeModuleControl>&, const std::string&,
                                        const StateSnapshot&);
struct NativeStateAccess {
    static Result<StateParticipant> participant(const NativeInstance&, const StateSchema&);
};
} // namespace detail
} // namespace cordis::module
