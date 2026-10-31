# P5 替换与卸载验收

本表随 [P5 阶段](../../docs/p5-development.md)补齐，不能把候选并存当作热替换成功。完整出口包括 H01/H02/S09、版本化状态迁移、白名单物理卸载和真实活跃消费者/页面的升级演示。

## M1 候选目录

| 场景 | 当前实际检查 |
|---|---|
| 同 ID 两份真实 DLL 并存，默认装载仍选原当前代 | p5.native_candidate_coexist / p5.qt_candidate_coexist |
| 相同候选跨路径复用且不重复进入 query/root | p5.native_candidate_reuse / p5.qt_candidate_reuse |
| owner、无效/未知/非当前前驱、相同内容拒绝 | p5.native_candidate_guards / p5.qt_candidate_guards |
| 插件目录和 moduleId 不兼容时不映射候选 | p5.native_candidate_catalog / identity、p5.qt_candidate_catalog / identity |
| 失败候选留在账目但不改变旧代；失败当前代拒绝暂存 | p5.native_candidate_failed / failed_current、p5.qt_candidate_failed / failed_current |
| C ABI 发现通过但业务协商失败：禁止将当前代作为可恢复前驱或复用失败候选，普通发现复用保留 | p5.native_candidate_business_failed_current / p5.native_candidate_business_failed_candidate |
| 已 drain 的当前代拒绝新候选 | p5.native_candidate_drained |
| 旧 C ABI 业务保持运行且仍能正常关闭 | p5.native_candidate_live |
| 候选实际计入同一 64 代预算 | p5.native_candidate_budget |
| 同一物理 DLL 不作两代，失败别名不绕过候选缓存 | p5.native_candidate_physical / aliases |
| Qt 构造重入观察 Candidate/Validated，重复暂存拒绝 | p5.qt_candidate_reentry |

## M2a hash 安装事务

| 场景 | 实际检查 |
|---|---|
| 独立复制、原始声明保留、无隐式执行、凭据保护/复制/移动/释放 | p5.install_basic |
| 已有目录重验和跨线程复用，无 QCoreApplication 前提 | p5.install_reuse |
| 输入声明名可变，输出为 module.json；多于 64 KiB 的分块复制 | p5.install_manifest_name / multichunk |
| 显式根、UTF-8、限额、摘要/PE、来源写共享冲突与重试 | p5.install_roots / limits / invalid_source / source_busy |
| 活动/外来锁不接管，同 hash 并发安装只发布一个目录 | p5.install_lock / concurrent |
| 占用目录与不同声明身份不覆盖、DLL 名称冲突 | p5.install_occupied / conflict |
| 扩展长度、带 . / .. 的普通及扩展长度绝对路径、Unicode、根别名 | p5.install_long_path / normalized_root / unicode / store_alias |
| 原生创建身份在发布后可核对，字节相同的路径替身不是原文件 | p5.install_identity |
| 回收自有临时文件，外来条目和替身保留并报告不完整清理 | p5.install_rollback / rollback_foreign / rollback_replaced |
| 实际目录重命名在发布目标已有内容时失败，原内容保留 | p5.install_publish_conflict |
| 不沿缓存重解析目录、不复用硬链接成员或被篡改字节 | p5.install_redirected / hardlink / tamper |
| 安装目标经正常 stage 接入同一候选目录，旧 C ABI 业务仍 Started / Qt 当前 root 不变 | p5.install_native_stage / qt_stage |

M2a 的完整验证记录见 [开发记录](../../docs/p5-m2a-development.md)。上述安装检查不证明运行切换、业务兼容或页面升级已完成。

## M2b Core 服务发布组

| 场景 | 实际检查 |
|---|---|
| 真实闭包初始化、延迟消费者、全部成员准备后一次公开，组外通知观察全部输出 | p5.publication_closure |
| 放弃后拒绝新业务，等待异步调用和旧绑定清理，两道屏障均完成才静止 | p5.publication_abort_cleanup |
| 消费者失败禁止提交；仅为 S09 的 Core 前提，尚非完整 ReplacementReport 验收 | p5.publication_failure |
| 其他开放组不可消费 Held，公开后按正常依赖恢复 | p5.publication_groups |
| 已停止成员不从成功条件移除，旧键存活时候选不能抢占 | p5.publication_stopped_member、p5.publication_live_key |
| 放弃受理不冒称清理完成，清理失败保留 Retiring 与非静止报告 | p5.publication_abort_failure |
| 子实例继承发布组并计入成员，顶层重挂载不收养子实例 | p5.publication_children |
| 空/外来组、错误线程、移动后句柄和关闭组入口 | p5.publication_guards |
| 同视图的声明依赖环在激活前拒绝 | p5.publication_cycle |
| 通知投递失败保留 Published 与错误，重复提交不能抹掉 | p5.publication_notification_failure |
| 通知重入看到全部输出，但不能提前完成提交；重赋句柄不改写另一组 | p5.publication_notification_reentry、p5.publication_notification_reassign |
| 同一真实 C DLL：组内实例与手动外部消费者保持视图隔离，提交后可调用，放弃后不公开 | p5.native_publication_commit、p5.native_publication_abort |

完整验证与审查记录见 [M2b 记录](../../docs/p5-m2b-development.md)。本段未切换 ModuleManager 当前代或迁移业务状态，未将默认驻留修改为可卸载，也未宣称 UI 贡献已纳入发布组。

## M2c1 实际图与只读替换闭包

| 场景 | 实际检查 |
|---|---|
| 动态未声明的实际消费者、多级传递闭包和确定性顺序；查询不执行插件或排队任务 | p5.closure_chain |
| 可选输出尚未出现时仍包含等待消费者，epoch 为 0 | p5.closure_pending |
| 真实子实例句柄及其消费者，子 Runtime 不必注册为顶层定义 | p5.closure_children |
| 完整名称/major/realm 隔离、多目标 Runtime；同 Runtime 全部实例，跳过已终结实例 | p5.closure_realms、p5.closure_instances |
| 共享索引但非本 Kernel 持有的 Core 消费者，实际与等待情况均拒绝 | p5.closure_external、p5.closure_external_pending |
| 未使用的可选输出声明不覆盖已有宿主真实绑定，不捕获其消费者 | p5.closure_optional_external |
| Loading/Unloading 拒绝；开放发布组拒绝，提交后可查询 | p5.closure_transitions、p5.closure_publication |
| 空/无效/外来/重复 Runtime、错误线程、已关闭 Kernel；无实例的合法 Runtime 返回空闭包 | p5.closure_guards |
| 异步在途调用、停止中的 committed 租约直到真实释放才退出图 | p5.closure_in_flight |
| 服务完成投递失败但实例仍 Active 时拒绝，保留原故障屏障 | p5.closure_delivery |
| 联合依赖环拒绝，纯值快照保留旧 epoch/revision 且修改 DTO 不影响运行 | p5.closure_cycle、p5.closure_snapshots |
| 真实原生托管输出/消费者接入图并继续调用；手动原生消费者不会被误认为 Kernel 所有 | p5.native_replacement_graph、p5.native_replacement_external |

验证与审查见 [M2c1 记录](../../docs/p5-m2c1-development.md)。此查询不是已锁定执行计划或重建指令，不能直接改变当前运行。

M2 后续及 M3–M5 尚待实现和逐项验证：同模块/闭包互斥、旧代未清理不接管服务键、服务发布与定义/目录角色的整体切换、UI 贡献可见性、状态导出/迁移/校验、新代激活失败恢复、旧代恢复失败、消费者恢复失败、晚到回调/析构与卸载、白名单实际卸载、正常/故障下连续 100 次 Windows 替换及随机压力。最终必须把操作报告、实际运行和资源/文件/进程证据分别核对。
