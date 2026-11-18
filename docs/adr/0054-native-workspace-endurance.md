# ADR-0054：同进程真实 Native 连续替换资格

日期：2026-11-19。状态：按实施前设计完成实现、代码审查及三配置原生资格验证，随收口提交归档。源码基线1aa9e79（M5-d0b已审查提交同步）。仅实现M5-d1，随机/回放缩减另属d2。

## 原有职责与验证对象

复用原 Scene、Workspace、ReplacementOperation、ModuleManager、Driver/Session/Store，以及共享的实际 platform_upgrade_support。整个用例仅一个 QApplication/Scene/平台和模块目录；不逐轮重启，不提高64重代次/128纯历史预算，不给业务DLL加测试导出，不复制事务或State RPC。测试范围是固定counter/reporter A/B原产物，Notebook作为不受影响的真实UI贡献。

8次成功预热后再100次成功原事务，固定四相位counter→B、reporter→B、counter→A、reporter→A。共108次Applied，原操作ID必须有效且互异。Counter升级的旧Reporter清理发生于捕获之后，新Reporter启动净增1；Reporter升级在同一Counter上清理/启动净增2。初值1时108次后为163；逐轮用实际Reporter结果验证，不假设每次均加1。

## 拥有者与每轮证据

每轮依次读取原纯预览、执行并等待原报告；核Applied/committed、不恢复、不保留、原Current/Retired、真实Generation/Runtime/Instance/Epoch/Binding、规范名及来源。明确分开“应用”与“保存”：先证明dirty及磁盘仍为旧locator，再调用原显式保存并核磁盘新locator与Store静止。不使用会隐式保存的applied() helper。counterValue()会预约/冻结Core，因此测量内也不用它；实际Reporter业务结果作为逐轮证据，最后独立捕获如需执行必须标为测量外。

真实Notebook实例、epoch、页面和命令保持，逐轮编辑并发布真实草稿。旧Native每轮在下一次升级前立即使用原Image判据证明HMODULE离开、旧MEM_IMAGE范围离开及文件独占访问；复用同一路径的新Generation不能被历史“路径最终都不存在”误判。最终close仍沿原finish语义：Current Native close/drain为Resident，Qt保留loader root，不修改生产退出或清PIN。

## 三个具体测试职责

- 主用例负责动作、等待/收敛、业务/配置/物理断言、预热与固定基线比较、落盘逐轮纯值记录。
- Core/目录采样在原公开快照与实际私有moduleCatalog上复制数值、ID、标签和原因；锁顺序catalog→generation，离锁后返回纯值。不得保存Runtime、Generation、Instance、Scope、Task或回调跨轮，也不创建测试友元或第二目录。真实dependencyEvaluationCount只读查询，采样前后差值为激活依赖评估批次，不是图边访问或队列任务数。
- Windows/Qt采样负责真实windows QPA、本进程有效HWND、当前句柄/GDI/USER及去重后的QApplication/Widget/Window拥有树。只称可遍历拥有树，不声称所有进程QObject。窗口在首次show前设置DontShowOnScreen/ShowWithoutActivating，创建HWND并验证不显示，不抢焦点或剪贴板。

三者使用测试私有纯值结构，避免JSON/平台资源变成拥有者。只在测量开始前创建原生窗口；collector内不调用winId、绘制、插件、processEvents或wait。所有临时计时器、映像/文件证明句柄与目录强引用必须在采样前销毁。实际settle在collector之外完成队列、Store和DeferredDelete收敛。

## 基线和实测口径

预热8轮后按四相位冻结当前资源形状：真实账目分类、origins/admissions/readers/claims、调用/请求/队列及可遍历QObject直方图。Current合法callback/Qt root算基线，不要求全部callback为0；旧Unloaded账目/origins必须为空。InstanceOrigin.operation是历史来源，不能误作活动claim。纯卸载历史可增长，单独记录，不参与固定当前资源数量。

每相位最终预热完成后稳定采样，QObject/业务资源严格比较；Windows当前资源用预热实际观察的固定区间/上界，测量阶段不得临时放宽。若发现尚未预热的合法缓存，先说明原因并重建整次资格基线，不以常量容差掩盖。GDI合法0时按Win32错误码区分，不能为了数字非零造资源。

M5-e2 补充准备契约：初始采样前，用 public QMutex 完成一次真实正超时争用。主线程通过 QMutexLocker 持锁，临时 std::thread 调用 tryLock(1)，join 后验证未获得锁；锁、线程和互斥锁临时对象均在原窗口准备前销毁，worker 异常在 join 后由主线程重抛。它准备已独立证实的 Qt 进程级慢路径缓存，不使用私有布局或资源数字、不增加预热轮或容差，也不宣称旧失败的具体 Event 已完成归因。完整新运行仍沿原 8+100 和全部比较判据。

用steady_clock记录请求→settled、显式save、原FIFO探针排队延迟及可观察阶段区间；清理/停机如只能定位上下界则明确写区间，不能伪称内部精准时长或SLA。所有uint64 ID/计数用无损字符串输出。每轮实时落盘JSONL/最终报告，错误保留原原因及当轮上下文，不能静默跳过。

## 出口

新增原生Windows专用CTest入口并运行时断言QPA=windows，隐藏原生窗口仍须IsWindow/进程归属证明，offscreen不算本资格。Release/Debug/ASan实际运行100次与资源/来源/产物验证后审查提交同步，工作区干净再做d2；最终M5-e全支持矩阵、精确名单和新增桌面人工验收仍待完成。
