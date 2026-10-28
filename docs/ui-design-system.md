# UI 组件、字体与页面盘点

日期：2026-10-27。对应 [UI 专项 UI-00](ui-optimization-plan.md)。用户补充要求：UI 组件及字体统一使用 Ela，完成既定 UI 专项。以下规范约束第一方工作台和内置示例页。

## 组件与字体

| 用途 | 采用的实现 | 说明 |
|---|---|---|
| 窗口、菜单、搜索、导航 | ElaWindow、ElaMenuBar/ElaMenu、ElaSuggestBox、ElaNavigationBar | 工作台分区参考 VS Code，控件与主题来自项目维护的 Ela |
| 页签、状态栏、滚动页 | ElaTabBar/ElaTabWidget、ElaStatusBar、ElaScrollPage | 第一方页签封装限制未托管拖出，保留原有所有权与关闭屏障 |
| 文本和图标 | ElaText、ElaIcon / ElaAwesome | 正文字体由 ElaApplication::init 设置为 Microsoft YaHei；图标字体 ElaAwesome 由 Ela 资源加载 |
| 操作与开关 | ElaPushButton、ElaToolButton、ElaToggleSwitch | ActionButton / KeyboardToggleSwitch 继承 Ela，仅补齐焦点与键盘操作 |
| 设置输入 | ElaLineEdit、ElaComboBox、ElaSpinBox | 设置检索只过滤分组；名称、焦点和数值范围由第一方工作台设置，绘制沿用 Ela |
| 编辑、诊断、表格、树、列表 | ElaPlainTextEdit、ElaTableView、ElaTreeView、ElaListView | 配置编辑也继承 Ela 应用字体，使用横向滚动处理长行 |
| 品牌与卡片 | ElaImageCard、ElaScrollPageArea、ElaFlowLayout | 保留内容驱动高度与自适应换行，插件卡片不承担导航 |
| 布局与数据设施 | QWidget、QLayout、QSplitter、QStackedWidget、Qt model | 不承担替代 Ela 的按钮、编辑器或表格绘制；属于必要 Qt 布局/数据设施 |

不创建独立字体家族或全局覆盖 Ela 的配色。字号按标题/正文/说明层级设置；用户此次明确要求优先于第一轮“配置编辑器使用系统等宽字体”的建议，当前实现已统一回 Ela 字体。外部插件仍可通过公开页面服务贡献 QWidget，不通过修改 SDK 限制任意插件内部实现。

## 第一方复用接口

- appearance_p.hpp：主要按钮色、次级文字色、Ela 按钮键盘焦点、Ela 开关的 Space/Enter 操作及焦点环。对控件的增强保留原生绘制、动画、按下、悬停与禁用行为。
- status_p.hpp：直接由 InstanceState/清理失败事实映射图标与文字。Home、手动实例表和配置表共用映射，不解析中文状态文案。
- preferences_p.hpp / preferences.cpp：独立宿主偏好，版本/字段/范围校验、有界读取和原子保存。250ms 合并用户设置变化，关闭时补齐未保存的界面设置；不保存插件或业务配置。
- fixed_tabs_p.hpp：固定诊断页签继续采用 Ela 外观、禁止拖出独立窗口；HostPageTabs 保留中央可关闭页签的原有生命周期。

正文与说明颜色来自 Ela；次级色与正文色混合以提升小字号对比。主操作仅在一个操作区中突出一项。状态除图标外始终保留文字，换肤时立即更新图标，不依赖稍后的定时刷新。

页签菜单使用 ElaMenu，关闭动作绑定稳定 key 和每次打开的代次；面板复制/清空及通知历史入口使用 ElaToolButton。底部分隔条作为 Qt 布局设施，背景与悬停色显式跟随 Ela 主题，保留原生拖动行为，避免 Windows 原生浅色分隔条进入深色主题。

## 页面与状态盘点

| 页面/区域 | 实现位置 | 本专项处理 | 验证入口 |
|---|---|---|---|
| 框架、菜单与搜索 | src/ui/pages.cpp、workbench.cpp | 完整菜单、随窗口变化的搜索、轻量状态栏/面板入口；保留活动栏、历史、空画布 | workbench 的 history/tabs/search/menus/empty/activation/layout；原生 showcase |
| Home | src/ui/home.cpp | 内容高度品牌区、主次文字、Ela 卡片/状态图标、空状态；单个左右开关、长名称省略与完整提示 | workbench cards/power/power_busy/home_refresh_reentry；optimization states/showcase |
| 手动插件管理 | src/ui/management.cpp | 统一页内留白、表格行高/网格、真实状态图标、完整值提示和动作原因；详情保留独立标签 | management diagnostics/retry/stop/load/deselect/guards/theme/cleanup_failure；optimization states |
| 配置条目管理 | src/ui/management.cpp、configuration_management.cpp | 实际状态图标与期望值分开；配置拥有实例不降级为手动操作 | configuration_management 的 home/disabled/management/stale/unavailable/layout |
| 配置编辑 | src/ui/configuration.cpp | 源文件/运行/结果分区；编辑、运行、磁盘三种反馈；Ela 字体、长路径与恢复操作说明 | configuration_ui workflow/conflict/stale/editor_reentry/feedback/layout；配置工作台 smoke |
| 设置 | src/ui/workbench.cpp | Ela 主题、导航模式/宽度和底部高度；恢复默认；偏好错误提示；底部启动始终收起 | optimization roundtrip/invalid/failure/locked/isolation/keyboard/reentry；workbench layout |
| 关于 | src/ui/workbench.cpp | 统一标题、内容驱动卡片和可读运行环境，保留中央页签归属 | workbench layout；optimization showcase |
| 笔记与内部资源树 | examples/qt/ela_demo.cpp | ElaTreeView/ElaPlainTextEdit 等组件及统一页标题；树仍属于笔记插件 | ela_demo 明暗示例、已有原生模式检查；最终原生体验清单 |
| 命令/输出/通知 | src/ui/pages.cpp、services.cpp | ElaListView/ElaPlainTextEdit/ElaText；默认收起、图标入口/快捷键及原有在途退出 | ui_services interaction/notifications/shutdown；management workbench |

## 状态与恢复规则

运行、等待、加载、清理、失败和停止使用不同 Ela 图标并保留文字。忙碌覆盖图标与权限，不把已启用项画成关闭；清理受阻仍不视为已停止。配置的父项停用、等待父代、无实例和状态不可用单独表达，不编造实例号。

配置页面分别显示编辑是否已应用/检查、实际运行结果及磁盘保存结果。未应用草稿不会被“保存”写盘；外部修改不自动覆盖草稿。保存冲突、采用基线和显式载入仍使用原有会话协议；新反馈通过真实状态而非按钮点击次数生成。

手动操作没有选择或目标不可用时禁用；说明包含操作对象与不可操作原因。新开关可用 Space/Enter 操作，自动重复和禁用输入不重复提交。设置引发主题信号、页面刷新或关闭重入时，由宿主窗口访问屏障保留调用栈。

## 偏好文件

两个交互入口分别使用各自 AppLocalDataLocation 下的 ui-preferences.json；ElaShell 接受可选 UTF-8 路径，默认空路径为不落盘的会话模式。自动 smoke 与新测试均使用空路径或临时目录，不修改本机偏好。

版本 1 仅有 version、theme、navigationMode、navigationWidth、panelHeight 五项。允许 Compact/Maximal，导航宽 180–360，底部高度 120–480；显示时再按窗口剩余空间限幅。无效类型、版本、超限/损坏文件保留默认布局，无用户修改时不覆盖文件。保存失败保留旧文件、在设置页提示，解除故障后的下一次修改可重试；关闭时失败另有日志，不将界面偏好错误当作业务配置保存失败。

恢复只改变外观/布局，不恢复页签、旧 Context/epoch/InstanceId，不创建或启动插件，不自动展开底部。缺失偏好时不预先生成空文件。

## 验证记录边界

本盘点记录代码与已有检查的映射，不单凭文档就登记整个专项完成。当前构建、专项新回归及原生检查结果集中在 [UI 专项验证记录](ui-optimization-verification.md)；最终用户体验确认与系统 DPI/跨屏覆盖分别记录。第一轮证据继续见 [界面审查](ui-design-review.md)。

分阶段结构设计见 [VS Code 对照](vscode-ui-structure.md)，UI-S2/UI-S3 的新增行为对应专项验收 UI-A21–24；不能沿用 UI-S0 的确认代填新增体验。
