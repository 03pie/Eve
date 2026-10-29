# P4-M3c2 原生服务桥

日期：2026-10-29。基线 aa22275。M3c2 已完成真实服务桥及审查修复，最终验证通过，按用户要求随本提交归档；下一段为 M3d。

## 交付

- HostTable 开启 SERVICES，注册、撤回和调用全部进入真实 Core 注册表/提交租约。
- 可显式传入同 scheduler 的共享索引，支持两个真实 C DLL 互调；默认会话索引延迟创建。
- start 与 Core effect ready 双屏障发布；精确 realm、minor/能力匹配、归属及真实依赖环检查。
- 复制方法/JSON、独立两端请求、同步/异步/早到结果和真实协作取消。
- 消费者退出时使用旧绑定完成受限清理；提供者等待消费者与在途调用排空。失败清理及协议异常保留账目。
- ServiceObject 与操作捕获真实模块租约、容量限制和无 DLL 指针的服务快照。

实现决策和接入边界见 [ADR-0035](adr/0035-native-services.md)。Core 仅扩展内部异步清理门禁及适配器 friend；未改变普通 ServiceRef 的默认行为。未修改 UI、Loader、third_party/、research/ 或只读 sources/。

## 审查

首轮 13 个真实 DLL 场景通过，P4 合计 111/111。测试夹具在两个模块共存时原先取全局第一个代次，已改为按 generation 取值，并保存结果副本后遍历。

新增回归复现并修复两项 P2：

1. 宿主操作预算耗尽后，拒绝服务调用却遗留新 committed 租约。service_budget 修复前失败；现在受理失败显式释放新租约，既有绑定不受影响。
2. 提供者 cancel_request 返回非法 PENDING 时，仅消费者被隔离。service_cancel_protocol 修复前失败；现在提供者和消费者共同隔离并保留未静止账目。

红测试为 build/p4-m3c2-review-red-tests.log（0/2），修复后服务专项为 build/p4-m3c2-review-green-tests.log（19/19）。另覆盖 effect 未就绪/失败、两实例环、ACK 后捕获仍被调度器保留、跨线程释放及消费者清理失败。审查同时避免重复 publish 产生无用的注册表变化通知。

## 最终验证

| 范围 | 最终结果 | 证据 |
|---|---|---|
| MSVC Debug 全量 | 585/585 | build/p4-m3c2-reviewed-debug.log |
| MSVC Release 全量 | 585/585 | build/p4-m3c2-reviewed-release.log |
| 无 Qt GCC Debug 全量 | 241/241，含 native 公开头独立包含 | build/p4-m3c2-reviewed-gcc.log |
| MSVC ASan P4 与 Core 服务/依赖 | 152/152（P4 117、W4/W5 35） | build/p4-m3c2-reviewed-asan-tests.log |
| 原 Release 关闭示例 | P4 117/117，恢复 ON 并构建通过 | build/p4-m3c2-reviewed-no-examples-tests.log / restore.log |
| 最终源码审计 | 16 项源/构建哈希一致、8 个受影响编译命令有 ASan，编码/边界/文档及 diff 检查通过 | build/p4-m3c2-reviewed-source-sha256.json / asan-commands.json / audit.log |

首轮 ASan 只构建了原生夹具和账目目标，其他 P4 目标尚未生成，34 项未运行或失败；该轮保留于 build/p4-m3c2-asan-incomplete-targets.log。补齐整个 ASan 构建后重新运行上表 152 项，没有降低断言、超时或警告。最终验证期间源码哈希保持一致；本段无 UI 改动，不需要重新索取此前桌面人工确认。

## 后续

M3c 资源桥完成后，M3d 仍须将 NativeModule 接入 Kernel/Session，以两个业务服务 DLL 验证配置驱动、多实例、失败驻留及完整退出链。这里的两个 C DLL 是生产桥回归夹具，不代替该业务交付。M4 同工具链 UI DLL、工具链检查及 A/S06/S07 映射仍待实施；P4 继续为进行中，P5 未开始。
