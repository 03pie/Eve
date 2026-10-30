/** @file configuration_management.cpp
 * @brief 可选配置适配器；普通 UI 不包含 Loader，所有动作经过 Session 的 CAS。
 */
#include <algorithm>
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
        auto names = session->registeredPlugins();
        if (!names.hasValue())
            return Result<ConfigurationManagementSnapshot>::failure(names.error());
        result.availablePlugins = names.value();
        result.canEdit = result.open;
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
                row.configuration = entry.second.config;
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

    /** @brief 新实例仅追加到顶层；重复身份由完整结构校验拒绝，不覆盖已有实例。 */
    Result<Task<ConfigurationActionReport>> addInstance(const std::string& id, const std::string& plugin,
                                                        const std::string& json, std::uint64_t expected) override
    {
        return mutate({}, id, plugin, json, expected);
    }
    /** @brief 路径定位后只替换 config 字段，完整预览再次检查 schema 和当前修订。 */
    Result<Task<ConfigurationActionReport>> updateConfiguration(const std::string& path, const std::string& json,
                                                                std::uint64_t expected) override
    {
        if (path.empty())
            return Result<Task<ConfigurationActionReport>>::failure(
                makeError(ErrorCode::InvalidArgument, "configuration path is empty"));
        return mutate(path, {}, {}, json, expected);
    }
    /** @brief 保存报告失败继续作为失败，不把应用成功当作已保存。 */
    Result<Task<void>> saveConfiguration(std::uint64_t expected) override
    {
        auto session = session_.lock();
        if (!session)
            return Result<Task<void>>::failure(makeError(ErrorCode::ContextClosing, "configuration session expired"));
        auto saving = session->save(expected);
        if (!saving.hasValue())
            return Result<Task<void>>::failure(saving.error());
        Promise<void> done;
        auto receipt = saving.value().thenOn(scheduler_, [done](const Result<loader::SaveReport>& result) {
            if (!result.hasValue())
                done.reject(result.error());
            else if (result.value().error)
                done.reject(*result.value().error);
            else if (result.value().outcome == loader::SaveOutcome::Written ||
                     result.value().outcome == loader::SaveOutcome::Unchanged)
                done.resolve();
            else
                done.reject(makeError(ErrorCode::ConfigConflict, "configuration was not saved"));
        });
        cordis::detail::TaskAccess::observe(receipt, [done](Task<void>::Snapshot result) {
            if (!result->hasValue())
                done.reject(result->error());
        });
        return Result<Task<void>>::success(done.task());
    }

private:
    /** @brief 从已接受目标构造新文档；编辑文本不做字符串拼接，避免身份/配置注入。 */
    Result<Task<ConfigurationActionReport>> mutate(const std::string& path, const std::string& id,
                                                   const std::string& plugin, const std::string& json,
                                                   std::uint64_t expected)
    {
        const auto fail = [](ErrorInfo error) {
            return Result<Task<ConfigurationActionReport>>::failure(std::move(error));
        };
        auto session = session_.lock();
        if (!session)
            return fail(makeError(ErrorCode::ContextClosing, "configuration session expired"));
        auto snapshot = session->snapshot();
        if (!snapshot.hasValue())
            return fail(snapshot.error());
        if (snapshot.value().coordinator.desiredRevision != expected || snapshot.value().closing)
            return fail(makeError(ErrorCode::ConfigConflict, "configuration revision changed; reload before applying"));
        if (path.empty() && !snapshot.value().coordinator.desired && snapshot.value().store.baseline &&
            snapshot.value().store.baseline->version.exists)
            return fail(makeError(ErrorCode::ConfigConflict,
                                  "磁盘中已有配置尚未应用，请先通过“配置文件”窗口检查并应用现有目标，再添加实例。"));
        if (json.size() > 1024 * 1024)
            return fail(makeError(ErrorCode::InvalidArgument, "configuration JSON exceeds input budget"));
        if (path.empty())
            return convert(session->addInstance(id, plugin, json, expected));
        auto source = snapshot.value().coordinator.desired
                          ? Result<loader::ParsedConfiguration>::success(snapshot.value().coordinator.desired->source())
                          : loader::parseConfiguration("formatVersion: 1\nplugins: []\n");
        if (!source.hasValue())
            return fail(source.error());
        auto parsed = loader::setEntryConfiguration(source.value(), path, json);
        if (!parsed.hasValue())
            return fail(parsed.error());
        auto draft = session->preview(parsed.value().normalizedYaml());
        if (!draft.hasValue())
            return fail(draft.error());
        if (draft.value().baseRevision() != expected)
            return fail(makeError(ErrorCode::ConfigConflict, "configuration changed during validation"));
        return convert(session->apply(draft.value()));
    }
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
