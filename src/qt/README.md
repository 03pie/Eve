# Qt adapter

启用 CORDIS_REQUIRE_QT5152 后构建 Cordis::Qt，依赖 Cordis::Core 与 Qt5::Core，并向消费目标传播 QT_NO_KEYWORDS。公开入口为 cordis/qt/scheduler.hpp，头文件自身无需包含 Qt。

QtScheduler 在应用线程创建，任意线程 post 始终排队；由 Qt lifetime 对象持有任务，owner 调用 close 关门并通知未运行任务丢弃。正常退出须先保持事件循环等待 Kernel.close，再关闭 scheduler，最后退出应用。

Q2 已实现 QObjectResource 和 QtClock。前者接管无 parent 对象，在 Qt 延迟删除时机完成整个对象树析构后确认；后者实现 IClock，并等待计时回调载荷及原生对象完整释放。均只在应用线程操作；TimerToken.cancel 可跨线程。释放 facade 不取消已接受的资源工作，正常退出前必须显式等待关闭。

Q2c 新增 QtConnection：将信号作为零参数触发通知，按 Context/epoch 登记 effect，业务始终排队到 owner。close 等待托管断连栈、原生调用快照、异步业务和捕获释放，再确认私有 QObject 完整删除；不代替 Kernel 整体关闭。信号参数不复制或转发，调用方负责业务数据的线程安全，见 [ADR-0023](../../docs/adr/0023-managed-qt-connections.md)。headless 示例为 bin/cordis_qt_connection_demo.exe，无需 Ela。

任意插件 worker 仍待专用适配；配置专用后台 I/O/watcher 已在 ConfigIoQt 实现。QObject 删除屏障不能代替连接或线程的退出确认。调度契约见 [ADR-0004](../../docs/adr/0004-qt-scheduler.md)，资源契约见 [ADR-0005](../../docs/adr/0005-qt-resource-barriers.md)，证据见 [P3 开发记录](../../docs/p3-development.md)。

QtConnection 的最小接入形式（在已有插件激活回调中）：

```cpp
auto connection = cordis::qt::QtConnection::observe(
    context, scheduler, timer, &QTimer::timeout,
    [] { return cordis::completedTask(); });
if (!connection.hasValue())
    return cordis::failedTask(connection.error());
return connection.value().ready();
```

句柄可选保留用于主动 close 或诊断；effect 已登记资源清理。实际业务返回的 Task 必须覆盖全部异步工作，不能把依赖本回调结束的整体退出任务作为业务结果返回。
