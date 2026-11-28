# 工作台与 UI 贡献

`Eve::Ui` 提供页面、命令、输出和通知的贡献服务。业务贡献绑定实例与激活代的 effect。在替换事务就绪期间保存待发布值，随共同提交公开，撤销时等待界面及捕获完整释放。[页面](../../docs/adr/0006-ela-page-contributions.md) · [命令和通知](../../docs/adr/0007-ui-commands-notifications.md) · [发布协议](../../docs/adr/0051-ui-contribution-publication.md)

`eve_ui.dll` 提供业务插件调用的服务与句柄入口，每个入口调用所属桌面实例的后端。窗口、配置界面、输出与日志归档的实现归项目随附的桌面插件管理。[桌面源码](../../plugins/builtin/desktop-ela) · [装配边界](../../docs/adr/0075-builtin-desktop-source-boundary.md)

`Eve::QtUiPluginSdk` 聚合共享 Core、Qt 及 Ui。插件页面工厂返回 QWidget，宿主负责导航和管理入口。正常退出时宿主等待 Kernel 与窗口关闭，再关闭调度器与 Qt 事件循环。[共享运行时](../../docs/adr/0039-shared-qt-ui-runtime.md) · [SDK 接入](../../docs/sdk-consumer.md)
