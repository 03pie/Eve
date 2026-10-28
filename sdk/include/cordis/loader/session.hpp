/** @file session.hpp
 * @brief 将候选预览、目标接受和保存连接为宿主会话，不向普通插件提供运行控制。
 */
#pragma once
#include <cordis/loader/coordinator.hpp>
#include <cordis/loader/store.hpp>
#include <stdexcept>
#include <utility>

namespace cordis::loader {
namespace detail {
struct SessionControl;
}
/** @brief 不可伪造的会话候选；基于固定期望修订，编辑变化必须重新预览。 */
class ConfigurationDraft {
public:
    /** @brief 复制同一会话的完整候选。 */
    ConfigurationDraft(const ConfigurationDraft&) = default;
    /** @brief 转移候选，移动后原对象不可应用。 */
    ConfigurationDraft(ConfigurationDraft&&) noexcept = default;
    /** @brief 禁止部分赋值使候选和基准修订不一致。 */
    ConfigurationDraft& operator=(const ConfigurationDraft&) = delete;
    /** @brief 修改内容必须重新预览。 */
    ConfigurationDraft& operator=(ConfigurationDraft&&) = delete;
    /** @brief 读取已验证目标，不包含插件回调或运行能力。 */
    const ValidatedCandidate& candidate() const
    {
        if (!candidate_)
            throw std::logic_error("configuration draft was moved from");
        return *candidate_;
    }
    /** @brief 目标间差异仅用于审阅，不当作驱动执行计划。 */
    const PlanPreview& changes() const { return changes_; }
    /** @brief 接受时再次检查此修订。 */
    std::uint64_t baseRevision() const { return changes_.baseDesiredRevision; }

private:
    friend struct detail::SessionControl;
    friend class ConfigurationSession;
    /** @brief 仅本会话的校验流程可签发候选。 */
    ConfigurationDraft(std::weak_ptr<detail::SessionControl> owner, std::shared_ptr<const ValidatedCandidate> candidate,
                       PlanPreview changes)
        : owner_(std::move(owner)), candidate_(std::move(candidate)), changes_(std::move(changes))
    {
    }
    std::weak_ptr<detail::SessionControl> owner_;
    std::shared_ptr<const ValidatedCandidate> candidate_;
    PlanPreview changes_;
};
/** @brief 期望、真实运行、历史执行和保存状态分别保留；关闭不伪装成运行成功。 */
struct SessionSnapshot {
    CoordinatorSnapshot coordinator;
    StoreSnapshot store;
    std::optional<PlanReport> latestPlan;
    std::optional<ErrorInfo> executionError;
    bool closing = false;
    bool closed = false;
    bool deliveryBlocked = false;
};
/** @brief 宿主独占协调/保存入口；driver 交付后不能再从外部直接 apply/close。 */
class ConfigurationSession {
public:
    /** @brief 构造不接受目标、不读写文件；writer 和原始基线由宿主提供。 */
    ConfigurationSession(std::shared_ptr<IScheduler> scheduler, std::shared_ptr<IReconcileDriver> driver,
                         std::vector<PluginDefinition> definitions, std::shared_ptr<IConfigurationWriter> writer,
                         ConfigurationFileSnapshot baseline, YamlLimits limits = {}, StoreLimits storeLimits = {});
    /** @brief 释放 facade 不替代显式关闭。 */
    ~ConfigurationSession();
    /** @brief 唯一 facade 不复制协调与保存所有权。 */
    ConfigurationSession(const ConfigurationSession&) = delete;
    /** @brief 不覆盖尚未关闭的会话。 */
    ConfigurationSession& operator=(const ConfigurationSession&) = delete;
    /** @brief 解析、schema、驱动能力检查和目标差异；不挂载、不接受、不保存。 */
    Result<ConfigurationDraft> preview(const std::string& text) const;
    /** @brief 本会话候选按 CAS 接受，成功后立即跟踪至 Store；运行结果仍异步。 */
    Result<AcceptedTarget> apply(const ConfigurationDraft& draft);
    /** @brief 基于当前目标修改路径的本地启用标志，仍走完整校验和 CAS。 */
    Result<AcceptedTarget> setEnabled(const std::string& path, bool enabled, std::uint64_t expectedRevision);
    /** @brief 排队保存指定当前修订，不等待或改写运行结果。 */
    Result<Task<SaveReport>> save(std::uint64_t expectedRevision);
    /** @brief 用户明确保留目标、采用已捕获冲突文件作为新基线；之后仍需显式保存。 */
    Result<void> rebaseExternal(std::uint64_t expectedRevision);
    /** @brief owner 值快照允许在关闭后读取。 */
    Result<SessionSnapshot> snapshot() const;
    /** @brief owner 读取已注册定义的标识，不返回 schema 回调或运行句柄。 */
    Result<std::vector<std::string>> registeredPlugins() const;
    /** @brief 宿主装配只读检查调度器身份，不转移会话能力。 */
    bool usesScheduler(const std::shared_ptr<IScheduler>& scheduler) const noexcept;
    /** @brief 同步关门，排空真实驱动并刷新已接受目标；失败不宣称静止或保存成功。 */
    Task<void> close();

private:
    std::shared_ptr<detail::SessionControl> control_;
};
} // namespace cordis::loader
