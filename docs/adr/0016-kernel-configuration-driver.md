# ADR-0016：顶层内置配置的真实 Kernel 驱动

日期：2026-10-25。范围：Q4c2a，本文保留该阶段固定依赖的交付边界；后续动态 inject 已按 [ADR-0017](0017-instance-inject-contracts.md)、显式配置子树已按 [ADR-0019](0019-configuration-subtrees.md) 扩展。用户已确认桌面人工验收完成，登记于 [验收单](../../tests/acceptance/p3-ui.md)，解除接入真实运行驱动的前置条件。管理入口/保存装配继续待办。

## 模块与所有权

新增 `Cordis::ConfigKernel`，依赖 `Cordis::ConfigCoordinator`，公开入口为 `cordis/loader/kernel_driver.hpp`。Core 不依赖 Loader、Qt 或 Ela，驱动不包含 Qt 类型。宿主先在同一 Kernel 注册 Runtime，再将插件标识和不可变 Runtime 句柄交给 KernelReconcileDriver；构造只验证归属，不执行插件。Runtime 新增只读 requirements/outputs，ServiceRegistry 新增 owner 线程只读 ownsRealm，适配层不借助试挂载来检查契约或隔离域。

驱动记录自己挂载的路径、实际接受的输入和 Instance，配置修订与实例修订保持独立。宿主不能绕过驱动直接更新这些实例的配置；插件自己的动态子实例继续由 Core 的父代资源屏障管理，不是本阶段的显式配置 children。共享 Kernel 中其他宿主实例不属于驱动，driver.close 不调用 Kernel.close。

宿主为 realm 名称提供本 Kernel 注册表创建的 RealmId；别名和实际解析后的依赖/输出域均检查归属。未覆盖的服务键保留 Runtime 原有域。realm 或插件变化采用 stop 后重新 mount，不把两个不同身份当作原地配置更新。

## 接受前的能力检查

支持顶层 builtin 条目、固定 Runtime 契约、配置更新、禁用/重新启用及显式 realm 别名。显式 children（包括被禁用的子项）、其他模块、未知插件/别名在 preflight 拒绝，期望修订不推进。配置校验得到的 effectiveInject 必须与 Runtime 归一化后的固定依赖精确相同；增加或强化动态 inject 本阶段拒绝，不能仅在 YAML 中接受后无声忽略。

按真实 C++ 类型、服务名、主版本和解析后的精确 realm 建立提供者索引；配置内的提供者还须满足最低次版本及能力要求。检查重复输出键和依赖环，缺少配置内提供者时允许外部宿主服务或 Pending。可选输出也参与索引及冲突检查，因此本阶段采取保守规则，不承诺依据运行期是否实际发布来解除声明冲突。

两个仍启用路径之间转移输出键在 preflight 拒绝：当前协调协议每个路径只能有一个动作，不能原子交换两个现存实例的输出所有权。调用方须先接受并稳定执行移除旧提供者的目标，再接受新归属；realm 变化后仍由同一路径拥有输出则可以 Recreate。外部宿主服务冲突由真实 Kernel 挂载/激活结果报告，接受目标不保证运行必然成功。

## 规划与执行

每个目标从真实实例记录规划：旧依赖图中消费者先删除/禁用，新依赖图中提供者先添加/更新。路径按确定性拓扑次序排列，不直接执行 PlanPreview。没有实例或已 Disposed 时 Add；身份变化 Recreate；配置不同或可重试失败时 Restart；其余 NoOp。NoOp 不重复激活，缺失实例不能用 NoOp 或 Restart 隐式创建。

Add 等待真实 Instance 稳定操作；配置更新使用 revision CAS，失败同配置重试使用 retry。Recreate 等旧实例 stop 确认 quiescent 后才创建新实例。删除清理失败保留旧记录，并以 unsafe 阻断继续执行；失败回滚已清空资源时可以接受新目标重试。缺依赖返回 Pending，后续服务恢复由 Core 驱动同一实例，不改写历史 Pending 报告。

A 在途时 B 被 C 替代，等待 A 当前动作稳定，随后从已发生的真实部分状态为 C 再规划。已接受目标、动作结果、真实运行和历史保存状态分开；本驱动没有文件保存行为。

## 退出与投递故障

close 先向全部自有实例广播 stop，再等待全部清理及在途动作回执。同步访问深度防止清理回调重入关闭时过早释放实例表；关闭期间不会在旧实例停止后继续挂载替代实例。正常顺序是 coordinator.close 完成、Kernel.close 完成，再关闭 scheduler。

驱动 continuation 的投递失败可来自 worker，故障门禁只操作 atomic/mutex 和线程安全 Promise，不从 worker 修改 owner 实例表。失败回执会终结登记的动作及关闭任务，资源没有静止证明时保守自保留。若 scheduler 先关闭且 Core 内部目标尚未完成，驱动没有 scheduler 关闭订阅，不能保证立刻发现；宿主显式调用 driver.close 会发布 SchedulerClosed 并终结在途动作，仍不伪造资源已释放。

提交前审查增补：scheduler 仍开放也可能发生单次内部投递拒绝。驱动检查实例和 EffectScope 的 deliveryBlocked；初次挂载已经阻断时立即终结动作，后续阻断通过 owner 业务/快照/关闭入口识别。开放调度器下的关闭即使已记录故障，也先向自有实例广播停止；失败不宣称资源静止，健康的共享 Kernel 实例边界仍保留。资源内部阻断的快照返回错误，不用正常 Pending 或空列表掩盖故障。

## 验证与剩余范围

`p3.kernel_driver_*` 的 13 项场景使用真实 Kernel/Instance/ServiceRegistry，只将时间推进替换为 FakeScheduler，覆盖配置生命周期、依赖清理窗口、Pending 恢复、最新目标、失败重试、清理残留、异步退出、realm 替换、能力拒绝/环/冲突、输出归属转移、共享 Kernel 边界、线程/动作门禁及投递失败。`p3.reconcile_demo` 演示 YAML/schema → CAS 接受 → 提供者启动 → 消费者恢复 → 完整退出。

构建与 ASan 实测结果见 [开发记录](../p3-development.md#q4c2a-顶层内置配置驱动)。本轮不等同于全部 L01–L04/S05：显式配置子树、动态 inject Runtime 变体、管理页编辑/保存、Store 和驱动的全应用装配、watcher/后台 I/O 仍待实现。
