# Host replacement

外部低层原生服务的来源由宿主从已封存的真实 NativeServiceEndpoint/所有者取得，不能因它不属于 Core 拥有树就跳过。Core 只提供通用绑定视图，宿主同时转换托管来源，ModuleNative 核对自己的原身份账目；未托管来源没有 Runtime。此路径复用原代次读持有和故障中断，不新增全局来源表。

模块业务故障沿原预约的实际候选/恢复用途通知宿主，通知生产者只由操作短期持有，预约保存弱槽；不在驻留代次累积观察链。悬挂拥有者可被所需来源故障中断：候选独有来源失效可恢复不依赖它的旧树，恢复也需要的来源失效报告 RecoveryFailed 并保留实际预约。原代次错误、quarantine 和最终健康检查仍为原账目，不将外部故障资源冒称已清理。

`Cordis::Replacement` 是宿主操作层，依赖 ConfigReplacement/ModuleNative 和原生宿主适配 ModuleHost；目前在支持原生模块的 Windows 构建中启用。公开入口为 [replacement_operation.hpp](../../sdk/include/cordis/replacement_operation.hpp)。M2c5 的完整运行事务已完成实现、阶段审计和支持矩阵验证，见开发记录；M3–M5 仍继续。

宿主先装载并保留两代真实工厂，再以 `ReplacementRequest` 显式提供当前/候选代、Runtime 换代、Kernel/管理器/调度器，以及配置参与者的实际 Session、Driver、新目录、已校验目标和期望修订。`begin` 预检并排队执行，`settled` 返回纯值结果；保存仍由 Session 显式执行，替换不自动写盘。手动装配不需要虚构配置会话。

动态拥有单元使用宿主实现的 `IReplacementOwner`。`rebuild(ReplacementBuilder, ReplacementBuildPlan)` 只接收本次构建许可，不能通过它丢弃/释放 Scope 或取得恢复许可。候选和恢复使用不同发布组；准备、丢弃、失败或发布后 stage 拒绝，旧许可不能用于新阶段。许可为弱引用，不延长 Kernel、工厂或发布组寿命。构建计划中的 key 只用于对应，返回句柄须为真实成员并保留真实父子关系，任务完成后不可继续创建。报告区分真实提交、真实旧代恢复和保留资源，取消请求不撤销已发生的提交。

尚未发布的拥有者接口现要求 preflight 返回 ReplacementOwnerPreflight：仅描述动态成员及新增子节点的父级、配置、完整 realm、类型化依赖和输出。已知工厂由平台读取真实 Runtime，不能由拥有者覆盖。新增节点 key 只在本拥有单元的返回值中有效，平台重分配后通过实际 BuildPlan 传给 rebuild；拥有者必须读取新计划的 key，不能缓存预检局部编号。动态成员的实际配置、realm 和有效服务契约在发布前逐项核对，未声明子节点不能提交。恢复中映射到真实旧成员的动态描述，须在预检时保持其原配置、完整域映射和规范化有效契约；等价依赖合并允许，改变旧输入则冻结停止前拒绝。新增恢复子节点仍须明确声明并经全局求解及真实兑现核验。

ReplacementInput 追加实际实例的纯输出声明，临时子工厂仍不对外保留。宿主及 C++ 插件须一起重建；低层 ReplacementScope.stage 保持原协调者入口，C ABI 不变。这些是同工具链的计划值，不是可序列化的迁移状态。

Kernel 实际关闭完成会中断尚未完成的操作，即使拥有者的重建任务没有返回。宿主复用 Core 原清理结果；确认 quiescent 后结算为 Cancelled 并解除预约，清理失败按 Failed 保留资源处理，不在关闭中的 Kernel 恢复旧树。晚到拥有者结果不改变报告。

预检在实际 Session/Driver 配对后、Core 预约前取得会话持有，回调进入嵌套事件循环也不能提前关闭 Driver/Store。预检拒绝先撤销实际替换权限，再释放会话持有；若 Kernel 正在关闭而尚未静止，保留 Module 预约到真实清理结果。参与 Session 单独关闭会触发操作取消；若已经停止旧闭包，先完整恢复旧代（含其他会话及手动实例），再让关闭会话停止自己的实例并保存原目标。开始前关闭则直接释放预约。持有凭据只在 Loader 私有层使用，关闭期间只允许原会话的 Preserve 恢复，不能接受候选或新目标。恢复失败仍如实保留替换资源，但不让会话屏障永久等待已经终结的操作；Session 自己的关闭结果和平台替换资源报告须分别检查。

预检还组合 Driver 的逻辑路径/模块别名占用、Core 的规范工厂读意图和 Module 的计划代次读意图。重叠配置单元在冻结前拒绝，独立路径可共享未换代规范工厂并行提交；最新目标和目录只合并本次受影响部分。Module 从真实工厂来源扩展原预约的读者账目，保护尚未创建实例的候选/恢复工厂，拒绝其他操作换代、关闭或排空。Driver 与 Module 占用在 Core 释放通知前解除，失败则保留到实际资源释放。候选/恢复完整计划与 Core 实际外部可用绑定使用共同服务求解器，缺失、类型/版本/能力、未知 realm 和环在预检拒绝，冻结及构建前重验，根创建顺序使用求解结果。完整候选/恢复输出键的并集在冻结前封存到原 Core Scope/服务注册表权限簿；实际外来 Staged/Held/Retiring 绑定同样阻止预检。创建与真实服务入账共用封存检查，不能在阶段内扩张到计划外输出；低层未封存 Scope 的动态暂存语义保留，释放仍沿原路径。完整计划现同时封存实际外部 BindingId 的候选/恢复读意图，沿原 Scope/操作目录共享保护，不伪造实例 committed 租约。主动退役、提供者及祖先的停止/更新和另一 Core 替换拒绝；实例故障/Kernel 关闭沿私有原退役路径撤回并通知实际来源失效。候选独有来源故障可打断悬挂拥有者，再恢复不依赖它的旧树；实际身份在冻结、阶段许可及最终提交重验。Module 现从真实 Core 拥有视图核对托管提供者及模块祖先的实际实例/epoch/Runtime，匹配原来源账目并合入原代次读意图，阻止外部模块预约/close/drain。原生服务/effect/实例/模块主动撤回先尊重绑定读意图，正常拒绝不分配请求、不改关闭状态或隔离健康实例；解除后沿原路径完成。ModuleFactorySource 为纯代次/插件定位，不携带 RuntimeId 或执行权；拥有者复制预检默认来源或明确改变动态来源，空值表示内置。恢复成员保持旧来源，完整两阶段创建来源在原预约封存，创建前用真实 Context 和目录检查，不把读持有当作创建权；Prepared 逐成员核对真实来源，低层未封存协调保持动态行为。宿主及使用这些尚未发布结构的 C++ 代码须重建，C ABI 不变。本阶段故障与支持矩阵及整阶段审计已完成，范围不包括版本化状态迁移、白名单卸载和最终 UI/压力验收。

当前回归可在原 Release 目录构建 `cordis_module_reservation_tests`、`cordis_replacement_target_tests`，再执行 `ctest --test-dir build/ela-msvc-release -L "replacement_operation|replacement_target" --output-on-failure`。后端用真实 DLL，目标合并同时有无 Qt 检查。尚未接入工作台操作入口；阶段剩余范围和证据见 [M2c5](../../docs/p5-m2c5-development.md)与 [ADR-0048](../../docs/adr/0048-host-replacement-operation.md)。
