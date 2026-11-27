/** @file qt_state.cpp
 * @brief Qt 状态调用只借用实际工厂/实例，以原资源删除屏障保护同步虚调用。
 */
#include "../qt/object_call_p.hpp"
#include "json_p.hpp"
#include "qt_state_p.hpp"
#include "state_wire_p.hpp"
#include <utility>

namespace eve::module::detail {
namespace {
ErrorInfo error(ErrorCode code, const char* message)
{
    auto value = makeError(code, message);
    value.phase = "module.qt.state";
    return value;
}
EveBytesV1 bytes(std::string_view value)
{
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
/** @brief 工厂级防重入，实例另持 ObjectCall；不建立第二个对象所有者或清理入口。 */
class StateCall {
public:
    StateCall(std::shared_ptr<Generation> generation, std::shared_ptr<QtResidency> backend,
              std::shared_ptr<qt::QtScheduler> scheduler, const qt::QObjectResource* resource = nullptr,
              std::function<void(ErrorInfo)> failure = {})
        : generation_(std::move(generation)), backend_(std::move(backend)), scheduler_(std::move(scheduler)),
          resource_(resource), failure_(std::move(failure))
    {
    }
    StateCall(const StateCall&) = delete;
    StateCall& operator=(const StateCall&) = delete;
    Result<void> enter(const char* label)
    {
        auto allowed = current();
        if (!allowed.hasValue())
            return allowed;
        if (backend_->stateCalling || backend_->creating || backend_->negotiating)
            return Result<void>::failure(error(ErrorCode::ConfigConflict, "Qt factory call already active"));
        auto lease = generation_->ledger.acquire(ResourceKind::Call, label);
        if (!lease.hasValue())
            return Result<void>::failure(lease.error());
        lease_ = std::move(lease.value());
        if (resource_) {
            auto held = qt::detail::ObjectCall::enter(*resource_);
            if (!held.hasValue())
                return Result<void>::failure(held.error());
            objectCall_.emplace(std::move(held.value()));
        }
        backend_->stateCalling = true;
        entered_ = true;
        return Result<void>::success();
    }
    ~StateCall()
    {
        if (entered_)
            backend_->stateCalling = false;
        // 实际虚调用已经返回，先退出对象保护，再释放 DLL 调用租约。
        objectCall_.reset();
    }
    template <class T> Result<T> checked(Result<T> result)
    {
        if (!result.hasValue() && result.error().code == ErrorCode::AbiProtocol) {
            fault(result.error());
            return result;
        }
        auto allowed = current();
        if (!allowed.hasValue()) {
            if (allowed.error().code == ErrorCode::SchedulerClosed || allowed.error().code == ErrorCode::ModulePinned ||
                allowed.error().code == ErrorCode::AbiProtocol)
                fault(allowed.error());
            return Result<T>::failure(allowed.error());
        }
        return result;
    }

private:
    Result<void> current() const
    {
        auto allowed = qtAvailable(generation_, backend_);
        if (!allowed.hasValue())
            return allowed;
        if (!scheduler_ || !scheduler_->isOpen())
            return Result<void>::failure(error(ErrorCode::SchedulerClosed, "Qt state scheduler closed"));
        if (objectCall_)
            return objectCall_->check();
        if (resource_) {
            auto snapshot = resource_->snapshot();
            if (!snapshot.hasValue())
                return Result<void>::failure(snapshot.error());
            if (!snapshot.value().alive || snapshot.value().closing)
                return Result<void>::failure(error(ErrorCode::ContextClosing, "Qt state resource closed"));
        }
        return Result<void>::success();
    }
    void fault(const ErrorInfo& failure)
    {
        if (failure_)
            failure_(failure);
        else
            qtBusinessFault(generation_, failure);
    }
    std::shared_ptr<Generation> generation_;
    std::shared_ptr<QtResidency> backend_;
    std::shared_ptr<qt::QtScheduler> scheduler_;
    const qt::QObjectResource* resource_;
    std::function<void(ErrorInfo)> failure_;
    ModuleLease lease_;
    std::optional<qt::detail::ObjectCall> objectCall_;
    bool entered_ = false;
};
struct Binding {
    std::shared_ptr<Generation> generation;
    std::shared_ptr<QtResidency> backend;
    std::shared_ptr<qt::QtScheduler> scheduler;
    qt::QObjectResource resource;
    IQtStateInstanceV1* api;
    EveStateSchemaV1 schema;
    std::function<void(ErrorInfo)> failure;
    Result<std::string> capture() const
    {
        StateCall call(generation, backend, scheduler, &resource, failure);
        auto entered = call.enter("qt.state.capture");
        if (!entered.hasValue())
            return Result<std::string>::failure(entered.error());
        return call.checked(readStateJson([&](EveStateBufferV1* output) { return api->captureState(output); }));
    }
    Result<void> restore(std::string_view json) const
    {
        if (json.size() > maxStateJsonBytes || !validAbiJson(json))
            return Result<void>::failure(error(ErrorCode::InvalidArgument, "invalid Qt state input"));
        StateCall call(generation, backend, scheduler, &resource, failure);
        auto entered = call.enter("qt.state.restore");
        if (!entered.hasValue())
            return entered;
        try {
            return call.checked(stateStatus(api->restoreState(&schema, bytes(json))));
        } catch (...) {
            return call.checked(Result<void>::failure(error(ErrorCode::AbiProtocol, "exception escaped Qt restore")));
        }
    }
};
} // namespace
Result<void> negotiateQtState(const std::shared_ptr<QtResidency>& backend, const std::vector<std::string>& plugins)
{
    // 调用方已经进入原工厂协商窗口并持 Call。只有完整成功才缓存描述与可选接口。
    try {
        auto api = qobject_cast<IQtStatePluginV1*>(backend->root.data());
        if (!backend->root)
            return Result<void>::failure(error(ErrorCode::AbiProtocol, "Qt state root destroyed during metacast"));
        if (!api)
            return Result<void>::success();
        std::map<std::string, StateSchema> schemas;
        for (const auto& plugin : plugins) {
            auto schema =
                readStateSchema([&](EveStateSchemaV1* output) { return api->describeState(bytes(plugin), output); });
            if (!schema.hasValue())
                return Result<void>::failure(schema.error());
            if (!backend->root)
                return Result<void>::failure(error(ErrorCode::AbiProtocol, "Qt state root destroyed during describe"));
            if (schema.value())
                schemas.emplace(plugin, std::move(*schema.value()));
        }
        backend->stateFactory = api;
        backend->stateSchemas = std::move(schemas);
        return Result<void>::success();
    } catch (...) {
        return Result<void>::failure(error(ErrorCode::AbiProtocol, "exception escaped Qt state negotiation"));
    }
}
Result<std::string> validateQtState(const std::shared_ptr<Generation>& generation,
                                    const std::shared_ptr<QtResidency>& backend,
                                    const std::shared_ptr<qt::QtScheduler>& scheduler, const std::string& plugin,
                                    const StateSnapshot& input)
{
    using Output = Result<std::string>;
    auto schema = encodeStateSchema(input.schema);
    if (!schema.hasValue() || input.json.size() > maxStateJsonBytes || !validAbiJson(input.json))
        return Output::failure(error(ErrorCode::InvalidArgument, "invalid Qt state snapshot"));
    StateCall call(generation, backend, scheduler);
    auto entered = call.enter("qt.state.validate");
    if (!entered.hasValue())
        return Output::failure(entered.error());
    if (!backend->stateFactory || !backend->stateSchemas.count(plugin))
        return Output::failure(error(ErrorCode::InvalidArgument, "Qt plugin does not declare state"));
    return call.checked(readStateJson([&](EveStateBufferV1* output) {
        return backend->stateFactory->validateState(bytes(plugin), &schema.value(), bytes(input.json), output);
    }));
}
Result<StateParticipant> qtStateParticipant(const std::shared_ptr<Generation>& generation,
                                            const std::shared_ptr<QtResidency>& backend,
                                            const std::shared_ptr<qt::QtScheduler>& scheduler,
                                            qt::QObjectResource resource, QObject* object, const std::string& plugin,
                                            std::function<void(ErrorInfo)> failure)
{
    using Output = Result<StateParticipant>;
    StateCall call(generation, backend, scheduler, &resource, failure);
    auto entered = call.enter("qt.state.bind");
    if (!entered.hasValue())
        return Output::failure(entered.error());
    const auto found = backend->stateSchemas.find(plugin);
    if (found == backend->stateSchemas.end())
        return Output::failure(error(ErrorCode::InvalidArgument, "Qt state schema missing"));
    IQtStateInstanceV1* api = nullptr;
    try {
        api = qobject_cast<IQtStateInstanceV1*>(object);
    } catch (...) {
        return call.checked(Output::failure(error(ErrorCode::AbiProtocol, "exception escaped Qt state metacast")));
    }
    if (!api)
        return call.checked(Output::failure(error(ErrorCode::AbiProtocol, "stateful Qt instance is missing its IID")));
    auto encoded = encodeStateSchema(found->second);
    if (!encoded.hasValue())
        return Output::failure(encoded.error());
    auto binding = std::make_shared<Binding>(
        Binding{generation, backend, scheduler, resource, api, encoded.value(), std::move(failure)});
    StateParticipant participant{[binding] { return binding->capture(); },
                                 [binding](std::string_view json) { return binding->restore(json); }};
    return call.checked(Output::success(std::move(participant)));
}
} // namespace eve::module::detail
