# W6 交付与验证

日期：2026-10-24。状态：首阶段工作单范围已完成。范围：同工具链类型化 EventBus、五类分发、effect 归属、快照及异步屏障。

## 接口

`EventBus<T>` 在开放 owner-thread scheduler 上构造，副本共享索引。T 必须可复制、可移动，同时作为输入参数和显式 Stop 值类型。`declare(EventKey, EventMode)` 为名称、精确 realm 键固定协议；重复兼容声明幂等，不兼容声明拒绝。不同总线是独立命名空间；当前没有全局类型擦除 schema 注册表，不作为跨 ABI ValueTuple 协议。

`on(owner, key, callback)` 登记 emit/bail/Observer 同步回调；`onAsync` 登记 serial/parallel；`onWaterfall` 登记接收 EventNext 的同步中间件。owner 只能是同 scheduler 的 Context 或 EffectScope，返回 EffectToken；丢弃 token 不注销，显式 close 或所属作用域清理才负责关闭。Context 监听器调用前还检查实例 epoch、退役与清理权限。realm 为 EventKey 的显式精确域，不自动套用服务 RealmMap，无跨域回退。

```cpp
EventBus<int> events(scheduler);
events.declare({"validate"}, EventMode::Bail);
auto listener = events.on(context, {"validate"}, [](const int& value) {
    return Result<EventDecision<int>>::success(
        value == 0 ? EventDecision<int>::Stop(0) : EventDecision<int>::Continue());
});
auto result = events.bail({"validate"}, 0);
```

| 协议 | 返回与行为 |
|---|---|
| emit | Result<void>，按位置顺序执行全部，忽略 Stop，首个错误结束 |
| bail | Result<EventDecision<T>>，首个 Stop 或错误结束；零和空字符串可停止 |
| waterfall | Result<EventDecision<T>>，Next 显式调用下游并可变换参数；不调用 Next 即 veto |
| serial | Task<EventDecision<T>>，排队启动，逐项等待，首错或 Stop 结束 |
| parallel | Task<EventReport<T>>，排队启动全部合格项，等待全部，按 listener id 收集每项 Result |
| observe | Result<vector<ErrorInfo>>，独立 Observer 协议逐个隔离错误，继续其余监听器 |

全部继续或空分发时返回 Continue；parallel 空分发返回空报告。parallel 的监听器失败保存在报告各项中，Task 本身的失败用于入口校验或报告构造失败。prepend 改变调用位置，不改变编号，因此 parallel 报告仍按编号排序。异步回调引用的输入参数由分发控制块持有，到源任务完成并由 owner 处理结果为止；之后禁止保存使用。

## 生命周期与边界

分发在入口获取监听器快照，新增不进入当前快照；每次进入回调前重查 alive、取消令牌、实例权限及过滤器。过滤器执行后再次检查权限。once 在进入回调前失活，重入或多个排队分发不会重复执行；未通过过滤器不消耗 once。once 关闭调用入口，其 effect 捕获仍由显式关闭/作用域清理释放。

关闭令牌立即请求取消，已经被快照持有但尚未进入的项会跳过。快照保留回调闭包；effect 清理必须等待所有持有该记录的快照退出。异步分发即使已注销监听器，也保持所有参数和闭包到真实完成，不提前报告资源静止。这比只等待当前正在执行的回调更保守：serial 暂停也会保留其尚未调用的快照项。

EventNext 的副本共享一次性窗口，仅在当前同步回调栈内使用一次；重复、正常返回后或异常退出后调用均返回 NextInvalid。错误线程返回 WrongThread。保存 Next 不保留分发快照，窗口退出时撤销其下游闭包。不能用同步回调启动未登记的异步工作。

scheduler 拒绝或丢弃异步启动/完成通知时，返回 Task 保持 pending，自保留分发和闭包；EventBusSnapshot.deliveryBlocked 与 activeDispatches 用于诊断。关闭后禁止新分发/登记，owner 仍可读取诊断。资源未静止时不能强制清空记录。回调不得等待自身 listener/实例关闭完成，否则会形成用户 Task 等待环，本轮不检测这种任意等待。

Observer 协议仅提供错误隔离底座，尚未自动接线 Kernel 状态通知。post、跨线程参数编码、跨 ABI schema、ModuleLease 和 Qt 适配不在首阶段 W6 范围；异步任务可跨线程完成，插件对象访问仍遵守 owner 策略。

## 验证

十三项 `w6.events_*` 覆盖 snapshot、once_filter、bail_observe、next、serial、parallel、lifetime、epoch、blocked、validation、snapshot_lifetime、async_edges、result_copy。包括回调重入、注销快照、资源闭包保留、显式零/空停止值、Next 逃逸、异步乱序完成、源失败、过滤器抛错、跨 scheduler 拒绝、源完成及启动投递丢弃。

`w6.event_demo` 运行 [控制台示例](../examples/builtin/event_demo.cpp)：Context 注册异步保存事件，Kernel 关闭等待保存任务完成，再报告监听器及实例清理完成。

GCC 13.1、MSVC 19.44 x64 Debug/Release 四配置各 110/110 通过，见 [开发进度](progress.md)，日志为 `build/review-w6-core-<compiler>-<mode>.log`。event.hpp 通过 GCC C++17 独立包含、警告即错误检查。测试使用 FakeScheduler、gate 与线程 join，不依赖真实 sleep。未运行 ASan/TSan、Qt 或 DLL 验证。

下一工作包 W7：提供者/消费者/事件联合示例、残留快照与完整内核验收汇总。

提交前审查补充负载复制异常回归：serial 返回 CallbackFailed，parallel 将单项复制失败记录为该监听器错误；报告构造失败则拒绝整个任务。源任务已完成的路径均释放分发屏障，不能误报投递受阻而永久挂起。详见[审核记录](code-review.md)。
