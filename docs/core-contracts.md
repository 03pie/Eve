# 首阶段接口契约

本文件冻结接口行为和实现顺序，不是可编译 SDK 头文件。已存在的公共类型见 `sdk/include/cordis`；下列接口随对应工作包实现后才加入公共头文件。

## 1. Result 与错误

`Result<T>` 在 success T 与 ErrorInfo 中二选一；提供 void 特化，不存在“既成功又带错误”的状态。普通调用错误通过 Result 返回，Task 拒绝同样使用 ErrorInfo。插件回调边界捕获异常并转换，禁止异常破坏内核资源账目。

第一阶段最小 API：`success(value)`、`failure(error)`、`hasValue()`、受检查的 `value()` / `error()`。错误访问行为由实现显式规定并测试，禁止返回悬空引用。复制能力跟随 T，不能要求所有服务对象可复制。错误枚举当前未分配稳定线码，不用于 C ABI 或持久化。

## 2. IScheduler、Task、Promise、取消

| 接口 | 最小行为 |
|---|---|
| IScheduler::post | 接受宿主闭包，返回是否入队；不内联执行 |
| IScheduler::isCurrentThread | 检查单写线程 |
| Task<T>::thenOn | 在指定 scheduler 异步调用成功或失败 continuation |
| Promise<T>::resolve/reject | 可跨线程完成；原子仲裁只接受第一次 |
| CancellationSource::requestCancel | 请求取消，不伪造 Task 完成 |
| Task 观察超时 | 返回超时报告，底层 setup/cleanup 仍须真实完成 |

Task 可复制共享状态，Promise 为受控生产者句柄；continuation 接收不可变结果视图，支持多个观察者，不重复 move 同一个结果。需要独占转移的产物由显式拥有权句柄表示。T=void 单独特化。关闭 scheduler 后注册 continuation 必须得到明确的投递拒绝状态，不能无声丢失。

FakeScheduler 提供 `runOne()`、`runUntilIdle(stepLimit)` 和队列大小；遇循环超过上限返回测试失败。FakeClock 显式 advance，所有生命周期测试使用 gate，不依靠真实 sleep。

## 3. Effect

接口形状：

```text
Context.effect(setup, label) -> Result<EffectToken>
EffectSetupContext.addCleanup(cleanup) -> Result<void>
EffectToken.close() -> Task<CleanupReport>
EffectToken.snapshot() -> EffectSnapshot
```

必须先将控制块登记到 owner，再调用 setup。控制块涵盖 setup、已获得资源和 cleanup 屏障。setup 期间 close 后，后到资源仍需登记并清理。清理组内逆序，失败后继续其余项。所有 close 观察同一完成结果；token 析构不关闭。

`Failed` 作为错误信息保存，资源仍未清理完成时 EffectState 保持 Closing，不能用失败状态掩盖资源存活。清理函数不得创建新 effect、等待自己的 close，或阻塞 scheduler。

## 4. Kernel / Instance / Context

```text
registerPlugin(definition) -> Result<RuntimeHandle>
mount(runtime, rawConfig, options) -> Result<InstanceHandle>
handle.stop() -> Task<StopReport>
handle.update(config, expectedRevision) -> Result<OperationHandle>
handle.awaitTarget(operation) -> Task<StableReport>
handle.awaitActive(epoch) -> Task<ActiveReport>
handle.retry() -> Result<OperationHandle>
```

命令在 scheduler 所在线程接受，所有异步完成通过队列推进；不递归激活。stop 最终终结 InstanceId，后续 update/retry 失败；依赖失效只结束当前 epoch，仍可恢复。

Context 内部保存弱实例、capturedEpoch、realm 和能力，不持有实例强引用。激活 Context 和 CleanupContext 分离；cleanup 可读已提交旧绑定，但不能启动新业务。旧 Task 结果不得向新 epoch 发布输出。

## 5. 服务与最小闭环

服务 key 为 name + interfaceMajor + realm。provide 创建 BindingId，输出在 setup 完成前为 Staged。强依赖不足时实例保持 Pending。消费者保存 committed binding；提供者退出先 Retiring，拒绝新业务，再等消费者清理后销毁资源。

首阶段只提供内置同工具链类型化调用，不跨 DLL。不通过自由获取的裸指针绕过 epoch/调用门禁。跨 ABI 的函数表和序列化协议留到 P4。

## 6. Event

emit/bail/waterfall 是同步协议；serial/parallel 使用 Task。终止使用 Continue/Stop(value) 显式类型。once 在进入回调前注销；快照调用前检查 liveness；Next 限当前调用栈且至多一次。内部观察者错误逐个隔离，不中断清理。

完整参数与返回类型在 W6 实现时形成源码 SDK，必须保留上述语义，禁止以 Qt signal/slot 直接替代这些契约。
