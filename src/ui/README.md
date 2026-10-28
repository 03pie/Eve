# UI 贡献与管理

启用 CORDIS_BUILD_ELA_UI 后构建 Cordis::Ui，公开 pages.hpp 不依赖 Ela 类型。UiPagesService 登记 Context.effect，工厂返回 QWidget；PageHandle 支持 ready、navigate、close 与诊断。页面和分类按实例/epoch 隔离，关闭等待 Q2 完整删除和工厂载荷释放。

宿主 ElaShell 使用项目内维护的 ElaWindow 原生页面、展开分组及页脚导航，组件库位于 third_party/ela。延迟滚动、节点选择和动画归属已作本地修正；通用导航门禁接入 Context/epoch，插件不获得 ElaWindow。正常退出先 Kernel.close，再 shell.close，最后停 scheduler 与 Qt。当前决策见 [ADR-0010](../../docs/adr/0010-maintained-ela-navigation.md)。

工作台采用左侧原生活动栏、中央页面页签与页面区、底部命令/输出及状态栏；资源树属于具体插件页。页面页签与动态贡献同步删除，所有页签均可关闭，零页签中央空白；Ctrl+B 和 Ctrl+J 分别切换导航模式、底部面板，分隔条可调整宽高。底部默认收起，通知不会将其打开。面板均由根窗口持有；新增管理/配置窗口也由根窗口或贡献 effect 托管，不允许未托管浮动窗口。

页签右键提供关闭当前/其他/右侧/全部，动作绑定打开菜单时的键及页签代次，关闭后重开的视图不受旧菜单影响。输出页的复制/清空工具复用菜单动作，命令页隐藏输出专用工具。设置页支持中英文关键词过滤、Ctrl+F 聚焦和 Escape 清空，过滤不修改偏好。状态栏铃铛与“转到→通知历史”打开管理通知标签，历史读取独立服务快照，不依赖 Kernel 绑定，也不会主动展开底部。

CommandService（ui.commands/v1）按实例/epoch 登记带可选启用谓词的命令。execute 始终排队，撤销同步关门，effect 等待已接受调用和谓词返回后才释放闭包；worker 完成也需由 owner 消费结果。宿主列表双击或回车执行，插件不直接获得列表或 QAction。

NotificationService（ui.notifications/v1）只保存级别、文本、操作和归属值，最多 100 条、每条 4096 字节，显示为纯文本；历史不持插件对象，也不替代持久化日志。命令/通知门禁、在途调用和投递阻断契约见 [ADR-0007](../../docs/adr/0007-ui-commands-notifications.md)。

基础管理页由宿主 `shell.management()->attach(sharedKernel)` 绑定，后端只弱持 Kernel。ManagementController 排队停止/重试，按实例号防重复请求；DiagnosticsService（diagnostics/v1）仅返回值快照。界面包含顶层实例、精确依赖、资源、服务/界面、通知与操作记录；取消选择或已选实例移出后禁用操作，不把当前单元格当作已选行，也不把连续点击转给另一实例。关闭会停止刷新并等待当前界面写入及管理页导航栈返回，再删除窗口。契约见 [ADR-0008](../../docs/adr/0008-management-diagnostics.md)。

管理表格/命令列表使用 ElaTableView/ElaListView 与宿主模型；文字、按钮、只读详情和标签使用 ElaText、ElaPushButton、ElaPlainTextEdit、ElaTabWidget。背景、字体与容器调色板跟随 Ela 主题；诊断标签不能拖出为独立窗口。组件与主题契约见 [ADR-0009](../../docs/adr/0009-ela-components-theme.md)。Qt 布局及模型继续作为基础设施，普通页面工厂仍返回 QWidget。

UI 专项统一采用 ElaApplication 的 Microsoft YaHei 字体和 ElaAwesome 图标；配置编辑也继承 Ela 字体。第一方 ActionButton / KeyboardToggleSwitch 保留 Ela 绘制，补齐键盘焦点及 Space/Enter；状态图标直接由类型化快照产生并立即跟随主题更新。组件盘点见 [设计规范](../../docs/ui-design-system.md)。

ElaShell 的可选第二参数为 UTF-8 界面偏好文件路径；空路径为不落盘会话模式。偏好 JSON 独立于 Loader/插件 YAML，只保存主题、导航模式/宽度和底部高度，启动底部仍收起。读取校验版本/类型/范围，原子写入失败保留旧文件并提示；两个交互示例显式启用各自本地偏好，smoke 禁用。验证见 [UI 专项记录](../../docs/ui-optimization-verification.md)。

页面内的任意程序信号/worker 不自动成为托管资源；当前输入门禁覆盖所适配的输入事件，其他回调仍须遵循 Context/服务与专用清理契约。原生桌面验收按 [清单](../../tests/acceptance/p3-ui.md) 执行，随后推进配置/持久化/模块管理。页面契约见 [ADR-0006](../../docs/adr/0006-ela-page-contributions.md)，证据见 [P3 记录](../../docs/p3-development.md)。

Home 卡片的“管理”“配置”打开定位到该对象的 ElaDialog，不再将管理列为活动栏/中央页签。顶部“文件”菜单添加已注册配置实例或打开完整配置文件窗口；手动实例未绑定 Session 时不提供配置编辑。PagePresentation::Dialog 复用原页面 effect、输入门禁及 QObjectResource，隐藏不等于停止；退役删除整个窗口和内容。协议与边界见 [ADR-0028](../../docs/adr/0028-home-management-windows.md)，验证见 [Home 管理记录](../../docs/home-management-development.md)。
