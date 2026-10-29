# P4-M3d1 原生 Runtime 托管与配置目录

日期：2026-10-29。基线 d6d003f。实现、审查修复及最终验证已完成，随本提交归档；下一段为 M3d2。

## 交付

- runtime.hpp / NativeModule.runtime / nativeRequirement：宿主原生契约及不跨 C ABI 的端点类型描述；Kernel.registerRuntime 保留定义身份，登记不执行插件。
- 低层 open/create 行为保留；新增托管模式冻结 Core 的输入与 realm，复用同一个索引和已有服务桥，等待真实初始 setup，再由 Core 统一发布。
- 原生输出验证声明后交由 Core 持有，清理未完成前保留服务键；NativeServices 只借用 Core committed 租约，旧代清理可首次调用已声明服务。
- PluginModule 冻结目录，schema 前及驱动分别检查模块归属与定位文本；available/依赖限额在挂载前检查，失效模块仍可显式停用。
- 17 项真实 DLL 托管场景、4 项无 Qt 目录/Kernel 场景；公开 runtime 头独立进入无 Qt 构建。

决策见 [ADR-0036](adr/0036-managed-native-runtime.md)。未修改 UI 页面、参考或第三方源码。本段使用既有 C DLL 回归夹具和手动调度器，并以受控内存 writer 检查 Session 保存协议；未宣称已交付业务 DLL 示例、真实磁盘写入或原生 Qt 事件循环联合验收。

## 审查与回归

1. 低层行为兼容：初版把“已同步启动成功、关闭在回执前到达”的历史启动结果改成取消。既有 native_host_close_queued 真实失败，见 build/p4-m3d1-close-queued-red.log；现只对托管模式应用新增停机门禁，保留低层历史回执。
2. Core epoch 门禁：撤去执行前检查后，native_managed_stale_dispatch 在修复前 0/1，见 build/p4-m3d1-epoch-red-tests.log。即使 NativeInstance 尚未收到异步 close，Core stop 也立即禁止队列中的 DLL 回调主体，仍交付一次取消 ACK。
3. 服务键保留：套用低层 release 后，native_managed_key 在异步原生清理结束前发现键已被释放，修复前 0/1，见 build/p4-m3d1-key-red-tests.log。托管分支只退役端点能力，最终键由 Core 清理完成后释放。

4. 代次故障门禁：提供者另一实例隔离后，外部模块仍能调用其健康实例。native_managed_generation 修复前 0/1（build/p4-m3d1-generation-red-tests.log）；现在在受理与排队执行处分别检查提供者代次，新业务返回 CLOSED，已受理队列也不进入 DLL，调用方仍可正常清理，故障资源继续保留。
5. 业务入口配额：三个消费者同时提交 384 个调用时，提供者 256 个请求预算耗尽被错误隔离。native_managed_incoming_budget 修复前 0/1（build/p4-m3d1-admission-red-tests.log）；现在在尚未进入 DLL 时将普通 invoke/dispatch 的配额拒绝映射 LIMIT，不破坏健康模块，预算释放后能恢复。
6. 同步 ACK 容量：中间实例持有 256 个在途调用时，其向上游请求的 ACK 被额外异步请求登记阻塞。native_managed_ack_capacity 修复前 0/1（build/p4-m3d1-ack-red-tests.log）。同步 ACK 改用实际 CallLease 与最后操作捕获屏障，不申请虚构请求；拒绝/异常/协议故障仍隔离。覆盖满额 dispatch 的 LIMIT ACK 和服务 ACK 均能交付。

其他复核覆盖未声明/元数据错误回滚、setup 失败、关闭未 ready 的 setup、同 scheduler/索引归属、额外 inject 首次旧绑定清理、跨 realm 调用、只读预览、候选 CAS、保存及失效模块停用。初次默认 Core 空配置未转换为 JSON；现统一映射 null，低层 create 仍要求调用者提供 JSON。

红测恢复源码时曾因 Copy-Item 保留旧时间戳而复用红测对象，已刷新源码时间戳并实际重新编译，随后 P4 135/135 通过；容量审查后定向 P4 138/138 通过。格式检查另发现新测试头依赖隐含包含顺序，现显式包含其基础夹具，最终验证从新的源码快照开始；未禁用格式化、警告或既有超时。

## 验证映射

| 场景 | 自动化依据 |
|---|---|
| 身份、重复名称/身份、已移动定义、owner 与关闭登记 | p4.catalog_registration |
| 未用声明、模块重指向、目录冲突、schema 前拒绝和行列位置 | p4.catalog_catalog |
| 启用门禁、抛错、失效模块停用、依赖数量 | p4.catalog_availability / budget |
| Core 托管 DLL、借用租约不重复计数、异步 JSON 与完整关闭 | p4.native_managed_flow |
| 初始 setup 发布、取消及失败 | p4.native_managed_publication / setup_cancel / setup_failure |
| 清理期间键保留、首次旧绑定清理、epoch 回调撤销 | p4.native_managed_key / cleanup / stale_dispatch |
| 合法/错误契约、晚登记、未声明/错误元数据回滚 | p4.native_managed_guards / undeclared / metadata |
| realm、scheduler、已有私有索引不能切换 | p4.native_managed_realms / scheduler / index |
| 代次隔离、普通配额拒绝与恢复、满额时同步 ACK | p4.native_managed_generation / incoming_budget / ack_capacity |
| 真实 NativeModule/Kernel/Session：预览、注入、CAS、保存、移除及退出 | p4.native_managed_session |

## 最终验证

最终冻结源码验证如下；所有命令均返回成功，未放宽已有警告、超时或断言。

| 范围 | 结果 | 日志（build/ 下） |
|---|---|---|
| 原目录 MSVC Release 全量 | 606/606 | p4-m3d1-reviewed-release-build.log / release-tests.log |
| MSVC Debug Core/P4 | 260/260 | p4-m3d1-reviewed-debug-build.log / debug-tests.log |
| 无 Qt GCC Debug 全量 | 245/245 | p4-m3d1-reviewed-gcc-build.log / gcc-tests.log |
| ASan Core/P4 | 260/260 | p4-m3d1-reviewed-asan-build.log / asan-tests.log |
| 原 Release 关闭示例 P4 | 138/138，已恢复 ON 并重新构建 | p4-m3d1-reviewed-no-examples-*.log / p4-m3d1-reviewed-restore.log |
| cleanup/session/key/generation/incoming_budget/ack_capacity | Debug、ASan 各 6 项连续 20 次，共各 120 次通过 | p4-m3d1-reviewed-debug-repeat.log / asan-repeat.log |
| 源码、依赖与 SDK | 29 项源码/构建 SHA256 一致；15 个受影响 C/C++ ASan 编译命令含检测选项；8 个 SDK 头在 MSVC/GCC 独立编译通过 | p4-m3d1-reviewed-source-sha256.json / asan-commands.json / headers.log / audit.log / p4-m3d1-final-audit.log |

上述日志的简写沿用 p4-m3d1-reviewed- 前缀。格式、UTF-8/LF、差异、文档引用及只读边界审计通过。完整 Debug UI 不属于本轮验证范围，UI 回归由 Release 全量覆盖。首轮 P4 135/135、138/138 及 pre-* 日志仅保留过程证据，最终矩阵以后缀 reviewed 的当前源码快照为准。未发现阻止本段提交的剩余问题；M3d2 与 M4 未完成，不能据此标记整包完成。

## 接入顺序

宿主包含 cordis/module/runtime.hpp，先创建 scheduler/Kernel，再依次执行 ModuleManager.load、NativeModule.open（可显式传入 kernel.services()）、module.runtime(NativePlugin)、kernel.registerRuntime。ConfigSession 的 PluginDefinition 与 KernelPlugin 使用相同 PluginModule、真实 Runtime 和宿主 schema；按需用 nativeRequirement 声明可选 inject。只读状态门禁通过 NativeModule.snapshot 拒绝 closing/closed/faulted 的新激活，不进入 DLL。

退出时先 close 配置会话和 Kernel，等待其真实清理报告，再 close 模块会话，最后 drain 代次和关闭 scheduler。低层与托管身份不能混淆；低层共享索引的唯一 InstanceId 约束继续适用。DLL 默认 PIN 保留代码映像，业务模块示例及真实 Qt 退出入口在 M3d2 补齐。
