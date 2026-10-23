# 提交前代码审查记录

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
