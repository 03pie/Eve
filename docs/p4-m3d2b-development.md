# P4-M3d2b 原生取消与 DLL 捕获验收

日期：2026-10-29。基线 6c5931f。实现、审查和最终验证完成，随本提交归档。M3d/M3 在当前 Windows x64 支持矩阵收口，下一工作包为 M4 同工具链 UI DLL；整个 P4 仍进行中。

## 分层证据与边界

本段增加专用 lifecycle C++ DLL 夹具、真实 Qt 调度检查和构建接入。夹具描述使用既有纯 C discovery，业务表及捕获在新 C++ 文件内实现；控制导出仅供测试，不加入两个业务 DLL 或公共 SDK。没有生产代码改动。

S06 的取消语义在内部生产 RequestChannel 边界验证：ModuleManager 实际固定并装载 DLL，持真实代次的 Call/Instance 租约，查询零可选能力业务表，start 后关闭通道并登记 Stop。DLL 返回 PENDING，宿主记录一次取消并调用 DLL cancel_request；重复取消返回 false。取消与 seal 后，结果和 drained 仍未完成、CleanupTask/Instance 引用仍存在且代次不能 drain。随后另一线程在真实 DLL 内完成原 Stop；完成缓冲区随即改写，生产通道必须保留正确复制值，最终结果为 OK、cancelRequested=true，不能用取消覆盖已完成业务结果。真实 Qt 循环完成投递/退役后才允许 destroy 和排空。

NativeModule 当前没有公开 stop-cancel 操作，本段未增加这个接口，也未声称用户能取消 NativeModule.close。另以生产 HostTable/NativeModule 验证 PENDING stop 等待真实 DLL completion、复制回执不内联 destroy，随后完整关闭。两项共同映射 S06，请求边界与宿主集成的职责分别记录；不把测试 shim 冒充完整生产 HostTable。

S07 使用生产 NativeModule.runtime、Kernel/Core epoch 及真实 QtScheduler。DLL 创建 effect 后登记回调 token；其 std::function、lambda、shared Capture 都在 DLL 内。测试只额外保留一个已接受宿主任务的副本，原任务仍正常进入 Qt 队列，不由 FakeScheduler 驱动。Core stop 立即失活 epoch，排队业务主体不进入 DLL，但仍 ACK CANCELLED。DLL 自己的清理重置 lambda，Capture 析构在 DLL 内执行并记录日志；保留的宿主副本仍持 Callback/QueuePayload 租约，实例不可 destroy、stop 不得假静止、代次不能 drain。最后从另一线程释放副本，退役回到 Qt owner，才允许成功关闭和 Resident/PIN 排空。

另有活动回调对照（主体恰好执行一次）与清理失败对照（主体不执行、Capture 不析构、实例和执行引用保留）。成功路径不卸载 DLL，失败路径不伪造捕获释放。

## 验收映射

| 场景 | 自动化依据 |
|---|---|
| S06：真实 DLL Stop PENDING、一次取消、跨线程晚到完成及结果复制/退役 | p4.native_lifecycle_cancel_stop |
| S06 宿主集成：生产 HostTable 的 PENDING stop 不提前 destroy | p4.native_lifecycle_pending_stop |
| S07：Core 失活、主体抑制、DLL 内析构及最后宿主副本跨线程退役 | p4.native_lifecycle_stale_capture |
| 活动对照：主体一次、ACK 一次、析构一次、完整关闭 | p4.native_lifecycle_live_capture |
| 失败对照：清理失败不析构/不 destroy，PIN 与引用保留 | p4.native_lifecycle_failed_capture |

M3d1 的托管/目录和 M3d2a 的两个业务 DLL/真实 Qt Session/磁盘保存证据仍分别保留；本段不重复冒充业务交付或同工具链 UI DLL。M4 继续检查 UI 工厂、工具链及退出诊断，P5 和物理卸载未完成。

## 负向检查与审查

临时移除 dispatchOne 的执行前 allowed(false) 门禁后，stale_capture 检查因 DLL 主体被执行而 0/1 失败，见 build/p4-m3d2b-negative-tests.log。随后在 finally 中逐字节恢复 host_resources.cpp，并刷新时间戳实际重新编译，最终验证与 HEAD 的生产源码一致；负向控制不作为生产修复提交。

初次编译缺少私有请求头的显式包含，已补齐，见 build/p4-m3d2b-first-build.log。复核线程边界、Call 租约、Qt 嵌套循环的超时/异常收口、测试 shim 生命周期、C ABI 表与复制容量、最后捕获在错误线程释放、失败状态下保留对象及独立子进程 DLL 夹具清理。没有放宽既有超时/警告；未发现阻止本段提交的剩余问题。

## 最终验证

| 范围 | 结果 |
|---|---|
| 原 MSVC Release 全量 | 617/617 |
| MSVC Debug / ASan P4 | 各 149/149 |
| 无 Qt GCC Debug 全量 | 245/245；新 C++ 夹具严格 C++17 语法检查通过，不代表 GCC/MSVC ABI 互通 |
| 原 Release 关闭示例 P4 | 149/149，已恢复 ON 并重新构建 |
| 五项生命周期检查 | Debug/ASan 各连续 20 次，共各 100 次通过 |
| 源码与构建审计 | 4 项测试/构建快照一致；新夹具 C/C++ 与测试宿主共 3 个编译命令含 ASan；生产目录、SDK、业务示例、只读参考及第三方未改 |

最终日志前缀为 build/p4-m3d2b-reviewed-，包括 {release,debug,asan,gcc}-{build,tests}.log、{debug,asan}-repeat.log、no-examples-*.log、restore.log、gcc-fixture.log、source-sha256.json、asan-commands.json 及 audit.log。格式/UTF-8/LF/差异/引用和依赖检查通过。本段没有桌面 UI 改动或新增人工桌面验收要求。
