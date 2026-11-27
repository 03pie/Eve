/** @file replacement_commit_p.hpp
 * @brief 宿主整体提交的同步内部 SPI；不是用户或插件可调用的角色提升入口。
 */
#pragma once
#include <eve/module/replacement.hpp>
#include <eve/replacement_scope.hpp>
#include <eve/task.hpp>
#include <optional>

namespace eve::detail {
struct ReplacementInputBinding;
}

namespace eve::module::detail {
struct PreparedModuleCommitControl;
/** @brief 操作短期持有生产者；原预约只持弱槽，不在驻留代次累积订阅。 */
struct ReplacementSourceEvents {
    Promise<void> candidate, recovery;
};
struct ModuleInputSource {
    GenerationId generation;
    InstanceId instance;
    Epoch epoch;
    std::optional<RuntimeId> runtime;
    bool candidate, recovery;
};
/** @brief ModuleHost 检查已封存的绑定。普通服务返回空，Native 端点必须仍属于活跃的原提供者。 */
Result<std::optional<ModuleInputSource>> nativeInputSource(const eve::detail::ReplacementInputBinding& binding);
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
    static Result<std::shared_ptr<ReplacementSourceEvents>> watchSources(const ReplacementReservation&);
    static Result<void> checkSources(const ReplacementReservation&, bool recovery);
    /** @brief 停止前保护实际构造工厂，沿原预约持有到提交/恢复或失败保留。 */
    static Result<void> borrow(const ReplacementReservation& reservation, const ReplacementScope& scope,
                               const std::vector<Runtime>& candidate, const std::vector<Runtime>& recovery);
    /** @brief 完整计划封存后保护实际外部提供者及真实模块祖先，不从编号猜测来源。 */
    static Result<void> borrowInputs(const ReplacementReservation&, const ReplacementScope&,
                                     const std::vector<ModuleInputSource>&);
    static Result<void> sealSources(const ReplacementReservation&, const ReplacementScope&,
                                    const std::vector<ModuleFactorySource>& candidate,
                                    const std::vector<ModuleFactorySource>& recovery);
    static Result<PreparedModuleCommit> prepare(const ReplacementReservation& reservation,
                                                const ReplacementScope& scope);

private:
    static Result<void> borrowSources(const ReplacementReservation&, const ReplacementScope&,
                                      const std::vector<Runtime>& candidate, const std::vector<Runtime>& recovery,
                                      const std::vector<ModuleInputSource>&);
};
} // namespace eve::module::detail
