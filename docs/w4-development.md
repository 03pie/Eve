# W4 开发记录

日期：2026-10-23。状态：W4 首阶段工作单范围已完成。包含服务绑定、实例 committed 租约、托管输出校验、强依赖 Pending/恢复、自动退役传播、RealmMap 派生视图及同步/异步调用保留。下一工作包 W5：可选依赖、父子实例和联合等待图。

## 已实现

`sdk/include/cordis/service.hpp` / `src/core/service.cpp` 提供：

- ServiceKey = 名称、接口主版本、RealmId，完整键精确匹配，无跨域回退。根域为 RealmId{1}；createRealm 分配进程内不复用的私有域，其他注册表不能用它登记服务。
- ServiceRegistry::stage 保留完整键并创建进程内唯一 BindingId，Staged 不对业务查找可见。空对象、空名称、零主版本、未知 realm 被拒绝；Staged/Published/Retiring 同键均冲突。
- ProviderHandle::publish/retire/release 显式推进状态。发布和业务调用要求开放的 owner-thread scheduler；退役立即拒绝新业务，不能重新发布；只有已退役且无在途调用、无 committed 保留时才能释放注册表对象所有权和键。
- ServiceRef<T> 弱引用绑定，不导出对象所有权。find 检查同工具链 C++ 类型，withService 在每次调用前检查线程和 Published 状态，并为整个同步回调保持调用计数。调用中重入退役不会销毁仍被访问的对象，release 返回 ContextClosing，回调退出后需显式再次释放。
- 同步回调异常转换为 CallbackFailed 并恢复计数；withService 要求精确返回 void，返回 Task 等其他类型在编译期拒绝。同步回调不得保存服务引用/指针，也不能启动依赖该同步访问的异步业务；异步操作必须使用 invoke。C++ 不能阻止恶意闭包绕过此约定。
- release 先标 Released、移除旧键，再在容器外释放对象，允许对象析构重入索引。旧引用不会重新绑定到同键新服务。外部若另持 shared_ptr，对象实际析构仍取决于外部所有权。
- 注册表持有全部未释放绑定；资源未释放时固定索引所有权。注册表/提供者句柄析构不代表异步清理完成，必须显式 retire/release；遗漏释放将保守保留资源。注册表副本共享同一索引；移动后原注册表/提供者/服务引用仅用于销毁或重新赋值。
- Context::consume<T>(registry, key) 将当前 Published 绑定固定到本实例本代 committed 账目，返回弱 ServiceRef。本代同一注册表、完整键和类型的重复 consume 复用已提交保留；不同注册表不混用。跨 scheduler 连接被拒绝。
- committed ServiceRef 的普通调用同时检查消费者 epoch、业务模式和提供者 Published 状态；停止、退役或清理期间拒绝普通业务。CleanupContext::withService(ref, callback) 只允许有效清理窗口内、本实例本代已提交的引用，可访问旧绑定的 Retiring 状态；普通 registry.find 引用、其他实例/代次租约和过期清理上下文均不能冒充清理能力。
- 实例只有在全部 effect 清理成功且已进入的 committed 同步调用退出后才释放本代租约，随后才能换代或 Disposed。清理失败保留全部租约，不伪报资源静止；ProviderHandle::release 还要求 committedLeases 为零。BindingSnapshot 与 InstanceSnapshot 暴露保留数量，解释资源已清理但调用仍未退出的等待。

这里的清理调用提供了有效期受限的旧绑定访问路径，尚无独立可传递 CleanupLease 句柄。registry.find 仍为宿主底座查询入口，不携带实例权限；插件依赖应通过 Context::consume 获取，不能将低层 find 当成实例依赖接口。

## 托管依赖闭环

- Kernel 持有共享 ServiceRegistry，挂载时交给实例；Kernel::services 返回宿主索引句柄，用于创建隔离域或诊断。已有无注册表的直接 Instance::mount 仍可用于生命周期测试；带声明依赖时必须传入匹配 scheduler 的注册表，否则明确失败。挂载不接受跨 scheduler 托管索引。
- registerPlugin 的第三个参数为 `vector<ServiceRequirement>`，通过 `ServiceRequirement::of<T>(key)` 声明强依赖。激活前检查全部服务，缺失时保持 Pending，epoch 不递增、activate 不执行，操作返回 PendingMissingDependency；类型不匹配返回失败。全部可用后先固定 committed，再调用 activate。Context::consume<T>(key) 从托管索引获取并复用这些租约。
- Context::provide(key, shared_ptr<T>) 仅在启动中登记本代 Staged 输出，插件只获得 BindingId，不能自行发布/释放。所有输出必须以 ServiceOutput::of<T>(key, required, metadata) 声明；空输出表表示禁止 provide，required=false 允许可选输出缺席。发布前检查所有必供项；缺项、错误类型、未声明输出或冲突均阻止 Active 并回滚，插件忽略 provide 错误也无法绕过。所有输出验证通过后才在同一 owner 步骤发布，消费者通知全部排队。
- 输出退役会同步关闭消费者本代业务入口，并沿实际租约传播失效；每个实例先广播取消并退役自身输出，随后等待下游消费者保留和在途调用退出，最后才开始自己的 effect 清理。EffectScope::requestCancel 仅负责提前广播取消，close 仍承担最终资源屏障。没有全局串行 LIFO。
- 索引变化经弱订阅排队触发 Pending 重试，不递归激活；实例最终停止时解除订阅。依赖失效只结束 epoch，保留配置和修订；静止后进入 Pending，服务恢复时自动激活新代。每次自动收敛有独立 OperationId，已经返回 Pending/Active 的旧操作结果不改写。
- 提供者输出只在自己的清理和所有已进入的本代调用结束后释放；清理失败时保留输出与上游 committed。下游清理失败时，上游保持 Unloading 和未完成 stop，不伪报 Disposed。
- committed 时检查消费者→提供者的实际图，拒绝会形成环的动态回边。此检查尚不包含声明预检、父子所有权边或完整路径诊断，留待 W5。声明循环缺少已发布服务时仍可能同时 Pending，当前不声称能预先识别。

线程约束：可变操作、查找、诊断和调用入口均在 scheduler owner 线程。scheduler 关闭后拒绝新登记、发布、查找和调用，仍允许 owner 执行同步 retire/release 和读取残留快照；retire/release 命令本身不自动重试。异步调用的完成处理仍须由开放的 scheduler 投递。

## 输出描述、域视图和异步调用

- registerPlugin 的第四个参数为输出描述表；Runtime 构造也接收相同描述。服务实现 minor/capabilities 来自输出描述，强依赖通过 ServiceRequirement::of<T>(key, minimumMinor, capabilities) 要求最低 minor 和能力子集。主版本/realm 决定键，C++ 类型、minor 或能力不兼容明确失败，不假装缺依赖。空名称、零主版本/realm、重复键、无效类型及空/重复能力名称在描述创建时拒绝。
- RealmMap 是不可变的名称覆盖视图。`parent.isolate(name, realm)` 返回派生副本，保留其他覆盖；`Kernel::mount(runtime, config, realms)` 将视图同时用于依赖预检、provide 和 consume。Context::isolate 返回同代派生 Context，不修改父句柄。名称覆盖优先于传入键的 realm，未覆盖项保留原键；不存在的私有域服务不回退根域。挂载前校验 realm 归属与映射后输出重键，旧 Context 在关闭/换代后不能派生新能力。
- 冲突错误包含名称、主版本、realm 和原提供者 InstanceId，必供缺项及能力缺失错误包含具体名称。
- InstanceSnapshot.failure 保存当前失败原因，包括首次激活成功之后，某次更新遇到输出缺项、依赖不兼容或清理失败的错误。ready() 仍保持 W3 的首次激活观察语义，后续操作通过 target 与当前快照诊断；不能用首次成功的 ready() 判断新代成功。新一轮尝试开始时清除旧失败原因。
- ServiceRef::invoke 接受 `Task<void>(T&)`。在进入回调前固定绑定和消费者 CallLease，保持到源任务完成并由 owner 处理结果；源失败保留原错误，回调抛错转换为 CallbackFailed。回调入口在 owner 线程，后续访问服务对象的工作也必须遵守 owner 线程策略。返回任务完成后不得继续持有对象引用。
- 异步完成通知若被 scheduler 拒绝或丢弃，返回任务保持 pending，调用计数、对象和相关控制块保守保留；BindingSnapshot.deliveryBlocked 说明原因。源任务完成不等于所有权已释放，不能关闭调度器后伪造 quiescent。当前不支持强制释放此类资源。

## 实测

GCC 13.1、MSVC 19.44 x64 Debug/Release 各 86/86 通过（含 W1–W3 回归，W4 共 22 个独立 CTest 场景）。MSVC 使用已记录的临时目录与依赖前缀设置。提交前复核日志位于 `build/review-w4-<preset>.log`，此前增量记录见 `build/dependency-<preset>.log`、`build/committed-<preset>.log`、`build/w4-<preset>.log`。

| 新增 CTest | 行为证据 |
|---|---|
| w4.service_lifecycle | 参数验证、Staged 不可见、键冲突、退役门禁、释放和新 BindingId、旧引用失效 |
| w4.service_realms | 主版本与 realm 精确隔离、外域登记拒绝、无跨注册表查询 |
| w4.service_call_barrier | 调用中重入退役、对象保留、嵌套新调用拒绝、异常恢复计数 |
| w4.service_threads_close | 错误线程无状态修改、调度器关闭门禁、关闭后同步释放 |
| w4.service_ownership_reentry | 注册表句柄销毁后仍保留未释放资源、对象析构重入同键登记 |
| w4.committed_retirement | 退役拒绝新业务、旧绑定清理调用、异步消费者清理阻止提供者释放 |
| w4.committed_boundaries | 跨 scheduler 提交拒绝、跨实例/未提交清理引用拒绝、线程边界 |
| w4.committed_epochs_failure | 换代释放旧租约、旧引用失效、清理失败保留租约 |
| w4.committed_reentrant_call | 同步调用内重入停止并泵送队列，实例仍等待在途调用退出 |
| w4.dependency_recovery | A01–A03：Pending、不发布 Staged、发布后激活、消费者先清理、同名新绑定恢复且保留配置 |
| w4.dependency_transitive | C→B→A 传递清理，根关闭广播后等待下游 gate |
| w4.dependency_update_draining | 等待消费者期间连续更新不提前清理提供者，旧操作被覆盖，静止后恢复最新目标 |
| w4.dependency_rollback | 输出冲突即使被忽略也回滚；启动失败撤回 Staged 输出 |
| w4.dependency_cycle_realm | 动态实际环拒绝、根域提供者退出不影响私有 realm 同名提供者 |
| w4.dependency_blocked_cleanup | 下游清理失败，提供者保持资源和 pending stop，不执行上游清理 |
| w4.dependency_pending_stop_type | 最终停止的 Pending 实例不复活；类型错误不调用 activate |
| w4.contract_required | 必供缺项整体回滚、可选缺席成功、错误类型/未声明/空声明输出不能绕过；首代成功后的更新缺项仍可诊断 |
| w4.contract_versions | minor 与能力子集校验、不兼容时不调用 activate、实现元数据可诊断 |
| w4.contract_realms | 父 Context 不变、派生项继承、无跨域回退、两实例声明依赖按各自视图解析、根退出不影响私有域 |
| w4.contract_async | 源异常/失败、异步在途调用保留对象与消费者、退役阻止新调用、源完成后释放 |
| w4.contract_async_blocked | 错误线程拒绝、关闭后异步完成投递阻断、保留调用计数及对象 |
| w4.contract_validation | 重复描述、无效能力、外域视图及映射后重键拒绝 |

service.hpp、instance.hpp、kernel.hpp 独立翻译单元通过 GCC C++17 警告即错误检查；返回 Task 的回调编译检查按预期失败于静态断言（`build/service-async-rejected.log`）。未运行 ASan/TSan、Qt 或 DLL 验证。

## 退出判定与后续范围

工作单要求的 D01/D02/D03、传递依赖清理、realm 隔离与冲突诊断已有真实实现及上述测试证据。当前已提交引用有 owner/epoch 门禁，低层 registry.find 引用没有。只对显式 ServiceRequirement 声明的缺依赖自动 Pending；未声明的动态 consume 仍返回 DependencyMissing，调用方直接抛错将走失败回滚。

W5 继续完成可选依赖子实例、父子所有权与依赖联合等待图、声明环预检和完整环路径。异步服务入口当前返回 Task<void>，泛型值结果、跨 ABI 数据及线程模型扩展不在本轮；Qt/DLL、强制回收和历史记录裁剪均未实现。清理失败或不合作任务会保守保留资源，不能视为物理卸载安全。

A01–A03、A09 的首阶段服务/realm 场景已通过；A07 的传递依赖部分已通过，父子所有权部分明确留待 W5。W4 按当前首阶段工作单完成，不代表 W5/W6 或整个 P2 里程碑完成。
