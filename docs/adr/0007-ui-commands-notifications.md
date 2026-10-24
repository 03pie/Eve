# ADR-0007：命令调用屏障与纯值通知

日期：2026-10-24。范围：Q3b1 命令和通知服务；Q3b2 管理页及原生桌面人工交互另验。

## 服务与归属

新增 CommandService（ui.commands/v1）、NotificationService（ui.notifications/v1）。由宿主 ElaShell 创建并通过现有 Context.provide/consume 接入；普通插件不获得 ElaWindow、QListWidget、QAction 或宿主关闭能力。命令以 InstanceId/Epoch/id 命名，同一实例代次的重复 id 拒绝，不同实例可以使用相同 id。CommandHandle 是本代单条命令的能力，服务没有按裸 id 绕过归属的任意调用入口。

Q3c 将展示列表替换为 ElaListView 与宿主 QStandardItemModel；普通插件仍不获得列表、模型或项指针，调用和清理契约不变，见 [ADR-0009](0009-ela-components-theme.md)。

add/post 校验 owner 线程、有效 Context、同一 scheduler 及开放外壳。Context 的私有 guard 只捕获弱实例与 epoch；停止或服务恢复后旧代不能执行/重新贡献，也不能用旧 Context 提交通知。通知历史只保存复制的归属值，不保存 Context。

注册先保留 Context 值副本；std::function 的移动也可能重入关闭，因此接管回调后必须再次验证宿主与本代门禁，再预留贡献索引。列表项创建/登记在 effect setup 内完成；Qt rowsInserted 信号可重入关闭和事件循环，setup 未返回前不能清理该命令或销毁外壳。effect token 返回后再补发初始化期间收到的宿主关闭请求。

## 命令调用与清理

CommandSpec 包含 id、标题、返回 Task<void> 的 execute 和可选 enabled。CommandHandle.execute 接受后排队，不内联插件代码；真正进入时求值 enabled 并再次检查门禁。enabled=false 返回失败任务，谓词或回调异常转为 CallbackFailed；公共 enabled() 是显式同步求值，同样检查重入关闭。递归求值同一谓词拒绝。snapshot 只复制宿主账目，不求值插件谓词。

每次接受调用即登记保留，覆盖排队、执行、等待源结果和 owner 处理完成。显式谓词求值也占用保留，避免谓词内嵌套 Qt 循环推进清理时销毁正在执行的闭包。activeCalls 统计这些保留，包含调用内额外的谓词求值，不等同于业务任务数。允许多个异步调用并存；配置计划等互斥仍由具体业务实现。

CommandHandle.close 在 owner 同步 revoked；实例停止也由 Context guard 同步阻止新执行。排队但未进入的调用跳过插件代码；已经进入的调用等待源 Task 完成，不强制取消或伪造成功。源结果即使由 worker 完成，也必须经 owner 队列消费后才能释放调用保留。调用不得等待自身命令/实例/外壳关闭，否则会形成用户等待环，当前不检测任意 Task 等待环。

effect 清理先移除宿主列表项，再等待保留数归零，最后释放 execute、enabled 及 guard。捕获析构可以重入，但在析构返回前不报告 drained。贡献索引保留到整个 effect 的 CleanupReport 到达，随后才允许复用 id。宿主列表行只存标题，信号连接只持宿主弱记录；没有每条命令的插件 QObject 回调需要另行删除。

初始投递被拒绝时返回 SchedulerClosed 并释放尚未进入的保留。已接受调用的启动/完成投递遭丢弃时，返回 Task 保持 pending，deliveryBlocked 原子诊断置位，保守保留记录、闭包和作用域；禁止强制清空后宣称静止。这与现有事件、服务异步调用的阻断策略一致。

## 通知与展示

通知只有级别、UTF-8 文本及可选关联 OperationId；记录增加单调序号和 ContextIdentity。空文本、超过 4096 字节、未知级别或序号耗尽均拒绝；失败提交不占序号。历史最多 100 条，按提交顺序淘汰最旧项。历史不是持久化日志，不保存插件对象、回调或服务租约，因此不阻止实例退出；外壳关闭后仍可读取尚存宿主后端的历史。

宿主侧边栏列出命令，双击/回车经同一执行门禁；禁用或失败结果写入宿主错误通知，空错误消息使用宿主缺省文本。底部状态区显示最近通知，固定 Qt::PlainText，避免把插件文本解释为富文本。当前不提供插件自定义通知窗口、通知回调、全局快捷键或完整历史管理界面。

正常退出仍为 Kernel.close → ElaShell.close → scheduler.close → Qt quit。外壳关闭同步关门命令/通知和页面，等待所有命令 effect 以及页面完整清理，再删除窗口。管理页、Loader/Store、connection/worker/watcher、模块 ABI 和物理卸载沿原计划单独实现和验收。

## 证据

tests/qt/ui_services.cpp 使用真实 QApplication、QtScheduler 和固定 Ela DLL，覆盖实例命名空间、异步调用/worker 完成、排队撤销、禁用和异常、谓词关闭重入、提供者恢复、有界纯文本通知、线程/scheduler 门禁、窗口等待、键盘执行及投递阻断。结果及日志见 [P3 开发记录](../p3-development.md#q3b1-命令与通知)。offscreen 检查不替代原生 Windows 人工交互，不构成 Qt 全程序内存安全或插件卸载证明。
