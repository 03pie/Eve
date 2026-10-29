# P4-M3 请求与资源桥

日期：2026-10-29。M3 已完成（当前 Windows x64 支持矩阵）；M3a 请求组件已审核提交（1ffa0ff），M3b 基础生命周期已审核提交（f32191b），M3c1 dispatch/effect 已提交（e9c89dd），M3c2 服务桥已完成审查修复和最终验证，随本提交归档。M3d1/M3d2a/M3d2b 的托管、业务 DLL/配置链路与分层 S06/S07 均已验收，见 [M3d2b 记录](p4-m3d2b-development.md)。M4 UI DLL 尚未完成，不将 M3 完成等同于整个 P4。

## 分段与提交门槛

按用户要求，每段实现完成后审核 bug、规范和质量；修复并验证无未解决问题后提交，再进入下一段。

| 子阶段 | 内容 | 状态 |
|---|---|---|
| M3a | 生产请求控制块、复制/一次仲裁、取消、投递与租约退役 | 已完成；1ffa0ff，审查修复 seal 屏障及晚到取消 |
| M3b | 稳定 HostTable/模块业务表、真实 create/start/stop/destroy 与 ModuleGeneration | 已完成；审查修复及最终矩阵见 [M3b 记录](p4-m3b-development.md) |
| M3c | 生产 dispatch/service/effect 桥及真实资源租约 | 已完成；M3c1 已提交，M3c2 最终验证通过并随本提交归档，见 [服务记录](p4-m3c2-development.md)；前段历史见 [资源记录](p4-m3c-development.md) |
| M3d | Kernel/Session、两个服务 DLL、多实例、失败驻留及退出联合验收 | 已完成；M3d1 托管/目录、M3d2a 业务 DLL/Qt 配置与 M3d2b 分层 S06/S07 通过，见 [分段](p4-m3d-development.md) |

M3 结束后继续 M4 的同工具链 UI DLL、工具链预检、管理诊断与 P4 收口。上述拆分不减少原计划的服务 DLL、UI DLL 或 A/S06/S07 验收要求。

## M3a 交付

- Cordis::ModuleRequests / 私有 RequestChannel：绑定 InstanceId/epoch/context，单调请求编号；不直接执行 DLL 或改 Kernel。
- 合并早到/晚到 complete 与入口返回；只接受一次合法结果，取消不提前完成。
- 入口 Call、待处理 setup/cleanup/call/callback、结果 QueuePayload 分别登记；结果发布与资源静止分开。
- 调度器拒收、丢弃、抛错及协议冲突保留错误、pin 和未静止账目；通道遗弃不静默释放原生引用。
- close 拒绝新业务，seal 终结全部入口；只有 seal 后真实退役才发布 drained。
- 无 Qt 的 UTF-8 JSON 扫描/复制；大小/总字节/在途请求/深度有界，保留数字和重复成员文本。

实现决策见 [ADR-0032](adr/0032-native-request-arbitration.md)。JSON 的结构/Unicode 检查不代替服务/config schema；只读快照不包含业务 JSON。

## 复现与审查修复

首轮 MSVC/GCC 新增 17/17 均通过。审查发现 close 允许后续清理却已经发布 drained，可能让调用方使用失真的终结屏障。新增 request_fence 在修复前 0/1，证据为 build/p4-m3a-review-red-tests.log；改为独立 seal 门禁后请求专项 18/18，见 build/p4-m3a-review-green-tests.log。

回归覆盖 close 后空闲仍不终结、追加 cleanup、seal 后拒绝 Stop/Cleanup、请求未完成与投递捕获未退役时不得宣布排空。另以合法 scheduler 在 post 返回前运行队列副本的 gate，验证最后一个生产者捕获跨线程退役仍交回 owner。

进一步审查在同一个 gate 中复现：结果已发布、投递捕获仍由生产者保留时，cancel 还会登记新的取消意图。该行为可能诱导上层再次取消已终结原生工作。新增断言在修复前 0/1，见 build/p4-m3a-cancel-red-tests.log；现在已发布结果返回 false，原结果不变，投递捕获仍须真实退役。随后重新冻结最终源码并运行下表验证。

## 最终验证

| 范围 | 最终结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release 全量 | 515/515 | build/p4-m3a-reviewed-release-tests.log |
| MSVC Debug P4 专项 | 56/56 | build/p4-m3a-reviewed-debug-tests.log |
| 无 Qt GCC Debug 全量 | 241/241 | build/p4-m3a-reviewed-gcc-tests.log |
| MSVC ASan P4 专项 | 56/56 | build/p4-m3a-reviewed-asan-tests.log |
| 原 Release 关闭示例 | P4 56/56，检查后恢复 ON | build/p4-m3a-reviewed-no-examples-tests.log / restore.log |
| 18 项生产请求场景 | Debug/Release/GCC/ASan 全部通过；含两个真实红绿回归 | 上述日志及 build/p4-m3a-*-red-tests.log |
| 源与依赖审计 | 7 项源/构建文件冻结，新增 3 个编译单元有 ASan；格式、编码、边界、文档和 diff 检查通过 | build/p4-m3a-reviewed-source-sha256.json / audit.log / asan-commands.json |

代码在两项审查修复后重新冻结，验证期间逐项比对源码；此前 17 项初版、首个 seal 修复后的完整验证作为历史日志保留。最终阶段没有修改 Core、UI、Loader、参考材料或第三方源码，也没有修改原有 UI 超时预算。M3a 的 Debug 只跑 P4 专项，不将上一阶段 497 项历史误记为本阶段全量结果。

## 后续接入约束

M3b 已实现以下约束，M3c/d 接入须继续保持：在 DLL 调用实际退出后执行 returned，不能仅依据 complete 到达推进生命周期；稳定 HostTable 和通道覆盖所有合法回调寿命。Host.complete/log 的跨边界 shim 还须单独持 CallLease，工作线程退出也须有独立屏障，不能用请求结果代替。先关业务、登记并完成 Stop/资源清理，再 seal/等待 drained，最后 destroy；不能先让模块账目拒绝清理引用。

投递或协议不确定时保留实例和模块并返回可诊断的不静止结果，不能假装成功关闭。生产服务和 effect 继续使用真实账目，不将 M2b 的协商哨兵槽用作生产实现。
