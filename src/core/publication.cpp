/** @file publication.cpp
 * @brief 同一注册表的发布视图与全部成员就绪后的可见性提交。
 */
#include "replacement_scope_p.hpp"
#include "service_p.hpp"

namespace cordis::detail {
bool visiblePublication(const std::shared_ptr<PublicationControl>& binding,
                        const std::shared_ptr<PublicationControl>& viewer)
{
    return !binding || binding->state == PublicationState::Published ||
           (binding == viewer && binding->state == PublicationState::Open);
}
bool availableBinding(const BindingControl& binding, const std::shared_ptr<PublicationControl>& viewer, bool cleanup)
{
    if (!cleanup && !replacementAllowsBusiness(binding.replacement))
        return false;
    if (cleanup && binding.state == BindingState::Retiring)
        return true;
    if (!cleanup && viewer && viewer->state == PublicationState::Aborted)
        return false;
    return visiblePublication(binding.publication, viewer) &&
           (binding.state == BindingState::Published ||
            (binding.state == BindingState::Held && binding.publication == viewer));
}
} // namespace cordis::detail

namespace cordis {
Result<void> ServiceRegistry::publicationAccess() const
{
    if (!control_->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "publication access requires owner thread"));
    if (publication_ && publication_->state == PublicationState::Aborted)
        return Result<void>::failure(makeError(ErrorCode::ContextClosing, "publication group aborted"));
    return Result<void>::success();
}
Result<PublicationGroup> ServiceRegistry::holdPublication() const
{
    using Output = Result<PublicationGroup>;
    if (!control_->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "publication requires owner thread"));
    if (!control_->scheduler->isOpen())
        return Output::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    auto publication = std::make_shared<detail::PublicationControl>();
    publication->registry = control_;
    publication->scheduler = control_->scheduler;
    return Output::success(PublicationGroup(std::move(publication)));
}
Result<ServiceRegistry> ServiceRegistry::publicationView(const PublicationGroup& group) const
{
    using Output = Result<ServiceRegistry>;
    if (!control_->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "publication view requires owner thread"));
    if (!group.control_ || group.control_->registry.lock() != control_ ||
        group.control_->state != PublicationState::Open)
        return Output::failure(makeError(ErrorCode::InvalidArgument, "foreign or closed publication group"));
    auto view = *this;
    view.publication_ = group.control_;
    return Output::success(std::move(view));
}
Result<PublicationSnapshot> PublicationGroup::snapshot() const
{
    const auto control = control_;
    using Output = Result<PublicationSnapshot>;
    if (!control)
        return Output::failure(makeError(ErrorCode::InvalidArgument, "publication handle moved from"));
    if (!control->scheduler->isCurrentThread())
        return Output::failure(makeError(ErrorCode::WrongThread, "publication snapshot requires owner thread"));
    PublicationSnapshot result{control->state, control->members.size(), 0, control->deliveryBlocked};
    if (auto registry = control->registry.lock())
        for (const auto& entry : registry->bindings)
            if (entry.second->publication == control && entry.second->state == BindingState::Held)
                ++result.heldBindings;
    return Output::success(result);
}
Result<void> PublicationGroup::commit() const
{
    const auto control = control_;
    if (!control)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "publication handle moved from"));
    if (!control->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "publication commit requires owner thread"));
    if (!control->scheduler->isOpen())
        return Result<void>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (control->state == PublicationState::Published) {
        if (control->notifying || control->deliveryBlocked)
            return Result<void>::failure(
                makeError(ErrorCode::SchedulerClosed, "publication notification delivery is pending or blocked"));
        return Result<void>::success();
    }
    auto registry = control->registry.lock();
    if (control->state != PublicationState::Open || !registry || control->members.empty())
        return Result<void>::failure(
            makeError(ErrorCode::InvalidArgument, "publication group is aborted, empty or expired"));
    for (const auto& member : control->members) {
        auto ready = member.second.ready();
        if (!ready.hasValue()) {
            auto error = makeError(ErrorCode::ProvisionIncomplete, "publication member is not ready");
            error.instance = member.first;
            error.causes.push_back(ready.error());
            return Result<void>::failure(std::move(error));
        }
    }
    std::vector<std::shared_ptr<detail::BindingControl>> bindings;
    for (const auto& entry : registry->bindings)
        if (entry.second->publication == control) {
            if (entry.second->state != BindingState::Held || !control->members.count(entry.second->provider) ||
                entry.second->deliveryBlocked.load())
                return Result<void>::failure(
                    makeError(ErrorCode::ProvisionIncomplete, "publication contains unfinished or unowned output"));
            bindings.push_back(entry.second);
        }
    // 发布循环只改变已验证的宿主状态，不分配或调用外部代码。
    for (const auto& binding : bindings)
        binding->state = BindingState::Published;
    control->state = PublicationState::Published;
    control->notifying = true;
    control->deliveryBlocked = true;
    struct Delivery {
        detail::PublicationControl& state;
        ~Delivery() { state.notifying = false; }
    } delivery{*control};
    control->deliveryBlocked = !detail::notifyRegistry(registry);
    if (control->deliveryBlocked)
        return Result<void>::failure(
            makeError(ErrorCode::SchedulerClosed, "published outputs but notification delivery failed"));
    return Result<void>::success();
}
Result<void> PublicationGroup::abort() const
{
    const auto control = control_;
    if (!control)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "publication handle moved from"));
    if (!control->scheduler->isCurrentThread())
        return Result<void>::failure(makeError(ErrorCode::WrongThread, "publication abort requires owner thread"));
    if (control->state == PublicationState::Aborted)
        return Result<void>::success();
    if (control->state != PublicationState::Open)
        return Result<void>::failure(makeError(ErrorCode::InvalidArgument, "published group cannot be aborted"));
    std::vector<ProviderHandle> outputs;
    if (auto registry = control->registry.lock())
        for (const auto& entry : registry->bindings)
            if (entry.second->publication == control)
                outputs.push_back(ProviderHandle(entry.second));
    const auto members = control->members;
    control->state = PublicationState::Aborted;
    // 状态先冻结，索引回调或取消引起的重入不能再调用任何组内业务。
    for (const auto& output : outputs)
        output.retire().value();
    for (const auto& member : members)
        member.second.abort();
    return Result<void>::success();
}
} // namespace cordis
