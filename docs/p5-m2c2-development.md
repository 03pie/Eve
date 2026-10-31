# P5-M2c2 Core 替换作用域与配置门禁

日期：2026-10-31（北京时间）。基线 1cd701c，上一小阶段已审计、提交并同步。完整范围见 [P5 计划](p5-development.md)，架构决定见 [ADR-0045](adr/0045-replacement-scope-ownership.md)。

## 架构与实现

先检查生命周期、入口和资源所有权。Kernel 原来把执行账目直接放在门面，异步替换不能借用其地址；同时挂载可能在实际拥有者登记前触发调度重入。因此将原账目原样归到唯一私有 KernelControl，门面入口先强持控制块；拆分私有 prepareMount/startMount，根实例与子实例均先登记拥有者，再通知或排队。没有第二个 Kernel、ServiceRegistry、模块目录或配置执行器，Core 不依赖 Module/Loader/Qt/UI。

不可复制的 ReplacementScope 重新计算真实闭包，在同一服务索引原子登记 Runtime、实际实例和完整输出键。受影响子实例包含其实际祖先拥有单元；其他 realm 的独立闭包仍可并行。Reserved 保留既有业务，但拒绝冲突挂载、修改、注入、新输出和 committed 租约；freeze 再核对选择、epoch/revision、状态与键集合，关闭旧业务，保留原 CleanupContext 能力。stop 按真实消费者/子实例优先顺序等待清理，不跳过失败消费者去停止提供者。

停止前可撤销，恢复当前 Active 代的上下文等待者及暂停激活，不制造新 epoch；开始停止后只有真实静止才能 release。句柄析构、错误线程、投递失败均不能假装清理或恢复成功，失败占用保留；Kernel.close 是明确的全局关闭特权，先取消替换回执，再等待实际实例静止。公开诊断是值快照，解除通知只表示逻辑占用已移除，不表示业务升级成功。

配置预检与规划共用动作分类，在 Coordinator 接受目标 CAS/Store 登记之前检查真实修改和祖先意图；冲突目标不改变 desired 或保存版本。未变更的受占用条目只观察为 Pending，其他条目可独立应用；后台恢复等待解除事件，不轮询或抢先重建。Driver/Session 普通 close 仍只拥有自身实例停止权限，未来 Module 替换协调必须先结束或取消作用域，再正常关闭 Session；不授予它全局关闭特权。

Kernel 私有布局变化要求源码 SDK 一同重建。复用已有 Qt toolchain.sdkVersion 精确检查，将 SDK 从 0.1.0 更新为 0.1.1，防止同工具链旧声明通过；不增加平行 ABI 字段或修改 C ABI 版本。真实 Qt 元数据 DLL 声明旧 SDK 时在映射前拒绝，该检查不是执行旧 Kernel 二进制的兼容性测试。

## 审查与专项验证

当前代理审查，未使用子代理。新增 31 项真实入口检查：25 项无 Qt Core、2 项内存 Writer 的 ConfigSession、3 项真实 C DLL、1 项真实 Qt 元数据预检，逐项见 [验收映射](../tests/acceptance/p5-replacement.md#m2c2-core-替换作用域)。配置检查证明目标 CAS 与写入登记没有越过占用，不将内存 Writer 声称为文件系统验证；显式同目标重应用使用旧工厂，不是候选代切换。

审查重点包括门面重入析构、挂载时重入关闭/占用、冻结时既有绑定与清理能力、真实异步调用排空、子实例祖先扩展、解除通知重入/失败、移动重赋重入、跨线程丢弃拒绝投递、停止失败保留、Kernel 关闭取消与真实静止，以及配置 CAS/等待子实例恢复。普通 BindingSnapshot 声明与快照实现保持基线原样。

SDK 回归在版本更新前真实失败 0/1，更新后通过。另对根实例登记顺序做负向控制 0/2，对移动赋值顺序、新 committed 租约门禁、冻结绑定门禁、配置 CAS 门禁及解除后恢复通知分别做负向控制，各 0/1，再按原字节恢复并重建，专项 31/31。首次配置临时注入的常量条件被 /WX 拒绝；调整为可编译的错误分类后才计入功能失败，没有把编译拒绝当作功能证明。完整日志前缀为 build/p5-m2c2-，临时注入代码不进入提交。

## 最终验证

原目录 MSVC Release、Debug、ASan 重新构建后全量各 817/817；无 Qt GCC 全量 313/313。实际运行没有修改源码或测试预算。期间一次 45 秒工具等待实际记录为 2698.05 秒，随后检查到 Release 已通过、Debug/GCC 构建仍在运行；不据此重启进程或把停顿归因为代码故障。

| 最终检查 | 实际结果 | 日志（build/p5-m2c2- 前缀） |
|---|---|---|
| 原目录 MSVC Release / Debug / ASan 全量 | 各 817/817 | {release,debug,asan}-{build,tests}.log |
| 无 Qt GCC 全量 | 313/313 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 348/348 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 279/279 + 1/1，包含全部 31 项新增专项 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 并重建 | 111/111 P5 补验通过 | restore-{configure,build,tests}.log |
| 两编译器各四个独立 C++17 公开头 | instance/service/replacement_scope/kernel 均通过；实际 Core 测试编译无 Qt 路径 | {msvc,gcc}-header.log、*-header-probes.json、*-core-command.json |
| 普通绑定快照声明与实现 | 与基线完全一致 | snapshot-boundary.json |
| P4 验收清单及实际依赖图 | 237 项标签、54 项引用及 1 项支持检查完整；Core 仅直接依赖 Threads::Threads | p4-acceptance.json、dependencies.dot |

24 项源/构建输入哈希与最终源码一致；11 个相关编译单元均有实际 /fsanitize=address 命令、本轮对象生成日志和对象文件，覆盖新作用域、Kernel/Instance/服务/闭包、配置驱动、原生桥及三类测试。31 项专项逐项匹配验收引用，19 个 C++ 文件的修改区域格式、UTF-8/LF、文档链接和只读边界检查通过，汇总为 build/p5-m2c2-audit.log。未发现阻止本小阶段提交的问题，按要求随本提交归档并同步，之后再开始下一段。

本小阶段只交付 Core 的排他、冻结及有序旧代停止。模块代次逻辑锁、ReplacementOperation、冻结定义/目标/schema 协同、候选创建、工厂与服务/目录整体提交尚待后续 M2c；M3 状态迁移/恢复、M4 白名单实际卸载、M5 UI 和 H01/H02/S09 及 Windows 连续 100 次真实替换/随机压力均保留，不以本段测试数量代替。
