# W5 交付与验证

日期：2026-10-23。状态：首阶段工作单范围已完成。范围：可选依赖子实例、父代所有权屏障、实际租约与声明依赖的联合环诊断。

## 接口与所有权

`Context::inject(const Runtime&, string config = {}) -> Result<Instance>` 创建本实例本代拥有的子实例。Runtime 不必另外登记到 Kernel，描述校验沿用 Runtime/Instance::mount。子实例共享父注册表和当前 Context 的 realm 视图，配置由 inject 显式传入，默认空字符串。派生 Context 注入的子实例继承派生视图，不修改父视图。

子实例拥有独立 epoch、操作、资源作用域和依赖声明。缺服务只使该子实例 Pending；服务出现后自动激活，失效后清理并再次等待，父实例不因此重启。子激活失败也不自动结束父实例，调用方可通过子句柄观察失败。

```cpp
Runtime feature([](Context context) {
    auto store = context.consume<int>({"storage"});
    return store.hasValue() ? completedTask() : failedTask(store.error());
}, {ServiceRequirement::of<int>({"storage"})});
auto parent = kernel.registerPlugin("host", [feature](Context context) {
    auto child = context.inject(feature, "feature config");
    return child.hasValue() ? completedTask() : failedTask(child.error());
});
```

父代退役时先关闭业务入口、广播取消，并向全部子实例请求 stop；同时退役本代服务输出。必须同时等待所有子实例静止、输出消费者和调用退出，才开始父代自己的 effect 清理。父更新与最终 stop 使用同一条路径；更新完成后，新激活重新创建子实例，旧子实例不迁移。父清理中的旧绑定访问仍由 committed 租约保留。

子清理失败时，父保持 Unloading，保留自身资源和子实例记录，当前操作 Failed；stop 返回包含子错误的非静止报告，后到 stop 也能观察。update/retry 无法绕过失败屏障。不合作子任务或 scheduler 投递阻断会保持等待，不伪报静止。子报告中的错误也汇入父最终清理报告。

InstanceSnapshot.children 为父代持有的直接子实例记录数，包含本代已停止但尚未由父代清理移除的记录；不表示所有子实例都在 Active。只有父代确认静止后才清空。Kernel 的直接实例表仍仅记录通过 Kernel::mount 挂载的实例，子树由父实例拥有。

注入要求开放的 owner-thread scheduler 和当前有效业务 Context；错误线程、旧 epoch、清理/退役/停止期间拒绝登记。无效 Runtime 或外域描述在登记前拒绝。注入的激活始终排队，父激活中重入 stop 后，尚未启动的子实例不会执行插件。

## 联合图与诊断

图使用消费者→提供者、子→父两个方向，统一表示清理等待关系的反向。提交新 committed 租约前检查提供者能否返回消费者；子依赖父输出是允许的，父消费子/后代输出会被拒绝。不会通过等待顺序尝试解决已经成环的所有权。

挂载时按 realm 解析登记强依赖与必供输出。激活前使用当前绑定的实际提供者；没有绑定时，仅在该精确键具有唯一必供声明者时建立候选边，预检尚未激活的声明环。新增和终结实例均排队通知 Pending 实例重新检查。普通缺依赖仍 Pending；发现环则 Failed、epoch 不递增、activate 不执行。移除环成员后，已 Failed 实例需显式 retry/update。

DependencyCycle 的 message 和 causes 按路径顺序列出每一条边，包括实例编号、服务名称、主版本、realm 或 parent 边。自环为一条边；多节点环包含返回起点的完整路径。实例确认 Disposed 才撤销其声明和所有权记录。

此图局限于同一托管注册表中的实例。多个未发布候选不猜测提供者，非必供输出不作为缺服务时的候选边；它们实际发布并提交后仍受实际租约环检查。宿主任意 Task 等待、跨独立注册表的手动组合和用户回调内部等待不属于可证明图，不能声称检测所有死锁。当前扫描实现优先保证语义，未做大规模图性能优化。

## 验证

新增十项独立 CTest，沿用 gate、手动 scheduler 和线程 join，不依赖真实 sleep：

| CTest | 行为 |
|---|---|
| w5.children_optional | 子 Pending、恢复、失效再等待不重启父；父停止后不恢复 |
| w5.children_ordered | 父拥有 A/B/C，先广播停止，依赖 C→B→A 后才清理父；旧绑定清理访问 |
| w5.children_cycle | 子依赖父可运行；父消费后代输出拒绝，错误包含完整联合路径 |
| w5.children_update_realms | 派生域继承、更新等待子清理、新代重建子实例、旧 Context 失效 |
| w5.children_failure | 子清理失败保留父资源，错误及非静止报告传播，更新不能绕过 |
| w5.children_guards | 移动后 Runtime、错误线程及停止期间不登记子实例 |
| w5.children_reentrant_stop | 父 setup 内注入并重入 stop；子不启动，父等待真实 setup 完成 |
| w5.children_declared_cycle | 三个尚未激活的声明形成环，三个实例均报告逐边路径且无忙循环 |
| w5.children_pending_failure | Pending 后出现类型错误，新操作 Failed，旧操作仍可查询历史 Pending，修订不变 |
| w5.children_declaration_boundaries | 自环、realm 区分、歧义候选不臆测连边、移除成员后 retry |

2026-10-23 初验四配置各 95/95。2026-10-24 提交前审查修复 Pending 后失败的操作结果不一致问题，新增回归及最新四配置结果见 [开发进度](progress.md) 和 [审核记录](code-review.md)；复核日志为 `build/review-w5-core-<compiler>-<mode>.log`。工具链为 GCC 13.1、MSVC 19.44 x64 Debug/Release。service.hpp、instance.hpp、kernel.hpp 均通过 GCC C++17 独立包含与警告即错误检查。未运行 ASan/TSan、Qt、DLL 或大规模压力验证。

W5 覆盖第一阶段工作单的 inject 子实例、所有权/依赖联合诊断及 A07 父子清理要求。下一工作包 W6 EventBus；任意任务死锁检测、资源强制回收和历史账目裁剪不在本轮范围。
