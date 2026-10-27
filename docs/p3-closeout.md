# P3 收口审计

日期：2026-10-27（北京时间）。结论：**P3 已完成，当前为 P3 → P4 交接点；P4/P5 尚未开始。**

范围沿用设计说明书的 P3：Loader/Store、Qt 适配、UI 服务/Ela 管理页、笔记与配置预览，以及 L01–L04/Q01/U01/S05/S08/S10。当前实测支持组合为 Windows x64、Qt 5.15.2 msvc2019_64、MSVC 19.44/v143；Core 另有 GCC 13.1 Debug/Release 证据。动态模块、ABI、驻留与受控替换按原计划属于 P4/P5，不混入本次完成结论。

## 逐项核对

| 完成条件 | 当前权威证据 | 结论 |
|---|---|---|
| Qt 调度与对象/时钟/连接/线程资源 | sdk/include/cordis/qt、src/qt；QtScheduler、QObjectResource、QtClock、QtConnection、QtWorker/QtValueWorker 的真实 Qt 测试及联合退出示例 | 通过；异常退出拒绝静止证明的路径有单独回归 |
| Loader/Store 与配置/文件生命周期 | src/loader；真实 Kernel 驱动、Session、Store、原子事务、后台 I/O、原生 watcher、配置工作台 | 通过；未把目标接受等同于运行或保存成功 |
| UI 服务、Ela 管理页、笔记及预览 | src/ui、examples/qt/config_workbench.cpp、examples/qt/ela_demo.cpp；页面/命令/通知、管理诊断、Home 和配置路由专项 | 通过；此前 Q3/Q4e1/Q4e2 用户确认保留 |
| L01：无效配置不影响活动树、错误定位 | p3.acceptance_invalid，加上解析/schema、会话及编辑失效专项 | 通过；真实父子实例、epoch、页面和磁盘 hash 保持 |
| L02/S05：身份、改名、启动中更新与 supersede | p3.acceptance_identity；p3.kernel_driver_entry_identity / latest / tree_latest；p3.coordinator_latest | 通过；显示名修改保持 id，显式改 id 按移除/新增，A/B/C 最终为 C 且所有回执终结 |
| L03：部分执行及保存失败 | p3.acceptance_partial，加上 retry/tree_failure/Store/file 回归 | 通过；真实 Windows 提交失败，旧文件和已保存修订保持，故障解除后可保存且运行失败仍可见 |
| L04：监听合并、替换、重建和自身保存 | config_io 的 changes/replace/directory/own/own_revert/late_watch；configuration_ui_disk_changes；Q4d3 原生报告及用户确认 | 通过；不自动应用磁盘原文或覆盖未应用编辑 |
| Q01：晚到结果、事件循环退出及完整销毁 | worker/connection/resources/scheduler 专项；p3.acceptance_shutdown、native watcher 联合退出 | 通过；正常关闭等待真实资源；错误退出不伪报可卸载 |
| U01/S08：页面/子页/工厂与独立窗口清理 | ui_pages 的 lifetime/shutdown/factories/native_modes/native_close_reentry 等；workbench_tab_fallback；装配 identity/shutdown | 通过；旧页不可重新进入，工厂与页面完整退出 |
| S10：缺依赖可管理、循环保持至 drain | p3.acceptance_shutdown；management_pending/workbench；配置工作台明暗 smoke | 通过；实际窗口关闭后 worker gate 未释放时窗口/调度器/I/O 仍存活，排空后应用退出 |
| 本轮审核和提交 | e3491ae：信号前缀；766db1e：四项装配验收；代码审查记录；本收口文档另经审核提交 | 通过；不是未提交草案 |
| Q4d3 用户验收 | 本次用户明确回复“已手动验证通过”，对应文件监听、显式载入、保存冲突和正常退出 | 已登记通过；不补造逐项操作、分辨率或系统 DPI 明细 |

详细 CTest 对应见 [P3 验收映射](p3-acceptance.md)，其 61 个测试引用已对照当前 CTest 清单检查。设计/指南中的历史“待验”是研究文档当时状态，当前实测结论以本审计、开发进度及验收记录为准。

## 最终验证与原生补充

Ela/MSVC Debug、Release 全量各 **432/432**；无 Qt GCC Debug、Release 各 **210/210**。四项装配检查 Debug/ASan 各连续 20 次通过；Q2g 的 connection/worker/快照/驱动 ASan 专项 109/109，十一项前缀/原生 watcher/身份/最新目标 Debug/ASan 各连续 20 次通过。日志和插桩边界见 [开发记录](p3-development.md#p3-跨组件装配验收收口)及 [审核记录](code-review.md)。没有将预编译 Qt/系统库视为已插桩，也不作全程序无泄漏或 DLL 卸载认证。

在 766db1e 后对当前 Release 原生 cordis_ela_demo 补做 Home 路径复核：

- 笔记实例 #1 运行中 → 停止；卡片显示已停止，笔记贡献消失。
- 再加载成为运行中的新实例 #5，卡片总数保持 4，没有重复旧实例；页面留在 Home。
- 失败实例 #4 通过卡片开关重试后运行中；等待依赖实例继续显示等待，不妨碍操作。
- 普通进程与 QT_SCALE_FACTOR=1.5 原生窗口的明暗主题均可读，卡片、开关、文字未重叠。
- 两次均点击真实窗口关闭；同步启动进程实际退出码为 0，stderr 为空，随后无演示/工作台/验收进程残留。

该补充由 computer-use 原生窗口输入、UIA 和截图完成，属于代理复核，不写成用户亲自执行 Home 逐项操作。证据为本会话原生状态、build/p3-final-home[-150]-exit.txt（均为 0）及对应 stdout/stderr 日志。进程 150% 与系统 DPI/跨屏切换分别说明，不声称执行了未记录的系统设置变更；后续不同硬件/跨屏矩阵仍属平台扩展验证。

## 审计结论

P3 的交付、命名场景、自动化、原生复核、用户确认和阶段提交已有对应证据，没有剩余的 P3 实现或验收阻塞。收口没有修改产品代码，未触及 sources/、research/、第三方源码或用户配置。后续开发阶段为 P4 ModuleManager、ABI 桥与模块驻留账目；本目标到 P3 完成为止。
