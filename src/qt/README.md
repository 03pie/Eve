# Qt adapter

启用 CORDIS_REQUIRE_QT5152 后构建 Cordis::Qt，依赖 Cordis::Core 与 Qt5::Core，并向消费目标传播 QT_NO_KEYWORDS。公开入口为 cordis/qt/scheduler.hpp，头文件自身无需包含 Qt。

QtScheduler 在应用线程创建，任意线程 post 始终排队；由 Qt lifetime 对象持有任务，owner 调用 close 关门并通知未运行任务丢弃。正常退出须先保持事件循环等待 Kernel.close，再关闭 scheduler，最后退出应用。

Q2 已实现 QObjectResource 和 QtClock。前者接管无 parent 对象，在 Qt 延迟删除时机完成整个对象树析构后确认；后者实现 IClock，并等待计时回调载荷及原生对象完整释放。均只在应用线程操作；TimerToken.cancel 可跨线程。释放 facade 不取消已接受的资源工作，正常退出前必须显式等待关闭。

connection、worker 与 watcher 仍待专用适配，QObject 删除屏障不能代替这些资源的退出确认。调度契约见 [ADR-0004](../../docs/adr/0004-qt-scheduler.md)，资源契约见 [ADR-0005](../../docs/adr/0005-qt-resource-barriers.md)，证据见 [P3 开发记录](../../docs/p3-development.md)。
