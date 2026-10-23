# 提交前代码审查记录

## P3 Q2 提交前审核

日期：2026-10-24。范围：ce43cc6 之后全部未提交的 QObjectResource、QtClock、SDK、资源测试、构建登记与文档，包括新增文件。sources/research 未改动。

- 测试正确性：clock_close 原先在预期抛错的 discarded 回调内使用 CHECK。断言失败同样被隔离并计入 callbackErrors，可能被预期异常数量掩盖。现在回调只记录“重入 close 仍 pending”和“新计时被拒绝”的观察值，回调外独立断言，保留有意抛错以验证异常隔离。没有把这项测试缺陷记作生产关闭逻辑 bug。
- 回调所有权：waitTask 的通知标志改为共享值捕获，避免等待超时后栈已展开而排队通知仍访问局部变量。新增 p3.qt_resources_close_releases_handles，覆盖丢弃回调释放最后一个 QtClock facade 后继续完成整批通知，以及子对象析构释放最后一个 QObjectResource facade 后仍保持完整删除屏障。
- 规范与质量：补全直接使用的 memory/utility 标准头，拆开计时账目初始化；公共构造契约明确只有前置条件错误抛 invalid_argument，其他构造失败仍不转移所有权，并与 ADR 一致列出外部 deleteLater/WA_DeleteOnClose 限制。核对 owner 入口、令牌原子取消、已到期队列载荷、析构重入、异常隔离、退出兜底、Kernel 清理集成与无 Qt 构建边界。

Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 140/140；无 Qt GCC 13.1 Debug/Release 各 115/115。两份新公共头独立包含及 git diff --check 通过。完整日志：`build/review-q2-qt-msvc-<mode>.log`、`build/review-q2-core-gcc-<mode>.log`。

另以 MSVC /fsanitize=address、RelWithDebInfo、/MD、/O1 和 /EHsc 单独构建第一方 core/qt 与资源测试，14/14 资源场景通过，日志 `build/review-q2-asan.log`，重现命令见 [P3 Q2 记录](p3-development.md#q2-提交前审查)。Qt 5.15.2 预编译库未插桩，该结果不构成全程序内存安全或泄漏证明；未运行 TSan、connection 排空、worker join、Ela UI、ABI 或物理卸载验证。未发现其他阻止提交的问题。

## P3 Q0/Q1 提交前审核

日期：2026-10-24。范围：2e6efa2 之后全部未提交的 Qt 工具链配置、QtScheduler、SDK、测试与文档；并核对 FakeScheduler 的同类队列操作。sources/research 未改动。

- 正确性：QtScheduler 和 FakeScheduler 原先在队列锁内移动 ScheduledJob。MSVC std::function 的小对象存储会调用用户可调用对象的移动构造及移动后析构；若这些函数查询 isOpen，则重入同一把锁。现在在锁外创建 unique_ptr<ScheduledJob>，队列内只移动指针，执行和丢弃路径也不在锁内销毁用户对象。新增 p3.qt_scheduler_callable_reentry 与 w1.scheduler_callable_reentry，在修复前 MSVC Debug 两项均真实失败，见 `build/review-p3-before.log`；修复后均通过。
- 重入边界：FakeScheduler::runOne 在任务捕获析构前清除了 pumping，捕获的析构函数可提前运行后续任务。现在由后析构的 PumpGuard 恢复门禁，覆盖成功及异常返回。新增 w1.scheduler_destructor_guard；修复前 GCC Release 明确违反“后项仍排队”断言，见 `build/review-p3-guard-before.log`，修复后两个分支均通过。
- 所有权：QtScheduler 的 post/close 在进入用户代码前持有本地控制块，避免移动/丢弃回调释放 facade 与 Qt lifetime 后继续访问已释放控制块。新增 p3.qt_scheduler_close_releases_owner，验证丢弃回调释放全部所有者后抛错，仍完成后续通知；该场景为生命周期防护，未声称完成消毒器验证。
- 规范和质量：新增文件、类型及具名函数使用中文 Doxygen；重入探针由 Qt/Core 测试共用，避免只依赖编译器恰好使用堆存储。核对 owner 限制、锁外析构、关闭/投递竞争、异常隔离、账目与队列一致性，以及 CMake 可选目标、QT_NO_KEYWORDS 传播和开发脚本环境恢复。同步 ADR、接口契约和进度，保留初次验证与本轮审核的独立证据。

验证：Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 126/126；无 Qt GCC 13.1 Debug/Release 各 115/115。日志为 `build/review-p3-qt-msvc-<mode>.log`、`build/review-p3-core-gcc-<mode>.log`；QtScheduler/FakeScheduler 公共头仅使用 SDK include 的 GCC C++17 独立包含检查、git diff --check 通过。未发现其他阻止提交的问题。

本轮仍限 Q0 工具链与 Q1 调度；Q2 Timer/QObject 资源屏障、Ela UI、原生 ABI 与 DLL 卸载未实现或验收，未运行 ASan/TSan。

## W7 提交前审核

日期：2026-10-24。范围：25adfcc 之后全部未提交的联合示例、CTest 登记与验收/进度文档，包括新增文件。未改动核心 SDK 行为。

- 正确性与证据：原 recovery 只检查新 epoch/BindingId，没有实际分发恢复后的事件，无法证明新监听器连接到新服务。现在更换提供者前后均发送保存事件，检查恰好调用一次当前监听器、当前 Store 的实际文档内容和确认结果；旧 Store 必须在替换前过期，新监听器恰好一个。增强现有场景而非增加重复测试入口。
- 防御性检查：绑定读取集中到先验证数量的 binding()，避免空数组越界；清理失败快照先判断 optional 是否存在，弱引用只 lock 一次后读取，避免缺失诊断时空解引用。关闭检查复用不可变快照，并核对根、消费者及提供者的 Disposed、effects 和 committed 数量。
- 状态准确性：失败路径不再记录 consumer cleaned，改为 consumer cleanup failed，并断言提供者及根未运行清理。failure 示例返回成功只表示预期残留得到验证，不表示内核关闭成功。
- 规范与质量：核对所有新增文件、类型、具名函数的中文 Doxygen、标准头依赖和回调捕获所有权；统一资源查询与断言，整理进度工作记录表的空行，避免 Markdown 表格断裂。验收报告引用的 24 个唯一测试名均与 CTest 实际登记核对。

GCC 13.1、MSVC 19.44 x64 Debug/Release 各 113/113 通过，日志为 `build/review-w7-core-<compiler>-<mode>.log`；git diff --check 通过。未发现其他阻止提交的问题。未运行 ASan/TSan、Qt、ABI 或物理卸载验证，首阶段结论仅适用于文档明确的内置范围。

## W2/W3 提交前审核

日期：2026-10-23。范围：全部未提交的 W2 收尾、W3 Kernel/实例实现、SDK、测试及文档。

## 已修复问题

1. 移动后的 Runtime 保留原编号，但激活函数已为空。Kernel 仅核对编号会接受该句柄，随后访问空回调。现在移动后原句柄 id() 返回无效编号，Kernel 拒绝挂载；直接 Instance::mount 在登记资源前抛 invalid_argument。新增回归在修复前实际失败，日志为 `build/review-before-fix.log`。
2. 调度器关闭后 update/retry 可能先替换目标、分配操作，再投递失败或构造 EffectScope 抛错。现在在修改账目前返回 SchedulerClosed。回归同时检查 Active 更新和 Failed 重试被拒绝，操作编号、配置修订和状态保持原值。
3. 本机中文 MSVC 的 /showIncludes 前缀被 CMake 错误解码，Ninja 未记录头文件依赖。Release 增量构建因此混用旧 Kernel 目标和新 SDK。新增可选 CMake 缓存项 CORDIS_MSVC_SHOWINCLUDES_PREFIX，默认仍自动检测；本机使用 `注意: 包含文件:`。清洁构建后已核对依赖表，触碰 instance.hpp 会触发 instance.cpp、kernel.cpp 与 w3.cpp 重编译。

## 验证与边界

GCC 13.1、MSVC 19.44 x64 的 Debug/Release 各 61/61 通过。W3 共十组行为，共用一个 CTest 入口；新增测试不会增加 CTest 入口数量。日志为 `build/review-core-<compiler>-<mode>.log`；MSVC 清洁构建日志为 `build/review-build-core-msvc-<mode>.log`。`git diff --check` 通过。

本机 MSVC 配置需使用 W2 记录中的开发者环境及 TEMP/TMP 设置，再执行：

```powershell
cmake --preset core-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset core-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
```

修复旧依赖数据库后需使用 `cmake --build --preset <preset> --clean-first` 重建一次。不要只修前缀而继续使用缺失依赖记录的旧目标。此覆盖参数仅在检测错误时使用，值应与实际编译器输出匹配。

未发现其他阻止本次增量提交的问题。W3 仍未完成独立 CleanupContext，服务/realm 未实现；历史操作未裁剪，非合作清理及投递阻断会保守保留资源。上述已知边界见 W3 记录；未运行 ASan/TSan、Qt 或 DLL 验证。

## W4 提交前审核

日期：2026-10-23。范围：c3f3661 之后全部未提交的 CleanupContext、服务注册表、实例依赖收敛、输出声明、realm、异步调用、测试及文档；包含新增未跟踪文件。

发现并修复一项高优先级问题：`Instance::change` 在实例已退役时通过 `scope.close().tryResult()` 查询清理失败。提供者此时可能仍在等待消费者退出，调用 close 会提前启动提供者自身清理，破坏消费者优先清理的约束。现在只在快照表明 scope 已开始关闭时查询其结果，等待下游期间更新仅替换目标配置。

新增 `w4.dependency_update_draining`：消费者清理由 gate 阻塞，提供者连续更新两次，验证 gate 完成前提供者不执行清理、不启动新代，旧操作为 Superseded；消费者仍能通过 CleanupContext 访问旧绑定，之后才完成提供者更新及依赖恢复。修复前真实复现，GCC Debug 85 项通过、该项失败，见 `build/review-w4-gcc-debug-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 86/86 通过，日志为 `build/review-w4-core-<compiler>-<mode>.log`。同时修正文档中仍称服务依赖/realm 未实现的过期状态。`git diff --check` 通过，未发现其他阻止本次提交的问题。

已知范围：W5 可选依赖、父子所有权与联合等待图未实现；清理失败或投递阻断继续保守保留资源。未运行 ASan/TSan、Qt 或 DLL 验证；这些结果不构成物理卸载安全证明。

## W5 提交前审核

日期：2026-10-24。范围：748c8fb 之后全部未提交的子实例、父代清理屏障、实际/声明联合图、SDK、测试及文档，包括新增文件。

发现并修复一项操作结果一致性问题：实例已经返回 PendingMissingDependency 后，新的声明环或不兼容服务会使实例 Failed，但旧 target 已完成，再次 resolve(Failed) 无效，当前 operation 仍错误显示 Pending。现在激活前失败统一通过 failPreflight 收敛；若旧操作已稳定 Pending，先建立新 OperationId，再发布 Failed，配置修订和 epoch 不变，旧操作继续返回其历史 Pending 结果。

增强 `w5.children_declared_cycle` 检查自动失败操作与历史结果，新增 `w5.children_pending_failure` 检查晚到类型不兼容。修复前两项均真实失败（94/96 通过），见 `build/review-w5-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 96/96 通过，见 [开发进度](progress.md)，日志为 `build/review-w5-core-<compiler>-<mode>.log`。`git diff --check` 通过，未发现其他阻止本次提交的问题。审查涵盖父代广播停止、子清理失败保留、更新重建、realm 继承和环路径。已知图边界仍为同一托管注册表，候选提供者多义时不猜测连边；不覆盖任意 Task 等待。未运行 ASan/TSan、Qt 或 DLL 验证。

## W6 提交前审核

日期：2026-10-24。范围：462c594 之后全部未提交的 EventBus、Context/EffectScope 适配、测试、示例及文档，包括新增文件。

正确性：复制成功的源结果至 serial 返回任务或 parallel 汇总时，用户负载的复制构造可能抛错。原实现会使内部 continuation 失败，返回任务可能永久 pending、自保留控制块不能解除；parallel 还可能在已经减少 pending 后漏记该监听器结果。现在 serial 将复制异常转换为 CallbackFailed，parallel 每项复制失败记入对应 Result，组装报告本身失败则拒绝返回任务；这些路径都释放已完成的分发屏障。新增 `w6.events_result_copy` 使用可抛复制负载，同时验证 serial/parallel 结果、activeDispatches 归零与所属作用域清理成功。

规范与质量：补全标准库直接依赖，移除未使用的 algorithm；补充负载可移动的编译期约束。快照先成功存入记录再增加保留数，避免 vector 扩容失败留下未记账的保留；析构使用已有记录逐项释放并通知，不再额外分配 Promise 容器。修正“最后快照销毁闭包”的过强注释，明确最终回调所有权仍由 effect 清理闭包释放；拆开多个成员赋值，便于逐项检查。

审查同时核对 once/过滤重入、Next 窗口、注销快照、epoch 门禁、scheduler 关闭、异步投递阻断及实例清理路径。GCC 13.1、MSVC 19.44 x64 Debug/Release 各 110/110 通过；event.hpp 独立包含及 git diff --check 通过，未发现其他阻止提交的问题。矩阵结果见 [开发进度](progress.md)，日志为 `build/review-w6-core-<compiler>-<mode>.log`。未运行 ASan/TSan、Qt 或 DLL 验证；任意用户等待环、跨 ABI、全局 schema 与 post 仍不在本轮范围。
