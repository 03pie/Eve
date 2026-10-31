/** @file replacement.cpp
 * @brief 从实际 Kernel 所有权及同一服务图计算闭包，不执行或锁定运行资源。
 */
#include "kernel_p.hpp"
#include <algorithm>
#include <deque>
#include <set>

namespace cordis {
namespace {
ErrorInfo failure(ErrorCode code, const char* message, InstanceId instance = {})
{
    auto error = makeError(code, message);
    error.phase = "kernel.replacement.closure";
    if (instance.valid())
        error.instance = instance;
    return error;
}
} // namespace

Result<detail::ReplacementSelection>
detail::KernelControl::selectReplacement(const std::vector<RuntimeId>& requested) const
{
    using Output = Result<ReplacementSelection>;
    if (!scheduler_->isCurrentThread())
        return Output::failure(failure(ErrorCode::WrongThread, "replacement closure requires owner thread"));
    if (closing_)
        return Output::failure(failure(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen())
        return Output::failure(failure(ErrorCode::SchedulerClosed, "scheduler closed"));
    std::set<RuntimeId> selected;
    for (const auto id : requested) {
        const bool registered =
            std::any_of(runtimes_.begin(), runtimes_.end(), [id](const auto& item) { return item.second.id() == id; });
        if (!id.valid() || !registered || !selected.insert(id).second)
            return Output::failure(failure(ErrorCode::InvalidArgument, "invalid, duplicate or foreign runtime"));
    }
    if (selected.empty())
        return Output::failure(failure(ErrorCode::InvalidArgument, "replacement requires at least one runtime"));

    std::map<InstanceId, ReplacementMember> owned;
    std::map<InstanceId, Instance> handles;
    std::map<InstanceId, bool> blocked;
    std::vector<std::pair<Instance, InstanceId>> pending;
    for (const auto& entry : instances_)
        pending.emplace_back(entry.second, InstanceId{});
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        auto inspected = current.first.snapshot();
        if (!inspected.hasValue())
            return Output::failure(inspected.error());
        const auto& value = inspected.value();
        if (value.state == InstanceState::Disposed)
            continue;
        if (!owned
                 .emplace(value.instance, ReplacementMember{value.instance, value.runtime, current.second, value.epoch,
                                                            value.revision, value.state})
                 .second)
            return Output::failure(failure(ErrorCode::DependencyCycle, "duplicate instance ownership", value.instance));
        blocked.emplace(value.instance, value.deliveryBlocked || value.resources.deliveryBlocked);
        handles.emplace(value.instance, current.first);
        auto children = current.first.ownedChildren();
        if (!children.hasValue())
            return Output::failure(children.error());
        for (auto& child : children.value())
            pending.emplace_back(std::move(child), value.instance);
    }
    auto source = services_->dependencyGraph();
    if (!source.hasValue())
        return Output::failure(source.error());
    const auto& graph = source.value();
    std::map<InstanceId, const ServiceInstanceSnapshot*> declarations;
    std::map<ServiceKey, const ServiceBindingSnapshot*> bindings;
    for (const auto& item : graph.instances)
        declarations.emplace(item.instance, &item);
    for (const auto& item : graph.bindings)
        bindings.emplace(item.binding.key, &item);
    for (const auto& item : owned) {
        const auto declaration = declarations.find(item.first);
        if (declaration == declarations.end() || declaration->second->parent != item.second.parent)
            return Output::failure(
                failure(ErrorCode::ConfigConflict, "instance ownership changed during inspection", item.first));
    }

    // 索引中已存在的真实提供者优先；未绑定键保留所有潜在提供者，不臆测唯一来源。
    std::map<ServiceKey, std::set<InstanceId>> providers;
    for (const auto& item : graph.instances)
        if (owned.count(item.instance))
            for (const auto& key : item.outputs)
                if (!bindings.count(key))
                    providers[key].insert(item.instance);
    for (const auto& item : graph.bindings)
        if (item.managedProvider && owned.count(item.provider)) {
            providers[item.binding.key].insert(item.provider);
            blocked.at(item.provider) = blocked.at(item.provider) || item.binding.deliveryBlocked;
        }

    std::map<InstanceId, std::set<InstanceId>> dependents;
    for (const auto& item : graph.instances) {
        if (item.parent.valid())
            dependents[item.parent].insert(item.instance);
        for (const auto& key : item.dependencies)
            if (auto found = providers.find(key); found != providers.end())
                for (const auto id : found->second)
                    dependents[id].insert(item.instance);
    }
    // 保留未声明但实际已提交的消费者；原生编号不是托管或 Kernel 所有权证明。
    std::map<InstanceId, std::vector<ServiceConsumerSnapshot>> committed;
    for (const auto& item : graph.bindings)
        if (item.managedProvider && owned.count(item.provider)) {
            auto& list = committed[item.provider];
            list.insert(list.end(), item.consumers.begin(), item.consumers.end());
        }

    std::set<InstanceId> affected;
    std::deque<InstanceId> queue;
    for (const auto& item : owned)
        if (selected.count(item.second.runtime)) {
            affected.insert(item.first);
            queue.push_back(item.first);
        }
    while (!queue.empty()) {
        const auto id = queue.front();
        queue.pop_front();
        const auto& node = owned.at(id);
        // 一个子实例不能脱离仍持有它的祖先单独重建，整个实际拥有单元一起占用。
        if (node.parent.valid() && affected.insert(node.parent).second) {
            if (!owned.count(node.parent))
                return Output::failure(
                    failure(ErrorCode::ConfigConflict, "owning ancestor is not controlled", node.parent));
            queue.push_back(node.parent);
        }
        if (blocked.at(id))
            return Output::failure(failure(ErrorCode::SchedulerClosed, "affected resource delivery blocked", id));
        if (node.state == InstanceState::Loading || node.state == InstanceState::Unloading)
            return Output::failure(failure(ErrorCode::ContextClosing, "affected instance is transitioning", id));
        if (declarations.at(id)->publicationHeld)
            return Output::failure(
                failure(ErrorCode::ConfigConflict, "affected instance belongs to an open publication group", id));
        for (const auto& consumer : committed[id]) {
            auto found = owned.find(consumer.instance);
            if (!consumer.managed || found == owned.end())
                return Output::failure(
                    failure(ErrorCode::ConfigConflict, "affected service has an external consumer", consumer.instance));
            if (found->second.epoch != consumer.epoch)
                return Output::failure(failure(ErrorCode::ConfigConflict,
                                               "committed consumer epoch differs from owned instance",
                                               consumer.instance));
            dependents[id].insert(consumer.instance);
        }
        for (const auto consumer : dependents[id]) {
            if (!owned.count(consumer))
                return Output::failure(failure(ErrorCode::ConfigConflict,
                                               "affected declaration or child is outside kernel ownership", consumer));
            if (affected.insert(consumer).second)
                queue.push_back(consumer);
        }
    }

    std::map<InstanceId, std::size_t> waiting;
    std::map<InstanceId, std::set<InstanceId>> upstream;
    std::set<InstanceId> ready;
    for (const auto id : affected) {
        std::size_t count = 0;
        for (const auto consumer : dependents[id])
            if (affected.count(consumer)) {
                ++count;
                upstream[consumer].insert(id);
            }
        waiting.emplace(id, count);
        if (count == 0)
            ready.insert(id);
    }
    ReplacementClosure result;
    result.runtimes.assign(selected.begin(), selected.end());
    while (!ready.empty()) {
        const auto id = *ready.begin();
        ready.erase(ready.begin());
        result.members.push_back(owned.at(id));
        result.stopOrder.push_back(id);
        for (const auto provider : upstream[id])
            if (--waiting.at(provider) == 0)
                ready.insert(provider);
    }
    if (result.members.size() != affected.size())
        return Output::failure(
            failure(ErrorCode::DependencyCycle, "replacement closure has a service/ownership cycle"));
    result.startOrder.assign(result.stopOrder.rbegin(), result.stopOrder.rend());
    ReplacementSelection selection{std::move(result), {}, {}};
    for (const auto id : affected)
        selection.instances.emplace(id, handles.at(id));
    for (const auto& output : providers)
        if (std::any_of(output.second.begin(), output.second.end(),
                        [&](InstanceId id) { return affected.count(id) != 0; }))
            selection.outputs.push_back(output.first);
    return Output::success(std::move(selection));
}
Result<ReplacementClosure> detail::KernelControl::replacementClosure(const std::vector<RuntimeId>& requested) const
{
    auto selection = selectReplacement(requested);
    return selection.hasValue() ? Result<ReplacementClosure>::success(std::move(selection).value().closure)
                                : Result<ReplacementClosure>::failure(selection.error());
}
} // namespace cordis
