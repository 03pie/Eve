# P4-M3c 原生资源桥

日期：2026-10-29。基线 f32191b（M3b 已审核提交）。M3c 分为 dispatch/effect（M3c1）和服务（M3c2）；每段实现、审查修复、验证后提交，再进入下一段。该拆分不减少 M3d/M4 和两个服务 DLL 的原计划要求。

## M3c1 交付

- 稳定生产 HostTable 开启 DISPATCH/EFFECTS；真实排队目标调用、复制参数、独立消费方/目标请求及同步最终应答。
- 排队取消、在途取消后真实完成；关闭期间仍完成已接受操作，不提前销毁。
- Core EffectScope/EffectToken 的注册、一次 ready、主动清理、逆序整体清理及异步资源屏障。
- Callback/SetupTask/CleanupTask/QueuePayload/Call 的真实模块租约、捕获退役及资源/操作限额，公开值诊断。
- 真实 C DLL 对接全部新增函数，不用 M2b 的协商哨兵代替运行实现；Host 控制与资源实现分文件维护。

服务能力当前保持关闭，见 [ADR-0034](adr/0034-native-dispatch-effects.md)。未修改 Core/UI/Loader 或参考、第三方实现。

## 审查回归

首轮新增 17 项场景与既有 21 项 Host 场景合计 38/38。继续审查发现并复现：

1. 主动异步清理未完成时，实例关闭提前调用 stop，将正常等待误判为失败。resource_close_during_cleanup 修复前 0/1，见 build/p4-m3c1-cleanup-red-tests.log。现在等待已接受操作及已进入的清理，再推进 stop。
2. 宿主操作已经 ACK，但合法调度器还保留投递副本时，模块可过早报告静止。resource_retire 修复前 0/1，见 build/p4-m3c1-retire-red-tests.log。现在等待最后资源捕获析构，跨线程退役排队回 owner，字节预算也在实际释放后返还。

两个修复后的 19 项资源场景与 21 项 Host 场景合计 40/40，见 build/p4-m3c1-review-green-tests.log。之后增加容量释放后新 effect 不复用编号的检查。

最终审查另复现 P2：基础 scheduler 对象已释放时，资源包装器丢失 owner 身份，无法读取本应可用的失败诊断，模块聚合还可能抛出取值异常。resource_expired_scheduler 修复前 0/1，见 build/p4-m3c1-expiry-red-tests.log。现在包装器保留构造线程身份，业务开放检查仍依赖真实 scheduler；聚合显式传播子快照错误。修复前完整矩阵与源码另存 pre-expiry 日志，不覆盖为最终证据。

## 验证

| 范围 | 最终结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release 全量 | 557/557 | build/p4-m3c1-reviewed-release-tests.log |
| MSVC Debug P4 | 98/98 | build/p4-m3c1-reviewed-debug-tests.log |
| 无 Qt GCC Debug 全量 | 241/241 | build/p4-m3c1-reviewed-gcc-tests.log |
| MSVC ASan P4 | 98/98 | build/p4-m3c1-reviewed-asan-tests.log |
| 原 Release 关闭示例 | P4 98/98，检查后恢复 ON | build/p4-m3c1-reviewed-no-examples-tests.log / restore.log |
| 源与依赖审计 | 12 项源/构建哈希一致；8 个受影响编译行有 ASan，编码/格式/文档与 diff 检查通过 | build/p4-m3c1-reviewed-source-sha256.json / asan-commands.json / audit.log |

最终 P4 含 21 项新增资源场景。expiry 修复仅改 Windows 宿主/检查；GCC 编译的共同请求组件与公开头和其通过矩阵时哈希一致。三种 MSVC 矩阵在最后修复后重新构建。早期编译及三个红测试证据保留，不以未复核源码的历史结果代替最终验证。没有修改 Core/UI/Loader、第三方/只读参考或既有 UI 超时。

## 剩余

M3c2 接入真实服务注册/撤回/调用、版本/能力和 realm、旧绑定清理能力及租约；随后 M3d 接入 Kernel/Session、两个服务 DLL、多实例和退出链；M4 完成同工具链 UI DLL、工具链检查与 P4 整体收口。当前不计为 M3 或 P4 已完成。
