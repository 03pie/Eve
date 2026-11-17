/** @file native_state.cpp
 * @brief 原生状态使用实际 root/instance 和原 Call 账目；不另建实例或请求目录。
 */
#include "failure_p.hpp"
#include "host_p.hpp"
#include "json_p.hpp"
#include "native_state_p.hpp"
#include <utility>

namespace cordis::module::detail {
namespace {
/** @brief 只在 owner 栈内存在；关闭先失效，取消/销毁及 root 完成等待本调用退出。 */
class StateCall {
public:
    StateCall(std::shared_ptr<NativeModuleControl> root, std::shared_ptr<NativeInstanceControl> instance = {})
        : root_(std::move(root)), instance_(std::move(instance))
    {
    }
    StateCall(const StateCall&) = delete;
    StateCall& operator=(const StateCall&) = delete;
    Result<void> enter(const char* label, NativeInstanceState required = NativeInstanceState::Created)
    {
        if (!root_->ownerThread())
            return Result<void>::failure(hostError(ErrorCode::WrongThread, "state call requires native owner"));
        auto target = root_->scheduler.lock();
        if (!target || !target->isOpen())
            return Result<void>::failure(hostError(ErrorCode::SchedulerClosed, "native state scheduler closed"));
        auto held = root_->source.ledger.acquire(ResourceKind::Call, label);
        if (!held.hasValue())
            return Result<void>::failure(held.error());
        lease_ = std::move(held).value();
        auto admit = [&]() -> Result<void> {
            if (root_->closing || root_->closed || root_->faulted || !root_->record->stateApi)
                return Result<void>::failure(hostError(ErrorCode::ContextClosing, "native state source unavailable"));
            if (root_->stateCalls)
                return Result<void>::failure(hostError(ErrorCode::ConfigConflict, "native state call already active"));
            if (instance_ && (instance_->closing || instance_->uncertain || instance_->stateCalling ||
                              !instance_->native || instance_->state != required))
                return Result<void>::failure(hostError(ErrorCode::ContextClosing, "native state instance unavailable"));
            ++root_->stateCalls;
            if (instance_)
                instance_->stateCalling = true;
            entered_ = true;
            return Result<void>::success();
        };
        if (instance_) {
            std::scoped_lock lock(root_->mutex, instance_->mutex);
            return admit();
        }
        std::lock_guard<std::mutex> lock(root_->mutex);
        return admit();
    }
    ~StateCall()
    {
        if (!entered_)
            return;
        try {
            if (instance_) {
                std::scoped_lock lock(root_->mutex, instance_->mutex);
                instance_->stateCalling = false;
                --root_->stateCalls;
            } else {
                std::lock_guard<std::mutex> lock(root_->mutex);
                --root_->stateCalls;
            }
            lease_.reset();
            if (instance_)
                instance_->beginClose();
            root_->finish();
        } catch (...) {
            fault(hostError(ErrorCode::CleanupFailed, "native state close continuation failed"));
        }
    }
    template <class T> Result<T> checked(Result<T> result)
    {
        if (!result.hasValue() && result.error().code == ErrorCode::AbiProtocol) {
            fault(result.error());
            return result;
        }
        auto current = [&]() -> Result<void> {
            auto target = root_->scheduler.lock();
            if (!target || !target->isOpen())
                return Result<void>::failure(hostError(ErrorCode::SchedulerClosed, "state call lost scheduler"));
            auto healthy = [&] {
                return !root_->closing && !root_->closed && !root_->faulted &&
                       (!instance_ || (!instance_->closing && !instance_->uncertain && instance_->native));
            };
            bool available;
            if (instance_) {
                std::scoped_lock lock(root_->mutex, instance_->mutex);
                available = healthy();
            } else {
                std::lock_guard<std::mutex> lock(root_->mutex);
                available = healthy();
            }
            return available ? Result<void>::success()
                             : Result<void>::failure(hostError(ErrorCode::ContextClosing, "state call invalidated"));
        }();
        if (!current.hasValue()) {
            if (current.error().code == ErrorCode::SchedulerClosed)
                fault(current.error());
            return Result<T>::failure(current.error());
        }
        return result;
    }

private:
    void fault(const ErrorInfo& error) noexcept
    {
        if (instance_)
            instance_->quarantine(error);
        else
            root_->stateFault(error);
    }
    std::shared_ptr<NativeModuleControl> root_;
    std::shared_ptr<NativeInstanceControl> instance_;
    ModuleLease lease_;
    bool entered_ = false;
};
} // namespace
void NativeModuleControl::stateFault(ErrorInfo error) noexcept
{
    try {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (faulted)
                return;
            faulted = true;
            report.quiescent = false;
            report.errors.push_back(error);
        }
        (void)source.ledger.pin("nativeStateUncertain");
        recordBusinessFailure(source.generation, error);
        recordNativePhase(source.generation, ModuleState::Failed, error);
    } catch (...) {
    }
}
Result<std::optional<NativeStateApi>> queryNativeState(CordisQueryStateFnV1 query,
                                                       const std::vector<std::string>& plugins)
{
    using Output = Result<std::optional<NativeStateApi>>;
    if (!query)
        return Output::success({});
    auto table = queryStateApi(query);
    if (!table.hasValue())
        return Output::failure(table.error());
    NativeStateApi result{table.value(), {}};
    for (const auto& plugin : plugins) {
        auto schema =
            readStateSchema([&](CordisStateSchemaV1* output) { return result.table.describe(bytes(plugin), output); });
        if (!schema.hasValue())
            return Output::failure(schema.error());
        if (schema.value())
            result.schemas.emplace(plugin, std::move(*schema.value()));
    }
    return Output::success(std::move(result));
}
Result<std::string> validateNativeState(const std::shared_ptr<NativeModuleControl>& root, const std::string& plugin,
                                        const StateSnapshot& input)
{
    using Output = Result<std::string>;
    auto schema = encodeStateSchema(input.schema);
    if (!schema.hasValue() || input.json.size() > maxStateJsonBytes || !validAbiJson(input.json))
        return Output::failure(hostError(ErrorCode::InvalidArgument, "invalid native state input"));
    StateCall call(root);
    auto entered = call.enter("native.state.validate");
    if (!entered.hasValue())
        return Output::failure(entered.error());
    if (!root->record->stateApi->schemas.count(plugin))
        return Output::failure(hostError(ErrorCode::InvalidArgument, "plugin does not declare state"));
    return call.checked(readStateJson([&](CordisStateBufferV1* output) {
        return root->record->stateApi->table.validate(bytes(plugin), &schema.value(), bytes(input.json), output);
    }));
}
Result<StateParticipant> NativeStateAccess::participant(const NativeInstance& instance, const StateSchema& schema)
{
    using Output = Result<StateParticipant>;
    auto self = instance.control_;
    if (!self || !self->ownerThread())
        return Output::failure(hostError(self ? ErrorCode::WrongThread : ErrorCode::InvalidArgument,
                                         "native state participant requires actual owner"));
    auto encoded = encodeStateSchema(schema);
    if (!encoded.hasValue())
        return Output::failure(encoded.error());
    if (!self->root->record->stateApi)
        return Output::failure(hostError(ErrorCode::InvalidArgument, "native state extension missing"));
    const auto& schemas = self->root->record->stateApi->schemas;
    const auto found = schemas.find(self->pluginId);
    if (found == schemas.end() || found->second.id != schema.id || found->second.version != schema.version)
        return Output::failure(hostError(ErrorCode::InvalidArgument, "native state contract differs from factory"));
    StateParticipant participant;
    participant.capture = [self]() -> Result<std::string> {
        StateCall call(self->root, self);
        auto entered = call.enter("native.state.capture", NativeInstanceState::Started);
        if (!entered.hasValue())
            return Result<std::string>::failure(entered.error());
        return call.checked(readStateJson([&](CordisStateBufferV1* output) {
            return self->root->record->stateApi->table.capture(self->native, output);
        }));
    };
    participant.restore = [self, schema = encoded.value()](std::string_view json) -> Result<void> {
        if (json.size() > maxStateJsonBytes || !validAbiJson(json))
            return Result<void>::failure(hostError(ErrorCode::InvalidArgument, "invalid native state payload"));
        StateCall call(self->root, self);
        auto entered = call.enter("native.state.restore");
        if (!entered.hasValue())
            return entered;
        const CordisBytesV1 input{reinterpret_cast<const std::uint8_t*>(json.data()), json.size()};
        try {
            return call.checked(stateStatus(self->root->record->stateApi->table.restore(self->native, &schema, input)));
        } catch (...) {
            return call.checked(
                Result<void>::failure(hostError(ErrorCode::AbiProtocol, "exception escaped state restore")));
        }
    };
    return Output::success(std::move(participant));
}
} // namespace cordis::module::detail
