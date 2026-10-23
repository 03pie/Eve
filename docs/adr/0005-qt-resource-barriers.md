# ADR-0005：Qt 对象与计时资源的完整删除屏障

日期：2026-10-24。状态：Q2 Timer/QObject 已实现并验证；connection、worker、watcher 不在本次交付内。

## 依据与边界

Qt 5.15.2 的 QObject::destroyed 先于子对象析构；deleteLater 只登记延迟删除，依赖事件循环。事件过滤器删除 watched 后必须返回 true，阻止事件继续投递到已删除对象。依据为 [Qt 5.15.2 QObject 源码与随附文档](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qobject.cpp)。不能把信号到达或一次 processEvents 当作整个对象树的销毁屏障。

## QObjectResource

- 应用线程从 `unique_ptr<QObject>&` 接管 parent 为空的对象；构造失败不转移所有权。需要开放且属于应用线程的 QtScheduler。公开头通过前置声明保持无需 Qt include 即可包含。
- 接管后禁止外部 delete/deleteLater、QWidget::WA_DeleteOnClose、moveToThread、setParent、移除守卫或拦截 DeferredDelete；所有子对象遵循同样的线程要求。对象成员及所有关联回调仍须按各自资源契约管理，本适配器不证明任意外部 connection 或 worker 已退出。
- 应用持有独立 ObjectGuard。close 先请求目标 deleteLater，Qt 批准 DeferredDelete 后，守卫过滤该事件并执行目标 delete；完整 delete 调用返回后再完成 Task。守卫不属于目标对象树，因此可在目标及全部子对象析构返回后确认。
- destroyed 只记录 observed，不能完成成功屏障。子对象析构即使嵌套处理 Qt 事件，closed 仍保持 pending。QWidget 使用同样的完整删除边界。
- owner 的 close 幂等；错误线程 close/snapshot 返回 WrongThread，不改变共享清理结果。丢弃最后一个句柄不销毁 Qt 对象，也不取消已有清理；Qt 守卫继续持有控制块。
- 意外外部删除报告 CleanupFailed，不把观察到 destroyed 当作完整受控确认。aboutToQuit 仅请求兜底删除；若应用最终销毁守卫时目标仍未确认，报告失败并保留未能证明释放的对象，禁止伪报静止。正常退出仍必须先等待资源与 Kernel 清理完成。

## QtClock

- 实现 IClock，每项使用应用线程单次 QTimer，延迟支持 0..INT_MAX 毫秒，越界返回 InvalidArgument。Qt 原生 timeout 只登记宿主调度任务，业务 run 始终由 QtScheduler 排队执行，不内联。Qt timer 不承诺跨不同 timer 的全局 FIFO 或精确实时触发。
- 宿主 TimerEntry 分离业务 ScheduledJob 与 QtScheduler 排队载荷。close 可立即取走并释放未进入任务的业务载荷，即使队列里还保留只持宿主记录的通知，也不会留下插件闭包。
- TimerToken.cancel 可跨线程，只改变原子活动位。取消不运行 discarded，也不证明载荷已释放；载荷在到期回收或 close 时释放。pending 统计未回收记录，包括已取消或已到期排队项。
- close 在 owner 关门、停表并丢弃未运行任务；丢弃异常被隔离，通知重入不得改变遍历。已进入的任务继续返回，running 覆盖回调和业务捕获析构。
- QtClock 原生宿主及子 QTimer 通过 QObjectResource 删除；closed 只在原生树完整删除、整批丢弃结束、running 归零后成功。run/discarded 异常记录在 callbackErrors，资源已释放时不冒充仍有资源残留。
- 已接受且未取消任务恰好 run 或 discarded 一次。scheduler 提前关闭导致到期投递失败时通知 discarded；正常关闭时应先等 QtClock.close 和 Kernel.close，再关 scheduler。
- QtClock facade 释放不取消已接受计时任务，不跨线程析构其业务载荷。原生宿主持有控制块，正常退出前必须保留可用于显式关闭的句柄；aboutToQuit 兜底不是正常 Kernel 静止证明。

## 验证

14 个真实 QApplication/offscreen 场景涵盖完整父子析构、析构重入、QWidget、所有权/线程限制、外部销毁失败、实际 timer、observeFor、跨线程取消、已到期排队、异常隔离、执行中关闭、scheduler 提前关闭、最后句柄在回调中释放、退出兜底及 Kernel 清理集成；资源场景另通过第一方 MSVC ASan 插桩检查。结果见 [P3 Q2 记录](../p3-development.md#q2-timerqobject)。不覆盖任意第三方资源、connection 排空、worker join、模块 lease 或物理卸载。
