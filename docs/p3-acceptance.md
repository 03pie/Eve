# P3 验收映射

最后更新：2026-10-27（北京时间）。Q2g 为 e3491ae，跨组件装配验收为 766db1e；用户确认与最终收口记录经本轮审核提交。

本表把[开发指南](../Cordis-Cpp17-Qt5152-开发指南.md)的 L/Q/U 与[设计说明书](../Cordis插件平台-软件设计说明书.md)的 S05/S08/S10 对应到已存在的检查。自动化证据覆盖内置插件、真实 Kernel、配置文件、Qt 和 Ela；不把自动化结果视为人工体验或动态 DLL 场景均已验收。**P3 已完成。2026-10-27 用户确认 Q4d3 手动验证通过，当前 Release Home 手动路径及原生进程 150% 明暗布局补验通过；完整完成条件见 [收口审计](p3-closeout.md)。**

## 场景证据

下列名称均为 CTest 名称；同格省略的公共前缀只用于缩短表格。

| 场景 | 已有自动化证据 | 适用范围及剩余项 |
|---|---|---|
| L01：语法/schema 错误，运行树保持、错误定位 | p3.acceptance_invalid；p3.config_yaml_parsing / schemas；p3.session_preview；p3.configuration_ui_stale / disk_changes | 新装配验收在已保存的活动父子树上分别拒绝错误 YAML/schema，核对 entry/字段路径、UI 应用禁用、真实实例/epoch/页面/磁盘 hash 均保持 |
| L02：重排、改名、启动中再改配置 | p3.acceptance_identity；p3.kernel_driver_entry_identity / latest / lifecycle / tree_latest | title 配置改名保持 id/InstanceId 并推进 epoch、旧页完整销毁；重排不重建无关实例；A/B/C 最终 desired 与实际均为 C，A/B 终结。修改显式 id 按移除/新增处理，不能等同显示名修改 |
| L03：部分激活失败、保存失败 | p3.acceptance_partial；p3.kernel_driver_retry / tree_failure；p3.session_failure；p3.store_failure / conflict；p3.config_file_commit_failure；p3.store_qt_failure | 新装配验收同时保留已更新的活动项与失败项，以真实 Windows 共享删除限制使后台原子提交失败；旧文件/已保存修订保持，解除故障后可保存目标，执行失败仍如实保留 |
| L04：监听合并、rename、自身写入 | p3.config_io_changes / replace / directory / own / own_revert / late_watch；p3.configuration_ui_disk_changes | 真实文件、原子替换及删除重建、晚到通知和自身版本消重；显式载入不自动应用/覆盖编辑；Q4d3 新体验已获本次用户确认 |
| Q01：晚到结果与循环退出 | p3.acceptance_shutdown；p3.qt_worker_epochs / tls / application_exit / application_quit / typed_epoch；p3.qt_connection_typed_application_quit / prefix_file / prefix_directory；p3.qt_native_watcher_demo | Windows 线程/TLS、值寿命和正常联合 drain；循环先退报告失败并保留未知资源，不将失败终结当作销毁证明；DLL 捕获/删除器驻留留待 P4 |
| U01：当前页/子页删除、重复停用 | p3.acceptance_identity / shutdown；p3.ui_pages_lifetime / shutdown / deferred_removal / animation_cleanup / removed_expanded_group / native_close_reentry；p3.workbench_tab_fallback | 真实 Ela 页、导航与销毁；装配验收检查旧句柄不可导航、所有业务页和配置页完整销毁；先前工作台人工确认保留，本轮没有 UI 改动 |
| S05：A 执行时 B/C 到达 | p3.coordinator_latest；p3.kernel_driver_latest / tree_latest | 通用协议及真实 Kernel 两层证据；最新目标、实际值、实例身份与全部回执均检查 |
| S08：工厂/独立窗口清理 | p3.ui_pages_factories / native_modes / native_close_reentry；p3.management_queued_close / cleanup_failure | 页面、窗口及工厂闭包的内置插件范围；不作为插件 DLL 物理卸载证明 |
| S10：部分缺依赖可管理，退出保持循环至 drain | p3.acceptance_shutdown；p3.management_pending / workbench；p3.configuration_management_unavailable；p3.config_workbench_smoke / smoke-dark；p3.qt_worker_combined；p3.qt_native_watcher_demo | 同一装配中一个活动项和一个缺依赖项，管理页可打开；实际窗口关闭后 worker 未退出时会话仍 pending、窗口/调度器及 I/O 存活，Qt 回调释放 gate 后才完成保存、线程 join、Kernel 和窗口销毁、scheduler 关闭及应用退出 |

## 人工验收

Q3/Q4e1/Q4e2 既有用户确认保留。2026-10-27 用户明确回复“已手动验证通过”，Q4d3 文件监听、显式载入、保存冲突和正常退出已按本次确认登记，验收单不再列为待验。当前 Release Home 手动停止/新身份加载/失败重试及普通/进程 150% 明暗布局另经原生代理复核，退出码均为 0。具体观察见 [收口审计](p3-closeout.md)。

不补造用户逐项操作、分辨率或系统 DPI 明细；进程 150% 不等同于实际改变系统 DPI。此前“尚待用户确认”属于 e3491ae/766db1e 阶段当时状态，此次确认后不再作为 P3 阻塞。其他硬件、系统 DPI/跨屏组合保留后续平台扩展验证。P4/P5 仍未开始。

## 本轮自动化记录

Q2g 新增八项前缀场景、一个原生 watcher 示例和一个配置身份场景；增强既有 latest 对真实最终配置和身份的断言。完整矩阵、专项、编译约束和日志集中记录在 [Q2g 开发记录](p3-development.md#q2g-原生-qt-信号前缀与验收映射)。本表不以测试数量换算完成百分比。

后续新增四项装配验收，直接链接 ConfigUi、ConfigKernel、ConfigIoQt、Qt 和真实 Ela 动态库。Ela Debug/Release 全量各 432/432；新四项 Debug/ASan 各连续 20 次通过。ASan 本轮插桩 Core、Loader、Qt、UI、Ela 及新验收目标，Qt/系统预编译库未插桩；不是全程序或模块卸载认证。GCC 无 Qt 的 210 项基线保持，四项桌面验收不进入无 Qt 构建。详细故障记录和审核见 [装配验收记录](p3-development.md#p3-跨组件装配验收收口)。

## 完成条件核对

| P3 工作包 | 当前交付与证据 | 结论 |
|---|---|---|
| Qt 调度及资源适配 | Cordis::Qt；QtScheduler、QObjectResource、QtClock、QtConnection、QtWorker/QtValueWorker；scheduler/resources/connection/worker 回归与联合退出 | 当前 Windows x64 + Qt 5.15.2/v143 支持矩阵自动化通过 |
| Loader/Store | 解析/schema/树协调、Session、Store、真实文件事务、后台 I/O 和 watcher；L01–L04/S05 及四项装配验收 | 内置插件配置范围自动化通过 |
| UI 服务与 Ela 管理页 | 页面/命令/通知、管理/诊断、Home/配置路由、笔记与配置预览示例；U01/S08/S10 及既有 UI 验证 | 自动化通过，既有用户确认保留；Q4d3 已获本次用户确认；不补造其系统 DPI 明细 |
| 审核与提交 | Q2g 为 e3491ae；装配验收按本轮审核提交，见代码审查记录 | 不以未提交草案或执行意图代替提交证据 |
| 人工体验 | Q4e1/Q4e2 及本次 Q4d3 已获用户确认；Home 手动路径和进程 150% 明暗另经原生代理复核；不补造用户 DPI/跨屏细节 | 当前 P3 验收通过，见收口审计 |

当前没有以“待实现”的 P3 子任务隐藏在测试通过结论中。任意自定义信号类型、多任务 worker、其他操作系统线程后端为后续扩展；ModuleManager、ABI 桥、驻留及受控替换属于 P4/P5，保持未开始。
