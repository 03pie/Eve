/** @file kernel.cpp
 * @brief 实现插件登记、实例归属和全量停止结果汇总。
 */
#include <cordis/kernel.hpp>
#include <stdexcept>

namespace cordis {
/** @brief 校验开放调度器与 owner 线程。 */
Kernel::Kernel(std::shared_ptr<IScheduler> scheduler) : scheduler_(std::move(scheduler))
{
    if (!scheduler_ || !scheduler_->isOpen() || !scheduler_->isCurrentThread())
        throw std::invalid_argument("Kernel requires open owner-thread scheduler");
    services_ = std::make_shared<ServiceRegistry>(scheduler_);
}

/** @brief 在调用插件前完成名称登记；注册本身不执行激活。 */
Result<Runtime> Kernel::registerPlugin(std::string name, Runtime::Activate activate, std::vector<ServiceRequirement> dependencies, std::vector<ServiceOutput> outputs)
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

/** @brief 拒绝其他内核的运行时，并在激活前保存实例句柄。 */
Result<Instance> Kernel::mount(const Runtime& runtime, std::string config, RealmMap realms)
{
    if (!scheduler_->isCurrentThread()) return Result<Instance>::failure(makeError(ErrorCode::WrongThread, "mount requires owner thread"));
    if (closing_) return Result<Instance>::failure(makeError(ErrorCode::ContextClosing, "kernel closing"));
    if (!scheduler_->isOpen()) return Result<Instance>::failure(makeError(ErrorCode::SchedulerClosed, "scheduler closed"));
    for (const auto& entry : runtimes_) if (entry.second.id() == runtime.id()) {
        try {
            auto instance = Instance::mount(runtime, scheduler_, std::move(config), services_, std::move(realms));
            instances_.emplace(instance.snapshot().value().instance, instance);
            return Result<Instance>::success(std::move(instance));
        } catch (const std::invalid_argument& error) { return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, error.what())); }
    }
    return Result<Instance>::failure(makeError(ErrorCode::InvalidArgument, "runtime not registered here"));
}

/** @brief 先向所有实例广播停止，再在队列内汇总，不捕获 Kernel 裸指针。 */
Task<CleanupReport> Kernel::close()
{
    if (!scheduler_->isCurrentThread()) {
        Promise<CleanupReport> failed;
        failed.reject(makeError(ErrorCode::WrongThread, "close requires owner thread"));
        return failed.task();
    }
    if (closing_) return closed_.task();
    closing_ = true;
    /** @brief 独立于 Kernel 句柄生命周期的关闭汇总账目。 */
    struct Closing {
        std::size_t pending;
        CleanupReport report{true, {}};
        Promise<CleanupReport> promise;
    };
    auto closing = std::make_shared<Closing>(Closing{instances_.size(), {true, {}}, closed_});
    if (instances_.empty()) closed_.resolve(CleanupReport{true, {}});
    std::vector<Task<CleanupReport>> tasks;
    for (const auto& entry : instances_) tasks.push_back(entry.second.stop());
    for (const auto& task : tasks) {
        auto receipt = task.thenOn(scheduler_, [closing](const Result<CleanupReport>& result) {
            if (result.hasValue()) {
                closing->report.quiescent = closing->report.quiescent && result.value().quiescent;
                closing->report.errors.insert(closing->report.errors.end(), result.value().errors.begin(), result.value().errors.end());
            } else {
                closing->report.quiescent = false;
                closing->report.errors.push_back(result.error());
            }
            if (--closing->pending == 0) closing->promise.resolve(std::move(closing->report));
        });
        detail::TaskAccess::observe(receipt, [blocked = blocked_](Task<void>::Snapshot result) {
            if (!result->hasValue()) blocked->store(true);
        });
    }
    return closed_.task();
}

/** @brief 仅删除确认终结的实例，清理阻塞的实例保留诊断。 */
Result<KernelSnapshot> Kernel::snapshot()
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
Result<Instance> Kernel::findInstance(InstanceId id) const
{
    if (!scheduler_->isCurrentThread()) return Result<Instance>::failure(makeError(ErrorCode::WrongThread, "find instance requires owner thread"));
    auto found = instances_.find(id);
    if (!id.valid() || found == instances_.end())
        return Result<Instance>::failure(makeError(ErrorCode::InstanceDisposed, "instance not owned by kernel or already collected"));
    return Result<Instance>::success(found->second);
}
/** @brief 调度器身份在构造后不变，不触碰 Qt 或插件对象。 */
bool Kernel::usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const { return scheduler_ == scheduler; }
} // namespace cordis
