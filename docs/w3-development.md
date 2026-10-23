# W3 开发记录

日期：2026-10-23。状态：W3 无服务内核范围已完成；包含实例生命周期、配置更新、epoch 收敛、失败重试、Kernel、操作查询与独立清理上下文。下一工作包 W4 服务依赖。

## 已实现

`sdk/include/cordis/instance.hpp` 与 `src/core/instance.cpp` 提供源码 SDK：

- Runtime 保存共享的内置插件定义，Instance::mount 创建独立 EffectScope，并排队激活。
- Context 只保存弱实例引用；owner 线程可登记业务 effect；停止后拒绝新资源，实例销毁后返回 StaleContext。
- Context::onCleanup 返回独立 EffectToken，登记 `Task<void>(CleanupContext)` 清理函数。CleanupContext 只提供旧代 instance/runtime/epoch/revision/config 快照，不可转换为 Context，也没有 effect 或 onCleanup 入口。每次注册属于独立清理组，组间不保证全局逆序；需要组内逆序时继续使用 EffectSetupContext::addCleanup。
- 清理访问窗口自回调进入起，保持到返回的源 Task 真实完成；同步成功、失败、异常及工作线程完成均撤销访问。保存的上下文随后返回 StaleContext；有效期内查询要求 owner 线程。它只弱持有访问控制块，不延长实例资源生命周期。清理期间捕获的旧业务 Context 同样不能向该实例登记新资源。
- Instance::ready 观察首次有效激活，结果一旦完成不重置；后续配置操作使用 OperationHandle::target。stop 幂等终结实例，等待激活和全部业务 effect 的 setup/cleanup 屏障。停止期间迟到成功不会发布 Active。
- 激活异常通过 Effect 屏障转换为错误，关闭当前代作用域以回滚业务资源，静止后停留 Failed，允许显式 retry；不自动重复失败激活。
- update(config, expectedRevision) 接受字符串配置（尚无 schema），校验修订后更新目标。未完成操作以 Superseded 终结；旧代资源继续由清理屏障保留，只有静止后才启动最新目标。失败清理阻止换代。
- 每次实际激活递增 epoch；Context 捕获配置副本和 epoch，旧代 Context 不得向新代登记资源。update 递增配置 revision；retry 保留配置和 revision，但分配新的 OperationId。修订冲突、已终结、错误线程不改变目标。
- OperationHandle::target 为当前操作的 Task<StableReport>，提供 Active/Failed/Superseded/Disposed、修订及代次。Failed 结果不证明资源静止，仍须查看资源快照；stop 的 quiescent 才是资源释放结论。已完成的操作结果不随后续更新改变。
- stop 报告确认 quiescent 后才标 Disposed。清理失败保留 Unloading 和所有权 pin；调度投递失败时保留 pending，并暴露 deliveryBlocked。
- 同一 Runtime 可创建多个独立实例，句柄析构不执行异步 stop；正常退出必须显式停止后再关闭 scheduler。
- `kernel.hpp` / `kernel.cpp`：Kernel 在 owner 线程登记具名 Runtime，拒绝重名和跨内核挂载，强持有实例；close 先广播停止再汇总报告。snapshot 回收已 Disposed 的实例，阻塞实例继续保留。关闭回调投递失败时 close 保持 pending，snapshot.deliveryBlocked 为 true。
- RuntimeId、InstanceId、OperationId 在进程内按类型单调分配、不复用；到达上限抛 overflow_error，不发生绕回。Runtime 副本共享标识，多个实例独立分配标识。不是跨 DLL ABI 或持久化编号。
- operation() 获取当前操作（包含初始 mount）；awaitTarget 根据本实例账目查找任务，拒绝跨实例/未知操作，不能通过替换句柄 target 改变查询结果。
- awaitActive(epoch) 只查询已经启动的代次；未启动/未来代次返回 InvalidArgument。已启动未激活的代次在退役时失败为 OperationCancelled，激活失败保留原错误。历史成功结果保持成功，只表示该代曾进入 Active，不授予当前业务能力。历史操作及代次结果保留至实例控制块释放，尚无有界历史裁剪。

激活回调本身与各个业务 effect 是不同的屏障；插件若要求资源初始化成功后才 Active，须在激活返回的 Task 中等待这些资源的 ready。内部不会将尚未完成的业务 effect 自动解释为激活成功。

## 实测

GCC 13.1 和 MSVC 19.44 x64 的 Debug/Release 四配置均完成配置、构建与 CTest，各 64/64 通过，含 W1/W2 回归。MSVC 使用 W2 记录中的进程内 TEMP/TMP 设置及审核记录中的依赖前缀覆盖。最新日志为 `build/cleanup-<preset>.log`。公共头 instance.hpp、kernel.hpp 已分别通过 GCC C++17 独立包含检查；新增 CleanupContext 后 instance.hpp 再次检查通过。

`w3.single_activation` 保留原测试名，现包含十组真实行为：启动前停止；启动中重入停止与晚到资源；多实例隔离与激活异常回滚；非 owner 调用及投递丢弃；连续更新只启动最新配置、等待两类屏障和旧 Context 失活；失败后保配置修订重试；清理失败阻止换代；内核归属与操作/代次查询；内核所有权与统一关闭屏障；移动后 Runtime 与调度器关闭后的命令拒绝。共用一个 CTest 入口，所以检查数仍为 61。测试使用 gate 和线程 join，不依赖真实 sleep；尚未运行消毒器。提交前审核修复及 MSVC 依赖检测复现见 [审核记录](code-review.md)。

另新增三个独立 CTest 入口：`w3.cleanup_epoch_window` 验证旧代快照、线程边界、异步完成即失效和换代屏障；`w3.cleanup_failures` 验证异常/失败撤销能力且继续其他清理；`w3.cleanup_token_boundary` 验证独立 token 异步清理期间的业务注册拒绝和重复关闭。包含编译期断言确认 CleanupContext 不提供 effect 且不能转换成 Context，因此总计增至 64 项。测试使用 gate，不依赖真实 sleep。

## 剩余范围

realm、committed binding、CleanupLease、父子所有权及服务失效恢复属于 W4/W5，尚未实现；当前 CleanupContext 仅提供受限旧代信息，不能用它访问尚不存在的服务。A05 的停止屏障、A08 的连续更新以及 A09 的多实例资源独立部分已有覆盖；A09 的 realm 服务隔离待 W4。服务与事件仍未实现。stop 报告覆盖当前代，历史已完成操作由各自 target 保存，不提供跨代错误总账；历史结果尚未裁剪。清理失败/投递阻断保守保留资源，不支持强制回收；未运行 ASan/TSan 或 Qt/DLL 验证。
