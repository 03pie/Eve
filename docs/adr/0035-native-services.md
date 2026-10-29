# ADR-0035：原生服务注册、提交租约与清理调用

日期：2026-10-29。状态：采用（P4-M3c2）。补充 [ADR-0034](0034-native-dispatch-effects.md)，开启生产 SERVICES 能力，不改变 C ABI V1 布局。

## 注册表与发布

原生服务复用 Core ServiceRegistry、ProviderHandle 与 committed 租约，不另建模拟服务索引。服务对象是宿主私有 endpoint，DLL 只接收自身的 service_token、方法、JSON 和独立目标请求。endpoint 持有真实 ServiceObject 模块租约，最终捕获退役后才释放。

NativeModule.open 可显式接收共享 ServiceRegistry，须归属同一个 scheduler。未传入时，首次服务操作创建该模块会话的私有索引；不自动连接其他模块。活动会话使用过索引后不能切换。共享索引内的活动 InstanceId 由调用方保证唯一，后续 M3d 由 Kernel 分配。公开头不依赖 Qt/Windows；Qt JSON 解析仅私有链接到 Windows 宿主，用于能力数组解码。

注册必须归属本实例仍开放的 effect，且 token 非零。先 stage，只有原生 start 成功和 Core EffectToken.ready 真正成功后才 publish；仅收到 effect_ready 或 start 的早到 complete 均不足以发布。清理或撤回立即 retire，拒绝新的查找和普通调用；键保留到旧租约与在途调用排空。

## 调用与关闭

按名称、接口 major、精确 realm 查找，校验 minimum minor 和全部所需能力，不跨域回退。每个实例/epoch 按完整键固定首次成功受理的绑定，复用时重新检查版本、能力和绑定状态。受理失败撤销新建租约，不产生隐藏依赖。Core 的真实依赖图拒绝自环与多实例环。

复制方法及 JSON 后排队进入提供者 invoke_service；消费者宿主操作与提供者 RequestChannel 使用不同编号。同步、异步和早到完成均经过已有请求仲裁。取消调用提供者的 cancel_request，并等待真实完成；取消协议故障隔离两端，保留账目。最终 request_complete 在消费者侧独立执行。

提供者退役通知消费者排队 close。消费者业务关闭后，在 stop 或 effect 清理期间，仅可通过本实例/epoch 已提交的旧绑定访问 Published/Retiring 提供者，不能建立新绑定。Core 内部异步调用入口增加与同步清理一致的实例/epoch 校验；普通 C++ ServiceRef.invoke 默认门禁不变。

提供者等待 committed 租约和在途调用排空后才 stop/清理。消费者等待操作 ACK、effect 清理与最后宿主捕获退役，再释放旧租约。失败清理、协议不确定或投递故障保守保留，不能以收到结果或取消成功代替静止。主动 effect 清理也先撤回所属服务并等待消费者。

## 容量与边界

每实例最多 128 个提供者和 128 个已提交完整键，继续受模块总账目及 128 个宿主操作限制。方法计入既有 8 MiB 尚未退役输入预算。能力 JSON 最多 16 KiB、64 个唯一非空字符串，每名最多 128 个 UTF-8 字节；转义解码后的重复名称拒绝。业务 JSON 仍用严格扫描器校验并保留原字节，不经 Qt 数值转换。

公开快照增加服务 BindingSnapshot 与 committed 数量，不暴露 endpoint、DLL 指针或 Core 私有租约。本段只完成资源桥；Kernel/Session、两个业务服务 DLL 的配置激活与退出联合验收属于 M3d，UI DLL 与工具链检查属于 M4，物理卸载和替换不在本段。
