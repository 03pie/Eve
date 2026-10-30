# P4-M4c2 配置驱动的业务页面 DLL

日期：2026-10-30。基线 1b5899d。M4c2 已完成实现、审查修复和最终验证，按用户要求随本提交归档并同步。范围：真实业务 UI DLL、Kernel/Session 配置、同插件多实例、换代/启停/失败退出及模块诊断。实施前检查职责、依赖与所有权，沿用 M4c1 的共享 SDK 和现有资源协议；决定见 [ADR-0040](adr/0040-configured-qt-workspace.md)。

## 实现

- examples/qt_workspace 的笔记 DLL 实际提供 Ela 页面、命令和按实例隔离的输出，没有测试专用导出。按钮调用同一 CommandHandle，排队、门禁和执行退出屏障复用已有机制。
- 宿主通过冻结 QtPlugin/PluginModule、KernelReconcileDriver 与 ConfigurationSession 装配；配置文件界面和卡片编辑/添加复用 ConfigUi。宿主配置/诊断 Runtime 同样受保护。
- 模块诊断窗口只读使用现有 ModuleManager/DiagnosticsService 值，显示引用/驻留/错误原因链，不增加 Ui 对 Module 的依赖或卸载入口。
- Session 从冻结定义取得外部模块归属并追加配置实例；保留完整解析、schema、预检、CAS、数字原文和原有 builtin 调用。声明 helper 继续统一生成链接后真实摘要。
- 关闭先保存；冲突时保持 Session 与页面开放。成功后依次停止配置、Kernel、页面/命令及外壳，再释放平台服务/定义。清理失败保留业务对象，Qt root/映像始终驻留。

## 审查与复现

由当前代理审查，未使用子代理。复现并修复：

1. 原添加入口默认 builtin，真实外部 DLL 添加实例被目录校验拒绝。原 10 场景 9/10，失败日志为 build/p4-m4c2-module-add-red-tests.log；修复由 Session 查询冻结定义，避免 UI 猜测模块。
2. 示例初版退出直接关闭 Session，保存冲突后会话也已关闭，无法继续解决冲突。close_conflict 修复前失败，见 build/p4-m4c2-review-red-tests.log；现在保存成功才开始不可逆的退出，冲突后可重基并重试关闭。
3. 示例初版遗漏保护宿主配置/诊断 Runtime，会被普通管理操作选中。guards 增强断言修复前失败，见 build/p4-m4c2-host-guard-red-tests.log；现在与业务 Runtime 一并绑定管理保护。

首轮依赖声明未同步到配置定义、缺少宿主 Ela 链接及 Result 不可赋值引起的编译问题均已按现有接口修正，未放宽编译警告。复查同时将按钮改为经已注册命令执行，避免原生 Qt 闭包直接调用业务对象；超长输出保留真实错误并显示反馈。未修改 Core、Qt 适配器、第三方或只读参考源码。

## 验证

| 范围 | 最终结果 | 证据（build/p4-m4c2- 前缀） |
|---|---|---|
| MSVC Debug/Release/ASan 全量 | 各 706/706 | final-debug.log、final-release.log、final-asan-tests.log |
| 无 Qt GCC Debug 全量 | 258/258 | final-gcc.log |
| 关闭示例，共享 Core | 按名称 P4 236/236，另补标为 P4 的 Kernel 检查 1/1，合计 237 | no-examples-tests.log、label-ON-tests.log |
| 继续关闭共享 Core | P4 176 项和 Session 追加 1 项合计 177/177，另补 Kernel 检查 1/1 | static-tests.log、label-OFF-tests.log |
| 恢复原 Release 的示例/共享运行库 ON | 重建成功，新增联合/Session 检查 13/13 | final-restore-build.log、final-restore-tests.log |
| 源码与链接审计 | 18 项源/构建/配置哈希一致，9 个受影响编译命令有 ASan，3 份 DLL 声明摘要一致 | final-source-sha256.json、final-asan-commands.json、final-audit.log |

补查项 p3.kernel_driver_finalizer_failure 属于 P4 标签，按名称筛选会遗漏；以上明确分列补验，没有将原 236/177 项名称筛选冒记为完整标签范围。源码在全部验证期间哈希一致，Release 最终开关已恢复。实际 DLL 直接导入共享 Core/Ui，Qt 经 Ui 复用；没有把模块宿主或配置执行器链接进业务插件。

12 项 p4.qt_workspace 场景覆盖 normal/multi/update/toggle/add/save/start_failure/cleanup_failure/diagnostics/guards/close_conflict/output_limit；p3.session_module_add 覆盖空目标的外部别名创建、精确归属、大整数、旧修订、重复身份、别名冲突、错误线程和关闭重入。原配置管理的既有检查继续运行，没有调整超时。

## 剩余

本段不代替 M4c3：仍需在真实桌面操作该业务页面，逐项核对 P4 需求和原生验收，再完成 P4 收口。P5 受控替换及物理卸载未开始。新程序入口和持久化行为见 [示例说明](../examples/qt_workspace/README.md)。
