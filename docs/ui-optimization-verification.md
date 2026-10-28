# UI Ela 基线验证与完成核对

日期：2026-10-27。基线提交：1afd44b，范围为当前未提交的 UI 专项实现、SDK 可选偏好路径、两个桌面示例、测试及文档。UI 组件和字体以用户本轮明确要求为准；设计和盘点见 [组件与字体规范](ui-design-system.md)，工作范围见 [专项计划](ui-optimization-plan.md)。

本记录现归入 [UI-S0](ui-special-stages.md)；用户后续要求的 VS Code 全结构分析、框架/页面增量进入 UI-S1–04，不由本基线通过记录代填。

## 实现对应

| 工作包 | 当前交付 | 验证依据 |
|---|---|---|
| UI-00 | Ela 组件/字体基线、页面盘点、复用接口、偏好与业务配置边界 | ui-design-system.md；第一方可见控件审计；SDK/依赖边界与格式检查 |
| UI-01 | 完整菜单、响应式搜索、可关闭中央页签、空画布、面板图标与轻量状态栏 | workbench 既有行为/布局；optimization showcase 的宽窄几何与菜单检查；长通知不会撑高状态栏 |
| UI-02 | 内容驱动横幅、卡片/身份层级、Ela 状态图标、空状态和键盘启停基础 | workbench cards/power/power_busy/refresh_reentry；optimization states 的空状态、真实状态和卡片身份保持 |
| UI-03 | 手动/配置表格密度、类型化状态图标、完整提示、选择与动作说明 | management 与 configuration_management 既有权限/失败/清理/重入；optimization states |
| UI-04 | 编辑/运行/磁盘三种独立反馈、明确恢复操作、Ela 字体和横向滚动 | configuration_ui feedback；acceptance_partial 同时检查真实部分运行失败、磁盘提交失败与成功恢复后的独立 UI 状态 |
| UI-05 | Ela 设置控件、主题/导航/底部尺寸、有版本独立偏好、恢复默认 | optimization roundtrip/invalid/failure/locked/isolation；无效数据不覆盖、写入失败保留旧文件、下一次变更可重试 |
| UI-06 | Ela 开关键盘与焦点、按钮焦点、图标动作名称、主题刷新及缩放 | optimization keyboard/keyboard_hidpi/reentry/showcase；workbench 搜索方向键/回车/Escape、pages 导航键、ui_services 命令回车；用户系统 150% 确认 |
| UI-07 | 真实窗口、两入口、构建回归、审查、用户体验及边界归档 | 下文构建/原生记录及 [审核记录](code-review.md)；工作区保留可评审差异，尚未提交 |

## 构建与自动化

| 最终范围 | 实测结果 | 日志 |
|---|---|---|
| 原目录 Ela/MSVC Release 全量 | 444/444 | build/ui-special-verified-release.log |
| Ela/MSVC Debug 全量 | 444/444 | build/ui-special-verified-debug.log |
| ASan UI、配置及新增专项 | 104/104 | build/ui-special-verified-asan.log |
| ElaShell 公开头独立包含 | GCC/MSVC C++17 均通过，不需要 Qt/Ela 头路径 | 本会话编译输出；build/ui-special-header.cpp |

上述是最后通知/提示/几何及键盘检查变更后的结果；源文件哈希记录在 build/ui-special-source-sha256.json，后续已重新比对 17 个变更源码/头文件保持一致。预编译 Qt/系统库未插桩，ASan 不作为全程序或 DLL 卸载认证。

新专项检查为 ui.optimization_roundtrip、invalid、failure、isolation、keyboard、states、showcase、reentry、locked、keyboard_hidpi、showcase_hidpi，另新增 p3.configuration_ui_feedback，并增强 p3.acceptance_partial 的真实 UI 结果断言。

## 原生 Windows 复核

当前代理使用 windows 平台运行真实 Qt/Ela 窗口及 Qt 输入事件，不把这些脚本检查写成用户手工操作：

- QT_SCALE_FACTOR=1 和 1.5 分别执行 keyboard、roundtrip、states、showcase，共 8 项，退出码均为 0。
- cordis_config_workbench 和 cordis_ela_demo 分别执行 smoke / smoke-dark，共 4 项，均经过自身完整退出流程且退出码为 0。
- showcase 保存 Home、管理、设置、关于的明暗与进程 150% 对照；已查看代表图像。配置工作台另生成配置/笔记等原生截图。
- 环境采集在上述检查之后、用户补充系统 150% 确认之前：系统 DPI 120（125%），单屏 1920×1080，见 build/ui-special-native/environment.json。

最终基线还在 windows 平台按进程 1 / 1.5 再次执行 keyboard、showcase，四项均退出 0；包括已激活窗口中的 Tab/Shift+Tab 和宽窄工具栏几何检查，日志为 build/ui-special-native/final-*.log。

记录位于 build/ui-special-native/native-*.log，截图位于原 Release 的 bin/ui-special-*.png、config-workbench-*.png 及 ela-*.png。后续若再次运行同名检查会覆盖图像，应结合对应日志和代码版本判断。

上述进程缩放不能证明改变了系统设置。不同硬件/跨屏未执行，当前只有一块已记录屏幕；没有声称完成缺少硬件的跨屏验证。

## 用户体验确认

本次为新版独立确认，未沿用 P3 历史验收。已打开配置工作台（独立 build/ui-special-native/manual.yaml）及手动插件演示，向用户提出完整的 Home、配置操作、主题/布局、键盘开关和正常关闭体验问题。用户明确回复：**“已体验，通过”。**

随后单独询问是否也已将 Windows 系统显示缩放切换到 150%，验证两个入口的文字、卡片、设置和窗口操作。用户明确回复：**“系统 150% 也已验证通过”。**

据此登记本次正常入口体验及系统 150% 验收通过；没有补造逐项点击、第二次分辨率、截图、跨屏或代理亲自更改系统设置的记录。

## 审查与实际限制

审查发现并处理状态图标换肤后旧颜色保留、示例仍等待旧合并状态文案，以及长通知可能挤占工作区的问题。设置变更可重入关闭，已通过窗口访问屏障保护；偏好报告回调复制后调用，避免报告期间关闭销毁原闭包。测试中的 Qt5 QList 比较触发 MSVC 的弃用迭代器警告，改为按索引核对真实控件身份，没有关闭警告即错误。Tab 检查最初在窗口尚未激活时直接发送按键，未建立真实焦点；补齐激活和初始焦点断言后，进程 100%/150% 的 headless 与原生检查通过，没有把测试准备失败误记为产品焦点缺陷。

首轮集成失败保留在 build/ui-special-ui-final.log（旧 smoke 文案）、build/ui-special-closeout-*-build.log（Qt5 比较的编译告警）及 build/ui-special-closeout-targeted.log（焦点准备）。不以早期失败日志代替最终结果，也不将测试适配错误伪报为生产缺陷。

字体当前使用 ElaApplication 设置的 Microsoft YaHei，ElaAwesome 用于图标；配置编辑不再使用第一轮的系统等宽字体。正文和次级文字、主动作色遵循 Ela 主题；颜色检查仅针对使用到的基础组合，不宣称整个外部插件生态通过无障碍认证。

偏好存储不改变业务配置事务及退出结果；关闭时偏好写入失败另记日志，业务保存仍按原 Session/Store 流程处理。尚未实现的 P4/P5 模块加载、ABI、驻留与替换没有被算入 UI 专项。

## 完成审计

UI-S0 的实现、适用自动化、原生窗口和用户确认已有对应证据，当前源快照验证通过。文档、依赖和差异格式按本轮最终审计记录归档。此处是 UI-S0 的历史收口证据；后续分阶段 VS Code 结构改进已通过独立实现/验证和用户新增确认收口，见 [UI-S1–S4 完成审计](ui-structure-verification.md)，没有以本基线通过替代增量完成。

最终审计见 build/ui-special-audit.log：第一方 UTF-8/LF、只读/第三方边界、17 项源快照、文档链接、7 个 Core 编译单元隔离、普通 Ui/SDK 依赖边界、12 个变更编译单元 ASan 插桩及工作包/阶段映射通过。10 组受控文字/主动作颜色组合的最低对比为 4.93；该测量不扩大为全部第三方控件/占位文字/外部插件的认证。未提交增量及基线 hash 归档在 build/ui-special-baseline-review.zip，属于评审快照，不是发布包。
