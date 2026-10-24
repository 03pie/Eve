# ADR-0013：目标接受与串行协调基础

日期：2026-10-24。范围：Q4c1。提供目标接受/CAS、最新目标收敛、串行动作和关闭协议；尚未提供 Kernel 运行树驱动，不宣称真实插件配置执行或管理页集成已完成。

## 模块边界

新增 Cordis::ConfigCoordinator，公开头 cordis/loader/coordinator.hpp，依赖 ConfigYaml 和 Core 的 Task/IScheduler/CleanupReport 协议，不依赖 Qt/Ela。Core 不反向引用 Loader，结构预览和解析目标的原有依赖边界保持不变。

协调器必须由宿主提供 IReconcileDriver。preflight 无业务副作用地检查整个目标是否受支持；plan 根据实际运行资源产生有序动作；apply 返回一个动作的稳定结果；snapshot 复制实际资源；close 广播停止并异步排空。当前没有默认的假运行驱动。单测的 ControlledDriver 仅用于检查协议、gate、实际部分值和调度顺序，不是 Kernel 适配器，也不证明消费者/提供者或真实父子树的执行顺序。

驱动必须明确拒绝未实现的能力，不能忽略子树、realm 或有效 inject；负责消费者先移除、提供者先添加及父子归属，不能直接把 PlanPreview 当成队列。协调器检查动作数量（最多 65536）、非空/唯一条目路径及有效动作枚举，但不替驱动证明业务计划正确。

## 接受与结果

accept 只接受 Q4b 的有效 ValidatedCandidate，移动后的候选拒绝。初始 desiredRevision=0、desired 为空；每次接受成功递增修订，和 Instance 的配置修订相互独立。比较调用方 expectedDesiredRevision；预检可同步重入，返回后必须再次检查 owner、关闭/投递门禁及 CAS。拒绝不改变外层目标，不启动资源动作。

接受成功即发布完整用户目标，返回独立 AcceptedTarget.settled。计划失败或 Pending 不回滚目标，也不伪造运行成功；相同文本再次被接受也形成新修订，以便从当前真实状态重试。尚无 PersistedSnapshot 或保存行为。

Driver.snapshot 失败时，CoordinatorSnapshot 仍返回 desired 与修订，通过 runningError 表示运行状态不可读；不能把此时空 running 列表当成“全部资源已停止”。这使运行诊断失败不会隐藏已接受、以后可保存的目标意图。

## 串行与替代

最多保留一个执行中的工作项和一个最新待执行目标。accept 不直接调用 apply；owner job 排队运行，已完成 Task 的回执同样排队。

当前动作在途时接受 B，再接受 C：未开始的 B 立即完成 Superseded；A 等当前动作实际稳定后完成 Superseded，保留该动作实际结果，其余项标为 Superseded。随后为 C 再调用 driver.plan，从真实部分状态规划，不以旧 desired 推断已经执行了什么。

EntryStatus 区分 NotStarted/Running/Applied/Pending/Failed/Skipped/Superseded；PlanOutcome 区分 Applied/Pending/Failed/Superseded/Cancelled。Pending 是历史稳定结果，不阻塞无关动作，也不随未来资源恢复改写旧 Task。默认动作失败后停止本计划全部后续项，保留已应用部分；safeToContinue=true 的失败允许以后接受新目标重试，false 则冻结新执行入口。

异常 Task、apply 抛错及无效 StepOutcome 无法证明安全检查点，保守置 unsafe；规划失败不执行动作。Failed 没有错误时补充协议诊断。完成报告只含宿主值，不保留候选、驱动、插件闭包或无界历史。

## 重入、投递与关闭

owner 线程是工作账目的唯一写者；DriverAccess 与 pump 门禁使嵌套事件循环不能在同步驱动调用尚未返回时启动下一次动作或驱动关闭。preflight/plan 等只读方法本身仍须遵守驱动的无副作用契约。

同步规划返回后、发布规划失败报告前及进入下一次动作前重新检查投递能力。规划期间 scheduler 关闭时，全部未完成的接受任务以 SchedulerClosed 失败，禁止继续调用 apply；即使规划同时抛错，也不能把投递故障伪装成正常完成的计划报告。完成旧工作项后同样重查关闭和投递门禁。

关闭先同步关门，再在 owner 队列中调用 driver.close；即使一个 apply Task 尚未稳定，也必须广播停止，避免“等动作先结束才关闭”的循环等待。驱动须在 close 中终结在途动作，不能反过来等待协调器先处理该动作。协调器同时等待关闭证据与所有在途回执，再完成同一个关闭 Task；尚未执行项为 Skipped，计划结果为 Cancelled。构造时不创建驱动业务资源，facade 析构不替代显式异步 close。

投递失败可能来自工作线程。DeliveryGate 只在 mutex/atomic 和线程安全 Promise 上发布故障，锁外向全部未完成登记目标及关闭 Task 发布失败，不从 worker 访问 owner 工作报告或运行驱动。成功完成与故障同时到达时遵循 Task 的一次完成仲裁，不改写既有历史结果；后续执行停止。已经开始工作但无法证明静止时保守自保留控制块及驱动，不强制释放资源，也不把失败报告当作卸载证据。正常关闭仍须先于 scheduler.close。

## 验证及后续

十一项 coordinator 场景覆盖 CAS/移动后拒绝、串行 gate、A/B/C、部分失败重试、Pending 历史、关闭重入/优先停止、不静止保留、worker 投递失败/预检竞态、预检/规划重入、规划期间关闭门禁、非法驱动结果及错误线程。它们验证的是通用协调协议，不等于设计中的完整 L01–L04/S05 验收。

Q4c2 接入 Kernel 运行树驱动，补齐真实依赖排序、子树/realm/inject 映射及管理入口。原生桌面人工验收仍待完成；Q4d 再接持久化冲突、原子保存与 watcher。
