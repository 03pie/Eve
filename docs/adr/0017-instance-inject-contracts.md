# ADR-0017：实例级类型化配置依赖

日期：2026-10-25。范围：Q4c2b，承接 [顶层配置驱动](0016-kernel-configuration-driver.md)。本轮完成动态 inject 的真实执行；显式配置 children 仍由 preflight 拒绝。

## 不可变定义与实例契约

Runtime 的固定依赖、输出、身份及激活函数保持不可变。Kernel.mount 和 Instance.mount 新增可选 additionalDependencies 参数，类型仍为 Core 的 ServiceRequirement；Core 不读取 YAML、不引用 Loader/Qt/Ela。

挂载时按解析后的 ServiceKey 联合固定与附加依赖：同键必须同 C++ 类型，minimumMinor 取最大值，capabilities 去重取并集。非法键、void 类型、重复/空能力名称、外来 realm 及解析域后的类型冲突在建立运行账目前拒绝。同键只形成一项契约和一份提交租约；附加低要求不能删除或削弱固定定义。

求得的依赖保存在实例中，参与激活预检、声明环、committed 租约、服务退役及恢复。update/retry 不改变这份契约；remount 复制最新配置、realm 和完整实例依赖，新实例不丢失附加要求。缺依赖为真实 Pending，不提前执行 activate。只修改配置值时沿用同一实例及契约，改变有效依赖则必须停止并重建。

这里采用实例级联合，未建立“每种配置对应一个 Runtime”的变体缓存，因此反复更改依赖不增加 Kernel 的注册定义数量，也不需要把激活回调暴露给 Loader。

## 类型来源与最低要求

KernelPlugin 新增 configurableInject 目录，宿主用 ServiceRequirement::of<T> 声明允许按需追加的服务类型、原始 realm 及最低要求。例如：

```cpp
KernelPlugin plugin{"consumer", consumerRuntime,
    {ServiceRequirement::of<int>({"counter"})}};
```

```yaml
formatVersion: 1
plugins:
  - id: reader
    plugin: consumer
    inject: [counter/v1]
```

目录不代表自动注入；是否追加由 Q4b 校验得到的 effectiveInject 决定。固定依赖可由配置收紧，无须重复登记。目录中新增接口必须按 name/major 唯一，不能与固定接口重复、不能指定 void 或非法能力列表。没有宿主类型声明的新接口在接受目标前拒绝，不从磁盘文本、服务对象或当前提供者猜测 C++ 类型。

驱动复核 effectiveInject 保留 Runtime 的全部固定要求，防止另一套来源定义/schema 遗漏固定依赖。请求可选接口时也必须满足目录声明的 minimumMinor/capabilities 下限；默认下限为零和空集合。条目 realm 覆盖应用于真实依赖键，未覆盖时保留描述符原始域，精确检查本 Kernel 注册表归属。

## 从实际状态规划

驱动为每个已挂载记录保存实际接受的类型化依赖。提供者先启动、消费者先移除的拓扑图同时包含固定和配置追加边，检查精确类型、major/realm、minor/capability、重复提供者及环。缺少配置内提供者时允许等待外部宿主服务。

新目标的依赖按完整键排序后比较。新增、移除、收紧或放宽有效契约使用 Recreate，等待旧实例全部清理后才挂载；禁止用 Restart/NoOp 越过该身份变化。固定要求已经覆盖的较弱 inject 简写不会重复激活。重新规划使用运行记录中的旧契约，不以最新用户目标推断旧实例已经接受新要求。

目标图严格验证提供者类型/版本/能力；旧运行图只用于消费者优先的清理排序，不要求部分应用的新旧契约已经相互满足。例如新低版本提供者刚完成添加、旧消费者尚待放宽并重建时被更新目标替代，不能用旧消费者的高要求拒绝重新规划；新目标仍须独立通过全部兼容性检查。此场景已加入 inject_recreate，修复前真实失败，证据为 build/q4c2b-replan-before.log。

清理失败保留旧实例、输入与依赖记录，冻结不安全后续执行；不把新用户目标覆盖到未成功替换的运行记录。已接受目标、真实运行与历史保存状态仍保持分离。输出所有权跨仍启用路径转移继续遵守 ADR-0016 的显式移除阶段约束。

## 验证与后续

三项 Core 场景覆盖固定/附加契约联合、精确域、去重租约、类型冲突及非法描述拒绝、停止后 remount 保留。六项真实配置驱动场景覆盖追加边排序及清理租约、隔离域 Pending/恢复、有效契约重建/冗余 NoOp、目录及来源定义守卫、动态类型/版本/能力/环拒绝、清理失败不挂载新实例。

cordis_reconcile_demo 改由 YAML 的 counter/v1 inject 建立消费者强依赖，宿主提供 int 类型目录，更新提供者后消费者恢复；固定 Runtime 仍无该附加依赖且总注册数量保持两个。工具链和 ASan 实测见 [开发记录](../p3-development.md#q4c2b-动态-inject-契约)。

显式配置子树、配置拥有条目的 UI 启停/保存装配、watcher/后台 I/O 继续待办。宿主管理手动挂载实例的操作不能替代配置协调器的期望状态变更。本轮不是全部 L01–L04/S05 或跨 ABI 验收。
