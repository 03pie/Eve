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

## M2c4 共同定义与候选来源

| 场景 | 实际检查 |
|---|---|
| 构造不执行 schema/激活，规范模块来源，独立目录与普通校验身份区分，候选复制/移动 | p5.definitions_basic |
| 无效/重复/无 schema 定义、错误来源、移后 Runtime、实际 Kernel 归属 | p5.definitions_invalid |
| 固定依赖不可弱化，额外必需接口须有兼容类型化契约，可选声明不可重复 | p5.definitions_contracts |
| 实际 Kernel 驱动直接 preflight/plan/apply 拒绝其他目录的候选，无实例副作用 | p5.definitions_driver |
| 真实 Session 应用/添加/启停/保存及完整关闭，均来自驱动的共同目录 | p5.definitions_session |
| 完整驱动拒绝独立 schema；保留旧运行时构造，新完整 Session 拒绝旧不完整驱动 | p5.definitions_constructors |
| Coordinator 在预检前后检查身份，失败不增加 desired 修订 | p5.definitions_foreign、p5.definitions_reentry |
| 预览后的目录改变使草稿失效；schema 回调改变目录也不发布草稿 | p5.definitions_draft、p5.definitions_schema_reentry |
| schema 释放最后目录门面，调用栈保留工厂至返回，候选仅保留无回调身份 | p5.definitions_lifetime |
| 查目录回调关闭并销毁 Session 门面，读取帧保持控制块至返回 | p5.definitions_read_reentry |
| 驱动快照改变目录或关闭 Session 后，不能发布草稿或登记保存目标 | p5.definitions_snapshot_reentry、p5.definitions_snapshot_close |
| 查目录异常在预览/枚举/添加处返回失败，目标与文件不变 | p5.definitions_getter_failure |
| 真实两版本 C ABI/Qt 工厂、独立定义来源和实际文件保存/读回/关闭，模块角色不改变 | p5.native_definition_binding、p5.qt_definition_binding |

边界与审查记录见 [M2c4](../../docs/p5-m2c4-development.md)。协议替身只验证来源门禁，真实双版本场景验证独立绑定，不代表已运行替换；完整事务仍待后续。

## M2c5 整体运行事务（已完成）

以下通过公开 `ReplacementOperation` 执行真实 C ABI/Qt 两版本 DLL，不由测试手动推进提交凭据。`native` / `qt` 前缀分别对应两后端。

| 场景 | 检查名称（p5 前缀） |
|---|---|
| 配置根、显式子项、手动实例共同换代及真实保存 | `{native,qt}_operation` |
| 新工厂失败、配置子项失败后的旧配置重建 | `{native,qt}_operation_recovery`、`{native,qt}_operation_child_failure` |
| 启动前取消、最终 schema 重入取消 | `{native,qt}_operation_cancel`、`{native,qt}_operation_cancel_commit` |
| 候选核对后恢复失败，报告无已销毁候选映射 | `{native,qt}_operation_recovery_mapping` |
| 两个实际 Session/Driver 共同发布和旧代重建、修订/保存账目一致 | `{native,qt}_operation_sessions`、`{native,qt}_operation_sessions_recovery` |
| 第一参与者已取得真实最终准备槽，第二参与者 schema 抛错或取消；角色/磁盘未发布，完整旧代恢复且两会话可再次保存 | `{native,qt}_operation_sessions_prepare_failure`、`{native,qt}_operation_sessions_prepare_cancel` |
| 实际工厂来源门禁、无关目标编辑合并与大整数原文保存 | `{native,qt}_operation_guards`、`{native,qt}_operation_merge` |
| 宿主根拥有者协议、创建前拒绝、遗漏附加依赖 | `{native,qt}_operation_owner`、`{native,qt}_operation_owner_before_create`、`{native,qt}_operation_owner_contract` |
| 旧代恢复创建前/后失败，清理、保留预约与关闭 Scope 权限 | `{native,qt}_operation_owner_recovery_failure`、`{native,qt}_operation_owner_recovery_after_create_failure` |
| 工作线程完成时拒投，恢复调度器后 Scope 仍拒绝创建 | `native_operation_owner_delivery` |
| 动态根→子→孙实际对应、遗漏成员或错误父级后的完整旧树恢复 | `{native,qt}_operation_subtree`、`{native,qt}_operation_subtree_omit`、`{native,qt}_operation_subtree_parent` |
| 宿主内置根包含业务 DLL 子实例及临时孙实例，祖先共同换代/恢复 | `{native,qt}_operation_subtree_nested`、`{native,qt}_operation_subtree_nested_omit` |
| 已停止配置子项连同活动祖先重建、候选失败恢复；祖先也停止时完整配置树重建 | `{native,qt}_operation_waiting`、`{native,qt}_operation_waiting_recovery`、`{native,qt}_operation_waiting_root` |
| 无 Qt Core 实际祖先/子树/消费者闭包，重复选择、外 Kernel、冲突与 freeze | `configuration_commit_anchors` |
| 父项失败致子项未执行，候选成功或失败后按旧配置重建并保存 | `{native,qt}_operation_unstarted`、`{native,qt}_operation_unstarted_recovery` |
| 未执行条目的准备放弃、目标外新条目拒绝、域兼容和节点发布 | `configuration_commit_unstarted` |
| 旧候选构建许可不能在恢复阶段创建内置根 | `{native,qt}_operation_subtree_nested_stale` |
| 构建许可绑定实际组、空组丢弃、错误线程与弱引用寿命 | `builder_phases`、`builder_empty`、`builder_lifetime` |
| 启动前、停止中及拥有者候选任务悬挂时 Kernel 实际关闭，操作取消、解除意图及拒绝晚到结果 | `{native,qt}_operation_owner_kernel_close`、`{native,qt}_operation_owner_kernel_close_prepared`、`{native,qt}_operation_owner_kernel_close_stopping` |
| Session 关闭与拥有者悬挂、启动前、停止中、最终 schema、旧代恢复最后回调及恢复失败交错；第二会话/手动实例与文件实际结果 | `{native,qt}_operation_session_close`、`{native,qt}_operation_session_close_{prepared,stopping,commit,recovery_commit,recovery_failure}` |
| 预检拥有者嵌套事件循环关闭、抛出及更早 availability 关闭；撤销后原文件/其他会话/手动原实例保留；预检 Kernel 关闭须保留预约到真实静止 | `{native,qt}_operation_session_close_preflight{,_throw,_available,_kernel}` |
| Core 活动候选或错误线程拒绝不得提前解锁，实际释放通知须晚于宿主解锁 | `builder_release_order` |
| 多持有关闭屏障、外来/已释放凭据拒绝、仅 Preserve、放弃启动、worker 结算与拒投 | `configuration_commit_session_hold`、`configuration_commit_session_hold_delivery` |
| 两个真实模块共享未创建祖先时预检排他；未创建的计划工厂拒绝低层模块预约/关闭/drain，完成后可重试；独立且有字符串前缀的路径并行提交并保存两模块目录 | `{native,qt}_operation_paths_conflict`、`{native,qt}_operation_paths_parallel` |
| 无实例路径占用、普通目标 CAS/直接动作门禁、Pending 不写记录、必须持有真实凭据、空会话目标 | `configuration_commit_claims`、`configuration_commit_claims_empty` |
| 同 Kernel 规范工厂读意图共享/排他/释放/线程/来源及低层原成员读权限 | `builder_readers` |
| 拥有者声明新增动态子节点并创建；遗漏未来描述冻结前拒绝；实际服务契约不同则恢复原树 | `{native,qt}_operation_subtree_future_{child,missing,contract}` |
| 完整未来图缺失提供者/环、未知 realm、外部版本不足冻结前拒绝；外部提供者在预检后停止则冻结前重验拒绝并保留原业务根 | `{native,qt}_operation_subtree_future_{missing_service,cycle,realm,version}`、`{native,qt}_operation_subtree_provider_loss` |
| 两个实际拥有单元的服务依赖与真实激活顺序、跨单元环及重复提供者；不能代替跨模块完整矩阵 | `{native,qt}_operation_graph{,_cycle,_conflict}` |
| 两个真实模块争用未来输出在冻结前拒绝，取消后重试；外来实际 Staged/Held/Retiring 绑定拒绝 | `{native,qt}_operation_outputs{,_staged,_held,_retiring}` |
| 同 Kernel 输出封存、错误线程/重复拒绝、失败不落半组权限、计划外已注册工厂拒绝及低层动态兼容 | `builder_output_plan` |
| 外部绑定读共享、BindingId 重验、输入失败不占用输出、原始退役门禁、祖先 stop/update 与另一替换拒绝、Kernel 关闭及真实失效信号，不伪造 committed 消费者 | `builder_input_plan` |
| 真实 native/Qt 操作冻结前保护外部提供者 stop/update，取消后解除；上游原始绑定故障传播，冻结前拒绝而原业务根保留 | `{native,qt}_operation_subtree_provider_{guard,loss}` |
| 拥有者候选任务悬挂时，候选独有外部服务真实故障中断，完整恢复旧树、关闭旧许可及忽略晚到结果 | `{native,qt}_operation_subtree_input_suspend` |
| 外部提供者是内置动态子实例，其真实 C ABI 模块祖先不在闭包中，预检后另一预约/close/drain 拒绝及取消释放；目标为真实 native/Qt | `{native,qt}_operation_sources_ancestor` |
| 候选独有的真实 C ABI 工厂来源违反 create 协议，代次故障中断悬挂拥有者并恢复旧树；旧许可/晚到回执拒绝，peer 不确定资源诚实保留 | `{native,qt}_operation_sources_loss` |
| 提供者真实 Native 模块祖先同时被候选/恢复读取，故障阻止恢复并报告 RecoveryFailed、保留预约及空映射 | `{native,qt}_operation_sources_loss_shared` |
| 真实低层原生服务不属于 Core 拥有树：从实际端点保护代次读持有；协议 quarantine 中断悬挂拥有者，恢复失败保留资源 | `{native,qt}_operation_sources_raw_loss` |
| 正确声明同代次/插件的新 RuntimeId；没有已有来源读权限时取得新模块读持有；真实候选完成和模块角色 | `{native,qt}_operation_sources_{declared,independent}` |
| 仅有服务读持有不授予创建，未声明或实际代次不符在 DLL create 前拒绝，双代实际计数不增并完整恢复 | `{native,qt}_operation_sources_{undeclared,mismatch}` |
| 未知插件、非目标候选代次或恢复来源改变冻结前拒绝，原三成员仍 Active，权限解除 | `{native,qt}_operation_sources_{plugin,role,recovery_source}` |
| 全局已允许的来源被放到错误成员，真实 create 发生后 Prepared 逐成员拒绝并恢复，不能误称创建前拒绝 | `{native,qt}_operation_sources_misattributed` |
| 真实 C ABI 服务/effect/实例/模块主动撤回在绑定读冲突时无副作用拒绝，健康与全部服务保留、零请求/无残留、解除权限后可完成原撤回/关闭 | `native_service_{,effect_,instance_,module_}read_conflict` |
| 恢复动态成员的配置、域映射、依赖或输出改变在停止前拒绝，修正计划可重试并取消；等价重复依赖可完成旧树恢复 | `{native,qt}_operation_subtree_recovery_{config,realm,requirements,outputs,equivalent}` |

纯目标合并另由 `replacement_target` 标签覆盖，不替代 DLL/运行检查。动态子树检查覆盖链式结构与嵌套业务子实例，不能代替任意拓扑及晚到任务故障；多会话成功/激活失败检查也不代表所有准备失败交错。阶段结果见 [开发记录](../../docs/p5-m2c5-development.md)和 [审查](../../docs/code-review.md#p5-m2c5-动态子树拥有者复查)。

M2c5 的实际模块来源、未托管原生服务、两阶段来源故障、多参与者部分准备失败/取消及最终支持矩阵已补齐；阶段审计和范围见 [收口记录](../../docs/p5-m2c5-development.md#阶段收口结论)。后续版本化状态迁移已由 M3-c（b454d85）完成，见 [M3 要求映射](../../docs/p5-m3-development.md)；上述 M1/M2 表保留各阶段当时的验收范围，当前卸载状态见下段。

## M4 白名单物理卸载

M4-a 的 [22 项装载策略检查](../qt/module_load_policy.cmake)、M4-b 的 [37 项物理终结检查](../qt/module_unload.cmake)已随各阶段审计归档。M4-c 新增 [11 项替换终结组合](../qt/module_unload_replacement.cmake)，并补强真实 Qt 状态替换的 `p5.state_operation_qt_roundtrip`；受影响的 37+11+1 项通过 49/49 后，当前源码 MSVC Release/Debug/ASan（共享 Core/Ela）全量各 1344/1344、静态 Core（关闭示例/Ela）970/970、无 Qt GCC 440/440 全部通过。整 M4 要求审计和最终证据核对完成，阶段范围见 [M4 收口](../../docs/p5-m4-development.md#m4-c-收口结论)。

下表装载策略名统一加 `p5.load_policy_` 前缀，物理终结及组合名统一加 `p5.unload_` 前缀；单独标出的测试名保持原名。

| 分段 | 验收条件 | 实际检查名称 |
|---|---|---|
| M4-a | manifest 请求不授予卸载权限；宿主按重新固定的 moduleId/SHA256 精确批准，未授权或产物改变在映射前拒绝 | `approved`、`resident`、`capable_resident`、`wire`、`unauthorized`、`wrong_id`、`wrong_hash`、`invalid_entry`、`tampered_preview`；另有 `p4.manifest_unload_policy` |
| M4-a | 原发现 1.0 驻留兼容与显式 1.1 协商独立；错误协商保持真实映像/文件与失败诊断 | `legacy_reject`、`legacy_flags`、`explicit_flags`、`explicit_minor` |
| M4-a | 首次策略/owner 不变，不能升级已 resident/PIN 代；别名、候选和同 hash 独立副本沿原目录处理 | `reuse`、`immutable_{resident,approved}`、`alias_{resident,approved}`、`candidates`、`same_hash_copy`、`threads`、`reentry` |
| M4-b | 同进程动态 detach 恰一次、原 allocation 完整离开、文件保护释放与独占写入；文件改名/删除和重新装载新 ID；系统失败及未知外部映射保留准确所有权 | `physical`、`file`、`reload`、`resident`、`rejected`、`release_retry`、`external`、`query_failure`、`probe_other`、`probe_release_failure` |
| M4-b | 实际会话、实例、异步任务、工厂/状态、调用、显式 worker/QObject、捕获和服务完整退出；清理不确定保留资源 | `session`、`instance`、`setup`、`cleanup`、`runtime`、`state`、`call`、`worker`、`qobject`、`capture`、`stale_capture`、`cleanup_failure`、`service` |
| M4-b | 同一 ledger 原子封门、首次 owner、最后租约跨线程不卸载；重入和关闭 facade 不读取已退役函数表 | `seal`、`owner`、`owner_gone`、`last_lease`、`reentry`、`closed_facade`、`create_reentry` |
| M4-b | 真实候选/Current/兄弟代与预约/readers 门禁；64 重代预算、128 条纯历史，终结 ID 不复用 | `lineage_{candidate,current,reservation,sibling}`、`reader`、`heavy`、`history` |
| M4-c | 真实共同事务提交后旧代成为 Retired，状态值和完整消费者继续工作；旧宿主显式关闭并释放工厂后才物理回收，卸载失败不改写 Applied 报告 | `replacement_applied`、`replacement_release_retry` |
| M4-c | 候选导入/激活失败或取消时完整恢复原值和消费者，再显式回收废弃候选；恢复期间两代不可回收，普通恢复失败保留 Execution/预约，协议不确定另保留真实对象及 pin | `replacement_restore_reject`、`replacement_start_failure`、`replacement_cancel_restore`、`replacement_recovery_failure`、`replacement_restore_pending` |
| M4-c | 新 Session/Driver/Kernel、目标修订和磁盘保持；旧 Runtime 已释放后，旧不可变目录单独阻碍卸载；FreeLibrary 失败仅更新独立 unloadFailure，重试可完成 | `configured`、`configured_definitions`、`configured_unload_failure` |
| M4-c | A 在私有释放边界等待时，另一真实首次 owner 可完整卸载独立模块；真实 Qt 替换产生 Retired 后仍策略拒绝卸载，新对象可继续创建/激活/关闭 | `parallel_owners`；`p5.state_operation_qt_roundtrip` |

回收由原宿主在替换终结后关闭旧 NativeModule、释放旧 Runtime/定义目录/请求及探针 session，再由首次 owner 显式调用 `tryUnload`。该入口只核对真实资格并执行物理终结，不代替业务清理，不自动解除 PIN，也不在最后租约析构时卸载。历史 Operation/任务/报告、ValidatedCandidate 和已 Disposed 的旧 Instance 可保留；新代继续持有自己的执行来源。默认 Native 与 Qt 保持 resident，专用夹具白名单不能升级既有代次。当前 C++ SDK 为 0.1.5，宿主与同工具链插件须一起重建；原业务/状态 C ABI 与 Qt IID 不变。

见证只统计动态 DLL_PROCESS_DETACH，进程退出不计成功。`probe_other` 验证另一真实映像的拥有引用归属，`owner_gone` 验证首次线程退出后的身份拒绝，均不声称实际地址或系统线程 ID 已复用；worker/QObject 检查依赖显式登记和完整 join/析构，不代表自动追踪插件线程。`parallel_owners` 证明平台锁不跨系统释放边界，不声称 Windows loader lock 内系统调用并行；Qt 检查始终验证驻留兼容。

M5 仍须交付真实业务产物白名单、服务与 Qt 页面在线升级、UI 贡献可见性、H01/H02、S09 全范围、Windows 连续 100 次真实 ReplacementOperation 及随机/故障压力。M4 专用夹具、目录循环和局部消费者恢复证据不能替代这些出口，P5 尚未完成。

## M5-a1 页面与命令发布边界

本段沿原 Context/PublicationControl 完整准备页面、命令及清理，再由原共同发布状态控制外部呈现；普通导航、搜索、Home、页签、紧凑菜单和执行入口不能提前开放候选。诊断仍保留实际准备资源，不另建 UI 事务或伪造 Context。[实现、阶段审查与验证](../../docs/p5-m5-development.md#m5-a1-收口结论)已完成，随本次收口提交归档；提交同步后才进入 M5-a2 的输出/通知 pending 值隔离。

| 验收条件 | 实际检查 |
|---|---|
| 原发布组提交/放弃、组内外隔离、完整清理及通知故障回归 | 原 13 项 `p5.publication_*`，见 [Core 选择](../CMakeLists.txt)与 [真实公开 API 用例](../unit/publication.cpp) |
| 无 Binding 变化的 Aborted 也通知；拒投可见且幂等不抹掉故障，通知/放弃重入及无输出清理失败保留真实资源 | 新增 `p5.publication_abort_notification`、`abort_notification_failure`、`abort_notification_reassign`、`abort_no_output_failure`；与原项联合为 Core 17 项 |
| ready 前完整建页，Held 不可导航/输入；真实 `CordisPageSearch`、Home、中央页签在提交前无候选，提交后原控件可进入，同分类首个页面关闭不影响其他页面 | `p5.ui_publication_pages_held`、`pages_input`、`pages_home`；使用真实 Qt/Ela、Kernel 与 `holdPublication/mountHeld` |
| 放弃、工厂失败、提交后立即停止、登记中重入关闭、Dialog、观察通知丢弃；原 Scope 的候选提交与旧代恢复沿实际准备/发布/释放顺序兑现 | `pages_aborted`、`pages_factory_failure`、`pages_stopped`、`pages_registration_close`、`pages_dialog`、`observer_discarded`、`pages_scope_commit`、`pages_scope_recover`；与上行共 11 项页面/观察组合 |
| 命令行完整准备但候选不可见，enabled/execute 不进入插件谓词；提交复用原行，放弃释放捕获，缓存索引/建议不可绕过门禁 | `commands_held`、`commands_aborted`、`suggestion_gate`；后者保留 click/Return 内部 setText 改查另一页时不发旧选择的回归，共 3 项 |
| 原节点/堆栈的可见索引、父子与稳定 key、紧凑菜单及动画退出均与呈现门禁一致；删除无关页面后合法动画目标仍可见 | `ela_held`、`ela_indices`、`ela_stack`、`ela_compact`、`ela_animation`、`ela_animation_cancel`，共 6 项 |
| 模型 reset 回调同步规范化查询后，Return 使用当前 Beta 结果，不出现 Beta 文本配 Alpha 模型 | 原 `p3.workbench_search_normalization`，保留旧断言；与新增 20 项 UI 组合联合复验 21/21 |

表内页面/命令/Ela 简名统一加 `p5.ui_publication_`；Core 简名加 `p5.publication_`。Core 联合专项在 MSVC/GCC 各 17/17。当前 Release 全目标构建后的完整基线为 1368/1368；随后仅增强 `tests/qt/page_presentation.cpp` 的真实搜索/Home/页签断言，重建对应可执行文件并复验上述 21/21，其他 182 个运行产物 SHA256 未变。Release 由完整基线与受影响重验共同覆盖 1368 个唯一用例，不重复累加，也不宣称最后断言增强后又跑过全量；见 build/p5-m5a1-release-verified.json。

静态 Core（关闭 Ela/示例）全量 974/974、无 Qt GCC 全量 444/444 已通过；Debug/ASan 均在完整构建并更新增强后的 TU 后，顺序完成全量各 1368/1368。39 项源码/构建输入冻结，38 项格式、20 组独立公开头与 5 份原协议检查通过；29 个实际 ASan 对象的全量前后记录及哈希一致。真实旧 SDK 0.1.5 notebook DLL 已在当前宿主执行 normal 通过；该实物兼容证据不扩大为任意旧 C++ 二进制 ABI 承诺。

本段不交付输出/通知缓冲、真实双版本 counter/reporter/notebook 升级、业务名单、H01/H02/S09 全范围或压力/新版桌面验收，亦未宣称整 P5 完成。后续按 [M5 工作单](../../docs/p5-m5-development.md)逐段审计、提交同步后推进。

## M5-a2 输出与通知发布边界

本段将通知存储从命令后端拆出，输出与通知复用有界 pending 值机制，只读原发布组终态；完整发布后锁存为纯历史，不保存 Context、插件对象或 DLL 捕获。Held 值不创建通道、不改变选择或状态栏，也不淘汰已发布历史。实现、阶段审查与五配置验证见 [M5-a2 收口记录](../../docs/p5-m5-development.md#m5-a2-收口结论)，随本次收口归档；提交同步后才进入 M5-b。

下表简名统一加 `p5.ui_publication_`，对应 [正常用例](../qt/value_publication.cpp)九项及 [故障/Scope 组合](../qt/value_publication_faults.cpp)四项，共 13 项真实 Qt、Kernel 和发布组检查。

| 验收条件 | 实际检查 |
|---|---|
| Held 直接输出与通知零可见副作用；纯快照可先结算，已排队观察仍刷新原通道/状态栏，通知镜像恰一次 | `values_held` |
| 两组逆序提交仍保留原服务序号，旧通知不覆盖新状态；严格校验拒绝不占号，Abort 留空洞且不留下历史/镜像 | `values_order` |
| commit 后立即 stop 并释放调用方 Instance/原组句柄，刷新前可结算成功值；停止完成后旧 Context 拒绝而纯历史保留 | `values_stopped`；不据此宣称 observer 前 OS 对象或 Core owner 已析构 |
| 不推进 observer 即关闭外壳，已发布值及通知镜像保留、Open 值丢弃；禁用 QAction 的同步回调中通知/命令立即拒绝 | `values_close`；关闭回归修复前 0/1，前移服务关门后通过 |
| commit→新写→清空→迟到 watch 不复活旧值，序号不重用；清空后才公开的通知仍产生未来镜像 | `values_clear` |
| 输出淘汰当前平台记录触发同步关闭时，已发布批次的直接输出和两条通知镜像完整接纳，窗口最终销毁 | `values_reentry` |
| 两个候选 owner 合计达到条数预算、输出/通知字节预算及输出精确目标预算，拒绝不损当前业务；提交后沿原可见容量淘汰 | `values_count`、`values_bytes`、`values_targets`；通知目标上限由条数同时约束，不另称独立击中该边界 |
| 无 Binding 输出且清理失败时真实 Abort 仍丢弃候选值，保留非静止资源报告；真实 Qt 队列丢弃原观察后开放服务报 SchedulerClosed，关闭后旧纯历史可读 | `values_abort_failure`、`values_discarded` |
| 同一真实 Scope 的页面/命令/直接输出/通知共同隐藏，候选提交与失败后的旧代恢复均在 prepareCommit→commitPrepared→release 屏障后公开，旧资源完整退出 | `values_scope_commit`、`values_scope_recover` |

Output 的每外壳 pending 预算为 1000 条、1 MiB 字符串和 64 个精确目标，Notification 为 100 条、256 KiB 文本和 100 个归属目标，均独立于可见历史。镜像在通知公开接纳时才取得独立输出序号；清空不取消尚 Open 的组或尚未创建的镜像。普通平台日志、已公开命令结果及关闭后纯历史保留，接受序号不等于已经呈现。

当前包含上述 13 项的受影响联合检查为 55/55，不能与全量重复累加。18 项源码/构建输入冻结后，当前源码 MSVC Release/Debug/ASan 全目标构建及全量各 1381/1381、静态 Core（关闭 Ela/示例）974/974、无 Qt GCC 444/444 均通过；本段三项 MSVC 结果都是最终输入的实际全量，不采用 M5-a1 的组合覆盖或历史矩阵。16 项 C/C++ 格式、24 组独立公开头和 5 份原协议检查通过；20 个实际 ASan 对象的插桩/Module 异常模型预检及全量后的哈希一致核对通过，证据归档于 build/p5-m5a2-*。

首次本段重建前保存的实际旧 SDK 0.1.5 notebook DLL（SHA256 `45267e3e698a259daf8e5bdb6187abcdb0f291a91f530cf7f6349d28d902eaf0`）已在当前 Release 宿主执行 normal 通过，嵌入元数据与摘要保持。首次脚本传相对路径被入口拒绝，改绝对路径后成功，两份日志分别保存；兼容结论仅限该实物与场景，不扩大为任意旧 C++ 二进制承诺。Scope 用例证明新贡献完整恢复并可重新进入，不代表自动恢复活动页签、焦点或 Notebook 草稿。

本段不交付真实双版本 counter/reporter/notebook 业务升级、产品升级入口、业务白名单、完整 H01/H02/S09、同一 Windows 进程 100 次真实替换、随机压力或新增原生桌面升级验收。SDK 0.1.5、原业务/状态 C ABI、Qt resident 和默认空名单保持；M5/P5 尚未完成，后续仍须逐段审查、提交同步。

## M5-b1 Native 宿主装配与关闭

本段将原 Native 示例拆为实际装配/异步关闭库和薄运行器；调用方提前共享拥有并注入 QtScheduler，模块门禁弱持原代稳定记录，关闭结果只保留值诊断。实现、独立审查与专项证据见 [b1 收口记录](../../docs/p5-m5b-development.md#b1-收口结论)。

| 验收条件 | 实际检查 |
|---|---|
| 原 CLI 仍验证真实 counter/reporter 业务、多实例/realm、磁盘保存和启动失败；cleanup failure 保留非静止资源 | 原 `p4.native_qt_normal`、`multi`、`realms`、`start_failure`、`cleanup_failure` 五项（简名补 `p4.native_qt_`） |
| 未应用配置的装配可关闭且幂等；错误线程关闭拒绝，原拥有者仍可正常关闭 | `p5.native_pipeline_empty_close`、`wrong_thread` |
| 外部 Runtime 保留使真实 counter drain 拒绝；释放后再 drain 成功，原关闭报告仍保留当时错误，装配不会额外永久 pin | `retained_runtime` |
| 空队列/错误线程及缺 root 的初始化前置失败拒绝，同一真实队列随后排空复用 | `open_failure`；缺 root 不代表逐个中间装配步骤均已故障注入 |
| 实际 QtScheduler.close 丢弃已接受关闭回执，Task 报 SchedulerClosed，未确认的装配/Kernel/Driver/Session 和 ledger 保留 | `discarded_close` |

后四行简名补 `p5.native_pipeline_`，五项直接装配检查见 [实际用例](../qt/native_pipeline.cpp)。最终 MSVC Release、Debug、ASan 及静态 Core 关闭示例的 probe 配置分别通过相同受影响专项 10/10（原五 CLI + 五直接检查），各报告 `whole_stage=false`，不宣称重新运行平台全量。5 项源码/构建输入冻结、4 项 C++ 格式检查及 3 个实际 ASan 对象运行前后插桩/哈希核对通过，证据见 build/p5-m5b1-*；原 M5-a2 全量历史保持，未变 Core/SDK 与无 Qt GCC 不重跑。

本段未增加 SDK 契约、业务 DLL 版本/状态能力或物理卸载名单，不以宿主整理代替升级验收。b1 审计提交同步后才进入 b2 实际 Native A/B；b3 Notebook 草稿与 b4 同平台事务，以及 M5-c/d/e 的产品入口、完整故障/压力和桌面验收仍待推进。

## M5-b2 实际 Native A/B 与状态契约

实现与有限覆盖见 [b2 收口记录](../../docs/p5-m5b-development.md#b2-收口结论)。四个固定资格 DLL 为 counter/reporter 各 1.1.0、1.2.0，临时精确允许集合不修改生产名单；原 1.0.0/resident 保持。

| 用例（前缀 p5.business_state_） | 实际检查 |
|---|---|
| abi_discovery | 六个实际 DLL 的版本/minor/flags/容量，真实来源及映像身份，legacy 无状态扩展 |
| abi_state_schema | 两个 counter A/B 的状态查询/描述和 schema，短结构边界 |
| abi_state_values | canonical uint64、2^53+1/MAX、非法数字/schema 与未知 PluginId |
| abi_state_buffers | 实际可写容量/canary、size=0、空/短/非法 buffer 与未知实例 |
| runtime_a / runtime_b | 实际 Context 导入、Scope 冻结捕获、真实 reporter start/cleanup/重建的精确计数 |
| runtime_boundaries | 独立 realm 的 0/MAX 导入捕获，两个真实实例 |
| runtime_rejections | 实际状态桥拒绝非法数字/schema 后无实例残留、健康会话和完整关闭 |

当前最终源码 Release/Debug/ASan 各受影响专项 30/30（新增八项、原 Native 十项及 Notebook 十二项）；静态 Core 关闭示例 probe 18/18，whole_stage=false。七项输入、五项格式、八个实际 ASan 对象前后核对通过。未重跑未变平台全量；没有完整 A→B 操作、物理卸载/100 次或业务名单资格宣称。b3/b4 和 M5-c/d/e 继续后续验收。

## M5-b3 Notebook 草稿状态与实际版本

实现、协议取舍与准确边界见 [b3 收口记录](../../docs/p5-m5b-development.md#b3-收口结论)。实际 legacy 1.0.0 无状态 IID，A/B=1.1.0/1.2.0 声明原独立 IID；三者均 resident，A/B 每用例独立进程，不绕过同身份代次门禁。

| 用例（前缀 p5.notebook_state_） | 实际出口 |
|---|---|
| protocol_a / protocol_b | 来源/toolchain/标准 root 与纯状态 IID；schema/canonical、UTF-8/Unicode/NUL、容量与失败不部分写 |
| draft_a / draft_b | 导入覆盖配置初值、CRLF 纯状态保留、真实输出、编辑后不泵事件立即捕获、旧 key 与控件完整退出 |
| draft_empty | 空稿导入不退回配置 |
| draft_limits | UTF-8 恰 65536 字节、65537 拒绝且不截断/新 pin，缩短后再捕获成功 |
| draft_multi | 两真实实例草稿/页面/命令/输出隔离，停止一个后另一可用 |
| draft_frozen | 程序信号回滚、真实键盘/按钮拒绝，解除后无迟到输出且可继续编辑 |
| draft_rejections | 非法 schema/数字/重复字段/非规范输入/超限不损原健康实例 |
| draft_start_failure | 导入后实际启动失败完整退出，既有页面/草稿保持 |
| legacy | 原入口无状态能力，普通编辑输出保持，状态输入拒绝 |

当前九项冻结输入，Release/Debug/ASan 各受影响专项 23/23（新增十一项加原十二项），whole_stage=false；八项格式、七个实际 ASan 对象前后核对和九套实际产物/metadata/moc 核对通过。三个 Scope 句柄必须在 close 前析构，最终只保留 qt.loader.root，不能把 release 回执当工厂引用已销毁。未重复未变平台全量；没有同进程 A→B、物理卸载/100 次或新增桌面验收宣称。私有 PageHandle/CommandHandle 不为测试开放，本段以实际 key/撤销记录/QPointer 核退出。

## M5-b4-1 单一三模块 Workspace

实现与关闭语义见 [收口记录](../../docs/p5-m5b4-development.md#b4-1-收口结论)。全部使用同一 Workspace 的实际 Kernel/manager/Driver/Session、Native 两服务及 stateful Notebook，不拼两套示例。

| 用例（前缀 p5.platform_workspace_） | 实际出口 |
|---|---|
| normal | 三个真实安装来源、schema、同平台实例/业务依赖及 Notebook 编辑输出、正常关闭 |
| multi | 真实多实例/second realm 隔离，同 Session/Driver/Core 与页面归属 |
| existing_current | 独立进程直接装配已保存三 B hash 定位，未强制 A 或改写原文件 |
| save_conflict | 关闭保存冲突时完整业务/窗口仍在，rebase 后正常重试 |
| open_failure | 空/关闭/错误线程队列拒绝，前置失败后新窗口销毁、原队列仍可装配正常平台 |
| partial_open_failure | 三 Runtime 实际准备后 schema 拒绝，资源/窗口收尾且原文件不变；不复活已 drain Native |
| retained_runtime | 外部工厂使单个 drain 真实拒绝，Workspace 已释放，解除外部引用后可再 drain |
| discarded_close | Qt 真实丢弃关闭回执，Task 错误且所有实际拥有者/账目保留 |
| cleanup_failure | counter 与 Notebook 的真实 cleanup 失败，仍尝试 reporter，原阶段结果与拥有者保留 |

最终十七项输入冻结，Release/Debug/ASan 各受影响专项 50/50（新九项+原 Native 十八+Qt 二十三），静态 Native probe 18/18；whole_stage=false，不冒充全平台。十五项格式、十四个实际 ASan 对象前后哈希/插桩和三十三份原产物摘要核对通过。两个真实 exe 的三配置共六次错误文件输入收尾检查独立归档，预期退出 1，不计入五十项或原生桌面验收。legacy Pipeline 的故障报告/控制块保留不等于 facade 保活，Workspace 则完整保留 UI 诊断拥有链；具体保留原因和局限见工作单。整体替换、locator/动态管理保护、物理/压力和新增人工验收继续后续。

## M5-b4-2 实际业务整体接线

[收口及证据范围](../../docs/p5-m5b42-development.md#收口结论)使用同一 Workspace 的三个实际 DLL，没有测试导出。十二项前缀 p5.platform_upgrade_：

| 场景 | 实际检查 |
|---|---|
| counter / reporter | 实际 A→B、原消费者重建/提供者借用、计数、规范名称与配置共同提交、保存、旧 Native 映像与独占文件证据 |
| native_roundtrip | 同平台有限 A→B→A、新 GenerationId 与继续计数；不是 100 次 |
| retained_factory | 外借原 Runtime 使 drain 拒绝，业务 Applied 且不 pin 整个平台；释放原借用后可真实回收 |
| notebook | 即时编辑草稿导入、新 widget/命令、旧 key 和控件退出、Native 代次/实例/计数不变 |
| notebook_restore | 同业务源实际 1.3 故障候选在导入后启动失败，原事务恢复旧代/locator/草稿，新 widget 不复活旧页；Candidate 角色按原契约保留 |
| held_protection | 真实 source model 同步信号观测 Held，手动 stop/retry/load 均拒绝，提交后 Current 仍受配置保护 |
| committed_discard | 真实可见命令发布信号后丢弃 Qt 已接受回执，原 committed/Current/locator 不回退，已释放 claims 不补造，保留原执行 |
| admission | 纯预览不安装/执行、错误身份/同产物、伪造/陈旧纯值、错误线程和双击拒绝 |
| stale_revision / changed_source | 修订或源改变在安装前拒绝，store 无候选、旧实例与目录不变 |
| close_inflight | 尚未泵队列的 Prepared 操作真实取消，非 committed/recovered，随后正常关闭；不声称覆盖全部中途关闭点 |

新增 YAML 三项和 provider_home_close/provider_home_replace 两项锁住 locator 保真、Home 回调关闭/销毁重建。最终 Release/Debug/ASan 各 219/219 受影响专项、静态 29/29、GCC YAML 11/11；完整清单与唯一 Passed 名称对应，whole_stage=false。32 输入/30 格式、31 ASan 对象和36实际产物核对通过；原 .5 实物当前 normal 和六次真实入口失败收尾独立归档，不计入 219 或人工桌面验收。

产品升级按钮、完整 H01/S09、同进程 100 次/随机、最终精确批准及新增原生桌面验收继续 M5-c/d/e。Qt 驻留、生产 Native 名单空，不能把本段有限物理证明扩为所有插件可卸载。
