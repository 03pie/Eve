/** @file session_commit_p.hpp
 * @brief 泛型会话的宿主提交槽，不依赖具体 Kernel 驱动或模块代次。
 */
#pragma once
#include "target_commit_p.hpp"
#include <cordis/loader/session.hpp>
namespace cordis::loader::detail {
struct SessionCommitControl;
struct SessionHoldControl;
/** @brief 在途宿主工作阻止会话后端提前关闭；仅同会话凭据允许 Preserve 恢复。 */
class SessionReplacementHold {
public:
    SessionReplacementHold(const SessionReplacementHold&) = delete;
    SessionReplacementHold& operator=(const SessionReplacementHold&) = delete;
    SessionReplacementHold(SessionReplacementHold&&) noexcept = default;
    SessionReplacementHold& operator=(SessionReplacementHold&&) = delete;
    ~SessionReplacementHold();
    Task<void> closing() const;

private:
    friend struct SessionCommitAccess;
    explicit SessionReplacementHold(std::shared_ptr<SessionHoldControl> control) : control_(std::move(control)) {}
    std::shared_ptr<SessionHoldControl> control_;
};
class PreparedSessionCommit {
public:
    PreparedSessionCommit(const PreparedSessionCommit&) = delete;
    PreparedSessionCommit& operator=(const PreparedSessionCommit&) = delete;
    PreparedSessionCommit(PreparedSessionCommit&&) noexcept = default;
    PreparedSessionCommit& operator=(PreparedSessionCommit&&) = delete;
    bool ready() const noexcept;
    void publish() noexcept;
    Result<void> finish(Result<void> notifications);
    std::optional<AcceptedTarget> accepted() const;

private:
    friend struct SessionCommitAccess;
    explicit PreparedSessionCommit(std::shared_ptr<SessionCommitControl> control) : control_(std::move(control)) {}
    std::shared_ptr<SessionCommitControl> control_;
};
struct SessionCommitAccess {
    struct View {
        std::uint64_t revision;
        std::shared_ptr<const ValidatedCandidate> desired;
        Task<PlanReport> settled;
        bool idle;
    };
    static Result<SessionReplacementHold> hold(ConfigurationSession&, const IReconcileDriver&, const Task<void>&);
    static Result<View> inspect(ConfigurationSession&, const IReconcileDriver&,
                                const SessionReplacementHold* = nullptr);
    static Result<PreparedSessionCommit> prepare(ConfigurationSession&, const IReconcileDriver&,
                                                 std::shared_ptr<const ValidatedCandidate>, std::uint64_t expected,
                                                 TargetCommitMode, const std::vector<EntryOperation>& applied,
                                                 const SessionReplacementHold* = nullptr);
};
} // namespace cordis::loader::detail
