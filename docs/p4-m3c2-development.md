# P4-M3c2 原生服务桥

日期：2026-10-29。基线 e9c89dd（M3c1 已审核提交）。本段已实现、审核修复并完成最终验证，随本提交归档；M3d/M4 未完成。

## 交付

- NativeModule.runtime / NativePlugin / NativeService：把原生实例及 JSON 服务接入真实 Core Context/Runtime。
- service_register 暂存输出、effect.ready 与 start 联合发布；精确匹配类型、minor、能力及 realm。
- service_invoke 经 Core 异步 CallLease、独立提供者请求和最终消费方 ACK；在途取消不提前完成。
- service_withdraw 等待消费者与调用；effect cleanup 在撤回后执行，失败保留 ServiceObject 和模块。
- CleanupContext 的异步旧绑定调用、ServiceRef 值快照、Context 自有输出撤回和提交前版本检查。
- 有界 JSON 字符串数组解码，支持 UTF-8/Unicode 转义，拒绝空值、重复名称及预算越界。

设计见 [ADR-0035](adr/0035-native-core-services.md)。新函数遵循同代 owner 和真实 scheduler 归属；未绑定 Core 或未协商服务族时拒绝服务操作。公开源码 SDK 包装不是跨 DLL 的 C++ ABI。

## 审查与真实红绿回归

首轮 Core 新场景在无 Qt GCC 下 5/5 通过；真实 C 服务 DLL 的 13 项场景及 JSON 检查 15/15 通过。目前是同一真实 DLL 的不同 Core 实例，不冒充 M3d 的两个独立服务 DLL。

审查共复现并修复六项 P2：

| 问题 | 修复 | 真实红测试证据 |
|---|---|---|
| 动态调用因 minor 不足被拒绝后仍提交依赖租约 | Context.consume 在提交前验证最低版本/能力；拒绝不改依赖账目 | build/p4-m3c2-version-red-tests.log，0/1 |
| 动态消费缓存随已完成请求累积，没有实例上限 | 每实例至多 128 个消费绑定；新查询超限不提交租约，声明同样有 ABI 可表达性/数量预算 | build/p4-m3c2-budget-red-tests.log，0/1 |
| 提供者请求仲裁满额，在未调用 DLL 时就把健康实例隔离 | 业务入口准入失败返回 LIMIT，既有 256 项调用继续完成且之后能再次调用 | build/p4-m3c2-admission-red-tests.log，0/1 |
| 同步最终 ACK 也占异步请求仲裁预算，满额时无法回应已接受操作 | ACK 使用独立实际 Call 租约及操作最后捕获屏障，不构造虚假的 PENDING 请求 | build/p4-m3c2-ack-red-tests.log，0/1 |
| Core 已停用、Native 清理尚未开始时旧排队回调仍执行 | 接受和执行两次核对真实 Context；旧任务只收到 CANCELLED 应答 | build/p4-m3c2-authority-red-tests.log，合计 0/2 |
| 一个实例使模块代次故障后，直接 C++ 服务引用仍能进入同代健康实例 | 服务入口再次核对代次门禁，不绕过根模块的故障状态 | 同上，合计 0/2 |

修复后的全部 P4 定向 123/123，见 build/p4-m3c2-authority-green-tests.log；最终矩阵额外核对配额释放后的再次调用。审查还覆盖锁外调用 DLL、实际入口返回后退役、提供者撤回先于清理、消费者及调用排空、无输出/部分输出回滚、setup 未就绪不得发布、清理能力窗口和对象键保留。

## 测试数据修复与历史证据

首轮宏字符串化导致 MSVC 对测试坏 Unicode 字面量诊断，改为独立测试变量，未放宽警告。随后 ASan 在新测试构造 129 项无效声明时发现 vector 扩容读取失效的自身元素引用；现先复制样本再扩容。该错误属于测试构造，已修复并重新验证；失败的 Debug/ASan、原始冻结源码和 ASan 堆栈保留于 build/p4-m3c2-pre-admission-*。Debug 的该次异常同时导致历史夹具清理失败；一次单独清理被自动审批以 blocked by policy 拒绝，该目录保留作诊断，没有尝试绕过。最终矩阵各自的夹具清理均成功。

## 最终验证

| 范围 | 最终结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release 全量 | 582/582 | build/p4-m3c2-reviewed-release-tests.log |
| MSVC Debug Core/P4 | 245/245 | build/p4-m3c2-reviewed-debug-tests.log |
| 无 Qt GCC Debug 全量 | 247/247 | build/p4-m3c2-reviewed-gcc-tests.log |
| MSVC ASan Core/P4 | 245/245 | build/p4-m3c2-reviewed-asan-tests.log |
| 原 Release 关闭示例 | P4 123/123，检查后恢复 ON | build/p4-m3c2-reviewed-no-examples-tests.log / restore.log |
| 源/构建审计 | 24 项哈希一致；13 个受影响编译行有 ASan；编码/格式/文档/依赖及 diff 检查通过 | build/p4-m3c2-reviewed-source-sha256.json / asan-commands.json / audit.log |

最终含 19 项真实服务 DLL 场景、6 项 Core 服务访问场景及字符串数组解码检查。末次修复仅修改 Windows 宿主和对应检查；GCC 使用的 SDK、Core、JSON/请求组件及单测与其完整通过时的哈希一致。三种 MSVC 配置均在最后修复后重新构建。Core/SDK 仅格式化新增和变更区域，保留无关旧代码；未修改 UI/Loader、参考/第三方或原有 UI 超时。

## 后续

本段审核提交后再进入 M3d 的 Kernel/Session、两个独立服务 DLL 与真实退出链，然后完成 M4 同工具链 UI DLL 和整个 P4 的完成审计。当前不把 Core Runtime 工厂当作 Kernel 注册或 Session 接入完成。全部 P4 完成并审查提交后按用户要求同步 GitHub。
