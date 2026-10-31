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

## M2c2 Core 替换作用域

| 场景 | 实际检查 |
|---|---|
| 冲突挂载、实例修改/停止、注入、手动 Core 挂载与发布拒绝；既有 committed 业务仍有效 | p5.scope_admission |
| 冻结关闭 Context/effect/旧 ServiceRef 业务，绑定仍 Published；撤销恢复同 epoch 等待者 | p5.scope_freeze |
| 消费者 C→B→A 等待真实清理，冻结期间 CleanupContext 仍可用旧输入 | p5.scope_ordered |
| 原异步调用未排空不停止提供者；Pending 激活暂停并恢复 | p5.scope_inflight、p5.scope_pending |
| 不同 realm 独立作用域并行，同消费者及未发布输出键冲突拒绝 | p5.scope_parallel、p5.scope_overlap、p5.scope_unbound |
| 目标子实例扩展实际祖先拥有单元；外部宿主变化导致冻结重验拒绝 | p5.scope_children、p5.scope_stale |
| 清理失败保留作用域与提供者；停止前析构撤销，停止后析构保留 | p5.scope_cleanup_failure、p5.scope_abandoned |
| Kernel 门面先销毁仍可正常停止/释放；错误线程访问拒绝，析构排回 owner | p5.scope_owner_gone、p5.scope_cross_thread |
| 空/移动后/外来 Runtime、空闭包、重复 release 与关闭门禁 | p5.scope_guards |
| Kernel.close 取消作用域回执，但自身等待真实异步清理后才解除占用 | p5.scope_shutdown |
| 挂载/注入通知重入关闭时真实实例已登记；通知重入占用包含 Pending 实例 | p5.scope_mount_close、p5.scope_inject_close、p5.scope_mount_claim |
| 门面入口强持账目直到 API 返回，回调中销毁 Kernel 不提前释放工厂 | p5.scope_facade_reentry |
| 解除通知失败保留 Released 与错误；通知重入不会提前报告完成或清除新占用 | p5.scope_release_failure、p5.scope_release_reentry |
| 未被闭包拥有但契约键冲突的 Pending 实例暂停，解除后恢复 | p5.scope_contract_resume |
| worker 析构投递被拒绝保留排他，正常 Kernel 关闭可回收 | p5.scope_abandon_rejected |
| 移动重赋解除旧作用域时重入再次重赋，最终权限不被外层覆盖 | p5.scope_move_reentry |
| 配置冲突在 CAS/保存登记前拒绝；冻结/已停止条目未变更时其他条目可独立应用 | p5.scope_configuration_cas |
| 父实例恢复时受占用的等待子意图不抢先创建；解除信号后恢复，目标/保存版本不变 | p5.scope_configuration_restore |
| 真实 C DLL 冻结/撤销与有序 stop；新手动消费者 BUSY，不新增租约，解除后可调用 | p5.native_scope_freeze、p5.native_scope_stop、p5.native_scope_external |
| 同工具链但声明旧 SDK 0.1.0 的实际 Qt 元数据 DLL 在映射前拒绝 | p5.scope_legacy_sdk |

验证与边界见 [M2c2 记录](../../docs/p5-m2c2-development.md)。此处不是完整运行升级入口，Session 普通关闭仍须先由上层协调结束替换作用域。

## M2c3 模块预约与创建准入

| 场景 | 实际检查 |
|---|---|
| 参数/错误线程、空/移动后权限、跨管理器排他、值快照不影响实际所有权 | p5.native_reservation_guards、p5.qt_reservation_guards |
| 双版本真实业务、旧定义迟到创建拒绝，已有业务继续；解除后按原 Core 操作重试 | p5.native_reservation_admission、p5.qt_reservation_admission |
| 当前/候选 drain 与合法暂存拒绝，当前发现可复用，未映射第三份产物；解除后原生正常排空 | p5.native_reservation_drain、p5.qt_reservation_drain |
| 管理器销毁不撤销预约，权限析构自动解除；worker 接收并释放预约 | p5.native_reservation_released、p5.qt_reservation_released、p5.native_reservation_worker、p5.qt_reservation_worker |
| 选定候选已有真实业务实例时拒绝，完整关闭后可预约 | p5.native_reservation_candidate_live、p5.qt_reservation_candidate_live |
| 发现通过但业务协商失败的候选拒绝，健康候选仍可预约 | p5.native_reservation_failure、p5.qt_reservation_failure |
| 不同模块独立预约，释放一个不清除另一个；两个线程的管理器争同一模块只允许一个 | p5.native_reservation_parallel、p5.native_reservation_race |
| 真正 Qt 工厂 query/create 重入不能取得预约；结束后可取得 | p5.qt_reservation_query_reentry、p5.qt_reservation_create_reentry |
| 其他候选正在装载 root 或正在协商工厂时拒绝；已持有预约时其工厂准入也拒绝 | p5.qt_reservation_stage_reentry、p5.qt_reservation_sibling_query_reentry |

本段使用真实两版本业务 DLL，检查预约与入场，不迁移或提交业务，不以发现/预约代替工厂/schema 验证。完整记录见 [M2c3](../../docs/p5-m2c3-development.md)。

M2 后续及 M3–M5 尚待实现和逐项验证：完整 ReplacementOperation、服务发布与冻结定义/目录角色的整体切换、UI 贡献可见性、状态导出/迁移/校验、新代激活失败恢复、旧代恢复失败、消费者恢复失败、晚到回调/析构与卸载、白名单实际卸载、正常/故障下连续 100 次 Windows 替换及随机压力。最终必须把操作报告、实际运行和资源/文件/进程证据分别核对。
