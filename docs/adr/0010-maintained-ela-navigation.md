# ADR-0010：项目内维护 Ela 基础库与原生导航

日期：2026-10-24。决策：按用户授权，将 Ela UI 组件库作为项目内维护的基础设施，可按需求修改通用组件实现。此决策替代 ADR-0006/0009 中暂用宿主树导航、保持外部源码不变的适配策略。

## 来源与维护

third_party/ela 导入上游 aae58f5d0d98e05eabe10c263aa7da5f2491d702 的库目录、图标字体及图片，约 4.5 MB；不导入上游演示工程及其大体积演示资源。UPSTREAM.md 记录来源、版本和本地修改，MIT LICENSE 保留并继续随 DLL 分发。构建直接读取本目录，不联网下载，不自动跟随上游，不修改 sources/research。

本地修正直接进入可评审的源码；不依赖构建时字符串替换。未来升级必须显式比较上游和本地修改，并重新验证工具链及动态生命周期。

## 原生导航与布局

使用 ElaWindow 的 addPageNode、addExpanderNode、addFooterNode、navigation 和 removeNavigationNode，复用官方导航的图标、选择标记、展开分类和紧凑/展开模式。概览、管理页的页面对象由宿主保留，所有页签均可关闭；插件页面按实例/epoch 隔离，同名分类不合并。标题/图标使用本项目内容，保留 Fluent 组件风格。

布局参考 VS Code 的工作台分区：最左侧使用 Ela 紧凑插件导航，点击入口在中央页面区打开；顶部是原生 Ela 菜单、页面搜索及前进/后退，中央保留可关闭/重开页签，底部是可收起命令/输出及状态栏。Ctrl+B 展开/收起 Ela 插件导航，Ctrl+J 切换底部工作区，Ctrl+Shift+P 打开命令。设置/关于页采用 Example 的 ElaScrollPage 和 ElaScrollPageArea 分区，主题和面板开关连接真实工作台状态。

按用户进一步澄清，取消全局“资源管理器”及概览/管理/插件的重复树。树状资源属于具体插件页内部；演示笔记页面自行拥有 ElaTreeView、内部内容堆栈与分隔条，内部树切换不产生宿主路由。切换到管理或设置页时，内部树随插件页一起离开；页签仅在访问时创建，关闭后重开仍复用原 QWidget；全部关闭后显示空画布，停止插件才完整撤销其资源。ElaWindow 的通用 setWorkbenchWidgets 允许可选侧栏为空，不把插件内部结构搬到根窗口。

宿主只保存原生 key 和持久模型索引，不把内部节点或 ElaWindow 交给插件。鼠标与键盘使用同一导航门禁。页面切换动画和独立窗口继续关闭。前进/后退改用每窗口最多 128 个稳定 key 的历史：相同页面不重复，回退后新导航截断前进分支，插件撤销时删除相关历史和搜索建议。原生导航的 IsRouteHistoryEnabled 默认保持上游行为，Cordis 显式关闭全局命令记录，避免跨窗口/插件撤销后的旧对象引用。分组入口配置稳定键默认目标，单击直达上次主动选择的子页，右键保留子页菜单；关闭页签时的自动回落不覆盖插件选择。菜单、搜索、历史均重新经过 Shell 的 key/epoch 门禁；记录原生堆栈最终选择，避免重入切页使页签和历史停留在过时请求。


Home 复用官方示例的 ElaImageCard、ElaFlowLayout、ElaPopularCard 和悬浮详情；品牌背景由 Qt 原生绘制。卡片按插件实例聚合，读取只读诊断并与界面贡献合并；有界面时打开其最近选择，无界面时进入管理。待依赖、失败和已停止实例也有状态展示。卡片和浮层不持有插件工厂或上下文，动态撤销后旧目标仍受门禁保护。空画布不形成伪页面历史，回退可重开最近关闭的页面。

## 通用组件修正

- 延迟滚动使用 QPersistentModelIndex 并在执行时检查有效性，节点删除后不访问旧 internalPointer。
- 删除节点同步清空模型的选中页/选中展开项；动画中的旧节点引用改为 QPointer，节点实际析构后自动失效。
- 紧凑菜单用分组/页面 key 代替节点地址，撤页同步删除动作并释放空菜单；动作触发时重新查找节点。模式切换只缓存展开分组的 key，恢复前检查分组仍在模型中。
- 原生切页/页脚信号可能重入关闭并推进 DeferredDelete；信号返回后检查导航控制块、节点的 QPointer 及模型归属。导航的后续滚动只使用仍有效的持久索引，鼠标释放也检查视图是否仍存活。
- 导航视图显式销毁自己的样式；动画以所属样式/delegate 为 parent，页脚 delegate 由视图拥有，防止窗口销毁后动画继续访问旧 viewport。Debug 崩溃的临时调用栈定位在 ElaNavigationStyle 的 valueChanged 回调，证据留在 build/q3-workbench-trace.log；临时诊断代码未纳入产品构建。
- ElaNavigationBar 提供可选的宿主 navigationGuard，节点切换前校验，拒绝时不切页、不记录路由。此接口只接受 key 和通用谓词，不依赖 Kernel、Instance 或插件类型；宿主以弱控制块实现生命周期门禁。

## 架构不变量

Core 保持无 Qt/Ela；公开 UI SDK 不新增 Ela 类型。Ela 库只负责通用视觉/交互，Shell 负责把插件服务及实例状态接入 UI。普通插件通过页面/命令/通知贡献服务工作，不能操作宿主窗口或原生导航。

ElaWindow 移除节点时仅移除路由和堆栈关系，不承担插件页面的删除证明。贡献撤销先关闭业务入口，选取其他有效已开页或空画布，再移除原生节点；随后解除 parent，由 QObjectResource 确认完整析构后释放工厂和 effect。外壳仍等待管理访问/导航调用栈、页面和命令退出，再销毁窗口。退出顺序仍是 Kernel.close → shell.close → scheduler.close → Qt quit。Ela 全局对象继续按进程生命周期驻留，不开放其 DLL 卸载。

首页定时刷新和菜单控件写入也纳入宿主窗口访问屏障，属性信号重入关闭时先关门并等待当前写入返回，随后才删除根窗口；不将仅保留 C++ 控制块误当作 Qt 对象仍存活的证明。刷新嵌套由外层一次更新收敛，关闭后不再接受新入口。

## 证据与限制

新增 deferred_removal 检查反复导航后立即撤页，并在窗口存活时推进到原生延迟滚动触发；native_modes 检查紧凑/展开模式、页面归属及直接调用 Ela 导航不能绕过已撤销门禁；animation_cleanup 确认关闭后样式及其动画真正销毁；management_workbench 检查中央页签同步、面板隐藏恢复、纯文本输出及撤页后页签同步移除。既有选择、服务恢复、键盘、输入及关闭重入测试继续运行。

提交前审查另增 compact_removal、removed_expanded_group、native_close_reentry，分别检查菜单动作与分组撤销、缓存分组删除后恢复模式、原生切页信号内完整删除窗口。后两项在修复前由 ASan 实际报告 heap-use-after-free，详见 [代码审查记录](../code-review.md)。通知测试同时覆盖输出信号中重入提交的独立返回序号。

新增 workbench 测试覆盖历史分支、视图关闭与状态保留、键盘搜索/撤页、菜单/设置、多窗口隔离、建议点击中删除控件及切页重入；搜索弹层归根窗口拥有，避免被标题栏裁剪。

官方参考：[窗口接口](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetTools/ElaWindow.h)、[官方示例窗口](https://github.com/Liniyous/ElaWidgetTools/blob/main/ElaWidgetToolsExample/mainwindow.cpp)。官网 API 页面此次无法访问，接口核对采用官方 GitHub 源码与本地基线。原生 Windows 的缩放、主题动画及完整交互仍须人工验收；ASan 不替代泄漏或物理卸载证明。
