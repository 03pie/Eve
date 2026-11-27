/** @file service_notifications.cpp
 * @brief 从原声明收集完整或按键的激活作业；稳定列表之外才执行排队与拒投回调。
 */
#include "service_p.hpp"

namespace eve::detail {
namespace {
bool notify(const std::shared_ptr<ServiceRegistryControl>& registry, const ServiceKey* key)
{
    std::vector<std::shared_ptr<ScheduledJob>> jobs;
    for (auto it = registry->watchers.begin(); it != registry->watchers.end();) {
        if (auto job = it->lock()) {
            jobs.push_back(std::move(job));
            ++it;
        } else {
            it = registry->watchers.erase(it);
        }
    }
    const auto collect = [&](const InstanceDeclaration& declaration) {
        if (auto job = declaration.activationJob.lock())
            jobs.push_back(std::move(job));
    };
    if (key) {
        const auto interested = registry->interestedInstances.find(*key);
        if (interested != registry->interestedInstances.end())
            for (const auto instance : interested->second) {
                const auto declaration = registry->declarations.find(instance);
                if (declaration != registry->declarations.end())
                    collect(declaration->second);
            }
    } else {
        for (const auto& entry : registry->declarations)
            collect(entry.second);
    }
    // post 的闭包复制及拒投回调都可能重入；这里不再持有任何索引或声明迭代器。
    bool delivered = true;
    for (const auto& job : jobs) {
        try {
            if (registry->scheduler->post(*job))
                continue;
        } catch (...) {
        }
        delivered = false;
        if (job->discarded)
            job->discarded();
    }
    return delivered;
}
} // namespace

bool notifyRegistry(const std::shared_ptr<ServiceRegistryControl>& registry)
{
    return notify(registry, nullptr);
}
bool notifyRegistry(const std::shared_ptr<ServiceRegistryControl>& registry, const ServiceKey& key)
{
    return notify(registry, &key);
}
} // namespace eve::detail
