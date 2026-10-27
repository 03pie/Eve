# P3 验收映射

最后更新：2026-10-27（北京时间）。Q2g 基线提交 179aca9；增量随本阶段代码审核提交归档。

本表把[开发指南](../Cordis-Cpp17-Qt5152-开发指南.md)的 L/Q/U 与[设计说明书](../Cordis插件平台-软件设计说明书.md)的 S05/S08/S10 对应到已存在的检查。自动化证据覆盖内置插件、真实 Kernel、配置文件、Qt 和 Ela；不把多项单测组合视为所有人工体验或动态 DLL 场景均已验收。**P3 仍处于集成收口。**

## 场景证据

下列名称均为 CTest 名称；同格省略的公共前缀只用于缩短表格。

| 场景 | 已有自动化证据 | 适用范围及剩余项 |
|---|---|---|
| L01：语法/schema 错误，运行树保持、错误定位 | p3.config_yaml_parsing / schemas；p3.session_preview；p3.configuration_ui_stale / disk_changes | 解析/schema 定位、拒绝候选和 UI 不接受无效编辑分别验证；完整用户操作体验参见下方人工待验 |
| L02：重排、改名、启动中再改配置 | p3.kernel_driver_entry_identity / latest / lifecycle / tree_latest | 重排保持 InstanceId/epoch；A/B/C 最终 desired 与实际配置均为 C、A/B 终结、同 id 推进 epoch。当前 id 是 EntryPath 身份，修改 id 按移除/新增；没有独立显示名字段，不能将改 id 记为保留身份的改名 |
| L03：部分激活失败、保存失败 | p3.kernel_driver_retry / tree_failure；p3.session_failure；p3.store_failure / conflict；p3.config_file_commit_failure；p3.store_qt_failure | 真实内核失败和重试、期望/运行/保存结果分离；写盘失败不伪报成功 |
| L04：监听合并、rename、自身写入 | p3.config_io_changes / replace / directory / own / own_revert / late_watch；p3.configuration_ui_disk_changes | 真实文件、原子替换及删除重建、晚到通知和自身版本消重；显式载入不自动应用/覆盖编辑；新增原生体验仍待用户确认 |
| Q01：晚到结果与循环退出 | p3.qt_worker_epochs / tls / application_exit / application_quit / typed_epoch；p3.qt_connection_typed_application_quit / prefix_file / prefix_directory；p3.qt_native_watcher_demo | Windows 线程/TLS、值寿命和正常联合 drain；循环先退报告失败并保留未知资源，不将失败终结当作销毁证明；DLL 捕获/删除器驻留留待 P4 |
| U01：当前页/子页删除、重复停用 | p3.ui_pages_lifetime / shutdown / deferred_removal / animation_cleanup / removed_expanded_group / native_close_reentry；p3.workbench_tab_fallback | 真实 Ela 页、导航与销毁；先前工作台人工确认保留，本轮没有 UI 改动 |
| S05：A 执行时 B/C 到达 | p3.coordinator_latest；p3.kernel_driver_latest / tree_latest | 通用协议及真实 Kernel 两层证据；最新目标、实际值、实例身份与全部回执均检查 |
| S08：工厂/独立窗口清理 | p3.ui_pages_factories / native_modes / native_close_reentry；p3.management_queued_close / cleanup_failure | 页面、窗口及工厂闭包的内置插件范围；不作为插件 DLL 物理卸载证明 |
| S10：部分缺依赖可管理，退出保持循环至 drain | p3.management_pending / workbench；p3.configuration_management_unavailable；p3.config_workbench_smoke / smoke-dark；p3.qt_worker_combined；p3.qt_native_watcher_demo | 缺依赖管理和退出由不同集成检查验证；原生工作台进程退出码 0 的代理复核另有记录 |

## 人工待验

Q4e1 配置页、Q4e2 Home/管理路由及此前工作台的用户确认保留。Q4d3 文件变化/冲突恢复/退出已经完成[原生代理复核](q4d3-desktop-verification.md)，**用户人工确认仍待登记**。代理使用进程缩放 150%，不能代替真实系统 DPI/跨屏检查。

因此尚不登记“所有 L/Q/U/S 场景最终通过”，也不启动 P4/P5 的实现。下一次人工确认应聚焦 Q4d3 新体验：外部改写/原子替换/删除重建、自身保存消重、未应用编辑保护、显式载入和冲突重试、正常退出及真实 DPI。复核操作与历史证据见上述报告。

## 本轮自动化记录

Q2g 新增八项前缀场景、一个原生 watcher 示例和一个配置身份场景；增强既有 latest 对真实最终配置和身份的断言。完整矩阵、专项、编译约束和日志集中记录在 [Q2g 开发记录](p3-development.md#q2g-原生-qt-信号前缀与验收映射)。本表不以测试数量换算完成百分比。
