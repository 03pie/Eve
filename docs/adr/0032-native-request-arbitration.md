# ADR-0032：原生请求仲裁与终结屏障

日期：2026-10-28。状态：M3a 组件实现；阶段验证及审查见 [M3 开发记录](../p4-m3-development.md)。

## 决定

新增无 Qt 的 Cordis::ModuleRequests。每次激活持有独立 RequestChannel，绑定不可变 InstanceId/epoch 和进程内不复用的 context；请求编号同样不复用。除 complete 外的状态操作在 owner 线程进行，complete 只修改宿主请求控制块并投递，不进入 Kernel registry 或执行插件函数。

将入口返回、完成到达、任务结果发布、投递捕获退役四个时点分别管理。入口在栈期间有 Call 租约；未终结操作按 setup/cleanup/call/callback 分类登记；复制/排队结果有 QueuePayload 租约。只有入口已退出，才核对 PENDING 与复制完成；结果回调通知投递后，等待宿主投递捕获退役再释放对应账目。

合法 scheduler 可以在 post 返回前已经运行队列副本。若最后一个投递捕获在生产者线程析构，则再向 owner 交接退役，不从工作线程宣布资源排空。测试使用显式 gate 复现该次序，而非依赖 sleep。

close 只拒绝新业务，仍接收 Stop/Cleanup；seal 才不可逆地拒绝所有新请求。drained 仅在 seal 之后且真实账目退役后发布。不能在 close 时先发布完成，然后允许清理请求重新打开账目。模块代次的 ledger.beginDraining 应在业务清理、请求 seal/drained 完成后执行，不能提前禁止清理登记。

取消只记意图，由后续生命周期桥调用插件 cancel_request；不伪造请求完成或强改插件的最终状态。已发布结果的请求不再登记新的取消意图，投递捕获退役仍独立等待。已关闭/已 seal 上下文的合法未完成请求仍能 complete。新 context/新 epoch 不接受旧编号组合。

重复/未知完成被拒绝并固定协议诊断，不重复推进任务。入口返回未知状态、同步返回却发送完成，或者调度器拒收/丢弃/抛错时，错误可观察且账目保守保留；不会以 Promise 失败代替资源静止。未终结通道被遗弃时控制块自保留并 pin，保留量受请求/字节预算约束；M3b 还须约束宿主上下文总数及正确持有通道。

JSON 传输使用有界 UTF-8 单值扫描器，依据 [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259.html) 文法；实现限制容器深度 64，拒绝非 Unicode 标量的字符串编码/转义。文本不经浮点 DOM，重复成员名及数字原文保持；业务 schema 另行负责语义。空 span 表示没有结果。默认单值 1 MiB、同时复制/排队字节 8 MiB、在途请求 256，并检查配置及 ledger 容量。

## 阶段边界

这是供原生宿主复用的生产请求组件，不是测试宿主的升级，也不代表生产 HostTable、DLL 生命周期、Kernel/Session、服务/effect 桥已接入。M3b–M3d 继续实现真实调用与资源屏障；M4 和 P5 保持独立，P4 默认驻留不改变。
