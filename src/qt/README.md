# Qt adapter

启用 CORDIS_REQUIRE_QT5152 后构建 Cordis::Qt，依赖 Cordis::Core 与 Qt5::Core，并向消费目标传播 QT_NO_KEYWORDS。公开入口为 cordis/qt/scheduler.hpp，头文件自身无需包含 Qt。

CORDIS_SHARED_CORE=ON 时 Qt 也为共享运行库，避免宿主和插件各自静态注册私有唤醒事件；Debug 使用 cordis_qtd.dll。关闭该开关保留原静态兼容路径。Qt 不依赖 Module/UI，插件通过现有 SDK 目标链接，不能自行复制实现，见 [ADR-0039](../../docs/adr/0039-shared-qt-ui-runtime.md)。

QtScheduler 在应用线程创建，任意线程 post 始终排队；由 Qt lifetime 对象持有任务，owner 调用 close 关门并通知未运行任务丢弃。正常退出须先保持事件循环等待 Kernel.close，再关闭 scheduler，最后退出应用。

Q2 已实现 QObjectResource 和 QtClock。前者接管无 parent 对象，在 Qt 延迟删除时机完成整个对象树析构后确认；后者实现 IClock，并等待计时回调载荷及原生对象完整释放。均只在应用线程操作；TimerToken.cancel 可跨线程。释放 facade 不取消已接受的资源工作，正常退出前必须显式等待关闭。

Q2c 新增 QtConnection：将信号作为零参数触发通知，按 Context/epoch 登记 effect，业务始终排队到 owner。close 等待托管断连栈、原生调用快照、异步业务和捕获释放，再确认私有 QObject 完整删除；不代替 Kernel 整体关闭。信号参数不复制或转发，调用方负责业务数据的线程安全，见 [ADR-0023](../../docs/adr/0023-managed-qt-connections.md)。headless 示例为 bin/cordis_qt_connection_demo.exe，无需 Ela。

Q2f 的 QtConnection::observeValues 支持数值/枚举、std::string、QString、QByteArray；发射时冻结独立数据，owner 接收 const 引用，资源保留数据至回调栈和返回 Task 完成。条数预算覆盖排队/复制中/在途票据，内容字节默认 1 MiB/份，上限 16 MiB。拒绝指针、借用视图和自定义类型；业务不得在 Task 完成后继续借用参数。关闭在 owner 释放数据并等待原有屏障，见 [ADR-0026](../../docs/adr/0026-typed-qt-connections.md)。headless 示例为 bin/cordis_qt_value_connection_demo.exe。

```cpp
auto connection = cordis::qt::QtConnection::observeValues(
    context, scheduler, source, &Source::packet,
    [](const QString& text, const QByteArray& bytes, int number) {
        // 业务在 owner；返回的 Task 必须覆盖参数引用的全部异步使用。
        return processPacket(text, bytes, number);
    });
```

Q2d 新增 QtWorker：在当前代登记一次同步工作，用 WorkerCancellation 查询代次/本地取消；Windows 后端以原生线程信号确认 TLS 退出，再在 owner 释放捕获。正常路径的 result 在全部资源及本 effect 账目确认后交付业务结果，退出协议失败不作静止证明；close 是资源屏障，Kernel 静止另验。见 [ADR-0024](../../docs/adr/0024-managed-workers.md)，自然完成会排队退役 effect，ready 延续先于本次清理窗口运行；资源 closed 和 Core retired 分别记账，退役投递受阻会终结结果失败；headless 示例为 bin/cordis_qt_worker_demo.exe。

Q2e 新增 `cordis/qt/value_worker.hpp` 的 QtValueWorker<T>，工作返回 Result<T>，result() 返回 Task<T>。成功数据沿用上述全部屏障，支持不可复制类型；发布只转发冻结快照。数据快照可以超过 Kernel 的寿命，最终析构线程由最后一个数据持有者决定，不能携带 GUI/Context 借用或模块资源。见 [ADR-0025](../../docs/adr/0025-typed-worker-results.md)，headless 示例为 bin/cordis_qt_value_worker_demo.exe。

```cpp
auto work = cordis::qt::QtValueWorker<std::string>::start(
    context, scheduler,
    [](cordis::qt::WorkerCancellation stop) {
        if (stop.isCancellationRequested())
            return cordis::Result<std::string>::failure(cordis::makeError(
                cordis::ErrorCode::OperationCancelled, "cancelled"));
        return cordis::Result<std::string>::success("computed data");
    });
```

调用方检查 start 的 Result，使用 result().thenOn(scheduler, ...) 观察不可变数据；不通过共享变量提前读取工作结果。

worker 内 Qt 事件循环、多任务队列及其他原生后端仍待专用适配；配置专用后台 I/O/watcher 已在 ConfigIoQt 实现。QObject 删除屏障不能代替连接或线程的退出确认。调度契约见 [ADR-0004](../../docs/adr/0004-qt-scheduler.md)，资源契约见 [ADR-0005](../../docs/adr/0005-qt-resource-barriers.md)，证据见 [P3 开发记录](../../docs/p3-development.md)。

Q2g 的 `observePrefixValues<Values...>` 可显式选择前 N 项，接入带私有尾参数的原生 Qt 信号。选中类型去除 cv/ref 后必须与信号前缀完全相同，沿用白名单、预算及关闭屏障；未选尾参数不访问、不冻结。无需选择数据时继续使用 observe。见 [ADR-0027](../../docs/adr/0027-native-signal-prefix.md)。

```cpp
auto connection = cordis::qt::QtConnection::observePrefixValues<QString>(
    context, scheduler, watcher, &QFileSystemWatcher::fileChanged,
    [](const QString& path) {
        // path 可以借用至本次返回的 Task 完成。
        return processChangedFile(path);
    });
```

`bin/cordis_qt_native_watcher_demo.exe` 在专属临时目录修改真实文件，以原生通知驱动异步回调，等待 watcher、连接、时钟与 Kernel 完整关闭。该示例只展示资源接入；原子替换后的监听恢复由配置专用 ConfigIoQt 实现。

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

QtWorker 的最小接入形式（Windows，已有插件激活回调中）：

```cpp
auto worker = cordis::qt::QtWorker::start(
    context, scheduler,
    [](cordis::qt::WorkerCancellation stop) {
        if (stop.isCancellationRequested())
            return cordis::Result<void>::failure(cordis::makeError(
                cordis::ErrorCode::OperationCancelled, "cancelled"));
        return cordis::Result<void>::success();
    });
if (!worker.hasValue())
    return cordis::failedTask(worker.error());
return worker.value().result();
```

此处激活等待工作真实完成；也可返回 ready 仅等待初始化。长期工作必须主动检查取消，禁止等待 owner 关闭或借用普通 Context/GUI。正常退出保留 Qt 循环，先等待 Kernel，再关闭 scheduler 和应用。
