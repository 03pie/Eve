# ADR-0036：在现有原生桥上增加 Core 托管模式

日期：2026-10-29。状态：采用。

## 背景

远端 M3c2 的 NativeModule.open/create 已有共享服务索引、真实 committed 租约及原生资源退出协议。M3d 需要由 Kernel 分配实例身份、执行类型化依赖预检，并由配置会话控制实例；沿用低层桥，但不能让原生服务提前发布或提前释放 Core 持有的键。

## 决策

NativeModule.runtime 接受宿主 NativePlugin 契约并创建 Runtime；Kernel.registerRuntime 登记现有定义且保留 RuntimeId。声明及 nativeRequirement 只含宿主 C++ 值，端点类型留在模块实现内部，不将 STL、Context、ProviderHandle 或函数对象跨业务 C ABI 传递。低层 open/create 的动态服务行为保留。

托管激活先检查同 scheduler、绑定相同 Core 索引、冻结本代已提交的原生输入，再登记 Core 清理组，最后进入 foreign create/start。未显式指定索引的会话可首次绑定到该 Core 索引，已使用的其他索引不能切换。空 Core 配置转换为 JSON null；其他配置必须是合法 JSON。

原生输出先 Staged，再通过 Core 私有适配入口接管：验证索引、owner、类型、Loading 状态、完整键及精确 minor/capability 声明。原生 start 终结前完成输出登记；初始 effect setup 可晚到，托管 start 等待其 ready，最终由 Core 激活屏障统一发布。未声明、元数据不符及 setup 失败都回滚清理，不冒充已激活。

托管输入来自激活预检的 committed 保留，包括配置追加的原生 inject；NativeServices 只借用这些租约，不新建或提前 release。因此清理第一次调用旧绑定也可执行，提供者 Retiring 时继续遵守本代受限访问。托管模式未声明的新服务调用拒绝；低层 create 仍支持原有动态消费。

Core 停用立即使业务门禁失效，排队 dispatch 和服务调用在执行前再检查 epoch。已接受操作的 ACK、清理及实际完成仍要退役；不能因停用直接释放捕获。提供者代次故障也在受理/执行两端阻止新业务；普通入口超限返回 LIMIT，清理失败仍保守驻留。同步 ACK 只持实际 CallLease 和操作捕获屏障，不占异步请求预算。托管实例只能通过其 Core 所有者发起关闭；模块会话不能越过仍活动的 Core 实例自行关闭。

原生服务撤回后等待消费者和调用归零，托管分支将端点变为不可调用的宿主值并退役其 ServiceObject 引用。Core 保留 Retiring 的键及宿主端点对象，直到所属实例全部清理完成后才 release；不能套用低层桥提前 release 的行为。端点随后析构不会再次退役或调用 DLL。

PluginDefinition/KernelPlugin 追加缺省为 builtin 的 PluginModule。宿主先验证并装载产物，再冻结配置插件标识、模块别名和 manifest 定位文本。定义校验及执行驱动分别匹配所有声明（含未使用声明）和条目；预览不读文件或装载 DLL。只读 available 门禁限制启用项，停用仍可执行；依赖总数可在挂载前限额。Loader 不依赖 Module/Qt，Core 不导入 Module/Qt/Windows 实现。

## 结果与边界

M3d1 完成托管与目录这一可验证工作包，证据见 [开发记录](../p4-m3d1-development.md)。业务服务 DLL 示例、真实 Qt 事件循环、失败驻留与 S06/S07 联合退出属于 M3d2；M4 UI DLL、P5 受控替换和物理卸载不由本决策完成。默认驻留不变。
