# ADR-0035：原生 JSON 服务复用 Core 实例契约

日期：2026-10-29。状态：采用（P4-M3c2）。

## 决策

NativeModule.runtime 接受已验证模块内的 NativePlugin 定义，生成 Core Runtime。宿主声明 NativeService 的依赖、输出、minor 和能力；不在 C ABI 中传递 Context、ServiceRef、std::function 或服务对象指针。C 插件仍只使用 context/effect/binding/request 等不透明编号及有界 UTF-8 JSON。

托管实例绑定真实 Context 和相同 scheduler。注册输出必须属于本代 effect、匹配宿主契约，先由 Context.provide 暂存；外层激活同时等待原生 start 与已登记 effect.ready，成功后由 Core 统一发布。Core 撤回输出、等待全部消费者及调用排空后，才进入原生实例清理。托管实例必须先关闭其 Core 所有者，不能直接通过 NativeModule.close 绕过依赖图销毁提供者。

服务调用使用 Core ServiceRef 的异步调用屏障和独立目标请求，源模块最终通过 request_complete 收取复制结果。消费者编号不作为提供者请求使用。取消发送到实际目标，不伪造源任务结束。提供者容量不足时只返回 LIMIT，不把尚未进入 DLL 的新请求当作协议故障；同步 ACK 不占异步请求仲裁预算，其 Call 和最后捕获仍分别退役。实现 minor、能力和解析后的 realm 均沿用 Core 检查，失败的动态版本检查必须在提交新依赖租约前完成。

每实例最多缓存 128 个消费绑定，宿主定义至多 128 个依赖/输出，服务名与能力符合 V1 的 UTF-8/字节/条数预算。拒绝新查询不新增 committed 租约。入口接受、排队执行和直接服务访问分别核对 Context/代次门禁，不能让迟到任务绕过 Core 停用或模块故障。

## 旧绑定与清理

CleanupContext.invokeService 只能访问本实例、本代已经提交的服务租约，允许旧绑定为 Retiring，不能获取新服务或转换为业务 Context。身份/窗口/线程检查在调用前执行，绑定和消费者的调用计数持续到真实异步源完成并交回 owner。

原生清理仅使用此前冻结的引用及受限 CleanupContext。主动撤回走 Context.withdraw，只撤回本代自有输出并等待消费者/调用；对象和键仍由 Core 保留到实例最终清理。服务资源所属 effect 在撤回屏障之后才调用 DLL cleanup，成功后释放真实 ServiceObject 租约；失败保留私有对象、回调、绑定与模块。

## ABI 与所有权

稳定 HostTable 开启 SERVICES，并保留 DISPATCH/EFFECTS。未绑定 Core 的低层实例拒绝服务操作；未协商的函数族不得通过非空全局宿主表绕过能力检查。NativeService/NativeServiceCall 是宿主源码 SDK 包装，C++ 对象从不越过业务 C ABI。

最终应答为同步 ACK，不能为该 ACK 再调用 complete。另一个已登记的 PENDING 请求可以在应答处理中到达 complete；宿主仍只复制/排队，须在原入口返回后合并，避免把合法的异步清理链误判为递归完成同一请求。V1 表布局和线码不变。

本段仅提供 Core 实例/服务底座；Kernel 注册、Session、两个服务 DLL 与真实退出链继续在 M3d 完成，同工具链 Qt UI DLL 继续在 M4 完成。默认驻留保持，不提供物理卸载。
