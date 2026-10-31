/** @file kernel_driver.cpp
 * @brief 以真实 Kernel 及父代能力执行内置配置树，运行账目与目标、保存状态分别维护。
 */
#include "definitions_p.hpp"
#include "module_catalog_p.hpp"
#include <cordis/loader/kernel_driver.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cordis::loader {
namespace {
/** @brief 运行适配错误保留规范条目路径，不在消息中输出配置负载。 */
ErrorInfo driverError(ErrorCode code, std::string message, const std::string& path = {})
{
    auto error = makeError(code, std::move(message));
    error.phase = "loader.kernel";
    if (!path.empty())
        error.entryPath = path;
    return error;
}
/** @brief 创建已经失败的类型化任务，错误线程不污染共享关闭结果。 */
template <class T> Task<T> failed(ErrorInfo error)
{
    Promise<T> promise;
    promise.reject(std::move(error));
    return promise.task();
}
/** @brief 文本有效依赖必须保留固定 Runtime 和宿主可选接口的最低要求。 */
bool satisfies(const InjectSpec& requested, const ServiceRequirement& floor)
{
    return requested.minimumMinor >= floor.minimumMinor &&
           std::all_of(floor.capabilities.begin(), floor.capabilities.end(), [&](const std::string& capability) {
               return std::find(requested.capabilities.begin(), requested.capabilities.end(), capability) !=
                      requested.capabilities.end();
           });
}
/** @brief 比较排序后的真实依赖，不因配置简写或冗余低要求改变实例身份。 */
bool sameRequirements(const std::vector<ServiceRequirement>& a, const std::vector<ServiceRequirement>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].key.name != b[i].key.name || a[i].key.interfaceMajor != b[i].key.interfaceMajor ||
            a[i].key.realm != b[i].key.realm || a[i].type != b[i].type || a[i].minimumMinor != b[i].minimumMinor ||
            a[i].capabilities != b[i].capabilities)
            return false;
    return true;
}
/** @brief 固定依赖相同后，仅插件、模块和 realm 变化需要替换实例身份。 */
bool sameIdentity(const EntryValue& a, const EntryValue& b)
{
    return a.plugin == b.plugin && a.module == b.module && a.realms == b.realms;
}
/** @brief 规范路径直接取真实父路径，不把相同前缀的其他条目算作祖先。 */
std::string parentPath(const std::string& path)
{
    auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}
/** @brief 物化继承的域别名，子项局部覆盖父视图；原校验候选保持不可变。 */
std::map<std::string, EntryValue> runtimeValues(const ValidatedCandidate& target)
{
    auto result = target.source().structure().entries();
    for (auto& entry : result) {
        auto parent = parentPath(entry.first);
        if (parent.empty())
            continue;
        auto local = std::move(entry.second.realms);
        entry.second.realms = result.at(parent).realms;
        for (auto& realm : local)
            entry.second.realms.insert_or_assign(realm.first, std::move(realm.second));
    }
    return result;
}
} // namespace

namespace detail {
/** @brief 已执行条目保存真实实例或等待父代的意图；清理失败不会删除所有权账目。 */
struct KernelRecord {
    EntryValue configuration;
    std::optional<Instance> instance;
    std::vector<ServiceRequirement> requirements;
    Epoch parentEpoch = 0;
    std::optional<ErrorInfo> failure{};
};
/** @brief 每次动作拥有独立 Promise，投递故障仅跨线程终结 Promise。 */
struct KernelAction {
    EntryOperation operation;
    std::shared_ptr<const ValidatedCandidate> target;
    Promise<StepReport> done;
};
/** @brief 回执失败可从工作线程到达，不读取实例表或 owner 状态。 */
struct KernelDelivery {
    std::atomic_bool blocked{false};
    std::mutex mutex;
    std::optional<Promise<StepReport>> waiting;
    Promise<CleanupReport> closed;
    /** @brief 先登记动作 Promise，关闭回执失败也能终结在途动作而不读取 owner 表。 */
    bool track(Promise<StepReport> promise)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (blocked.load())
            return false;
        waiting = std::move(promise);
        return true;
    }
    /** @brief action 仍强持 Promise，锁内移除登记不释放最后一个业务载荷。 */
    void untrack()
    {
        std::lock_guard<std::mutex> lock(mutex);
        waiting.reset();
    }
    /** @brief 原子关门后保守保留控制块，不能把丢失回执当作实例静止。 */
    void fail(const std::shared_ptr<KernelAction>& action, ErrorInfo error)
    {
        std::optional<Promise<StepReport>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            blocked.store(true);
            pending.swap(waiting);
        }
        error.phase = "loader.kernel.delivery";
        if (pending)
            pending->reject(error);
        if (action)
            action->done.reject(error);
        closed.reject(std::move(error));
    }
};
/** @brief 除投递门禁和 Promise 外只由 owner 线程访问，关闭不影响同 Kernel 的其他实例。 */
struct KernelDriverControl : std::enable_shared_from_this<KernelDriverControl> {
    std::shared_ptr<IScheduler> scheduler;
    std::shared_ptr<Kernel> kernel;
    std::shared_ptr<const ConfigurationDefinitions> definitions;
    const auto& plugins() const { return definitions->data_->entries; }
    const ConfiguredPlugin& plugin(const std::string& id) const { return plugins().at(id); }
    std::map<std::string, RealmId> realms;
    std::map<std::string, KernelRecord> records;
    std::shared_ptr<KernelDelivery> delivery = std::make_shared<KernelDelivery>();
    std::shared_ptr<KernelDriverControl> keepAlive;
    std::shared_ptr<KernelAction> active;
    std::shared_ptr<const ValidatedCandidate> currentTarget;
    std::optional<ErrorInfo> backgroundUnsafe;
    CleanupReport cleanup{true, {}};
    std::size_t pendingStops = 0, accesses = 0;
    bool closing = false, closePublished = false;
    bool restoreScheduled = false;
    std::set<OperationId> replacementWaiters;
    /** @brief 同步重入关闭时，外层实例操作返回前不能完成驱动关闭。 */
    struct Access {
        KernelDriverControl& state;
        /** @brief 调用方强持控制块到此访问析构之后。 */
        explicit Access(KernelDriverControl& value) : state(value) { ++state.accesses; }
        /** @brief 最后一个同步调用返回后重新检查真实排空条件。 */
        ~Access()
        {
            --state.accesses;
            state.checkClosed();
        }
    };
    /** @brief Core 自身也可能丢失内部回执；调度器开放不等于所有实例仍可推进。 */
    Result<void> resourceHealth() const
    {
        for (const auto& entry : records) {
            if (!entry.second.instance)
                continue;
            auto snapshot = entry.second.instance->snapshot();
            if (!snapshot.hasValue())
                return Result<void>::failure(snapshot.error());
            if (snapshot.value().state != InstanceState::Disposed &&
                (snapshot.value().deliveryBlocked || snapshot.value().resources.deliveryBlocked))
                return Result<void>::failure(
                    driverError(ErrorCode::SchedulerClosed, "owned instance delivery unavailable", entry.first));
        }
        return Result<void>::success();
    }
    /** @brief 读取诊断仅要求 owner；业务还要求开放投递和未关闭。 */
    Result<void> check(bool business = true) const
    {
        if (!scheduler->isCurrentThread())
            return Result<void>::failure(driverError(ErrorCode::WrongThread, "kernel driver requires owner thread"));
        if (!business)
            return Result<void>::success();
        if (closing)
            return Result<void>::failure(driverError(ErrorCode::ContextClosing, "kernel driver closing"));
        if (delivery->blocked.load() || !scheduler->isOpen())
            return Result<void>::failure(driverError(ErrorCode::SchedulerClosed, "kernel driver delivery unavailable"));
        if (backgroundUnsafe)
            return Result<void>::failure(*backgroundUnsafe);
        auto state = kernel->snapshot();
        if (!state.hasValue())
            return Result<void>::failure(state.error());
        if (state.value().closing)
            return Result<void>::failure(driverError(ErrorCode::ContextClosing, "kernel closing"));
        return resourceHealth();
    }
    /** @brief realm 名称由宿主绑定，未覆盖服务保留 Runtime 原键的精确域。 */
    RealmMap realmMap(const EntryValue& value) const
    {
        RealmMap result;
        for (const auto& entry : value.realms)
            result = result.isolate(entry.first, realms.at(entry.second)).value();
        return result;
    }
    /** @brief 实际实例或等待意图均使用 Core 门禁，不从 UI 状态推定占用。 */
    Result<void> entryAccess(const KernelRecord& record, const std::string& path) const
    {
        auto checkEntry = [&](const KernelRecord& item) {
            return item.instance ? item.instance->checkMutation()
                                 : kernel->checkMount(plugin(item.configuration.plugin).runtime,
                                                      realmMap(item.configuration), item.requirements);
        };
        auto own = checkEntry(record);
        if (!own.hasValue())
            return own;
        for (auto parent = parentPath(path); !parent.empty(); parent = parentPath(parent)) {
            auto at = records.find(parent);
            if (at != records.end()) {
                auto allowed = checkEntry(at->second);
                if (!allowed.hasValue())
                    return allowed;
            }
        }
        return Result<void>::success();
    }
    /** @brief 预检与规划共用分类，未变更的受占用条目只观察，不抢先重建或重试。 */
    ChangeKind kindFor(const std::string& path, const EntryValue& value,
                       const std::vector<ServiceRequirement>& required) const
    {
        const auto found = records.find(path);
        if (found == records.end())
            return ChangeKind::Add;
        const auto& record = found->second;
        const bool same = sameIdentity(record.configuration, value) && record.configuration.config == value.config &&
                          sameRequirements(record.requirements, required);
        if (same) {
            const auto gate = entryAccess(record, path);
            if (!gate.hasValue() && gate.error().code == ErrorCode::ConfigConflict)
                return ChangeKind::NoOp;
        }
        if (!record.instance || record.instance->snapshot().value().state == InstanceState::Disposed)
            return ChangeKind::Add;
        if (!sameIdentity(record.configuration, value) || !sameRequirements(record.requirements, required))
            return ChangeKind::Recreate;
        if (record.configuration.config != value.config ||
            record.instance->snapshot().value().state == InstanceState::Failed)
            return ChangeKind::Restart;
        return ChangeKind::NoOp;
    }
    /** @brief 解除占用后重新检查当前意图；观察没有管理权限，不轮询或创建第二份执行计划。 */
    void waitReplacement(const ErrorInfo& error)
    {
        if (error.code != ErrorCode::ConfigConflict || !error.operation.valid() ||
            !replacementWaiters.insert(error.operation).second)
            return;
        const auto operation = error.operation;
        std::weak_ptr<KernelDriverControl> weak = shared_from_this();
        observe(
            kernel->whenReplacementReleased(operation).thenOn(scheduler, [weak, operation](const Result<void>& result) {
                if (auto self = weak.lock()) {
                    self->replacementWaiters.erase(operation);
                    if (result.hasValue() && !self->closing)
                        self->requestRestore();
                }
            }));
    }
    /** @brief 每个新增接口必须有宿主类型；只对目标值求解，不创建 Runtime 或实例。 */
    Result<std::vector<ServiceRequirement>> requirements(const EntryValue& value, const std::vector<InjectSpec>& inject,
                                                         const std::string& path) const
    {
        using Output = Result<std::vector<ServiceRequirement>>;
        const auto& fixed = plugin(value.plugin).runtime.requirements();
        const auto& optional = plugin(value.plugin).configurableInject;
        for (const auto& requirement : fixed) {
            auto found = std::find_if(inject.begin(), inject.end(), [&](const InjectSpec& spec) {
                return spec.name == requirement.key.name && spec.major == requirement.key.interfaceMajor;
            });
            if (found == inject.end() || !satisfies(*found, requirement))
                return Output::failure(
                    driverError(ErrorCode::ConfigInvalid, "effective inject weakens fixed runtime contract", path));
        }
        auto view = realmMap(value);
        std::map<ServiceKey, ServiceRequirement> merged;
        for (const auto& spec : inject) {
            bool known = false;
            for (const auto* list : {&fixed, &optional}) {
                for (const auto& requirement : *list) {
                    if (requirement.key.name != spec.name || requirement.key.interfaceMajor != spec.major)
                        continue;
                    known = true;
                    if (!satisfies(spec, requirement))
                        return Output::failure(
                            driverError(ErrorCode::ConfigInvalid, "inject weakens host interface floor", path));
                    auto typed = requirement;
                    typed.key = view.resolve(typed.key);
                    typed.minimumMinor = spec.minimumMinor;
                    typed.capabilities = spec.capabilities;
                    auto found = merged.find(typed.key);
                    if (found != merged.end() && found->second.type != typed.type)
                        return Output::failure(
                            driverError(ErrorCode::ConfigInvalid, "inject resolves conflicting service types", path));
                    const auto key = typed.key;
                    merged.emplace(key, std::move(typed));
                }
            }
            if (!known)
                return Output::failure(
                    driverError(ErrorCode::ConfigInvalid, "inject interface has no host type declaration", path));
        }
        std::vector<ServiceRequirement> result;
        for (auto& entry : merged)
            result.push_back(std::move(entry.second));
        return Output::success(std::move(result));
    }
    /** @brief 按真实类型/主版本/域建立提供者索引并做确定性拓扑排序。 */
    Result<std::vector<std::string>> order(const std::map<std::string, EntryValue>& entries,
                                           const std::map<std::string, std::vector<ServiceRequirement>>& contracts,
                                           bool validateProviders = true) const
    {
        /** @brief 服务键索引同时保存路径与类型/版本契约。 */
        struct Provider {
            std::string path;
            ServiceOutput output;
        };
        std::map<ServiceKey, Provider> providers;
        std::map<std::string, std::set<std::string>> edges;
        std::map<std::string, std::size_t> degrees;
        for (const auto& entry : entries) {
            degrees.emplace(entry.first, 0);
            auto view = realmMap(entry.second);
            for (const auto& output : plugin(entry.second.plugin).runtime.outputs()) {
                auto inserted = providers.emplace(view.resolve(output.key), Provider{entry.first, output});
                if (!inserted.second)
                    return Result<std::vector<std::string>>::failure(driverError(
                        ErrorCode::ConfigInvalid, "multiple configured providers for one service key", entry.first));
            }
        }
        for (const auto& entry : entries) {
            auto view = realmMap(entry.second);
            auto parent = parentPath(entry.first);
            if (!parent.empty() && entries.count(parent) && edges[parent].insert(entry.first).second)
                ++degrees.at(entry.first);
            for (const auto& requirement : contracts.at(entry.first)) {
                auto found = providers.find(view.resolve(requirement.key));
                if (found == providers.end())
                    continue;
                const auto& output = found->second.output;
                if (validateProviders &&
                    (output.type != requirement.type || output.metadata.minor < requirement.minimumMinor ||
                     std::any_of(
                         requirement.capabilities.begin(), requirement.capabilities.end(),
                         [&](const std::string& capability) {
                             return std::find(output.metadata.capabilities.begin(), output.metadata.capabilities.end(),
                                              capability) == output.metadata.capabilities.end();
                         })))
                    return Result<std::vector<std::string>>::failure(
                        driverError(ErrorCode::ConfigInvalid, "configured provider does not satisfy runtime dependency",
                                    entry.first));
                if (edges[found->second.path].insert(entry.first).second)
                    ++degrees.at(entry.first);
            }
        }
        std::set<std::string> ready;
        for (const auto& degree : degrees)
            if (degree.second == 0)
                ready.insert(degree.first);
        std::vector<std::string> result;
        while (!ready.empty()) {
            auto path = *ready.begin();
            ready.erase(ready.begin());
            result.push_back(path);
            for (const auto& consumer : edges[path])
                if (--degrees.at(consumer) == 0)
                    ready.insert(consumer);
        }
        if (result.size() != entries.size())
            return Result<std::vector<std::string>>::failure(
                driverError(ErrorCode::DependencyCycle, "configured runtime dependency cycle"));
        return Result<std::vector<std::string>>::success(std::move(result));
    }
    /** @brief 验证禁用项同样不能携带未实现的配置能力。 */
    Result<void> preflight(const ValidatedCandidate& target) const
    {
        auto checked = check();
        if (!checked.hasValue())
            return checked;
        if (!target.source().isValid())
            return Result<void>::failure(driverError(ErrorCode::ConfigInvalid, "candidate was moved from"));
        if (!definitions->owns(target))
            return Result<void>::failure(
                driverError(ErrorCode::ConfigConflict, "candidate definition identity changed"));
        auto bindings = checkModuleBindings(target.source().structure(), definitions->data_->modules);
        if (!bindings.hasValue())
            return Result<void>::failure(driverError(bindings.error().code, bindings.error().message,
                                                     bindings.error().entryPath.value_or(std::string{})));
        std::map<std::string, EntryValue> enabled;
        std::map<std::string, std::vector<ServiceRequirement>> contracts;
        for (const auto& entry : runtimeValues(target)) {
            auto plugin = plugins().find(entry.second.plugin);
            if (plugin == plugins().end())
                return Result<void>::failure(
                    driverError(ErrorCode::ConfigInvalid, "plugin is not registered in this driver", entry.first));
            for (const auto& realm : entry.second.realms)
                if (realms.find(realm.second) == realms.end())
                    return Result<void>::failure(
                        driverError(ErrorCode::ConfigInvalid, "unknown host realm alias", entry.first));
            auto view = realmMap(entry.second);
            for (const auto& output : plugin->second.runtime.outputs())
                if (!kernel->services().ownsRealm(view.resolve(output.key).realm).value())
                    return Result<void>::failure(
                        driverError(ErrorCode::ConfigInvalid, "output uses foreign realm", entry.first));
            auto effective = target.effectiveInject().find(entry.first);
            if (effective == target.effectiveInject().end())
                return Result<void>::failure(
                    driverError(ErrorCode::ConfigInvalid, "missing effective inject", entry.first));
            auto typed = requirements(entry.second, effective->second, entry.first);
            if (!typed.hasValue())
                return Result<void>::failure(typed.error());
            const auto limit = plugin->second.maximumDependencies;
            if (limit && typed.value().size() > limit)
                return Result<void>::failure(
                    driverError(ErrorCode::ConfigInvalid, "plugin dependency budget exceeded", entry.first));
            for (const auto& dependency : typed.value())
                if (!kernel->services().ownsRealm(view.resolve(dependency.key).realm).value())
                    return Result<void>::failure(
                        driverError(ErrorCode::ConfigInvalid, "dependency uses foreign realm", entry.first));
            if (!entry.second.effectiveDisabled) {
                const auto& gate = plugin->second.available;
                if (gate) {
                    try {
                        auto ready = gate();
                        if (!ready.hasValue())
                            return ready;
                    } catch (...) {
                        return Result<void>::failure(
                            driverError(ErrorCode::ConfigInvalid, "host availability check threw", entry.first));
                    }
                }
                enabled.emplace(entry.first, entry.second);
                contracts.emplace(entry.first, std::move(typed).value());
            }
        }
        auto sorted = order(enabled, contracts);
        if (!sorted.hasValue())
            return Result<void>::failure(sorted.error());
        // 路径唯一的单动作协议无法原子交换两个仍启用实例的输出所有权，先明确拒绝。
        std::map<ServiceKey, std::string> owned;
        for (const auto& record : records) {
            if (!record.second.instance)
                continue;
            auto snapshot = record.second.instance->snapshot();
            if (!snapshot.hasValue())
                return Result<void>::failure(snapshot.error());
            if (snapshot.value().state == InstanceState::Disposed || enabled.find(record.first) == enabled.end())
                continue;
            const auto& value = record.second.configuration;
            auto view = realmMap(value);
            for (const auto& output : plugin(value.plugin).runtime.outputs())
                owned.emplace(view.resolve(output.key), record.first);
        }
        for (const auto& entry : enabled) {
            auto view = realmMap(entry.second);
            for (const auto& output : plugin(entry.second.plugin).runtime.outputs()) {
                auto holder = owned.find(view.resolve(output.key));
                if (holder != owned.end() && holder->second != entry.first)
                    return Result<void>::failure(driverError(
                        ErrorCode::ConfigConflict,
                        "service ownership transfer requires an explicit removal target first", entry.first));
            }
        }
        for (const auto& record : records) {
            const auto desired = enabled.find(record.first);
            const auto kind = desired == enabled.end()
                                  ? ChangeKind::Remove
                                  : kindFor(record.first, desired->second, contracts.at(record.first));
            if (kind != ChangeKind::NoOp) {
                auto gate = entryAccess(record.second, record.first);
                if (!gate.hasValue()) {
                    auto error = gate.error();
                    error.entryPath = record.first;
                    return Result<void>::failure(std::move(error));
                }
            }
        }
        for (const auto& entry : enabled)
            if (kindFor(entry.first, entry.second, contracts.at(entry.first)) != ChangeKind::NoOp) {
                auto gate = kernel->checkMount(plugin(entry.second.plugin).runtime, realmMap(entry.second),
                                               contracts.at(entry.first));
                if (!gate.hasValue()) {
                    auto error = gate.error();
                    error.entryPath = entry.first;
                    return Result<void>::failure(std::move(error));
                }
            }
        return Result<void>::success();
    }
    /** @brief 所有 Task 回执都由 owner 接收，投递失败不从 worker 改运行表。 */
    void observe(const Task<void>& receipt, const std::shared_ptr<KernelAction>& action = {})
    {
        cordis::detail::TaskAccess::observe(receipt, [gate = delivery, action](Task<void>::Snapshot result) {
            if (!result->hasValue())
                gate->fail(action, result->error());
        });
    }
    /** @brief 发布动作结果前结束在途账目，历史结果不保留目标或实例能力。 */
    void finish(const std::shared_ptr<KernelAction>& action, StepReport result)
    {
        if (active != action)
            return;
        active.reset();
        delivery->untrack();
        action->done.resolve(std::move(result));
        requestRestore();
        checkClosed();
    }
    /** @brief 失败仅在资源已清空且没有投递阻断时允许后续目标继续协调。 */
    void observeTarget(const std::shared_ptr<KernelAction>& action, const Instance& instance,
                       const Task<StableReport>& target)
    {
        auto self = shared_from_this();
        observe(target.thenOn(scheduler,
                              [self, action, instance](const Result<StableReport>& result) {
                                  Access access(*self);
                                  if (!result.hasValue()) {
                                      self->finish(action, {StepOutcome::Failed, false, result.error()});
                                      return;
                                  }
                                  if (self->closing) {
                                      self->finish(action, {StepOutcome::Failed, true,
                                                            driverError(ErrorCode::OperationCancelled, "driver closing",
                                                                        action->operation.path)});
                                      return;
                                  }
                                  switch (result.value().outcome) {
                                  case StableOutcome::Active:
                                      self->finish(action, {StepOutcome::Applied, true, {}});
                                      return;
                                  case StableOutcome::PendingMissingDependency:
                                      self->finish(action, {StepOutcome::Pending, true, {}});
                                      return;
                                  default:
                                      break;
                                  }
                                  auto state = instance.snapshot();
                                  bool safe = state.hasValue() && !state.value().deliveryBlocked &&
                                              !state.value().resources.deliveryBlocked &&
                                              !state.value().hasRetainedResources();
                                  auto error = state.hasValue() && state.value().failure
                                                   ? *state.value().failure
                                                   : driverError(ErrorCode::ActivationFailed,
                                                                 "runtime operation did not become active",
                                                                 action->operation.path);
                                  self->finish(action, {StepOutcome::Failed, safe, std::move(error)});
                              }),
                action);
    }
    /** @brief 顶层经 Kernel 挂载，子项只能使用真实父代能力；等待父代不创建虚构实例。 */
    Result<KernelRecord> create(const std::string& path, const EntryValue& value,
                                const std::vector<ServiceRequirement>& typed)
    {
        KernelRecord record{value, {}, typed};
        const auto parent = parentPath(path);
        if (parent.empty()) {
            auto mounted = kernel->mount(plugin(value.plugin).runtime, value.config, realmMap(value), typed);
            if (!mounted.hasValue())
                return Result<KernelRecord>::failure(mounted.error());
            record.instance.emplace(mounted.value());
        } else {
            auto found = records.find(parent);
            if (found == records.end() || !found->second.instance ||
                found->second.instance->snapshot().value().state != InstanceState::Active)
                return Result<KernelRecord>::success(std::move(record));
            auto available = found->second.instance->whenActiveAfter().tryResult();
            if (!available)
                return Result<KernelRecord>::success(std::move(record));
            if (!available->hasValue())
                return Result<KernelRecord>::failure(available->error());
            auto context = available->value();
            auto identity = context.identity();
            if (!identity.hasValue())
                return Result<KernelRecord>::success(std::move(record));
            for (const auto& realm : value.realms) {
                auto isolated = context.isolate(realm.first, realms.at(realm.second));
                if (!isolated.hasValue())
                    return Result<KernelRecord>::failure(isolated.error());
                context = isolated.value();
            }
            auto mounted = context.inject(plugin(value.plugin).runtime, value.config, typed);
            if (!mounted.hasValue())
                return Result<KernelRecord>::failure(mounted.error());
            record.instance.emplace(mounted.value());
            record.parentEpoch = identity.value().epoch;
        }
        return Result<KernelRecord>::success(std::move(record));
    }
    /** @brief 每个实例只保持一个未来成功观察，旧身份通知不能操作替代实例。 */
    void watchEpoch(const std::string& path, const Instance& instance, Epoch after = 0)
    {
        const auto id = instance.snapshot().value().instance;
        std::weak_ptr<KernelDriverControl> weak = shared_from_this();
        observe(instance.whenActiveAfter(after).thenOn(scheduler, [weak, path, id](const Result<Context>& result) {
            auto self = weak.lock();
            if (!self || self->closing)
                return;
            Access access(*self);
            auto found = self->records.find(path);
            if (found == self->records.end() || !found->second.instance ||
                found->second.instance->snapshot().value().instance != id)
                return;
            if (!result.hasValue()) {
                if (result.error().code == ErrorCode::SchedulerClosed)
                    self->delivery->fail({}, result.error());
                return;
            }
            auto identity = result.value().identity();
            self->watchEpoch(path, *found->second.instance, identity.hasValue() ? identity.value().epoch : 0);
            self->requestRestore();
        }));
    }
    /** @brief 后台恢复不能把清理残留当作安全状态；历史 Pending 报告保持原值。 */
    void observeRestored(const std::string& path, const Instance& instance)
    {
        auto operation = instance.operation().value();
        std::weak_ptr<KernelDriverControl> weak = shared_from_this();
        observe(operation.target.thenOn(scheduler, [weak, path, instance](const Result<StableReport>& result) {
            auto self = weak.lock();
            if (!self || self->closing)
                return;
            Access access(*self);
            auto snapshot = instance.snapshot().value();
            if (!result.hasValue() ||
                (result.value().outcome == StableOutcome::Failed && snapshot.hasRetainedResources())) {
                self->backgroundUnsafe.emplace(driverError(
                    ErrorCode::CleanupFailed, "restored subtree did not establish a safe checkpoint", path));
            }
        }));
    }
    /** @brief 自动恢复只处理曾接受执行的子路径，不抢先创建计划尚未执行的新子项。 */
    void restore()
    {
        if (closing || active || !currentTarget || backgroundUnsafe || delivery->blocked.load())
            return;
        const auto values = runtimeValues(*currentTarget);
        std::map<std::string, EntryValue> enabled;
        std::map<std::string, std::vector<ServiceRequirement>> contracts;
        for (const auto& entry : values) {
            if (entry.second.effectiveDisabled)
                continue;
            auto typed = requirements(entry.second, currentTarget->effectiveInject().at(entry.first), entry.first);
            if (!typed.hasValue())
                return;
            enabled.emplace(entry);
            contracts.emplace(entry.first, std::move(typed).value());
        }
        auto sorted = order(enabled, contracts);
        if (!sorted.hasValue())
            return;
        for (const auto& path : sorted.value()) {
            if (closing || active)
                return;
            auto found = records.find(path);
            if (parentPath(path).empty() || found == records.end() || found->second.failure ||
                (found->second.instance && found->second.instance->snapshot().value().state != InstanceState::Disposed))
                continue;
            const auto admitted = entryAccess(found->second, path);
            if (!admitted.hasValue() && admitted.error().code == ErrorCode::ConfigConflict) {
                waitReplacement(admitted.error());
                continue;
            }
            auto made = create(path, enabled.at(path), contracts.at(path));
            if (!made.hasValue()) {
                found->second.failure.emplace(made.error());
                if (made.error().code == ErrorCode::SchedulerClosed) {
                    delivery->fail({}, made.error());
                    return;
                }
                continue;
            }
            found->second = std::move(made).value();
            if (!found->second.instance)
                continue;
            auto instance = *found->second.instance;
            if (closing)
                closeInstance(instance);
            else {
                watchEpoch(path, instance);
                observeRestored(path, instance);
            }
            auto health = resourceHealth();
            if (!health.hasValue()) {
                delivery->fail({}, health.error());
                return;
            }
        }
    }
    /** @brief 恢复使用独立 FIFO 工作项，活动配置动作结束后再从当前目标检查。 */
    void requestRestore()
    {
        if (closing || restoreScheduled || !currentTarget)
            return;
        restoreScheduled = true;
        std::weak_ptr<KernelDriverControl> weak = shared_from_this();
        auto gate = delivery;
        try {
            if (scheduler->post({[weak] {
                                     if (auto self = weak.lock()) {
                                         Access access(*self);
                                         self->restoreScheduled = false;
                                         self->restore();
                                     }
                                 },
                                 [gate] {
                                     gate->fail(
                                         {}, driverError(ErrorCode::SchedulerClosed, "subtree restoration discarded"));
                                 }}))
                return;
        } catch (...) {
        }
        delivery->fail({}, driverError(ErrorCode::SchedulerClosed, "subtree restoration was not delivered"));
    }
    /** @brief 实例建立后立即记账并观察真实稳定操作；父尚未激活时返回明确 Pending。 */
    void mount(const std::shared_ptr<KernelAction>& action)
    {
        auto values = runtimeValues(*action->target);
        const auto& path = action->operation.path;
        const auto& value = values.at(path);
        auto typed = requirements(value, action->target->effectiveInject().at(path), path);
        if (!typed.hasValue()) {
            finish(action, {StepOutcome::Failed, true, typed.error()});
            return;
        }
        auto made = create(path, value, typed.value());
        if (!made.hasValue()) {
            finish(action, {StepOutcome::Failed, true, made.error()});
            return;
        }
        records.insert_or_assign(path, std::move(made).value());
        const auto& record = records.at(path);
        if (!record.instance) {
            finish(action, {StepOutcome::Pending, true, {}});
            return;
        }
        auto instance = *record.instance;
        if (closing)
            closeInstance(instance);
        else
            watchEpoch(path, instance);
        auto health = resourceHealth();
        if (!health.hasValue()) {
            delivery->fail(action, health.error());
            return;
        }
        observeTarget(action, instance, instance.operation().value().target);
    }
    /** @brief 关闭广播中接纳的实例同样等待完整 stop，重入挂载不能漏出关闭屏障。 */
    void closeInstance(const Instance& instance)
    {
        ++pendingStops;
        auto self = shared_from_this();
        observe(instance.stop().thenOn(scheduler, [self](const Result<CleanupReport>& result) {
            Access access(*self);
            if (!result.hasValue()) {
                self->cleanup.quiescent = false;
                self->cleanup.errors.push_back(result.error());
            } else {
                self->cleanup.quiescent = self->cleanup.quiescent && result.value().quiescent;
                self->cleanup.errors.insert(self->cleanup.errors.end(), result.value().errors.begin(),
                                            result.value().errors.end());
            }
            --self->pendingStops;
        }));
    }
    /** @brief 已证明静止或尚未挂载的路径才可忘记；删除父意图也删除全部等待后代。 */
    void eraseRecords(const std::string& path, bool descendants)
    {
        records.erase(path);
        if (!descendants)
            return;
        const auto prefix = path + "/";
        for (auto it = records.lower_bound(prefix);
             it != records.end() && it->first.compare(0, prefix.size(), prefix) == 0;)
            it = records.erase(it);
    }
    /** @brief Recreate 不复用旧实例，必须等 stop 的完整清理证明后才挂载新实例。 */
    void remove(const std::shared_ptr<KernelAction>& action, const Instance& instance, bool recreate)
    {
        auto self = shared_from_this();
        observe(instance.stop().thenOn(scheduler,
                                       [self, action, recreate](const Result<CleanupReport>& result) {
                                           Access access(*self);
                                           if (!result.hasValue() || !result.value().quiescent) {
                                               auto error = driverError(ErrorCode::CleanupFailed,
                                                                        "runtime cleanup did not establish quiescence",
                                                                        action->operation.path);
                                               if (result.hasValue())
                                                   error.causes = result.value().errors;
                                               else
                                                   error.causes.push_back(result.error());
                                               self->finish(action, {StepOutcome::Failed, false, std::move(error)});
                                               return;
                                           }
                                           self->eraseRecords(action->operation.path, !recreate);
                                           if (self->closing)
                                               self->finish(action,
                                                            {StepOutcome::Failed, true,
                                                             driverError(ErrorCode::OperationCancelled,
                                                                         "driver closing", action->operation.path)});
                                           else if (recreate)
                                               self->mount(action);
                                           else
                                               self->finish(action, {StepOutcome::Applied, true, {}});
                                       }),
                action);
    }
    /** @brief 只有全部原生 stop 回执及在途动作返回后才发布关闭，非静止时继续自保留。 */
    void checkClosed()
    {
        if (!closing || closePublished || accesses != 0 || pendingStops != 0 || active || delivery->blocked.load())
            return;
        closePublished = true;
        if (cleanup.quiescent)
            records.clear();
        delivery->closed.resolve(cleanup);
        if (cleanup.quiescent)
            keepAlive.reset();
    }
};
} // namespace detail

KernelReconcileDriver::KernelReconcileDriver(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<Kernel> kernel,
                                             std::vector<KernelPlugin> plugins, std::map<std::string, RealmId> realms)
    : KernelReconcileDriver(std::move(scheduler), std::move(kernel),
                            ConfigurationDefinitions::runtimeOnly(std::move(plugins)), std::move(realms),
                            DefinitionsTag{})
{
}
std::shared_ptr<KernelReconcileDriver>
KernelReconcileDriver::configured(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<Kernel> kernel,
                                  std::shared_ptr<const ConfigurationDefinitions> definitions,
                                  std::map<std::string, RealmId> realms)
{
    return std::shared_ptr<KernelReconcileDriver>(new KernelReconcileDriver(
        std::move(scheduler), std::move(kernel), std::move(definitions), std::move(realms), DefinitionsTag{}));
}
KernelReconcileDriver::KernelReconcileDriver(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<Kernel> kernel,
                                             std::shared_ptr<const ConfigurationDefinitions> definitions,
                                             std::map<std::string, RealmId> realms, DefinitionsTag)
{
    if (!scheduler || !scheduler->isCurrentThread() || !scheduler->isOpen() || !kernel ||
        !kernel->usesScheduler(scheduler))
        throw std::invalid_argument("kernel driver requires Kernel and open owner scheduler");
    auto snapshot = kernel->snapshot();
    if (!snapshot.hasValue() || snapshot.value().closing)
        throw std::invalid_argument("kernel driver requires open Kernel");
    control_ = std::make_shared<detail::KernelDriverControl>();
    control_->scheduler = std::move(scheduler);
    control_->kernel = std::move(kernel);
    if (!definitions)
        throw std::invalid_argument("definitions are required");
    control_->definitions = std::move(definitions);
    for (const auto& entry : control_->plugins())
        if (std::none_of(snapshot.value().names.begin(), snapshot.value().names.end(),
                         [&](const auto& name) { return name.id == entry.second.runtime.id(); }))
            throw std::invalid_argument("foreign kernel runtime");
    for (const auto& realm : realms)
        if (realm.first.empty() || !control_->kernel->services().ownsRealm(realm.second).value())
            throw std::invalid_argument("invalid realm alias");
    control_->realms = std::move(realms);
}

KernelReconcileDriver::~KernelReconcileDriver() = default;
std::shared_ptr<const ConfigurationDefinitions> KernelReconcileDriver::configurationDefinitions() const
{
    return control_->definitions->data_->identity ? control_->definitions : nullptr;
}

Result<void> KernelReconcileDriver::preflight(const ValidatedCandidate& target)
{
    return control_->preflight(target);
}

Result<std::vector<EntryOperation>> KernelReconcileDriver::plan(const ValidatedCandidate& target)
{
    auto state = control_;
    auto checked = state->preflight(target);
    if (!checked.hasValue())
        return Result<std::vector<EntryOperation>>::failure(checked.error());
    std::map<std::string, EntryValue> actual, desired;
    std::map<std::string, std::vector<ServiceRequirement>> actualContracts, desiredContracts;
    for (const auto& entry : state->records) {
        if (!entry.second.instance || entry.second.instance->snapshot().value().state == InstanceState::Disposed)
            continue;
        actual.emplace(entry.first, entry.second.configuration);
        actualContracts.emplace(entry.first, entry.second.requirements);
    }
    for (const auto& entry : runtimeValues(target))
        if (!entry.second.effectiveDisabled) {
            desired.emplace(entry.first, entry.second);
            auto typed = state->requirements(entry.second, target.effectiveInject().at(entry.first), entry.first);
            if (!typed.hasValue())
                return Result<std::vector<EntryOperation>>::failure(typed.error());
            desiredContracts.emplace(entry.first, std::move(typed).value());
        }
    // 部分应用状态只用于消费者优先清理排序，不能要求旧、新契约已经互相满足。
    auto oldOrder = state->order(actual, actualContracts, false);
    if (!oldOrder.hasValue())
        return Result<std::vector<EntryOperation>>::failure(oldOrder.error());
    auto newOrder = state->order(desired, desiredContracts);
    if (!newOrder.hasValue())
        return Result<std::vector<EntryOperation>>::failure(newOrder.error());
    std::vector<EntryOperation> operations;
    // 等待意图没有输出或租约，不能当成真实提供者参与旧图，但仍要显式移除它的登记。
    for (const auto& entry : state->records)
        if (!actual.count(entry.first) && !desired.count(entry.first)) {
            const bool retained = target.source().structure().entries().count(entry.first) != 0;
            operations.push_back({entry.first, retained ? ChangeKind::Disable : ChangeKind::Remove});
        }
    for (auto it = oldOrder.value().rbegin(); it != oldOrder.value().rend(); ++it)
        if (desired.find(*it) == desired.end()) {
            auto found = target.source().structure().entries().find(*it);
            operations.push_back(
                {*it, found == target.source().structure().entries().end() ? ChangeKind::Remove : ChangeKind::Disable});
        }
    for (const auto& path : newOrder.value()) {
        const auto& value = desired.at(path);
        operations.push_back({path, state->kindFor(path, value, desiredContracts.at(path))});
    }
    return Result<std::vector<EntryOperation>>::success(std::move(operations));
}

Task<StepReport> KernelReconcileDriver::apply(EntryOperation operation,
                                              std::shared_ptr<const ValidatedCandidate> target, std::uint64_t revision)
{
    auto state = control_;
    auto checked = state->check();
    if (!checked.hasValue())
        return failed<StepReport>(checked.error());
    if (state->active || state->accesses != 0)
        return failed<StepReport>(
            driverError(ErrorCode::ConfigConflict, "kernel driver already has an in-flight action"));
    if (!target || revision == 0)
        return failed<StepReport>(
            driverError(ErrorCode::InvalidArgument, "kernel driver requires target and revision"));
    auto supported = state->preflight(*target);
    if (!supported.hasValue())
        return failed<StepReport>(supported.error());
    const auto values = runtimeValues(*target);
    auto desired = values.find(operation.path);
    auto found = state->records.find(operation.path);
    const bool removing = operation.kind == ChangeKind::Remove || operation.kind == ChangeKind::Disable;
    if ((removing && desired != values.end() && !desired->second.effectiveDisabled) ||
        (!removing && (desired == values.end() || desired->second.effectiveDisabled)))
        return failed<StepReport>(
            driverError(ErrorCode::ConfigConflict, "operation does not match target entry", operation.path));
    switch (operation.kind) {
    case ChangeKind::Add:
    case ChangeKind::Remove:
    case ChangeKind::Disable:
    case ChangeKind::Restart:
    case ChangeKind::Recreate:
    case ChangeKind::NoOp:
        break;
    default:
        return failed<StepReport>(driverError(ErrorCode::InvalidArgument, "invalid kernel operation", operation.path));
    }
    std::vector<ServiceRequirement> desiredRequirements;
    if (!removing) {
        auto typed = state->requirements(desired->second, target->effectiveInject().at(operation.path), operation.path);
        if (!typed.hasValue())
            return failed<StepReport>(typed.error());
        desiredRequirements = std::move(typed).value();
    }
    if (!removing && found != state->records.end() && found->second.instance &&
        found->second.instance->snapshot().value().state != InstanceState::Disposed &&
        operation.kind != ChangeKind::Recreate &&
        (!sameIdentity(found->second.configuration, desired->second) ||
         !sameRequirements(found->second.requirements, desiredRequirements)))
        return failed<StepReport>(
            driverError(ErrorCode::ConfigConflict, "runtime identity requires recreation", operation.path));
    if (!removing && found != state->records.end() && operation.kind == ChangeKind::Add &&
        parentPath(operation.path).empty() && found->second.instance &&
        found->second.instance->snapshot().value().state != InstanceState::Disposed)
        return failed<StepReport>(driverError(ErrorCode::ConfigConflict, "entry is already mounted", operation.path));
    if (!removing && found != state->records.end() && operation.kind == ChangeKind::NoOp &&
        found->second.configuration.config != desired->second.config)
        return failed<StepReport>(
            driverError(ErrorCode::ConfigConflict, "configuration change requires restart", operation.path));
    const auto existingAccess =
        found != state->records.end() ? state->entryAccess(found->second, operation.path) : Result<void>::success();
    const bool reservedNoOp = operation.kind == ChangeKind::NoOp && !existingAccess.hasValue() &&
                              existingAccess.error().code == ErrorCode::ConfigConflict;
    if ((operation.kind == ChangeKind::NoOp || operation.kind == ChangeKind::Restart) && !reservedNoOp &&
        (found == state->records.end() ||
         (parentPath(operation.path).empty() &&
          (!found->second.instance || found->second.instance->snapshot().value().state == InstanceState::Disposed))))
        return failed<StepReport>(
            driverError(ErrorCode::ConfigConflict, "entry must be mounted before restart or NoOp", operation.path));
    auto action = std::make_shared<detail::KernelAction>();
    action->operation = std::move(operation);
    action->target = std::move(target);
    if (!state->delivery->track(action->done))
        return failed<StepReport>(driverError(ErrorCode::SchedulerClosed, "driver delivery unavailable"));
    state->active = action;
    state->currentTarget = action->target;
    state->keepAlive = state;
    detail::KernelDriverControl::Access access(*state);
    if (reservedNoOp) {
        state->waitReplacement(existingAccess.error());
        state->finish(action, {StepOutcome::Pending, true, {}});
    } else if (removing) {
        if (found == state->records.end() || !found->second.instance) {
            state->eraseRecords(action->operation.path, true);
            state->finish(action, {StepOutcome::Applied, true, {}});
        } else
            state->remove(action, *found->second.instance, false);
    } else if (found == state->records.end() || !found->second.instance ||
               found->second.instance->snapshot().value().state == InstanceState::Disposed)
        state->mount(action);
    else if (action->operation.kind == ChangeKind::Recreate &&
             (parentPath(action->operation.path).empty() ||
              !sameIdentity(found->second.configuration, desired->second) ||
              !sameRequirements(found->second.requirements, desiredRequirements) ||
              found->second.configuration.config != desired->second.config))
        state->remove(action, *found->second.instance, true);
    else {
        auto instance = *found->second.instance;
        auto result = [&] {
            if (found->second.configuration.config != desired->second.config)
                return instance.update(desired->second.config, instance.snapshot().value().revision);
            if (action->operation.kind == ChangeKind::Restart &&
                instance.snapshot().value().state == InstanceState::Failed)
                return instance.retry();
            return instance.operation();
        }();
        if (!result.hasValue())
            state->finish(action, {StepOutcome::Failed,
                                   result.error().code != ErrorCode::CleanupFailed &&
                                       result.error().code != ErrorCode::SchedulerClosed,
                                   result.error()});
        else {
            found->second.configuration = desired->second;
            state->observeTarget(action, instance, result.value().target);
        }
    }
    return action->done.task();
}

Result<std::vector<RunningEntry>> KernelReconcileDriver::snapshot() const
{
    auto state = control_;
    auto checked = state->check(false);
    if (!checked.hasValue())
        return Result<std::vector<RunningEntry>>::failure(checked.error());
    auto health = state->resourceHealth();
    if (!health.hasValue())
        return Result<std::vector<RunningEntry>>::failure(health.error());
    std::vector<RunningEntry> result;
    for (const auto& entry : state->records) {
        const auto& record = entry.second;
        if (!record.instance || record.instance->snapshot().value().state == InstanceState::Disposed) {
            if (!parentPath(entry.first).empty() && !state->closing)
                result.push_back({entry.first,
                                  record.configuration,
                                  record.failure ? InstanceState::Failed : InstanceState::Pending,
                                  {},
                                  0,
                                  !record.failure,
                                  0,
                                  record.failure});
            continue;
        }
        auto snapshot = record.instance->snapshot().value();
        result.push_back({entry.first, record.configuration, snapshot.state, snapshot.instance, snapshot.epoch, false,
                          record.parentEpoch, snapshot.failure});
    }
    return Result<std::vector<RunningEntry>>::success(std::move(result));
}

Task<CleanupReport> KernelReconcileDriver::close()
{
    auto state = control_;
    auto checked = state->check(false);
    if (!checked.hasValue())
        return failed<CleanupReport>(checked.error());
    if (state->closing) {
        auto health = state->resourceHealth();
        if (!health.hasValue())
            state->delivery->fail({}, health.error());
        return state->delivery->closed.task();
    }
    if (!state->scheduler->isOpen()) {
        state->delivery->fail({}, driverError(ErrorCode::SchedulerClosed, "scheduler closed before kernel driver"));
        return state->delivery->closed.task();
    }
    state->closing = true;
    state->keepAlive = state;
    detail::KernelDriverControl::Access access(*state);
    std::vector<Instance> instances;
    for (const auto& entry : state->records)
        if (entry.second.instance)
            instances.push_back(*entry.second.instance);
    for (const auto& instance : instances)
        state->closeInstance(instance);
    auto health = state->resourceHealth();
    if (!health.hasValue())
        state->delivery->fail({}, health.error());
    return state->delivery->closed.task();
}
} // namespace cordis::loader
