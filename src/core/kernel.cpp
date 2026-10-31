/** @file kernel.cpp
 * @brief 实现插件登记、实例归属和全量停止结果汇总。
 */
#include "kernel_p.hpp"
#include "replacement_scope_p.hpp"
#include "service_p.hpp"
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cordis {
/** @brief 校验开放调度器与 owner 线程。 */
detail::KernelControl::KernelControl(std::shared_ptr<IScheduler> scheduler) : scheduler_(std::move(scheduler))
{
    if (!scheduler_ || !scheduler_->isOpen() || !scheduler_->isCurrentThread())
        throw std::invalid_argument("Kernel requires open owner-thread scheduler");
    services_ = std::make_shared<ServiceRegistry>(scheduler_);
}

/** @brief 在调用插件前完成名称登记；注册本身不执行激活。 */
Result<Runtime> detail::KernelControl::registerPlugin(std::string name, Runtime::Activate activate,
                                                      std::vector<ServiceRequirement> dependencies,
                                                      std::vector<ServiceOutput> outputs)
{
    if (!scheduler_->isCurrentThread()) return Result<Runtime>::failure(makeError(ErrorCode::WrongThread, "register requires owner thread"));
    if (closing_) return Result<Runtime>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen()) return Result<Runtime>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (name.empty() || !activate || runtimes_.find(name) != runtimes_.end())
        return Result<Runtime>::failure(makeError(ErrorCode::InvalidArgument, "invalid or duplicate plugin"));
    try {
        Runtime runtime(std::move(activate), std::move(dependencies), std::move(outputs));
        runtimes_.emplace(std::move(name), runtime);
        return Result<Runtime>::success(std::move(runtime));
    } catch (const std::invalid_argument& error) { return Result<Runtime>::failure(makeError(ErrorCode::InvalidArgument, error.what())); }
}

Result<Runtime> detail::KernelControl::registerRuntime(std::string name, const Runtime& runtime)
{
    if (!scheduler_->isCurrentThread())
        return Result<Runtime>::failure(makeError(ErrorCode::WrongThread, "register requires owner thread"));
    if (closing_)
        return Result<Runtime>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen())
        return Result<Runtime>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (name.empty() || !runtime.id().valid() || runtimes_.count(name))
        return Result<Runtime>::failure(makeError(ErrorCode::InvalidArgument, "invalid or duplicate runtime"));
    for (const auto& entry : runtimes_)
        if (entry.second.id() == runtime.id())
            return Result<Runtime>::failure(makeError(ErrorCode::InvalidArgument, "runtime already registered"));
    runtimes_.emplace(std::move(name), runtime);
    return Result<Runtime>::success(runtime);
}

/** @brief 拒绝其他内核的运行时，并在激活前保存实例句柄。 */
Result<Instance> detail::KernelControl::mount(const Runtime& runtime, std::string config, RealmMap realms,
                                              std::vector<ServiceRequirement> additionalDependencies)
{
    return mountWithRegistry(runtime, std::move(config), std::move(realms), std::move(additionalDependencies),
                             services_);
}
Result<PublicationGroup> detail::KernelControl::holdPublication() const
{
    if (!scheduler_->isCurrentThread())
        return Result<PublicationGroup>::failure(
            makeError(ErrorCode::WrongThread, "publication requires owner thread"));
    if (closing_)
        return Result<PublicationGroup>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    return services_->holdPublication();
}
Result<Instance> detail::KernelControl::mountHeld(const PublicationGroup& group, const Runtime& runtime,
                                                  std::string config, RealmMap realms,
                                                  std::vector<ServiceRequirement> additionalDependencies)
{
    auto view = services_->publicationView(group);
    if (!view.hasValue())
        return Result<Instance>::failure(view.error());
    return mountWithRegistry(runtime, std::move(config), std::move(realms), std::move(additionalDependencies),
                             std::make_shared<ServiceRegistry>(std::move(view).value()));
}
Result<Instance> detail::KernelControl::mountWithRegistry(const Runtime& runtime, std::string config, RealmMap realms,
                                                          std::vector<ServiceRequirement> additionalDependencies,
                                                          std::shared_ptr<ServiceRegistry> services)
{
    if (!scheduler_->isCurrentThread()) return Result<Instance>::failure(makeError(ErrorCode::WrongThread, "mount requires owner thread"));
    if (closing_) return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen()) return Result<Instance>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    for (const auto& entry : runtimes_) if (entry.second.id() == runtime.id()) {
        try {
            auto admitted = services->replacementMountAccess(runtime, realms, additionalDependencies);
            if (!admitted.hasValue())
                return Result<Instance>::failure(admitted.error());
            auto instance = Instance::prepareMount(runtime, scheduler_, std::move(config), std::move(services),
                                                   std::move(realms), std::move(additionalDependencies));
            instance.setKernelRegistry(services_);
            instances_.emplace(instance.snapshot().value().instance, instance);
            instance.startMount();
            return Result<Instance>::success(std::move(instance));
        } catch (const std::invalid_argument& error) { return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, error.what())); }
    }
    return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, "runtime not registered here"));
}

/** @brief 由旧句柄保留输入，允许已被 snapshot 回收的终结实例重新加载。 */
Result<Instance> detail::KernelControl::remount(const Instance& previous)
{
    if (!scheduler_->isCurrentThread())
        return Result<Instance>::failure(makeError(ErrorCode::WrongThread, "remount requires owner thread"));
    if (closing_)
        return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen())
        return Result<Instance>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    if (!previous.control_)
        return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, "remount source was moved from"));
    auto snapshot = previous.snapshot();
    if (!snapshot.hasValue())
        return Result<Instance>::failure(snapshot.error());
    for (const auto& entry : runtimes_)
        if (entry.second.id() == snapshot.value().runtime) {
            auto mounted = previous.remount(entry.second, scheduler_, services_);
            if (mounted.hasValue()) {
                instances_.emplace(mounted.value().snapshot().value().instance, mounted.value());
                mounted.value().startMount();
            }
            return mounted;
        }
    return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, "runtime not registered here"));
}

/** @brief 先向所有实例广播停止，再在队列内汇总，不捕获 Kernel 裸指针。 */
Task<CleanupReport> detail::KernelControl::close()
{
    if (!scheduler_->isCurrentThread()) {
        Promise<CleanupReport> failed;
        failed.reject(makeError(ErrorCode::WrongThread, "close requires owner thread"));
        return failed.task();
    }
    if (closing_) return closed_.task();
    closing_ = true;
    std::vector<Instance> instances;
    for (const auto& entry : instances_)
        instances.push_back(entry.second);
    detail::shutdownReplacementScopes(services_->control_, false);
    /** @brief 独立于 Kernel 句柄生命周期的关闭汇总账目。 */
    struct Closing {
        std::size_t pending;
        CleanupReport report{true, {}};
        Promise<CleanupReport> promise;
        std::shared_ptr<ServiceRegistryControl> registry;
    };
    auto closing = std::make_shared<Closing>(Closing{instances.size(), {true, {}}, closed_, services_->control_});
    if (instances.empty()) {
        detail::shutdownReplacementScopes(services_->control_, true, {true, {}});
        closed_.resolve(CleanupReport{true, {}});
    }
    std::vector<Task<CleanupReport>> tasks;
    for (const auto& instance : instances)
        tasks.push_back(instance.stopForShutdown());
    for (const auto& task : tasks) {
        auto receipt = task.thenOn(scheduler_, [closing](const Result<CleanupReport>& result) {
            if (result.hasValue()) {
                closing->report.quiescent = closing->report.quiescent && result.value().quiescent;
                closing->report.errors.insert(closing->report.errors.end(), result.value().errors.begin(), result.value().errors.end());
            } else {
                closing->report.quiescent = false;
                closing->report.errors.push_back(result.error());
            }
            if (--closing->pending == 0) {
                detail::shutdownReplacementScopes(closing->registry, true, closing->report);
                closing->promise.resolve(std::move(closing->report));
            }
        });
        detail::TaskAccess::observe(receipt, [blocked = blocked_](Task<void>::Snapshot result) {
            if (!result->hasValue()) blocked->store(true);
        });
    }
    return closed_.task();
}

/** @brief 仅删除确认终结的实例，清理阻塞的实例保留诊断。 */
Result<KernelSnapshot> detail::KernelControl::snapshot()
{
    if (!scheduler_->isCurrentThread()) return Result<KernelSnapshot>::failure(makeError(ErrorCode::WrongThread, "snapshot requires owner thread"));
    KernelSnapshot snapshot{closing_, blocked_->load(), runtimes_.size(), {}, {}};
    for (const auto& entry : runtimes_) snapshot.names.push_back({entry.second.id(), entry.first});
    for (auto it = instances_.begin(); it != instances_.end();) {
        auto instance = it->second.snapshot().value();
        if (instance.state == InstanceState::Disposed) it = instances_.erase(it);
        else { snapshot.instances.push_back(std::move(instance)); ++it; }
    }
    return Result<KernelSnapshot>::success(std::move(snapshot));
}
/** @brief 返回已有句柄，不启动新实例，也不允许跨内核按编号猜测归属。 */
Result<Instance> detail::KernelControl::findInstance(InstanceId id) const
{
    if (!scheduler_->isCurrentThread()) return Result<Instance>::failure(makeError(ErrorCode::WrongThread, "find instance requires owner thread"));
    auto found = instances_.find(id);
    if (!id.valid() || found == instances_.end())
        return Result<Instance>::failure(makeError(ErrorCode::InstanceDisposed, "instance not owned by kernel or already collected"));
    return Result<Instance>::success(found->second);
}
/** @brief 门面只持唯一内核账目；各入口先强持控制块，再进入可重入的调度边界。 */
Kernel::Kernel(std::shared_ptr<IScheduler> scheduler)
    : control_(std::make_shared<detail::KernelControl>(std::move(scheduler)))
{
}
Result<Runtime> Kernel::registerPlugin(std::string name, Runtime::Activate activate,
                                       std::vector<ServiceRequirement> dependencies, std::vector<ServiceOutput> outputs)
{
    const auto control = control_;
    return control->registerPlugin(std::move(name), std::move(activate), std::move(dependencies), std::move(outputs));
}
Result<Runtime> Kernel::registerRuntime(std::string name, const Runtime& runtime)
{
    const auto control = control_;
    return control->registerRuntime(std::move(name), runtime);
}
ServiceRegistry Kernel::services() const
{
    const auto control = control_;
    return *control->services_;
}
Result<Instance> Kernel::mount(const Runtime& runtime, std::string config, RealmMap realms,
                               std::vector<ServiceRequirement> additionalDependencies)
{
    const auto control = control_;
    return control->mount(runtime, std::move(config), std::move(realms), std::move(additionalDependencies));
}
Result<PublicationGroup> Kernel::holdPublication() const
{
    const auto control = control_;
    return control->holdPublication();
}
Result<Instance> Kernel::mountHeld(const PublicationGroup& group, const Runtime& runtime, std::string config,
                                   RealmMap realms, std::vector<ServiceRequirement> additionalDependencies)
{
    const auto control = control_;
    return control->mountHeld(group, runtime, std::move(config), std::move(realms), std::move(additionalDependencies));
}
Result<Instance> Kernel::remount(const Instance& previous)
{
    const auto control = control_;
    return control->remount(previous);
}
Task<CleanupReport> Kernel::close()
{
    const auto control = control_;
    return control->close();
}
Result<KernelSnapshot> Kernel::snapshot()
{
    const auto control = control_;
    return control->snapshot();
}
Result<Instance> Kernel::findInstance(InstanceId id) const
{
    const auto control = control_;
    return control->findInstance(id);
}
Result<ReplacementClosure> Kernel::replacementClosure(const std::vector<RuntimeId>& requested) const
{
    const auto control = control_;
    return control->replacementClosure(requested);
}
bool Kernel::usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const
{
    const auto control = control_;
    return control->scheduler_ == scheduler;
}
Result<ReplacementScope> Kernel::reserveReplacement(const std::vector<RuntimeId>& requested)
{
    const auto control = control_;
    return control->reserveReplacement(requested);
}
Result<std::vector<ReplacementScopeSnapshot>> Kernel::replacements() const
{
    const auto control = control_;
    return control->replacements();
}
Task<void> Kernel::whenReplacementReleased(OperationId operation) const
{
    const auto control = control_;
    return control->whenReplacementReleased(operation);
}
Result<void> Kernel::checkMount(const Runtime& runtime, const RealmMap& realms,
                                const std::vector<ServiceRequirement>& additionalDependencies) const
{
    const auto control = control_;
    return control->checkMount(runtime, realms, additionalDependencies);
}
} // namespace cordis
