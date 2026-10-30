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

M2–M5 尚待实现和逐项验证：hash 安装事务、同模块/闭包互斥、旧代未清理不接管服务键、新代输出发布、状态导出/迁移/校验、新代激活失败恢复、旧代恢复失败、消费者恢复失败、晚到回调/析构与卸载、白名单实际卸载、正常/故障下连续 100 次 Windows 替换及随机压力。最终必须把操作报告、实际运行和资源/文件/进程证据分别核对。
