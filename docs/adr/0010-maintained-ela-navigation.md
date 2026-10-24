# ADR-0010：项目内维护 Ela 基础库与原生导航

日期：2026-10-24。决策：按用户授权，将 Ela UI 组件库作为项目内维护的基础设施，可按需求修改通用组件实现。此决策替代 ADR-0006/0009 中暂用宿主树导航、保持外部源码不变的适配策略。

## 来源与维护

third_party/ela 导入上游 aae58f5d0d98e05eabe10c263aa7da5f2491d702 的库目录、图标字体及图片，约 4.5 MB；不导入上游演示工程及其大体积演示资源。UPSTREAM.md 记录来源、版本和本地修改，MIT LICENSE 保留并继续随 DLL 分发。构建直接读取本目录，不联网下载，不自动跟随上游，不修改 sources/research。

本地修正直接进入可评审的源码；不依赖构建时字符串替换。未来升级必须显式比较上游和本地修改，并重新验证工具链及动态生命周期。

## 原生导航与布局

使用 ElaWindow 的 addPageNode、addExpanderNode、addFooterNode、navigation 和 removeNavigationNode，复用官方导航的图标、选择标记、展开分类和紧凑/展开模式。概览、管理页是永久节点；插件页面按实例/epoch 隔离，同名分类不合并。标题/图标使用本项目内容，保留 Fluent 组件风格。

布局参考 VS Code 的工作台分区：默认以紧凑原生导航作为最左侧活动栏，其后是可调整宽度的资源管理器，中央是页面页签和页面区，底部是命令/输出面板及 ElaStatusBar。ElaWindow 的通用 setWorkbenchWidgets 接入 Qt splitter，不依赖插件模型。资源树和页签由 Shell 以同一原生 key 同步，插件撤销时同步删除；页脚终端按钮、面板按钮和 Ctrl+J 控制面板，侧栏按钮及 Ctrl+B 控制资源管理器。所有面板均归属于根窗口，不创建额外浮动窗口，也未实现工作区布局持久化。

宿主只保存原生 key 和持久模型索引，索引用于程序导航时同步键盘焦点，不把内部节点指针或 ElaWindow 交给插件。原生鼠标入口继续沿组件处理；固定版本未把方向键变化转为导航，宿主将 currentIndex 变化转发到同一原生导航信号。页面切换动画和独立页面窗口继续关闭；路由回退按钮暂不开放，动态撤页的历史路由另验。

## 通用组件修正

- 延迟滚动使用 QPersistentModelIndex 并在执行时检查有效性，节点删除后不访问旧 internalPointer。
- 删除节点同步清空模型的选中页/选中展开项；动画中的旧节点引用改为 QPointer，节点实际析构后自动失效。
- 紧凑菜单用分组/页面 key 代替节点地址，撤页同步删除动作并释放空菜单；动作触发时重新查找节点。模式切换只缓存展开分组的 key，恢复前检查分组仍在模型中。
- 原生切页/页脚信号可能重入关闭并推进 DeferredDelete；信号返回后检查导航控制块、节点的 QPointer 及模型归属。导航的后续滚动只使用仍有效的持久索引，鼠标释放也检查视图是否仍存活。
- 导航视图显式销毁自己的样式；动画以所属样式/delegate 为 parent，页脚 delegate 由视图拥有，防止窗口销毁后动画继续访问旧 viewport。Debug 崩溃的临时调用栈定位在 ElaNavigationStyle 的 valueChanged 回调，证据留在 build/q3-workbench-trace.log；临时诊断代码未纳入产品构建。
- ElaNavigationBar 提供可选的宿主 navigationGuard，节点切换前校验，拒绝时不切页、不记录路由。此接口只接受 key 和通用谓词，不依赖 Kernel、Instance 或插件类型；宿主以弱控制块实现生命周期门禁。

## 架构不变量

Core 保持无 Qt/Ela；公开 UI SDK 不新增 Ela 类型。Ela 库只负责通用视觉/交互，Shell 负责把插件服务及实例状态接入 UI。普通插件通过页面/命令/通知贡献服务工作，不能操作宿主窗口或原生导航。

ElaWindow 移除节点时仅移除路由和堆栈关系，不承担插件页面的删除证明。贡献撤销仍先关闭业务入口、切到安全页、移除节点，随后解除 parent，由 QObjectResource 确认完整析构后释放工厂和 effect。外壳仍等待管理访问/导航调用栈、页面和命令退出，再销毁窗口。退出顺序仍是 Kernel.close → shell.close → scheduler.close → Qt quit。Ela 全局对象继续按进程生命周期驻留，不开放其 DLL 卸载。

## 证据与限制

新增 deferred_removal 检查反复导航后立即撤页，并在窗口存活时推进到原生延迟滚动触发；native_modes 检查紧凑/展开模式、页面归属及直接调用 Ela 导航不能绕过已撤销门禁；animation_cleanup 确认关闭后样式及其动画真正销毁；management_workbench 检查页签/资源树同步、面板隐藏恢复、纯文本输出及撤页后模型同步移除。既有选择、服务恢复、键盘、输入及关闭重入测试继续运行。

提交前审查另增 compact_removal、removed_expanded_group、native_close_reentry，分别检查菜单动作与分组撤销、缓存分组删除后恢复模式、原生切页信号内完整删除窗口。后两项在修复前由 ASan 实际报告 heap-use-after-free，详见 [代码审查记录](../code-review.md)。通知测试同时覆盖输出信号中重入提交的独立返回序号。

官方参考：[窗口接口](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetTools/ElaWindow.h)、[官方示例窗口](https://github.com/Liniyous/ElaWidgetTools/blob/main/ElaWidgetToolsExample/mainwindow.cpp)。官网 API 页面此次无法访问，接口核对采用官方 GitHub 源码与本地基线。原生 Windows 的缩放、主题动画及完整交互仍须人工验收；ASan 不替代泄漏或物理卸载证明。
