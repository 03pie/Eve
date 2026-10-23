# ADR-0006：Ela 外壳中的宿主页面贡献

日期：2026-10-24。状态：Q3a 页面贡献已实现；命令、通知、完整管理页及原生桌面人工交互另验。

## 固定依赖与构建

ElaWidgetTools 固定为 `aae58f5d0d98e05eabe10c263aa7da5f2491d702`，归档 SHA256 为 `3f52ac2a47387ebaee0975c72a251795873f927cffee6ba179b17f2d1a2a307a`。只构建库子目录，不执行会 FORCE Qt 路径和安装路径的上游顶层 CMake；Qt 仍精确为 5.15.2。依赖下载、源码、moc/rcc 和二进制均位于 build，不修改 research/sources。上游 MIT 许可证复制到生成 DLL 旁；本项目许可证状态不变。

只在 CORDIS_BUILD_ELA_UI 开启时下载/构建 Ela，新增 ela-msvc-debug/release 预设；普通 Qt 和无 Qt 内核不引入 Ela。Ela 构建为 DLL，使用该版本真实导出宏，不能把其 STATIC 选项当作已验证路径。

## 动态导航的适配边界

初版使用原生 addPageNode/navigation/removeNavigationNode，在快速撤页的演示中真实触发了 heap-use-after-free。ASan 指向 ElaNavigationBarPrivate.cpp:429 的延迟 scrollTo 使用旧 QModelIndex，随后 ElaNavigationModel::parent 访问已释放的 ElaNavigationNode。证据日志：`build/p3-q3-native-navigation-asan-failure.log`；普通 Debug 也复现多个 SegFault，见 `build/p3-q3-native-navigation-failure.log`。相关固定源码为 [延迟滚动](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetTools/private/ElaNavigationBarPrivate.cpp#L420) 和 [节点删除](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetTools/Development/ElaNavigationModel.cpp#L336)。

因此当前使用 ElaWindow 作为外壳，通过公开 addCentralWidget 接入宿主 QTreeWidget/QStackedWidget。动态贡献不调用 Ela 原生导航，避免依赖未知延迟回调排空时长，也无需修改第三方源码。关闭原生导航、页面动画、路由按钮和独立页面窗口。未来若恢复原生导航，必须先修复/升级固定依赖并重验动态删除与历史路由；本次通过不能推广到该路径。

## 所有权与业务门禁

- 普通插件只消费 ui.pages/v1 的 UiPagesService，提供 id、标题、单层分类和页面工厂；不获得 ElaWindow、Qt 导航模型、原生 key 或宿主关闭权限。
- Context.identity 经过现有业务门禁返回 InstanceId/Epoch，只用于命名和诊断，不授予额外能力。页面 id 与分类都按实例/epoch 隔离，同名分类不会跨实例删除。
- add 先验证 Context、同 scheduler 与外壳状态，再预留 id 并建立 Context.effect；先注册 cleanup，再调用一次工厂。工厂返回无 parent、应用线程 QWidget，不启用 WA_DeleteOnClose，不自行销毁或转移页面。
- 页面导航句柄、鼠标及键盘导航共用 owner/epoch 门禁；选中行变化统一触发导航。PageHandle.close 在 owner 同步撤销业务门禁，随后异步清理；快照 retiring 也覆盖已撤销但 cleanup 尚未开始的窗口。输入守卫在鼠标、键盘、输入法、快捷键和触摸事件到达页面对象树前检查业务是否有效；QShortcut 等非 QWidget 接收者沿 QObject parent 判断归属。任意程序信号、worker、拖放/其他未适配事件或外部 queued callback 仍须遵循 Context 与专用适配契约，不声称通用 connection 排空已完成。
- 清理先标记 retiring、将当前页切到永久安全页、删除导航项并移出页面堆栈；只删除空的本实例分类。其他实例的当前页保持不变。页面解除 parent 后，由 QObjectResource 唯一负责完整对象树删除。
- 工厂闭包保留到整个页面析构返回之后；随后完成页面 cleanup Task。贡献记录保留到其 effect 的完整 CleanupReport 到达；外壳必须等待全部 effect，再删除窗口与安全页。清理失败保留记录和闭包，不强制伪报静止。
- 工厂抛错、空返回或初始化中重入关闭均通过 effect 回滚；旧句柄不能在服务恢复后的新 epoch 重新导航。提供者退役由已有 committed 服务依赖触发消费者先清理。

## 进程与退出

Ela 的单例、主题、资源字体和内部全局登记按宿主进程生命周期驻留，不尝试卸载 Ela DLL。正常关闭顺序为 Kernel.close → ElaShell.close → scheduler.close → Qt quit。窗口原生 close 事件先交宿主异步入口，禁止直接退出事件循环。

验证覆盖真实 DLL、offscreen QApplication、当前/非当前页、分类隔离、提供者恢复、工厂失败、初始化重入、错误线程/跨 scheduler、输入门禁和窗口关闭演示。当前不等同于原生 Windows 桌面人工交互、任意插件 DLL 卸载、完整 UI 管理平台或通用 QObject connection/worker 屏障。
