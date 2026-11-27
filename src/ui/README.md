# 工作台与 UI 贡献

`Eve::Ui` 提供页面、命令、输出和通知的贡献服务。业务贡献绑定实例与激活代的 effect。在替换事务就绪期间保存待发布值，随共同提交公开，撤销时等待界面及捕获完整释放。[页面](../../docs/adr/0006-ela-page-contributions.md) · [命令和通知](../../docs/adr/0007-ui-commands-notifications.md) · [发布协议](../../docs/adr/0051-ui-contribution-publication.md)

工作台使用 Ela 原生菜单、活动栏和导航、中央页签及底部「输出」「问题」面板。命令由工作台命令入口执行。概览的插件实例卡片可打开管理和配置。设置页管理主题、材质、背景和导航等界面偏好，偏好文件独立于工作区的业务配置。通知中心通过状态栏打开。[导航](../../docs/adr/0010-maintained-ela-navigation.md) · [管理窗口](../../docs/adr/0028-home-management-windows.md) · [日志与通知](../../docs/runtime-logging.md)

`Eve::QtUiPluginSdk` 聚合共享 Core、Qt 及 Ui。插件页面工厂返回 QWidget，宿主负责导航和管理入口。正常退出时宿主等待 Kernel 与窗口关闭，再关闭调度器与 Qt 事件循环。[共享运行时](../../docs/adr/0039-shared-qt-ui-runtime.md) · [SDK 接入](../../docs/sdk-consumer.md)
