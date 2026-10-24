# ADR-0008：基础管理页与只读诊断

日期：2026-10-24。范围：Q3b2 基础管理页；配置计划、保存状态、完整子树、模块引用及原生 Windows 人工交互另验。

## 能力与归属

ElaShell 新增永久管理页、ManagementController 和 DiagnosticsService。宿主以共享 Kernel 绑定控制器，但后端只保存 weak_ptr，不强持 Kernel，不接受重定向到其他内核；同一 Kernel 重复绑定幂等。绑定要求同一 scheduler。Kernel 过期时刷新返回错误并清除可操作列表，禁止使用悬空指针。普通插件只通过 diagnostics/v1 消费只读服务，不获得管理控制器、Kernel、Instance 或 Qt 控件。

DiagnosticsSnapshot 只含值数据：顶层实例、插件名称映射、依赖解析、当前操作结果、服务绑定、页面/命令、通知和管理操作记录。没有插件对象、Task、回调或服务租约；快照副本不延长插件生命周期。首版为全量快照，未实现过滤/分页。子实例目前显示数量，尚未把完整父子实例树展开为管理条目。

## 内核查询

KernelSnapshot.names 只复制 RuntimeId/名称，不导出 Runtime 激活能力。Kernel.findInstance 只在 owner 返回本内核持有的顶层实例，未知或已回收编号拒绝；管理层只在执行入口短暂使用句柄，不在快照中保存。usesScheduler 只比较构造后不变的调度器身份。

Instance.dependencies 复用激活预检的精确查找，返回应用 realm 后的 ServiceKey、minimumMinor、capabilities 和不可用原因。诊断不调用服务、不提交 committed 租约，也不自动重试失败实例；“当前依赖可用”和“实例已经成功激活”分别展示。已有 Kernel.snapshot 仍按契约回收 Disposed 索引，这不等于诊断触发了业务清理。

## 操作协议

ManagementController.stop/retry 在 owner 接受请求，立即按 InstanceId 加 busy 锁并分配本地 request；真正进入内核统一排队，再检查绑定 Kernel 是否存在、是否关闭，以及该编号是否仍归属本内核。双击不能形成同一实例的第二个在途管理请求。按钮按当前快照提示可用性，执行仍以内核的最终校验为准，不能用界面缓存绕过生命周期。

request 是管理请求号；operation 是内核真实 OperationId，尚未派发时保持无效零值，界面显示“尚未进入内核”。重试使用 Instance.retry 返回的新操作并经 awaitTarget 观察真实 StableReport，不重置配置修订，不把 Task 完成直接解释为 Active；Failed、PendingMissingDependency 或 Superseded 都保留真实结果。重试失败复制内核失败原因。停止调用 Instance.stop，等待 CleanupReport；quiescent=false 必须显示错误及 causes，保留资源账目，不开放强制释放或物理卸载按钮。

表格选择按 InstanceId 恢复；实例号只从 selectionModel 的唯一已选行读取，不能把仍保留的当前单元格当作选择。用户取消选择或已选实例移出时清空选择并禁用操作，不自动选中下一条，后续定时刷新也不重新选中。这样连续点击不会在首条停止完成后转向另一个实例。只有首次出现可选实例时才默认选中首条。

管理记录最多 100 条，优先淘汰已完成项；全为在途时拒绝新请求，不丢弃等待中的诊断。实例被 Kernel 回收后，停止结果仍可在“操作记录”查看。已接受派发/完成投递遭阻断时 pending 保留并以原子 deliveryBlocked 标记，不能伪报动作完成。晚到结果只更新宿主值；外壳关闭后禁止再刷新或操作原生窗口。

停止是终结当前 InstanceId，不等价于配置中的临时 disable/enable；重新挂载和配置计划随 Loader/Store 接入，当前管理页不伪造此类操作。

## 视图与关闭

管理页展示顶层实例表、实例详情、服务/界面账目、通知及操作记录。插件名称、错误原因和通知都按纯文本呈现。刷新只读取快照，不执行命令 enabled 谓词、页面工厂或插件服务。每 250 ms 的 QTimer 是宿主持有的刷新工具，属于管理页 QObject 树，不是新的插件资源适配；测试通过显式 refresh/Task/gate 验证，不依赖该定时间隔判断完成。

ElaActionCommander 的公开消息开关在宿主初始化中关闭，避免其内部“堆栈页面切换操作”提示混入业务界面；平台通知仍由 NotificationService 展示，第三方源码未修改。

刷新和详情渲染使用 UI 访问屏障；Qt 模型信号中重入 shell.close 时先关闭管理门禁、停止计时器，再等待当前 UI 写入栈返回，随后才能删除窗口。详情使用独立值副本，避免 setEnabled 等 Qt 调用重入刷新时失效的迭代器。关闭不会强制取消已进入内核的操作；Kernel 仍由宿主按 Kernel.close → shell.close → scheduler.close → Qt quit 顺序管理，Shell.close 只证明 UI 资源已退出。

管理页的程序入口和导航信号共用 selectManagement。外壳另以导航访问计数覆盖切页和选中调用；其同步 Qt 信号中重入关闭并处理延迟删除时，窗口关闭也必须等待导航栈返回。信号返回后重查门禁，已关闭则返回 ContextClosing，不再访问或改变导航选择。

管理页是宿主页，不依赖插件 ui.pages 贡献；部分插件缺服务或激活失败时仍可进入。演示包含运行中的笔记、缺依赖实例及可重试失败实例。当前不表示配置候选差异、持久化、模块代次/驻留原因、完整安全模式或原生 Windows 人工交互已验收。

## 证据

tests/unit/diagnostics.cpp 覆盖无 Qt 名称/归属查询、线程、精确 realm/minor/capability 与不新增租约。tests/qt/management.cpp 覆盖缺依赖恢复、diagnostics/v1 消费、按钮重试和操作号、异步停止、取消选择、跨线程/跨内核、关闭前已排队动作、Kernel 过期、刷新/导航关闭重入、清理失败及投递阻断。工具链与实测结果见 [P3 开发记录](../p3-development.md#q3b2-基础管理页)。offscreen/ASan 不替代原生桌面人工交互、泄漏或物理卸载证明。
