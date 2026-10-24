# ADR-0009：Ela 组件与主题统一

日期：2026-10-24。范围：P3 Q3c 宿主与内置演示界面的初次组件迁移。后续将 Ela 基础库纳入仓库维护并采用原生导航与工作台布局，替代本页树导航策略，见 [ADR-0010](0010-maintained-ela-navigation.md)。sources/research 始终只读。

## 组件与数据模型

基础管理页的生命周期和操作功能已在 Q3b2 验证，但此前仅外壳接入 Ela，内部原生控件不构成 Ela 界面适配完成。Q3c 补齐表格 ElaTableView、导航 ElaTreeView、命令 ElaListView、文字 ElaText、按钮 ElaPushButton、详情 ElaPlainTextEdit 和标签 ElaTabWidget。Qt 布局、页面容器、selectionModel 与 QStandardItemModel 保留为基础设施；普通插件页面工厂仍允许返回 QWidget，不强制所有插件依赖 Ela。

三个视图使用宿主持有的 QStandardItemModel。模型以视图为 parent；导航/命令记录只借用模型项，撤销先移除业务索引再经 removeRow 删除项。分类仍按实例/epoch 隔离。导航选中信号使用 QModelIndex 当场解析模型项，不跨任务保存临时索引，不恢复存在 UAF 的 Ela 原生动态导航路径。

管理表格保留 InstanceId 选择语义及取消选择后禁用操作的行为。刷新阻断 selectionModel 信号，避免逐单元格更新触发半成品详情；模型信号仍可触发关闭，原有 UI 访问屏障继续生效。命令列表通过 activated 支持回车/双击，初始化仍在 effect setup 内，不能在插入信号的嵌套循环中提前释放捕获。

## 主题与窗口归属

移除宿主导航固定浅色背景，标题使用 Ela 字体接口，正文统一设置字号。宿主稳定 objectName 会使 Ela 构造时的 ID 样式选择器失效；树、列表和编辑器改用类型选择器保留透明背景。标签容器的 Window/Base/Text 调色板与 ElaTheme 同步，避免深色文字叠在 Qt 默认浅色容器上。颜色取自固定 Ela 主题，不另建硬编码明暗色表。

详情标签固定属于管理页，禁用移动、关闭、接收拖放及拖出独立窗口。仅设置 movable=false 不足以阻止该版本 ElaTabBar 的拖出逻辑，因此在其处理按键拖动前过滤 MouseMove；普通悬停、点击和键盘切换保留。独立窗口并未成为新的未托管资源。

## 验证边界

新增 management_theme 检查实际组件类型、明暗调色板、选择保持、实例操作号/epoch 不因换肤改变及拖动不生成窗口。原选择、动态撤页、命令调用与关闭重入测试迁移至 view/model API 并继续运行。

演示新增 --smoke-dark，与 --smoke 分别生成 ela-smoke-dark.png、ela-smoke.png，再执行完整关闭链。offscreen 预览和自动化回归不替代原生 Windows 缩放、鼠标、键盘与主题动画的人工验收；验收单见 tests/acceptance/p3-ui.md。
