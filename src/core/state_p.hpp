/** @file state_p.hpp
 * @brief 通用状态协议；只由实际实例持有，不登记第二套实例身份。
 */
#pragma once
#include <cordis/migration_state.hpp>
#include <memory>
#include <optional>

namespace cordis::detail {
bool validStateSchema(const StateSchema&) noexcept;
Result<void> checkStateSnapshot(const StateSnapshot&);
class EpochState {
public:
    EpochState(std::shared_ptr<const StateContract>, std::optional<StateSnapshot>);
    Result<void> prepare();
    Result<void> bind(StateParticipant);
    Result<void> ready() const;
    Result<std::optional<StateSnapshot>> capture() const;

private:
    Result<void> fail(ErrorInfo);
    std::shared_ptr<const StateContract> contract_;
    std::optional<StateSnapshot> input_;
    std::optional<StateParticipant> participant_;
    std::optional<ErrorInfo> failure_;
    bool prepared_ = false;
};
} // namespace cordis::detail
