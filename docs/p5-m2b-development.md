# P5-M2b Core 服务发布组

日期：2026-10-30–31（北京时间）。基线 42269bd，M2a 已审查提交并同步后开始本段。完整 M2 仍需上层替换协调与当前代切换，见 [P5 计划](p5-development.md)。

## 架构与实现

先核对服务键保留、声明/实际依赖图、激活与清理、Kernel 归属和 NativeServices 的资源所有权。新增 PublicationGroup / Kernel.mountHeld，在同一注册表内以组视图控制 Held 的可见性，不另建 Registry、realm 或实例机制。私有控制块提取到 service_p.hpp，组状态和提交/放弃放到独立 publication.cpp，Core 没有反向依赖。

组内提供者与消费者都真正激活，只有全部成员就绪才公开输出。子实例继承组且纳入成功条件，已终结或失败成员不能被忽略；完整键的独占、旧 committed 清理与异步调用屏障保持原协议。顶层挂载单独登记规范 Kernel 索引，正常 remount 不将组内子实例误认为顶层归属。原生桥把组内视图保存在每个实例，避免权限随共享模块索引串到其他实例，详见 [ADR-0043](adr/0043-controlled-service-publication.md)。

## 审查与实际回归

由当前代理审核，未使用子代理。新代码复审发现并修复两项 P2：

1. 已公开输出后的通知投递失败被误报为提交成功。新 notification_failure 修复前失败；现在保留 Published + deliveryBlocked 和错误，重复提交仍失败，不伪装成未发布或可自动撤回。通知重入也不能提前报告成功。
2. 通知重入时外部重赋原句柄，后续诊断会写到另一发布组。notification_reassign 修复前 0/1；操作先持有自己的强控制块后通过，也避免句柄被释放时继续访问 this。

另以负向控制取消 Held 视图限制、成员就绪检查，并恢复错误的模块共享视图写入；groups、failure、native_publication_commit 实际失败 0/3。finally 按原字节恢复源码、重建后 15/15。日志为 build/p5-m2b-guards-{red,restore}-{build,tests}.log；通知和归属回归为 notify-*、owner-*。

首轮清理测试误把清理回调开始等同于在途调用排空，已改为分别检查两道真实屏障，未改变原清理顺序。首轮原生场景使用无依赖的托管实例消费未声明服务，被原契约正确拒绝；现用同 DLL 的真实低层外部消费者验证组间可见性，未放开托管输入契约。first-*、review-* 和 boundaries-* 保留这些过程证据。

## 场景与验证

15 项场景含 13 项无 Qt Core 检查与 2 项真实 C DLL 检查：闭包内真实初始化、延迟消费者、组外/组间隔离、失败和已停止成员、旧键冲突、清理失败保留、异步调用与旧绑定清理、子实例继承/归属/重挂载、空组/外来组/线程/移动门禁、依赖环、通知拒绝/重入/重赋句柄，以及同 DLL 的组内实例与手动外部消费者在提交/放弃后的实际调用。

| 最终检查 | 实际结果 | 日志（build/p5-m2b- 前缀） |
|---|---|---|
| 原目录 MSVC Release / Debug / ASan 全量 | 各 769/769 | {release,debug,asan}-{build,tests}.log |
| 无 Qt GCC 全量 | 271/271 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 300/300 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 231/231 + 1/1，含全部 15 项发布检查 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 并重建 | 63/63 P5 检查补验通过 | restore-{configure,build,tests}.log |
| 两编译器独立公开头及真实无 Qt 编译记录 | 均通过 | {msvc,gcc}-header.log、header-commands.json |
| P4 验收清单 | 237 项标签、54 项引用及 1 项支持检查完整；不代替执行 | p4-acceptance.json |
| 构建依赖图 | Core 仅直接依赖 Threads::Threads | dependencies.dot、dependencies.log |

23 项源/构建输入哈希一致，8 条实际 ASan 编译命令与本轮构建对象对应；15 项发布检查和验收引用逐项匹配，编码、修改区域格式、文档引用与只读边界审计通过，汇总为 build/p5-m2b-audit.log。未发现阻止本小阶段提交的问题，按要求随本提交归档并同步后才开始 M2c。

M2c 的模块/闭包逻辑锁、替换操作及定义/目录切换，M3 的迁移/恢复，M4 的白名单卸载，M5 的 UI 贡献发布与真实升级/H01/H02/S09/压力验收均继续；本段没有新增用户原生体验确认。
