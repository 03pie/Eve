# P4-M3b 原生宿主生命周期

日期：2026-10-29。基线 1ffa0ff（M3a 已审核提交）。本段完成基础 HostTable 与真实 create/start/stop/destroy；不替代 M3c/M3d 或 M4。

## 交付与约束

- Cordis::ModuleHost / NativeModule / NativeInstance：公开头无 Qt、Windows 或 C ABI 函数指针；同代至多一个活动宿主会话，业务协商缓存一次。
- 同步 create 契约、异步启动/停止、协作取消、多实例和 epoch 隔离；真实 DLL 返回之后才调用 RequestChannel.returned。
- Host.complete/log 独立 Call 租约及回调屏障；日志 UTF-8 复制、条数/字节限额，异步错误 JSON 在宿主保留。
- 模块 Serving/Draining/Failed 诊断；活跃会话不能先 drain，成功 destroy 后才释放 Instance，成功会话关闭才释放 ExternalHandle。
- 异常/协议冲突、停止或销毁失败、调度器关闭保守驻留，不将复制完成等同于 DLL 执行退出。

稳定基础表能力为 0；未用协商哨兵冒充生产服务/effect 实现。见 [ADR-0033](adr/0033-native-host-lifecycle.md)。没有修改 Core/UI/Loader 或参考、第三方实现。

## 审查与真实回归

发现并修复两项 P2：

1. 已知 context 在回调关闭门禁后继续 log，只返回 CLOSED，仍可能给出静止成功。真实 gate 将另一个 Host.complete 保留在生产者线程，复现迟到日志；修复前 host_late_log 0/1。现在记录协议不确定并保留实例/账目。
2. 请求结果已发布并退役、启动观察器仍在 owner 队列时立即关闭，会取消不存在的请求，将健康实例隔离。修复前 host_close_queued 0/1。现在仅对真实 PENDING 且尚未完成的源任务请求取消，等待启动观察器收敛后停止。

证据为 build/p4-m3b-late-log-red-tests.log 和 build/p4-m3b-close-queued-red-tests.log。修复后 21 项真实 Host 场景与 18 项请求检查合计 39/39，见 build/p4-m3b-review-green-tests.log。跨线程 gate 覆盖移动及复制 ScheduledJob：owner 可先处理结果，但必须等待最后生产者捕获和正在执行的宿主 shim。

同时检查表寿命、锁外执行 DLL/调度/通知、create 失败句柄契约、已关闭调度器、非 owner 无副作用、预算及诊断复制。普通启动失败与协议不确定分别处理；违规 C++ 异常由真实 C++ DLL 复现，未放宽异常模型或编译警告。

## 最终验证

| 范围 | 最终结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release 全量 | 536/536 | build/p4-m3b-reviewed-release-tests.log |
| MSVC Debug P4 | 77/77 | build/p4-m3b-reviewed-debug-tests.log |
| 无 Qt GCC Debug 全量 | 241/241 | build/p4-m3b-reviewed-gcc-tests.log |
| MSVC ASan P4 | 77/77 | build/p4-m3b-reviewed-asan-tests.log |
| 原 Release 关闭示例 | P4 77/77，检查后恢复 ON | build/p4-m3b-reviewed-no-examples-tests.log / restore.log |
| 源及构建审计 | 16 项哈希一致；9 个受影响 ASan 编译行、格式/编码/文档链接/依赖边界及 diff 检查通过 | build/p4-m3b-reviewed-source-sha256.json / asan-commands.json / audit.log |

最终矩阵对应同一份冻结源码，含 21 项真实 Host 场景和两个审查红绿回归。首轮编译告警和两个红测试日志作为历史保留；不以早期构建结果替代最终源码证据。没有关闭严格警告、修改原有 UI 超时或替换参考/第三方源码。当前审查未发现阻止本段提交的剩余问题。

## 下一段

本段审核提交后推进 M3c 的真实 dispatch/service/effect 与对应租约；随后 M3d 接入 Kernel/Session、两个服务 DLL 和联合退出，再推进 M4 的 UI DLL 与 P4 完成审计。上述后续要求未删减。
