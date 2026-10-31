/** @file replacement_scope.hpp
 * @brief Core 闭包的唯一排他所有者；不表示模块角色或业务升级已切换。
 */
#pragma once
#include <cordis/effect.hpp>
#include <cordis/replacement.hpp>
#include <cordis/service.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace cordis {
namespace detail {
struct ReplacementScopeControl;
}
enum class ReplacementScopeState { Reserved, Frozen, Stopping, Stopped, Failed, Released };
struct ReplacementScopeSnapshot {
    OperationId operation;
    ReplacementScopeState state;
    /** @brief 取得占用时的版本快照；不回写为最新实例状态。 */
    ReplacementClosure closure;
    std::vector<ServiceKey> reservedOutputs;
    bool abandoned;
    bool deliveryBlocked;
    bool cancelled;
    std::optional<CleanupReport> cleanup;
    std::optional<ErrorInfo> failure;
};
/** @brief owner 线程使用；停止开始后丢弃不会自动解除占用或伪报恢复完成。 */
class ReplacementScope {
public:
    ReplacementScope() noexcept = default;
    ReplacementScope(const ReplacementScope&) = delete;
    ReplacementScope& operator=(const ReplacementScope&) = delete;
    ReplacementScope(ReplacementScope&&) noexcept = default;
    ReplacementScope& operator=(ReplacementScope&& other) noexcept;
    ~ReplacementScope();
    Result<ReplacementScopeSnapshot> snapshot() const;
    /** @brief 重验已占用闭包并关闭旧业务；清理能力继续有效。 */
    Result<void> freeze() const;
    /** @brief 冻结后逐项等待真实 stop；观察共享回执，不创建/切换任何工厂。 */
    Task<CleanupReport> stop() const;
    /** @brief 停止前可撤销；停止后须全部静止，失败保留占用。 */
    Result<void> release() const;

private:
    friend class Kernel;
    friend struct detail::KernelControl;
    explicit ReplacementScope(std::shared_ptr<detail::ReplacementScopeControl> control) : control_(std::move(control))
    {
    }
    std::shared_ptr<detail::ReplacementScopeControl> control_;
};
} // namespace cordis
