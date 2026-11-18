# ADR-0053：服务可用性按键通知原激活作业

实施前设计日期：2026-11-18。2026-11-19完成实现、代码审查与五配置专项验证，随收口提交归档。源码基线 a9f4230（M5-d0 已审查提交同步）。观测口径沿用 [ADR-0052](0052-dependency-evaluation-observation.md)，实施出口见 [M5-d 工作单](../p5-m5d-development.md)。

## 问题与边界

原说明书/指南要求 key→interested instances 索引。当前每次 publish/retire 都广播实例观察作业；enqueue 能合并，但不相关服务仍会令 Pending 重新评估，Active 也收到空唤醒。d0 已给出真实评估批次基线，不能用其统计接口代替索引本身。

不能把所有通知机械改成直接 key：先 stage 未发布的外部 x，再挂载 A(dep x,out a)、B(dep a,out b)、C(dep b,out x)，外部 Staged 绑定遮住声明候选，三者可均 Pending。release 外部 x 后才显露声明环；只通知 x 的直接消费者会漏掉 B/C。声明、实际绑定和 child→parent 共同参与原判环，重写一个不完整的反向图会改变故障语义。

## 决定

继续由 InstanceControl::subscription 唯一强持原 ScheduledJob，其弱 self、enqueue 合并和 start 门禁不变。原 InstanceDeclaration 增加该作业的弱引用，ServiceRegistryControl 仅增加完整 ServiceKey→InstanceId 集合的派生索引；不保存第二份实例状态、作业拥有表、业务图或激活决定。键来自原已解析的 declaration.dependencies，包含名称、接口主版本和 realm。

startMount 在 declare 前准备唯一 subscription，不再把激活作业放入通用 watchers。私有 declareInstance 同步接纳声明、弱作业、全部兴趣键及原发布组成员，准备完成后才通知。准备期异常只回滚本次插入项/空桶，不发通知，不复制整份注册表。forgetInstance 在通知前按原声明删除全部兴趣成员/空桶及声明、父子记录。零依赖声明仍有弱作业，参与必要的结构广播。

Context::presentation 保留原通用 watch 机制并继续收到所有原通知；不重复注册同一个激活作业。通知器先复制稳定的强作业列表，离开所有索引/声明/观察者迭代后再调用 scheduler.post/discarded。投递允许重入 stop/forget，旧已收集作业仍由原 weak self 与 stopping 门禁处理，不再引入取消状态或内联执行。

将原通用通知算法从 service.cpp 移入私有 service_notifications.cpp，完整/按键两个实际调用路径共用收集和投递；删除原位置的实现。声明和兴趣索引的接纳/移除仍留在原 ServiceRegistry 方法，绑定生命周期入口只选择通知范围，避免把新的路由细节继续堆入已有服务实现，不引入路由类或框架。

| 变化来源 | 激活通知范围 |
|---|---|
| ProviderHandle::publish | 按完整 key；不改变绑定 provider、key、publication 身份 |
| retireForCleanup（含 retire） | 按完整 key；保留原 committed consumers 的同步失效传播 |
| release | 无条件完整广播，保留擦除绑定后重新解析声明提供者的行为 |
| declareInstance / forgetInstance | 完整广播，保留声明/所有权图检查 |
| PublicationGroup commit / abort | 完整广播，覆盖跨视图可见性及无输出终态 |
| ReplacementScope release | 完整广播，覆盖门禁解除和恢复 |

stageErased 原本不通知，仍保持；它会影响声明 provider 解析，不能描述为“不改变拓扑”。本段不增加一套判断 release 是否改变拓扑的算法。按键优化针对服务可用性，结构变化仍保守检查；不宣称所有变化仅直接 key 或完整图算法已增量化。

## 兼容与删除

删除实例在通用 watchers 的旧登记，保留 Context::presentation 的实际通用调用者；发布/退役改走唯一按键收集路径，排队与拒投算法共用，不能复制两套投递处理。公开类、现有快照、getter 口径、C ABI、元数据和 SDK 0.1.5 不变。私有 declareInstance 参数/导出名变化明确要求内部平台联动重建，不承诺旧私有符号或任意内部 DLL 混搭兼容；旧公开符号及实际 .5 宿主/插件继续验证。

## 验证出口

先在原 a9f4230 Core 上运行新定向通知判据，保存无关键仍触发评估的实际负例；随后实现索引并复验。真实公开路径覆盖完整 key 的 name/major/realm 分离、相关通知合并、Pending 恢复、Active/committed 失效、上述 Staged-x 声明环、Held commit/abort、零依赖结构唤醒、替换解除门禁、停止后排队作业与拒投。退出后原声明/资源必须清空，不暴露测试友元或索引写入口。

复验相应 Core/Loader、真实原生替换及 UI 呈现/故障，核旧公开布局/符号与实物、实际 ASan 依赖，以及静态 Core/无 Qt GCC。保留原始负例，不把专项当全量。完成源码和证据审查、提交同步且工作区干净后才冻结 d1 的连续替换资源基线。
