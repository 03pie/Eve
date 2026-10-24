# ADR-0015：目标与保存状态、单写队列和退出刷新

日期：2026-10-24。范围：Q4d2。承接 Q4d1 文件事务，不依赖 Kernel 运行树或管理页接入；原生桌面人工验收和 Q4c2 门禁保持不变。

## 模块边界

Cordis::ConfigStore 依赖 ConfigYaml 和 Core 的 Task/IScheduler，无 Qt/Ela 依赖。文件值类型移到 cordis/loader/file.hpp，原 qt_file.hpp 继续包含它，既有文件接口保持兼容。公开 Store 入口为 cordis/loader/store.hpp。

可选 Cordis::ConfigStoreQt 提供 QtConfigurationWriter，组合 ConfigStore 与 ConfigFileQt 的真实文件事务。构造不读取文件、不创建锁，也不启动 worker。当前后端在 Store 的 owner 调用线程同步执行 I/O，再返回完成 Task；回执依然排队处理。调用 save 会先返回任务，不能据此宣称磁盘 I/O 已在后台运行。后台 I/O 及其专用退出屏障另行实现。

IConfigurationWriter 的 commit 可同步重入，Store 只允许一个在途事务。后端须执行完整磁盘版本比较，并在任务终结前完成该次 I/O，不能遗留未纳入任务的后台工作。Store 检查报告路径、存在性/hash 形状和保存文本；Unchanged 另要求回执中的完整版本和原文均与提交前基线一致，缺失基线不能凭此变为存在。非法回执形成 Failed，保留历史保存修订和 dirty，并跳过后续项。Store 不重新计算后端 hash；实际哈希及原子文件提交责任在事务后端，测试替身不能替代真实文件证据。

## 目标与历史保存状态

宿主先读取初始 ConfigurationFileSnapshot，再构造 Store；原文不自动解析、接受或登记为已保存目标。协调器成功接受目标后，宿主将其共享不可变对象和 desiredRevision 交给 track。修订必须非零、递增，可以跳号；同一对象同修订可重复跟踪，不允许倒退或同修订换对象。Store 不再分配一套目标修订，也不等待运行结果。

persisted/persistedRevision 表示本 Store 最后一次获确认的历史保存目标，不表示磁盘永远没被外部修改。baseline 是下一次提交比较的获确认版本，external 是最后观察到的冲突原文。三者与当前 desired 分开保留。

| 事件 | 历史保存状态 | dirty |
|---|---|---|
| 初始读取，尚无目标 | 无本 Store 确认的修订 | false |
| track 新修订 N | 保留原 M | true |
| M 写完，但目标已变 N | 更新为本次写入的 M | true |
| 最新 N 完整提交或字节已经相同 | 更新为 N | false |
| I/O 失败、磁盘冲突或协议报告非法 | 保留原 M | true |
| 明确 rebase | 保留原 M，不采用外部目标 | 有目标时 true |

因此运行计划失败与目标已经保存可以同时存在。单测使用真实 ConfigurationCoordinator 和会失败的受控运行驱动验证此分离，不宣称已接入 Kernel 或完成全部 L/S 验收。

## 有界队列与业务结果

save(expectedDesiredRevision) 只对当前目标接受请求，排队时固定该修订与目标；磁盘基线在真正开始写入时读取，前一份成功保存产生的新版本供下一份使用。普通保存总数（在途加排队）默认最多 64，可由 StoreLimits 调整；满时拒绝新请求，但不丢失已跟踪目标。同修订尚未完成的重复保存共享任务，历史任务不随重试、后续保存或恢复改写。

SaveReport 区分 Written、Unchanged、Conflict、Failed、Skipped。后端 I/O 错误形成 Failed，磁盘变化形成 Conflict 并保留原文；后续未开始的请求统一 Skipped，原因链保留本次错误。失败不回滚目标、不推进历史保存修订，不宣称全局事务回滚。正常业务失败仍是可读报告；回执无法投递时 Task 本身失败，调用方须同时检查 Task 的 Result 和报告 outcome。

冲突后不能自动采用 external 为新基线。宿主明确选择继续保留目标时，先取得原文，再调用 rebase(disk, expectedDesiredRevision)；要求队列空闲、目标修订匹配且路径绑定不变。rebase 不意味着目标已经保存，也不绕过下一次真实磁盘比较。用户选择采用外部内容则须重新解析/schema 校验并经协调器接受为新目标，再转交 Store；本轮没有自动执行该决策的 UI。

## 关闭、重入与生命周期

close 同步拒绝新的 track/save/rebase，等待全部已经接受的写入。最新 dirty 目标尚未排队时，在旧队列排空后追加一次最终刷新，不额外突破普通写入槽位。关闭期间任何一次保存失败就停止剩余项并终结关闭失败，不无限重试。若在调用 close 前发生过可重试故障，关闭可以对尚未排队的最新目标做一次刷新尝试；已有磁盘冲突仍要通过原基线比较，不隐式覆盖。

关闭任务成功意味着队列排空且最新目标已获保存确认；失败时保留 dirty、最后错误及历史保存状态。close 幂等，不重新打开业务入口或在第二次 close 时重复提交。宿主须处理关闭失败后再决定退出策略，正常顺序是先等 Store 刷新，再完成 Kernel/外壳清理，最后关闭 scheduler 和 Qt 循环。本轮尚未把 Store 接入管理示例的整应用退出流程。

owner 是账目的唯一写者，重入后端期间 pump 不会并发进入下一次提交。已请求保存由控制块自保留，facade 析构不取消这些写入；队列正常排空后解除自保留，没有无界保存历史。未请求保存的目标须通过显式 close 刷新，析构不保证其落盘。

worker 投递失败只通过 mutex/atomic 和 Promise 发布，不从 worker 更新保存账目。故障终结所有未完成登记任务及关闭任务，成功/故障竞争遵循 Task 首次完成规则。无法在 owner 确认进度时保守保留控制块与后端，停止新业务；实际磁盘可能已经提交，但 Store 不伪造保存修订。此策略不构成泄漏或模块卸载证明。

## 验证与后续

十三项无 Qt 场景覆盖目标修订、FIFO/重复请求、容量/最终刷新、失败停止与重试、冲突/rebase、关闭成功/失败、worker 投递故障、同步重入、协议结果、错误线程、生命周期及运行失败仍可保存。新增 unchanged_protocol 验证错误 Unchanged 不能创建缺失文件状态、换入其他 hash 或修改原文，也不能使关闭虚报保存成功。四项真实 Qt 文件集成覆盖保存/无变化、外部非法 YAML、真实锁失败重试，以及生产 QtScheduler 事件循环保持到退出刷新完成。

不覆盖 watcher、文件读取/解析后台化、全应用启动装配、管理页保存 UI 或 Kernel 运行树。Q4d3 接 watcher、重命名后重新监听和 hash 消重；Q4c2 仍在原生桌面人工验收后接入。文件系统 CAS、非协作编辑窗口及文件故障验证范围沿用 [ADR-0014](0014-configuration-file-transactions.md)。
