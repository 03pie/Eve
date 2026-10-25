/** @file configuration_management.cpp
 * @brief 可选配置适配器；普通 UI 不包含 Loader，所有动作经过 Session 的 CAS。
 */
#include <cordis/ui/configuration.hpp>
#include <map>
#include <stdexcept>
#include <utility>

namespace cordis::ui {
namespace {
/** @brief 回执只包含值，不保留插件对象或配置页。 */
class ConfigurationManagement final : public IConfigurationManagement {
public:
    /** @brief 固定 scheduler，避免把回执交给另一个生命周期的执行器。 */
    ConfigurationManagement(std::weak_ptr<loader::ConfigurationSession> session, std::shared_ptr<IScheduler> scheduler)
        : session_(std::move(session)), scheduler_(std::move(scheduler))
    {
    }
    /** @brief 本地启用与实际运行分别映射，包括尚无身份的等待/禁用条目。 */
    Result<ConfigurationManagementSnapshot> snapshot() const override
    {
        auto session = session_.lock();
        if (!session)
            return Result<ConfigurationManagementSnapshot>::failure(
                makeError(ErrorCode::ContextClosing, "configuration session expired"));
        auto value = session->snapshot();
        if (!value.hasValue())
            return Result<ConfigurationManagementSnapshot>::failure(value.error());
        const auto& state = value.value();
        ConfigurationManagementSnapshot result;
        result.revision = state.coordinator.desiredRevision;
        result.persistedRevision = state.store.persistedRevision;
        result.open = !state.closing && !state.deliveryBlocked && !state.coordinator.deliveryBlocked &&
                      !state.store.deliveryBlocked && !state.coordinator.unsafe && !state.coordinator.runningError;
        result.dirty = state.store.dirty;
        result.writing = state.store.writing;
        result.error = state.coordinator.runningError ? state.coordinator.runningError : state.store.lastError;
        std::map<std::string, loader::RunningEntry> actual;
        for (const auto& entry : state.coordinator.running) {
            actual.emplace(entry.path, entry);
            if (entry.failure && entry.failure->code == ErrorCode::CleanupFailed)
                result.open = false;
        }
        if (state.coordinator.desired)
            for (const auto& entry : state.coordinator.desired->source().structure().entries()) {
                ManagedConfigurationEntry row;
                row.path = entry.first;
                row.plugin = entry.second.plugin;
                row.enabled = !entry.second.disabled;
                row.effectiveEnabled = !entry.second.effectiveDisabled;
                auto found = actual.find(entry.first);
                if (found != actual.end()) {
                    row.instance = found->second.instance;
                    row.epoch = found->second.epoch;
                    row.state = found->second.state;
                    row.hasActual = true;
                    row.waitingForParent = found->second.waitingForParent;
                    row.error = found->second.failure;
                } else if (state.coordinator.runningError)
                    row.error = state.coordinator.runningError;
                row.canSetEnabled = result.open;
                row.canReconcile =
                    result.open && row.effectiveEnabled && row.hasActual && row.state == InstanceState::Failed;
                result.entries.push_back(std::move(row));
            }
        return Result<ConfigurationManagementSnapshot>::success(std::move(result));
    }
    /** @brief 不从 UI 获取实例能力，完整修改配置目标。 */
    Result<Task<ConfigurationActionReport>> setEnabled(const std::string& path, bool enabled,
                                                       std::uint64_t expected) override
    {
        auto session = session_.lock();
        if (!session)
            return Result<Task<ConfigurationActionReport>>::failure(
                makeError(ErrorCode::ContextClosing, "configuration session expired"));
        auto accepted = session->setEnabled(path, enabled, expected);
        return convert(std::move(accepted));
    }
    /** @brief 重新协调使用当前完整目标，保留 Coordinator 统一的排序/失败重试语义。 */
    Result<Task<ConfigurationActionReport>> reconcile(const std::string& path, std::uint64_t expected) override
    {
        auto session = session_.lock();
        if (!session)
            return Result<Task<ConfigurationActionReport>>::failure(
                makeError(ErrorCode::ContextClosing, "configuration session expired"));
        auto state = session->snapshot();
        if (!state.hasValue())
            return Result<Task<ConfigurationActionReport>>::failure(state.error());
        if (state.value().coordinator.desiredRevision != expected || !state.value().coordinator.desired)
            return Result<Task<ConfigurationActionReport>>::failure(
                makeError(ErrorCode::ConfigConflict, "configuration revision changed"));
        auto visible = snapshot();
        if (!visible.hasValue())
            return Result<Task<ConfigurationActionReport>>::failure(visible.error());
        bool allowed = false;
        for (const auto& entry : visible.value().entries)
            if (entry.path == path)
                allowed = entry.canReconcile;
        if (!allowed)
            return Result<Task<ConfigurationActionReport>>::failure(
                makeError(ErrorCode::ContextClosing, "configuration reconciliation unavailable"));
        auto draft = session->preview(state.value().coordinator.desired->source().normalizedYaml());
        if (!draft.hasValue())
            return Result<Task<ConfigurationActionReport>>::failure(draft.error());
        return convert(session->apply(draft.value()));
    }

private:
    /** @brief 转换仍等待真实计划稳定；投递失败不能留下永远 pending 的管理回执。 */
    Result<Task<ConfigurationActionReport>> convert(Result<loader::AcceptedTarget> accepted)
    {
        if (!accepted.hasValue())
            return Result<Task<ConfigurationActionReport>>::failure(accepted.error());
        Promise<ConfigurationActionReport> done;
        auto receipt = accepted.value().settled.thenOn(scheduler_, [done](const Result<loader::PlanReport>& result) {
            if (!result.hasValue()) {
                done.reject(result.error());
                return;
            }
            ConfigurationActionReport report;
            report.revision = result.value().revision;
            switch (result.value().outcome) {
            case loader::PlanOutcome::Applied:
                report.outcome = ConfigurationOutcome::Applied;
                break;
            case loader::PlanOutcome::Pending:
                report.outcome = ConfigurationOutcome::Pending;
                break;
            case loader::PlanOutcome::Failed:
                report.outcome = ConfigurationOutcome::Failed;
                break;
            case loader::PlanOutcome::Superseded:
                report.outcome = ConfigurationOutcome::Superseded;
                break;
            case loader::PlanOutcome::Cancelled:
                report.outcome = ConfigurationOutcome::Cancelled;
                break;
            }
            if (!result.value().errors.empty())
                report.error = result.value().errors.front();
            done.resolve(std::move(report));
        });
        cordis::detail::TaskAccess::observe(receipt, [done](Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
        return Result<Task<ConfigurationActionReport>>::success(done.task());
    }
    std::weak_ptr<loader::ConfigurationSession> session_;
    std::shared_ptr<IScheduler> scheduler_;
};
} // namespace
/** @brief 身份校验不挂载实例，也不接管 Session 关闭责任。 */
std::shared_ptr<IConfigurationManagement>
createConfigurationManagement(const std::shared_ptr<loader::ConfigurationSession>& session,
                              std::shared_ptr<IScheduler> scheduler)
{
    if (!session || !scheduler || !scheduler->isCurrentThread() || !session->usesScheduler(scheduler))
        throw std::invalid_argument("configuration management requires same owner scheduler");
    return std::make_shared<ConfigurationManagement>(session, std::move(scheduler));
}
} // namespace cordis::ui
