/** @file replacement_inspect_p.hpp
 * @brief 宿主提交准备读取同一 Core 的真实候选；副本不授予独立权限。
 */
#pragma once
#include <atomic>
#include <cordis/kernel.hpp>
#include <cordis/replacement_builder.hpp>
#include <functional>
#include <set>
#include <typeindex>
namespace cordis::detail {
struct PreparedReplacementStamp;
struct ReplacementInstanceInput {
    Instance instance;
    ReplacementInput input;
};
struct PreparedReplacementView {
    std::shared_ptr<const PreparedReplacementStamp> stamp;
    OperationId operation;
    ReplacementCommitMode mode;
    ReplacementClosure original;
    std::vector<RuntimeReplacementIdentity> definitions;
    std::vector<ReplacementInstanceInput> members;
};
struct OriginalReplacementView {
    std::weak_ptr<ReplacementScopeControl> owner;
    OperationId operation;
    ReplacementClosure closure;
    std::vector<RuntimeReplacementIdentity> definitions;
    std::vector<ReplacementInstanceInput> members;
    std::vector<std::pair<Runtime, Runtime>> factories{};
};
struct ReplacementReadSource {
    InstanceId instance;
    RuntimeId runtime;
    Epoch epoch;
    bool candidate = false, recovery = false;
};
/** @brief 已封存的未托管输入；对象来自原绑定，不授予创建或发布权限。 */
struct ReplacementInputBinding {
    std::shared_ptr<void> object;
    std::type_index type;
    InstanceId provider;
    bool candidate, recovery;
};
struct CoreReplacementAccess {
    using InputBindings = std::map<ServiceKey, BindingId>;
    struct PlanningContext {
        std::vector<ServiceOutput> providers;
        std::set<RealmId> realms;
        InputBindings bindings;
    };
    static Result<PlanningContext> planningContext(const ReplacementScope&, const Kernel&, bool recovery = false);
    static Result<void> sealPlan(const ReplacementScope&, const Kernel&, const std::vector<ServiceKey>&,
                                 const InputBindings& candidate = {}, const InputBindings& recovery = {});
    static Result<void> activateInputs(const ReplacementScope&, const Kernel&, bool recovery);
    static Result<std::vector<ReplacementReadSource>> inputSources(const ReplacementScope&, const Kernel&);
    static Result<std::vector<ReplacementInputBinding>> inputBindings(const ReplacementScope&, const Kernel&);
    static Task<void> whenInputsLost(const ReplacementScope&, const Kernel&, bool recovery);
    /** @brief 原释放准入通过后、通知前执行宿主纯解锁；动作不得抛出或回调外部代码。 */
    static Result<void> release(const ReplacementScope&, const std::function<void()>& beforeNotifications);
    static Result<void> borrow(const ReplacementScope&, const Kernel&, const std::vector<Runtime>&);
    static Task<CleanupReport> whenKernelClosed(const Kernel&);
    static bool kernelClosing(const Kernel&) noexcept;
    static Result<ReplacementBuilder> builder(const ReplacementScope&, const Kernel&);
    /** @brief 宿主逻辑条目选择实际祖先；Core 从唯一真实图核对并原子占用，不把编号当作来源。 */
    static Result<ReplacementScope> reserve(Kernel&, const std::vector<RuntimeId>&, const std::vector<InstanceId>&);
    static std::shared_ptr<std::atomic_bool> failureSignal(const ReplacementScope&, const Kernel&) noexcept;
    static void retainFailure(const ReplacementScope&) noexcept;
    static Result<OriginalReplacementView> original(const ReplacementScope&, const Kernel&);
    /** @brief 只重验来源/Reserved 状态；实际闭包、epoch 和修订仍由 freeze 重新核对。 */
    static bool current(const OriginalReplacementView&) noexcept;
    static Result<PreparedReplacementView> inspect(const ReplacementScope& scope, const Kernel& kernel);
    /** @brief 当前准备身份的纯重验；不借用 facade 地址，拒绝丢弃后重新准备的旧视图。 */
    static bool current(const PreparedReplacementView& view) noexcept;
};
} // namespace cordis::detail
