# ADR-0018：宿主跨代观察与类型化子实例桥

日期：2026-10-25。范围：Q4c2c1，为显式配置子树提供 Core 前置能力，承接 [实例级依赖](0017-instance-inject-contracts.md)。

## 需要解决的问题

配置子实例归属父实例的某个激活代次。父配置更新、依赖退役或显式重试后，旧子树必须先完整清理，再由宿主在新父代中重新挂载。已有 awaitActive(epoch) 只查询已经登记的历史报告，不授予业务能力；ready() 只保留首次结果。宿主不能保存插件第一次激活传出的 Context 并跨代复用，也不应通过重新注册 Runtime 包装激活回调来观察父状态。

Core 因此提供一次性的成功激活观察；Loader 负责反复观察、维护配置路径与运行实例的对应关系及协调动作。Core 不认识 YAML、EntryPath、目标版本或保存状态。

## 成功代次观察

`Instance::whenActiveAfter(Epoch afterEpoch = 0) -> Task<Context>` 只允许 owner 线程调用：

- 当前实例仍具有业务能力且 epoch 大于阈值时，返回已成功的任务；否则等待下一次满足条件的成功激活。
- 阈值不得大于当前已启动代次。默认零允许在首次 Pending/Loading 前登记；不预先猜测未来 epoch。
- 不回放已经退役的成功代次。Pending 或激活失败不会制造上下文；失败后可继续等待显式 retry/update 或依赖恢复。
- stop 立即以 OperationCancelled 终结尚未完成的观察，不等待插件激活任务或清理屏障。stop 后新查询报 InstanceDisposed；已移动来源报 InvalidArgument；错误线程报 WrongThread。
- 调度器关闭或已有投递阻断时拒绝登记。登记后发生投递故障仍遵守现有 Core 保守保留协议，宿主必须同时检查实例/作用域的 deliveryBlocked，不能仅等待成功上下文推断进度。

这是下一次成功的观察任务，不是激活失败报告或清理完成证明。当前操作的失败、Pending 和稳定结果继续通过 operation().target/snapshot 观察；退出继续检查 stop/Kernel.close 的 quiescent。

Context 只保存控制块的弱引用和该代配置/realm 快照，不因保存观察结果延长实例生命。成功结果保持历史值，但每次业务调用重新检查 owner、epoch、停止及投递门禁。Task.thenOn 继续排队，收到通知时父代可能已经退役；宿主必须处理 Context.identity()/inject 的失效结果并重新读取当前状态。

激活成功时先复制当时的观察登记和上下文快照，再发布 ready/active/target 及上下文通知。通知只遍历这份观察快照，重入更新后新登记的观察不回放旧代。原登记保留到通知结束，重入 stop 仍能立即取消尚未完成的观察；已完成的 Promise 按一次完成协议保持原结果，最后从登记表清除。没有观察者的普通激活不额外复制配置和域映射。

## 子实例的附加依赖

`Context::inject(runtime, config = {}, additionalDependencies = {})` 沿用实例级契约联合规则：固定与附加要求按解析后的完整 ServiceKey 合并，类型冲突或非法描述在建立子账目前拒绝。子实例继承调用 Context 的 realm 视图及服务注册表；未满足依赖时独立 Pending，不能从其他 realm 回退获取服务。

子仍属于创建它的父代，参与父子与服务的联合环检查。父退役先停止整棵旧子树，等待子及消费者静止后清理父资源，完成后才允许父新代激活。宿主在新代 Context 中创建新身份的子实例，不能借用历史 Context 或改变已有实例的不可变依赖契约。

## 验证及边界

六项真实 Core 测试覆盖精确域附加依赖 Pending/恢复、旧子异步清理阻挡父重启、父先后配置快照、失败后 retry、owner/未来阈值/移动来源门禁、停止中即时取消、弱结果不保活实例，以及通知投递重入 stop/update。重入回归在修复前 0/2、修复后 2/2；真实 FIFO 不提前执行已投递任务。cordis_child_epoch_demo 演示更新父配置、检查旧子终结、在新代创建子实例及完整关闭。

本文保留 Q4c2c1 当时的范围：该阶段不解除 KernelReconcileDriver 对显式 children 的 preflight 拒绝。后续自动配置子树、失败协调及路径映射已按 [ADR-0019](0019-configuration-subtrees.md) 扩展，管理页保存装配仍待实施。不据此前置能力宣告全部 L01–L04/S05 或跨 ABI 验收完成。实测工具链及日志见 [开发记录](../p3-development.md#q4c2c1-父代上下文与子实例桥)。
