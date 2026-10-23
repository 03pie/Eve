# W3 开发记录

日期：2026-10-23。状态：进行中；已完成实例生命周期、配置更新、epoch 收敛、失败重试，以及 Kernel 注册、标识与查询接口。独立 CleanupContext 仍待实现。

## 已实现

`sdk/include/cordis/instance.hpp` 与 `src/core/instance.cpp` 提供源码 SDK：

- Runtime 保存共享的内置插件定义，Instance::mount 创建独立 EffectScope，并排队激活。
- Context 只保存弱实例引用；owner 线程可登记业务 effect；停止后拒绝新资源，实例销毁后返回 StaleContext。
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

GCC 13.1 和 MSVC 19.44 x64 的 Debug/Release 四配置均完成配置、构建与 CTest，各 61/61 通过，含 W1/W2 回归。MSVC 使用 W2 记录中的进程内 TEMP/TMP 设置。日志为 `build/w3-<preset>.log`。公共头 instance.hpp、kernel.hpp 分别以单独翻译单元通过 GCC C++17 警告即错误语法检查。

`w3.single_activation` 保留原测试名，现包含十组真实行为：启动前停止；启动中重入停止与晚到资源；多实例隔离与激活异常回滚；非 owner 调用及投递丢弃；连续更新只启动最新配置、等待两类屏障和旧 Context 失活；失败后保配置修订重试；清理失败阻止换代；内核归属与操作/代次查询；内核所有权与统一关闭屏障；移动后 Runtime 与调度器关闭后的命令拒绝。共用一个 CTest 入口，所以检查数仍为 61。测试使用 gate 和线程 join，不依赖真实 sleep；尚未运行消毒器。提交前审核修复及 MSVC 依赖检测复现见 [审核记录](code-review.md)。

## 剩余范围

独立 CleanupContext、realm、父子所有权和服务失效恢复尚未实现；清理回调目前仅使用已捕获资源，不能调用 Context 创建新业务。A05 的停止屏障及 A08 的连续更新已有覆盖；A09 尚无 realm 隔离。服务与事件仍未实现。stop 报告覆盖当前代，历史已完成操作由各自 target 保存，不提供跨代错误总账。下一步补齐清理能力边界后再推进 W4 服务依赖，W3 暂不标完成。
