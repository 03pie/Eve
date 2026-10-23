# ADR-0004：Qt 调度与退出边界

日期：2026-10-24。状态：Q1 调度契约已实现并通过真实 Qt 循环验证；QObject/Timer 资源屏障留待 Q2。

## 依据

现有 IScheduler 要求 post 可跨线程、continuation 不内联，已接受任务恰好执行或丢弃一次。Qt 的 AutoConnection 可在同线程直接调用，因此不能直接作为该契约的实现。[Qt 5.15.2 invokeMethod 官方源码与文档](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qmetaobject.cpp)

Qt 5.15.2 的 execCleanup 在主循环结束后触发 aboutToQuit，并处理 DeferredDelete；因此不能把需要多次普通排队 continuation 的 Kernel 异步关闭寄托于循环已退出后的收尾阶段。[Qt 5.15.2 事件循环源码](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qcoreapplication.cpp)

## 约定

- 新建 cordis_qt/Cordis::Qt 目标，core 保持无 Qt；Qt 消费目标使用 QT_NO_KEYWORDS，避免 SDK emit 名称冲突。
- 初版 owner 为 QCoreApplication 所在线程，也支持 QApplication。构造时验证应用和线程；跨线程仅 post/isOpen/isCurrentThread 及句柄释放可用。指定的 lifetime 须在应用线程且不可 moveToThread；处理事件期间应使用 deleteLater，不同步销毁正在收事件的对象。
- 无论调用方是否 owner，post 均排队。使用宿主控制块登记任务，再以 Qt 排队通知唤醒；用户回调及 discarded 不在内部锁内执行。队列接受与 close 竞争需要单一仲裁点。
- Qt 唤醒对象销毁或应用退出必须关闭入口并处理已接受但未运行的任务。不能仅依赖 Qt 删除一个回调 functor 而丢失 ScheduledJob.discarded；拒绝新任务返回 false，不替调用方伪报任务已经执行。
- 队列由 lifetime 的 QObject 子对象持有，默认 lifetime 为应用；最后一个 QtScheduler 句柄销毁不取消已接受任务，也不在 worker 上销毁 Qt 对象或已接受闭包。显式 close 只关门和丢弃排队项，不销毁 lifetime，也不等待正在执行的任务。
- 使用自定义 QEvent 唤醒宿主 FIFO，每次只执行一项。任务及捕获析构返回前保持 running，嵌套 processEvents 不重入下一项业务；唤醒事件不携带用户闭包。接受、关门和队列转移共用一把锁，执行/丢弃回调与捕获析构在锁外。
- 任务在锁外建立唯一所有权存储，队列在锁内只转移指针。不能假设 std::function 的移动不调用用户代码；MSVC 小对象存储会执行可调用对象的移动和析构。post/close 在进入用户代码前保留本地控制块，允许通知释放 facade 与 lifetime 后继续完成仲裁。
- 诊断区分排队、执行中、已丢弃和关闭；回调异常不得穿过 Qt 的事件处理边界。丢弃通知自身抛错也不能阻止其余任务完成丢弃。
- 正常退出顺序：停止接收新的业务命令，保持事件循环，等待 Kernel.close 及 UI/QObject/worker 资源报告静止，再关闭 scheduler，最后退出 Qt 主循环。aboutToQuit 只作意外退出的同步关门/残留诊断兜底。
- QObject 的析构与 deleteLater 属于实际资源屏障，不能把“已调用 deleteLater”当作“对象已销毁”。不合作任务保守保留资源并报告残留，超时不强制伪造静止。

## Q1 验证要求

使用真实 Qt 循环验证：同线程与跨线程 post 始终排队；已完成 Task 的 thenOn 不内联；FIFO/重入；关闭与投递竞争中执行/丢弃恰好一次；接收对象销毁；异常隔离；丢弃可观察；Kernel 异步关闭在循环停止前完成。不得仅用环境探测代替 QtScheduler 验收。

已由 tests/qt/scheduler.cpp 中 10 个独立 CTest 场景覆盖，包含 worker 释放最后一个句柄、错误线程拒绝、执行中关闭、可调用对象移动/析构重入、通知释放所有者和 aboutToQuit 兜底。具体结果见 [P3 开发记录](../p3-development.md)。这些结果不代表 QObject 资源屏障、Ela UI 或原生模块卸载已验收。
