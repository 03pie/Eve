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

M2 后续及 M3–M5 尚待实现和逐项验证：同模块/闭包互斥、旧代未清理不接管服务键、新代输出发布、状态导出/迁移/校验、新代激活失败恢复、旧代恢复失败、消费者恢复失败、晚到回调/析构与卸载、白名单实际卸载、正常/故障下连续 100 次 Windows 替换及随机压力。最终必须把操作报告、实际运行和资源/文件/进程证据分别核对。
