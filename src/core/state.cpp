/** @file state.cpp
 * @brief 值预算、契约转换和参与者兑现；不依赖 Module、Loader 或 JSON 解析器。
 */
#include "runtime_state_p.hpp"
#include "state_p.hpp"
#include <algorithm>
#include <utility>

namespace cordis::detail {
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "instance.state";
    return value;
}
bool validPayload(const std::string& json) noexcept
{
    return !json.empty() && json.size() <= maxStateJsonBytes;
}
Result<StateSnapshot> validateStateValue(const StateContract& contract, const StateSnapshot& input)
{
    using Output = Result<StateSnapshot>;
    if (!validStateSchema(contract.schema) || !contract.validate)
        return Output::failure(error(ErrorCode::InvalidArgument, "invalid target state contract"));
    auto valid = checkStateSnapshot(input);
    if (!valid.hasValue())
        return Output::failure(valid.error());
    auto converted = contract.validate(input);
    if (!converted.hasValue())
        return Output::failure(converted.error());
    if (!validPayload(converted.value()))
        return Output::failure(error(ErrorCode::InvalidArgument, "converted state exceeds payload budget"));
    return Output::success(StateSnapshot{contract.schema, std::move(converted.value())});
}
} // namespace
bool validStateSchema(const StateSchema& schema) noexcept
{
    return schema.version && !schema.id.empty() && schema.id.size() <= maxStateSchemaBytes &&
           std::all_of(schema.id.begin(), schema.id.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7e; });
}
Result<void> checkStateSnapshot(const StateSnapshot& snapshot)
{
    return validStateSchema(snapshot.schema) && validPayload(snapshot.json)
               ? Result<void>::success()
               : Result<void>::failure(error(ErrorCode::InvalidArgument, "invalid state schema or payload budget"));
}
Result<StateSnapshot> RuntimeStateAccess::validate(const Runtime& runtime, const StateSnapshot& input)
{
    using Output = Result<StateSnapshot>;
    const auto contract = runtime.state_;
    if (!contract)
        return Output::failure(error(ErrorCode::InvalidArgument, "state input requires a stateful runtime"));
    try {
        return validateStateValue(*contract, input);
    } catch (...) {
        return Output::failure(error(ErrorCode::CallbackFailed, "state validation threw"));
    }
}
EpochState::EpochState(std::shared_ptr<const StateContract> contract, std::shared_ptr<const OwnedStateSnapshot> input)
    : contract_(std::move(contract)), input_(std::move(input))
{
}
Result<void> EpochState::fail(ErrorInfo failure)
{
    if (!failure_)
        failure_ = std::move(failure);
    return Result<void>::failure(*failure_);
}
Result<void> EpochState::prepare()
{
    if (prepared_)
        return fail(error(ErrorCode::InvalidArgument, "state prepared twice"));
    prepared_ = true;
    if (!input_)
        return Result<void>::success();
    if (!contract_)
        return fail(error(ErrorCode::InvalidArgument, "state input requires a stateful runtime"));
    auto converted = validateStateValue(*contract_, input_->value);
    if (!converted.hasValue())
        return fail(converted.error());
    auto owned = ownStateSnapshot(std::move(converted.value()), input_->bytes.budget(), input_->format);
    if (!owned.hasValue())
        return fail(owned.error());
    input_ = std::move(owned.value());
    return Result<void>::success();
}
Result<void> EpochState::bind(StateParticipant participant)
{
    if (failure_)
        return Result<void>::failure(*failure_);
    if (!prepared_ || !contract_ || participant_ || !participant.capture || !participant.restore)
        return fail(error(ErrorCode::InvalidArgument, "invalid or duplicate state participant"));
    participant_.emplace(std::move(participant));
    if (!input_)
        return Result<void>::success();
    auto restored = participant_->restore(input_->value.json);
    if (!restored.hasValue())
        return fail(restored.error());
    imported_ = true;
    input_.reset();
    return Result<void>::success();
}
Result<void> EpochState::ready() const
{
    if (failure_)
        return Result<void>::failure(*failure_);
    if (!prepared_ || (contract_ && !participant_) || input_)
        return Result<void>::failure(error(ErrorCode::ActivationFailed, "state participant did not consume input"));
    return Result<void>::success();
}
Result<std::optional<StateSnapshot>> EpochState::capture() const
{
    using Output = Result<std::optional<StateSnapshot>>;
    auto available = ready();
    if (!available.hasValue())
        return Output::failure(available.error());
    if (!contract_)
        return Output::success({});
    auto json = participant_->capture();
    if (!json.hasValue())
        return Output::failure(json.error());
    if (!validPayload(json.value()))
        return Output::failure(error(ErrorCode::InvalidArgument, "captured state exceeds payload budget"));
    return Output::success(StateSnapshot{contract_->schema, std::move(json).value()});
}
} // namespace cordis::detail
