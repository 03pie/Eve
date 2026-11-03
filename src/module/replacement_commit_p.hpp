/** @file replacement_commit_p.hpp
 * @brief 宿主整体提交的同步内部 SPI；不是用户或插件可调用的角色提升入口。
 */
#pragma once
#include <cordis/module/replacement.hpp>
#include <cordis/replacement_scope.hpp>

namespace cordis::module::detail {
struct PreparedModuleCommitControl;
/** @brief owner 线程的同步准备窗口，持目录/代次/错误信号锁。
 * scope 必须由宿主事务持有到窗口结束；不得跨异步边界或在窗口内调用外部代码。
 * publish 后仍持锁，宿主可继续执行已准备的配置元数据无异常交换，再退出窗口。
 */
class PreparedModuleCommit {
public:
    PreparedModuleCommit(const PreparedModuleCommit&) = delete;
    PreparedModuleCommit& operator=(const PreparedModuleCommit&) = delete;
    PreparedModuleCommit(PreparedModuleCommit&&) noexcept;
    PreparedModuleCommit& operator=(PreparedModuleCommit&&) = delete;
    ~PreparedModuleCommit();
    /** @brief 无通知提交真实 Core 准备及角色；成功路径不分配、不调用插件/调度器。 */
    Result<void> publish();
    bool committed() const noexcept;

private:
    friend struct ReplacementAccess;
    explicit PreparedModuleCommit(std::unique_ptr<PreparedModuleCommitControl> control);
    std::unique_ptr<PreparedModuleCommitControl> control_;
};
struct ReplacementAccess {
    static Result<PreparedModuleCommit> prepare(const ReplacementReservation& reservation,
                                                const ReplacementScope& scope);
};
} // namespace cordis::module::detail
