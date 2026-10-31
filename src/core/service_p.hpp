/** @file service_p.hpp
 * @brief 服务与发布组共用同一索引、绑定及租约；不保存模块或配置策略。
 */
#pragma once
#include <atomic>
#include <cordis/service.hpp>
#include <set>

namespace cordis::detail {
struct InstanceDeclaration {
    std::vector<ServiceKey> dependencies;
    std::vector<ServiceKey> outputs;
    std::shared_ptr<PublicationControl> publication;
    std::vector<ServiceKey> optionalOutputs;
};
struct PublicationMember {
    std::function<Result<void>()> ready;
    std::function<void()> abort;
};
struct PublicationControl {
    std::shared_ptr<IScheduler> scheduler;
    std::weak_ptr<ServiceRegistryControl> registry;
    PublicationState state = PublicationState::Open;
    std::map<InstanceId, PublicationMember> members;
    bool notifying = false;
    bool deliveryBlocked = false;
};
struct ServiceRegistryControl {
    std::shared_ptr<IScheduler> scheduler;
    std::map<ServiceKey, std::shared_ptr<BindingControl>> bindings;
    std::set<RealmId> realms{RealmId{1}};
    std::shared_ptr<ServiceRegistryControl> pin;
    std::vector<std::weak_ptr<ScheduledJob>> watchers;
    std::map<InstanceId, InstanceId> parents;
    std::map<InstanceId, InstanceDeclaration> declarations;
};
struct BindingControl {
    std::shared_ptr<IScheduler> scheduler;
    std::weak_ptr<ServiceRegistryControl> owner;
    BindingId id;
    ServiceKey key;
    BindingState state = BindingState::Staged;
    std::size_t calls = 0;
    std::size_t committed = 0;
    std::shared_ptr<void> object;
    std::type_index type{typeid(void)};
    ServiceMetadata metadata;
    InstanceId provider;
    bool managedProvider = false;
    std::vector<std::weak_ptr<CommittedLease>> consumers;
    Promise<void> unused;
    std::atomic_bool deliveryBlocked{false};
    std::shared_ptr<PublicationControl> publication;
    BindingSnapshot snapshot() const { return {id, key, state, calls, committed, metadata, deliveryBlocked.load()}; }
};
struct CommittedLease {
    std::shared_ptr<BindingControl> binding;
    InstanceId instance;
    Epoch epoch;
    bool active = true;
    std::size_t calls = 0;
    Promise<void> drained;
    bool draining = false;
    std::function<Result<void>()> businessGuard;
    std::function<void()> invalidated;
    std::shared_ptr<PublicationControl> publication;
    bool managed = false;
};
void checkUnused(const std::shared_ptr<BindingControl>& binding);
bool notifyRegistry(const std::shared_ptr<ServiceRegistryControl>& registry);
bool visiblePublication(const std::shared_ptr<PublicationControl>& binding,
                        const std::shared_ptr<PublicationControl>& viewer);
bool availableBinding(const BindingControl& binding, const std::shared_ptr<PublicationControl>& viewer,
                      bool cleanup = false);
} // namespace cordis::detail
