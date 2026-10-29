# ADR-0034：原生排队操作与 effect 资源桥

日期：2026-10-29。状态：采用（P4-M3c1）。

## 决策

基础 HostTable 开启 DISPATCH 和 EFFECTS，函数族均为真实生产实现；SERVICES 继续为关闭状态，由 M3c2 接入。宿主函数仍使用进程寿命表、私有实例控制和每次跨边界 Call 租约。

插件发起的宿主操作与实际目标调用分配不同的、全进程不复用请求编号。dispatch 返回 PENDING 和消费方编号之前复制 UTF-8 JSON，目标 dispatch 与最终 request_complete 均排队。目标入口通过 RequestChannel 合并返回/完成；最终应答是另一次受 Call 屏障保护的同步入口，不允许 PENDING。取消排队操作可以跳过目标入口，但仍交付 CANCELLED 应答；已进入的异步工作只发送协作取消，等待真实 complete。

每个实例使用真实 Core EffectScope。effect_create 先注册宿主资源和 setup/cleanup 引用，再允许插件创建资源；effect_ready 恰好报告一次。主动清理和整体退出使用同一 EffectToken，失败保留清理闭包、模块和诊断。整体退出按资源注册逆序逐个等待，不在清理尚未完成时销毁实例。

关闭先拒新业务、取消并等待已接受操作及已经开始的清理，再 stop，随后清理剩余 effect、seal/drained、关闭宿主回调门禁并 destroy。不能把正在进行的主动清理当作 stop 失败，也不能在仍需清理请求时 seal 通道。

## 捕获与容量

结果发布、ACK 返回和最后投递捕获退役分别记账。每个操作/资源的最后宿主捕获析构后才释放 Callback/QueuePayload 引用；跨线程析构只释放线程安全租约，退役确认排队回 owner。最终静止同时等待请求通道、宿主调用和资源捕获。JSON 总字节在最后捕获释放后才返还预算，避免只移除索引就重复使用尚占用的容量。

最多 128 个活动宿主操作、128 个活动 effect、8 MiB 尚未退役的操作参数，且继续受模块总账目预算约束。释放后可再创建，旧编号不复用。公开诊断只包含值、Core 资源快照和数量，不暴露 HostTable、私有 callback 或 DLL 指针。

## 后果

资源调度包装器将 Core effect 投递的拒收/丢弃/异常转换为宿主不静止故障；不隐瞒失败，也不伪造清理完成。默认进程驻留保持。M3c2 服务桥、M3d Kernel/Session 与两个服务 DLL、M4 同工具链 UI DLL 仍是必需后续交付。
