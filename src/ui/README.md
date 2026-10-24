# UI 贡献与管理

启用 CORDIS_BUILD_ELA_UI 后构建 Cordis::Ui，公开 pages.hpp 不依赖 Ela 类型。UiPagesService 登记 Context.effect，工厂返回 QWidget；PageHandle 支持 ready、navigate、close 与诊断。页面和分类按实例/epoch 隔离，关闭等待 Q2 完整删除和工厂载荷释放。

宿主 ElaShell 使用项目内维护的 ElaWindow 原生页面、展开分组及页脚导航，组件库位于 third_party/ela。延迟滚动、节点选择和动画归属已作本地修正；通用导航门禁接入 Context/epoch，插件不获得 ElaWindow。正常退出先 Kernel.close，再 shell.close，最后停 scheduler 与 Qt。当前决策见 [ADR-0010](../../docs/adr/0010-maintained-ela-navigation.md)。

工作台采用左侧原生活动栏、资源管理器、中央页面页签与页面区、底部命令/输出及状态栏。资源树和页面页签与动态贡献同步删除；Ctrl+B 和 Ctrl+J 分别隐藏/恢复侧栏、底部面板，分隔条可调整宽高。面板均由根窗口持有，不建立额外浮动窗口。

CommandService（ui.commands/v1）按实例/epoch 登记带可选启用谓词的命令。execute 始终排队，撤销同步关门，effect 等待已接受调用和谓词返回后才释放闭包；worker 完成也需由 owner 消费结果。宿主列表双击或回车执行，插件不直接获得列表或 QAction。

NotificationService（ui.notifications/v1）只保存级别、文本、操作和归属值，最多 100 条、每条 4096 字节，显示为纯文本；历史不持插件对象，也不替代持久化日志。命令/通知门禁、在途调用和投递阻断契约见 [ADR-0007](../../docs/adr/0007-ui-commands-notifications.md)。

基础管理页由宿主 `shell.management()->attach(sharedKernel)` 绑定，后端只弱持 Kernel。ManagementController 排队停止/重试，按实例号防重复请求；DiagnosticsService（diagnostics/v1）仅返回值快照。界面包含顶层实例、精确依赖、资源、服务/界面、通知与操作记录；取消选择或已选实例移出后禁用操作，不把当前单元格当作已选行，也不把连续点击转给另一实例。关闭会停止刷新并等待当前界面写入及管理页导航栈返回，再删除窗口。契约见 [ADR-0008](../../docs/adr/0008-management-diagnostics.md)。

管理表格/命令列表使用 ElaTableView/ElaListView 与宿主模型；文字、按钮、只读详情和标签使用 ElaText、ElaPushButton、ElaPlainTextEdit、ElaTabWidget。背景、字体与容器调色板跟随 Ela 主题；诊断标签不能拖出为独立窗口。组件与主题契约见 [ADR-0009](../../docs/adr/0009-ela-components-theme.md)。Qt 布局及模型继续作为基础设施，普通页面工厂仍返回 QWidget。

页面内的任意程序信号/worker 不自动成为托管资源；当前输入门禁覆盖所适配的输入事件，其他回调仍须遵循 Context/服务与专用清理契约。原生桌面验收按 [清单](../../tests/acceptance/p3-ui.md) 执行，随后推进配置/持久化/模块管理。页面契约见 [ADR-0006](../../docs/adr/0006-ela-page-contributions.md)，证据见 [P3 记录](../../docs/p3-development.md)。
