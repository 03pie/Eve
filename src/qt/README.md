# Qt adapter

启用 CORDIS_REQUIRE_QT5152 后构建 Cordis::Qt，依赖 Cordis::Core 与 Qt5::Core，并向消费目标传播 QT_NO_KEYWORDS。公开入口为 cordis/qt/scheduler.hpp，头文件自身无需包含 Qt。

QtScheduler 在应用线程创建，任意线程 post 始终排队；由 Qt lifetime 对象持有任务，owner 调用 close 关门并通知未运行任务丢弃。正常退出须先保持事件循环等待 Kernel.close，再关闭 scheduler，最后退出应用。

Q1 已有真实事件循环测试。Timer、QObject/connection、worker 与 watcher 资源适配尚未实现；资源销毁必须有确认屏障。契约见 [ADR-0004](../../docs/adr/0004-qt-scheduler.md)，证据见 [P3 开发记录](../../docs/p3-development.md)。
