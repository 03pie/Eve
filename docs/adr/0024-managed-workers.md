# ADR-0024：插件工作线程的原生退出屏障

日期：2026-10-26。状态：已实施，限 Windows/MSVC、一次同步工作、内置插件。

## 背景

Q2c 已覆盖信号入口，但任意插件的后台函数和 C++ thread_local 析构仍须独立退役。工作函数返回不等于线程退出；Qt 5.15.2 Windows 的 wait 在内部 finished 标志已置位时可直接返回，不能单独用它证明原生线程和全部 C++ TLS 已终结。[Qt 固定版本实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/thread/qthread_win.cpp#L574)

## 决定

Cordis::Qt 新增 QtWorker::start。每项工作绑定当前 Context/epoch、同一个 QtScheduler 和一个 effect；先同步登记清理，再创建原生线程。工厂、close、snapshot 仅在 owner 使用，ready/result 为可跨线程观察的 Task；释放 facade 不取消资源，effect 和私有 Qt 宿主继续固定所有权。

工作函数接收 WorkerCancellation，返回 Result<void>，负责完整覆盖本次同步工作。组合令牌可查询所属 effect 的取消及任意线程 requestCancel 发布的本地取消；取消请求不代表函数已经退出。工作只能捕获冻结值或明确线程安全的宿主对象，禁止操作 GUI、普通 Context、默认线程约束服务，禁止等待 owner 的关闭任务。首版不提供 worker Qt 事件循环、队列、可重启线程或类型化数据结果；不得绕过资源再创建未登记的后台活动。

Windows 后端使用 _beginthreadex，owner 私有 Qt 计时器以 WaitForSingleObject(handle, 0) 观察实际线程终结，再关闭原生句柄。它不在 GUI 等工作完成，不使用 terminate、detach 或超时宣称成功。CRT 返回的句柄在调用方关闭前保持有效，线程终结进入 signaled 状态，故用该原生状态作为退出证明。[CRT 接口](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170)、[Windows 线程终结语义](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-thread)

工作函数和异常处理均位于原生线程，异常转为宿主错误值，不保留插件 exception_ptr。业务闭包的最终所有权保留在 owner，必须先确认原生退出及句柄关闭，随后释放闭包和捕获，再通过 QObjectResource 确认私有宿主/计时器完整删除。捕获析构可重入 close 或 Qt 循环，推进保护覆盖整个析构栈。

## 结果与失败边界

正常路径的 result 在上述资源以及所属 effect 的清理账目确认后交付业务结果；原工作失败优先保留错误，成功结果还须重查取消和 Context/epoch 门禁，旧代或已取消工作不能发布成功。物理工作完成时选定其错误/协作取消结果，之后的本地取消不追溯已结束的工作；公开发布前仍需核对 Context。自然完成会自动退役对应 effect，避免一次性任务账目累积；资源关闭后先向同一 owner FIFO 投递宿主退役通知，Core 再排队执行清理；本适配器同步 setup 的确认先于该通知，已登记的 ready 延续先于本次清理窗口运行。正常 result 等本 effect 的清理回执，防止延续被自己的退役窗口误拒绝；门禁只在真正发布时核对，不固定原生完成时其他清理造成的暂态错误。取消位包含后续 effect 退役，不等同于业务结果是否成功。业务失败或协作取消不妨碍已经排空的资源 close 成功；Kernel 整体静止另由 Kernel.close 证明。

正常退出必须保留 Qt 事件循环，先等待 Kernel 和资源，再关闭 scheduler，最后退出应用。scheduler 先关闭时，资源仍可依靠私有 Qt 通知完成原生等待和捕获释放，但不能据此声明 Core 清理账目已排空。退役等待期间保留一个可丢弃的宿主通知，Core 回执交付受阻时将业务任务终结失败；retired 与 retirementBlocked 分别记录 Core 确认和交付受阻。应用先退出、宿主意外销毁或原生等待/句柄关闭失败时报告 CleanupFailed，并固定无法证明静止的控制块、句柄和捕获；退出协议失败可提前拒绝 Task，这种失败不证明原生线程已退出；这属于失败保留，不是后台清理成功或可卸载证明。不合作工作持续未退出时，正常关闭保持 pending。

非 Windows 后端当前拒绝启动，不把 Windows/MSVC 证据外推到其他 Qt/CRT 组合。配置专用 I/O 是独立宿主实现，其既有 Qt 等待证据不能替代此处任意插件 C++ TLS 的原生退役证明。本次不改变该专用执行器，不提供 ModulePin、跨 ABI 或物理 DLL 卸载保证。

成功回执的边界：

| 入口/诊断 | 含义 |
|---|---|
| ready | 本 effect 初始化完成，不代表工作退出 |
| close / snapshot.closed | 原生线程、句柄、捕获和私有宿主已确认退出；失败不作证明 |
| result | 正常路径还等所属 effect 清理账目并重查 Context；业务或协议失败分别报告，不代表 Kernel 整体静止 |
| snapshot.retired / retirementBlocked | 所属 effect 确认 / 退役交付受阻，和物理资源状态分别记录 |

## 验证

审查后 21 项真实线程/Qt 场景覆盖正常完成、本地和所属代取消、不合作函数、TLS 析构阻塞、捕获析构重入、旧代更新、失败/抛错、非 owner 释放 facade、scheduler 提前关闭、线程门禁、激活中停止、初始化嵌套循环/自动退役、ready/result 延续使用 Context、清理重叠/退役投递受阻、worker/connection/clock 联合退出、应用先销毁和事件循环先退出。TLS 场景在真实 thread_local 析构中阻塞，检查函数已返回但 result/close 仍未完成、捕获仍存活；异常退出场景由测试另行等待原生线程，仍检查适配器报告失败及保留资源。

新增 headless worker_demo 展示真实信号触发 Kernel 关闭，确认 worker、连接和 Kernel 各自完成后退出。完整结果见 [Q2d 开发记录](../p3-development.md#q2d-托管工作线程)。
