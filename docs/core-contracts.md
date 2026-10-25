# 首阶段接口契约

本文件冻结接口行为和实现顺序。W1–W6 已有源码 SDK，见 `sdk/include/cordis`；各包验证范围见交付记录。

## 1. Result 与错误

`Result<T>` 在 success T 与 ErrorInfo 中二选一；提供 void 特化，不存在“既成功又带错误”的状态。普通调用错误通过 Result 返回，Task 拒绝同样使用 ErrorInfo。插件回调边界捕获异常并转换，禁止异常破坏内核资源账目。

第一阶段最小 API：`success(value)`、`failure(error)`、`hasValue()`、受检查的 `value()` / `error()`。错误访问行为由实现显式规定并测试，禁止返回悬空引用。复制能力跟随 T，不能要求所有服务对象可复制。错误枚举当前未分配稳定线码，不用于 C ABI 或持久化。

W1 实现：错误访问抛 `BadResultAccess`；左值访问返回引用，右值访问按值返回，避免临时 Result 的内部引用逃逸。Result 可复制构造（取决于 T）和移动构造，禁止重新赋值，保持唯一有效分支。Task 对外保存 `shared_ptr<const Result<T>>`，多个观察者共享结果，不重复移动 payload；const 视图不是对指针所指对象的深度不可变保证。

## 2. IScheduler、Task、Promise、取消

| 接口 | 最小行为 |
|---|---|
| IScheduler::post | 接受 ScheduledJob（run/discarded），返回是否入队；不内联执行 |
| IScheduler::isCurrentThread | 检查单写线程 |
| Task<T>::thenOn | 在指定共享 scheduler 上异步调用 `callback(const Result<T>&)`，返回 `Task<void>` 投递回执 |
| Promise<T>::resolve/reject | 可跨线程完成；原子仲裁只接受第一次 |
| CancellationSource::requestCancel | 请求取消，不伪造 Task 完成 |
| observeFor / FakeClock（W2） | 超时观察返回 Completed/TimedOut，底层 Task 不变；时钟关闭或参数错误通过观察 Task 报错 |

Task 可复制共享状态，Promise 为受控生产者句柄；continuation 接收不可变结果视图，支持多个观察者，不重复 move 同一个结果。需要独占转移的产物由显式拥有权句柄表示。T=void 单独特化。关闭 scheduler 后注册 continuation 必须得到明确的投递拒绝状态，不能无声丢失。

FakeScheduler 提供 `runOne()`、`runUntilIdle(stepLimit)` 和队列大小；遇循环超过上限返回 StepLimitExceeded。允许任意线程 post/close，但只允许创建线程 pump；拒绝嵌套 pump，门禁覆盖任务捕获析构及异常返回。close 丢弃尚未取出的队列任务并逐项执行宿主 discarded 通知，已取出的在途任务允许完成；不持锁调用回调、移动用户可调用对象或析构捕获。FakeClock 已在 W2 引入，测试使用显式时钟/gate/线程 join，不依靠真实 sleep。

`thenOn()` 的回执报告回调已执行或失败：成功/失败回调均通过同一个 Result 参数分支；回调抛错则回执为 CallbackFailed，不修改源任务。source 失败被成功观察时回执仍成功。scheduler 已关闭/销毁、post 抛错或已入队任务被丢弃时回执为 SchedulerClosed。若源任务还没完成，观察者尚未入队，此时关闭 scheduler 不取消源任务；源任务完成或最后 Promise 释放后，回执才报告投递失败。调用者必须保留并检查回执，不能靠业务回调获知“回调没有执行”。

Promise 可以复制为同一生产者的共享控制句柄；最后一个 Promise 未完成便释放时，源任务以 BrokenPromise 失败。`tryResult()` 返回拥有生命周期的不可变结果快照，pending 返回空指针。Task/Promise 支持跨线程注册、完成；同一实例的移动/赋值不得与访问并发，移动后句柄只用于销毁或重新赋值。CancellationToken 只支持查询，不在请求取消时内联调用用户处理器。

调度器/完成协议对正常运行内存资源成立；没有承诺 OOM 下可继续执行。闭包需要符合不抛异常析构约束，插件边界的模块引用和专用线程析构将在后续适配中实现。

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

### W2 实际接口与边界

`EffectScope(scheduler)` 是 Context 的资源归属底座。构造要求打开的 owner-thread scheduler，否则抛 invalid_argument。`scope.effect(setup, label)` 返回 Result<EffectToken>；成功仅表示已接受登记，启动结果由 token.ready() 观察。setup 接收按值 EffectSetupContext，返回 Task<void>。

`addCleanup(function<Task<void>()>)` 在 setup 完成通知被内核处理前接受资源，包括 close 已请求但 setup 还在运行时。完成后拒绝，控制块销毁后旧上下文返回 StaleContext。所有可变操作和快照只在 owner 线程进行；跨线程 setup/cleanup Task 完成通过 scheduler 回来处理。

`scope.close()` 先对所有 effect 标 Closing 并请求取消，再通过队列清理，组间可以同时等待，组内逆序。`token.close()` 只关闭该组，副本共享同一个 CleanupReport 结果。setup 中请求 owner close 安全；关闭期间 setup 后到的 cleanup 仍被收集。setup 已过期时 token.ready() 报 OperationCancelled，不能发布新的 Active 结果。

`context.adopt(childToken)` 将子组 close 加入父组 cleanup 队列，等待子组真正释放；要求相同 scheduler，并检查显式收养图环。组间没有全局 LIFO 顺序。任意用户 Task 构造的等待环仍不能全面检测，遇到时保持等待并通过超时观察诊断；实例/服务等待图留到 W3/W5。

close 的结果为 `Task<CleanupReport>`：`quiescent` 表示是否确认资源释放，errors 是 setup/cleanup 错误汇总。setup 失败但回滚全部成功时可 quiescent=true 且 errors 非空；任一 cleanup 失败则保留失败闭包、控制块和 scope 所有权 pin，状态保持 Closing，结果 quiescent=false。清理尚未结束、调度投递被丢弃时 close 继续 pending，快照以 setupPending、cleanupPending、deliveryBlocked、retainedCleanups 解释原因。

owner/token 析构不承担异步关闭。必须在 scheduler 关闭前显式 close 并检查 quiescent；丢弃未关闭 owner、清理失败或投递中断时会保守保留资源记录，当前没有强制释放/自动重试入口。scope 有任何 cleanup 正在等待时，暂时拒绝新建 effect；这是 W2 的保守策略，W3 的 Context mode 若细化该行为须单独评审。

### W2 时钟与观察

`FakeClock` 不可复制，after/advance/close 及查询在创建线程使用；TimerToken.cancel 可跨线程。时间以单调毫秒计，拒绝负值、溢出和嵌套 advance；同 deadline 按登记顺序触发，零延迟也不内联。advance 有步数预算，耗尽时停在当前已处理时间，StepLimitExceeded 的 causes 保留本次已发生的回调错误；再次推进由调用方决定。timer 抛错被汇总，不饿死其余到期项。

`observeFor(source, clock, timeout)` 返回 `Task<Observation<T>>`。Completed 持 source 的共享结果快照，源失败也是 Completed，真实错误在 snapshot 中；TimedOut 不包含结果也不取消 source。真实完成和到期竞争只产生一次观察结果，完成时不等待业务 continuation 队列排空。观察时源已经完成则立即形成结果，不创建定时器。业务回调仍须 thenOn，observeFor 不会内联执行业务代码。

取消的 FakeClock timer 载荷在下一次 advance 或 close 时释放，不承诺 cancel 即析构；source pending 时仍保留必要观察账目直至其完成。FakeClock 是测试工具，不是生产 timer、Qt 资源适配器或 DLL 卸载屏障。

## 4. Kernel / Instance / Context

W3 已有 `instance.hpp` 的生命周期、update/retry、operation/awaitTarget/awaitActive、Context::effect/config/onCleanup 与 CleanupContext，以及 `kernel.hpp` 的 registerPlugin/mount/close/snapshot，范围与验证见 [W3 开发记录](w3-development.md)。每代资源静止后才启动最新配置；失败静止后允许 retry，stop 才终结实例。Runtime/Instance/Operation 标识进程内唯一；retry 不改变配置修订。awaitActive 仅查询已启动代次，未来代次报 InvalidArgument，历史成功不授予当前业务权限。下列接口为概念形状，服务与域视图的实际签名见第 5 节及 SDK；通用挂载 options 仍为目标契约。

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

W3 实现 `Context::onCleanup(function<Task<void>(CleanupContext)>, label)`，返回独立清理组的 EffectToken。CleanupContext::snapshot 仅在 owner 线程和该次清理异步任务有效期内读取旧代不可变信息；任务真实完成或回调抛错即失效，后续查询报 StaleContext。没有业务注册接口，不能转换为 Context。清理中的同实例业务注册仍受 EffectScope/epoch 门禁限制。组间不保证逆序。W4 增量提供了下节所述旧绑定受限清理调用，不能仅凭此快照能力获取任意服务。

## 5. 服务与最小闭环

服务 key 为 name + interfaceMajor + realm。provide 创建 BindingId，输出在 setup 完成前为 Staged。强依赖不足时实例保持 Pending。消费者保存 committed binding；提供者退出先 Retiring，拒绝新业务，再等消费者清理后销毁资源。

首阶段只提供内置同工具链类型化调用，不跨 DLL。不通过自由获取的裸指针绕过 epoch/调用门禁。跨 ABI 的函数表和序列化协议留到 P4。

W4 的 `service.hpp` 提供 ServiceRegistry::stage/find/createRealm/snapshot、ProviderHandle::publish/retire/release 与 ServiceRef<T>::withService。Staged 不可查，Retiring 保留键并拒绝新调用；同步调用或 committed 租约存在时禁止 release。withService 回调必须返回 void，不导出对象所有权，禁止保存传入对象引用或用它启动异步操作；异步调用使用下述 invoke。范围、线程及所有权约定见 [W4 开发记录](w4-development.md)。

后续增量已实现 Context::consume，将精确绑定固定到本实例代次账目；其 ServiceRef 同时受消费者业务能力和提供者状态门禁限制。CleanupContext::withService 只访问本实例本代已提交的旧绑定，允许 Retiring，但受清理调用窗口限制。实例在 effect 清理与在途 committed 调用均静止后才释放本代租约，清理失败继续保留，提供者 release 也检查 committed 保留。低层 registry.find 不授予实例权限；托管实例的自动收敛接口见下段。

W4 已接入自动收敛：Kernel::registerPlugin 接受 ServiceRequirement::of<T>(key, minimumMinor, capabilities) 强依赖和 ServiceOutput::of<T>(key, required, metadata) 输出声明。缺依赖时 Pending、不执行 activate；类型/minor/能力不匹配明确失败；服务就绪后激活前固定租约。Context::provide 仅在 Loading 登记声明过的 Staged 输出，发布前核对所有必供项，缺项、未声明输出、冲突或失败均回滚，空输出表禁止 provide。提供者退役先取消和传播业务失效，再等待输出消费者归零，最后清理自身并释放输出与上游租约；消费者保配置恢复新 epoch。实际租约回边已拒绝，声明与父子联合图留待 W5。

RealmMap 为不可变名称覆盖视图，可由 isolate(name, realm) 派生并通过 Kernel::mount 的第三个参数传入；预检依赖、输出声明、provide 与 consume 都按视图解析。Context::isolate 派生同代 Context，不修改父句柄，未覆盖项继承；覆盖优先于传入键 realm，查找不得回退其他域。外域视图及映射后重复输出键在挂载前拒绝。

ServiceRef::invoke 使用 `Task<void>(T&)` 异步回调，在进入回调前固定 provider/consumer CallLease，直到源任务真实完成且 owner 处理结果后才释放计数。回调抛错转换为 CallbackFailed，源任务失败保留原错误；回调及后续服务对象访问遵守 owner 线程策略。完成投递若被阻断，返回任务保持 pending、对象和计数保留，BindingSnapshot.deliveryBlocked 为 true。withService 仍只接受同步 void 回调，CleanupContext 只开放有效窗口内的同步旧绑定访问。

InstanceSnapshot.failure 提供本轮错误，包括后续更新中的输出校验失败；新一轮尝试开始时清除。ready() 的首次结果不改写，后续成功/失败通过对应操作 target 与实例快照判断。缺依赖自动 Pending/恢复；类型、minor 或能力不兼容为明确失败，需要显式 retry 或 update 再尝试。

W5 实现 `Context::inject(const Runtime&, string config = {}) -> Result<Instance>`，子实例继承调用 Context 的域视图和托管注册表，独立等待声明依赖，不扩大父实例重启范围。父更新/停止先请求全部子实例停止，同时退役输出，等待子与消费者静止后再清理自身。子清理失败保留父资源，错误汇入父报告；旧 Context、关闭 scheduler、错误线程或父退役期间拒绝 inject。InstanceSnapshot.children 统计本代持有的直接子记录，父确认静止才清空。

W5 将子→父所有权边加入消费者→提供者的实际图，拒绝父消费后代输出等联合环；激活前还预检解析后的强依赖与必供输出声明。候选提供者缺失或多义时不臆测连边，实际绑定优先。发现环时 DependencyCycle 的 causes/message 提供逐边完整路径；尚未启动的实例不递增 epoch、不执行 activate。图范围和非合作任务边界见 [W5 交付记录](w5-development.md)。

已稳定返回 PendingMissingDependency 的实例，后来预检因环或版本不兼容而 Failed 时，会创建新的自动收敛操作发布 Failed；旧操作仍返回历史 Pending，配置修订不递增。当前失败通过 operation/awaitTarget 与 snapshot.failure 一致观察。

### 实例级附加依赖（Q4c2b）

Kernel::mount 和 Instance::mount 可传入 additionalDependencies，按解析后的完整服务键与 Runtime 固定需求联合，minor 取最大值、能力取并集。同键不同 C++ 类型拒绝；重复键归一化为一个提交租约。Runtime 本身不修改，不因配置创建新定义。实例的 update/retry 保留契约，停止后 Kernel::remount 也复制完整实例依赖；修改有效契约须停止并重建。Loader 的类型目录、接受前复核和配置规划规则见 [ADR-0017](adr/0017-instance-inject-contracts.md)。

## 6. Event

emit/bail/waterfall 是同步协议；serial/parallel 使用 Task。终止使用 Continue/Stop(value) 显式类型。once 在进入回调前注销；快照调用前检查 liveness；Next 限当前调用栈且至多一次。内部观察者错误逐个隔离，不中断清理。

W6 的 `event.hpp` 实现 EventBus<T>，每个 EventKey（名称、显式 realm）固定一种 EventMode，T 固定同工具链参数与停止值类型。on/onAsync/onWaterfall 要求同 scheduler 的 Context 或 EffectScope，返回 EffectToken；监听器始终归属资源作用域。Context 监听器调用前重查 epoch 与业务能力。prepend 改变调用位置，parallel 报告按 listener id 排序。过滤器执行前后重查权限，once 在回调之前失活。

异步分发入口也排队，参数由分发控制块持有。parallel 返回 EventReport<T>，各项 Result 汇集全部监听器错误；serial 首错终止。快照中的已注销项跳过，但其闭包保留仍阻止 effect 报告静止，直到该快照释放。启动或完成投递受阻时保守保留任务与资源，通过 EventBusSnapshot 诊断。observe 使用独立 Observer 协议隔离错误；尚未自动接线内核通知。

EventNext 共享一次性同步窗口，回调退出或抛错后失效，不保留下游快照。空分发返回 Continue 或空报告。当前仅类型化独立总线，没有全局跨类型 schema、post、跨 ABI ValueTuple 或 Qt 适配；图外的用户回调自等待关闭任务也不能自动排除。详见 [W6 交付记录](w6-development.md)。
