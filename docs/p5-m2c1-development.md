# P5-M2c1 实际依赖图与替换闭包

日期：2026-10-31（北京时间）。基线 e92fd5e，M2b 已审计、提交并同步后开始本段。本段是完整 M2c 的前提，范围见 [P5 计划](p5-development.md)。

## 架构与实现

先检查 Kernel 的真实实例树、ServiceRegistry 的声明/父子边、committed 生命周期与原生实例身份。复用同一索引与 Core 资源控制块，新增纯值服务图和独立 replacement.cpp，不向 ModuleManager 添加 Core 生命周期分支，不新建目录、注册表或配置驱动。

图记录实际提供者、活跃 committed 消费者、代次和 Core 接纳标记；Kernel 再按实际句柄核对所有权，不能从原生编号或接纳标记推定 Kernel 归属。遍历目标 Runtime 的全部未终结实例及其子树，沿实际和等待声明依赖扩展，按完整服务键隔离 realm/major，消费者/子实例先停止，逆序给出启动顺序。开放发布组、外部消费者、转换中实例、投递阻塞和联合依赖环均拒绝，查询不会调用插件或泵送任务。

快照只含值，不授予排他权，也不保存配置/工厂或承诺子实例可以独立重建。未来替换事务必须重新核对 epoch/revision、实际图及模块身份；配置驱动/Session 的冻结定义必须一起处理。详见 [ADR-0044](adr/0044-replacement-dependency-closure.md)。

## 审查与回归

由当前代理审查，未使用子代理。复审发现并修复两项 P2，均先真实复现：

1. 实例仍 Active 且自身投递正常时，服务调用的完成投递可能已经失败。delivery 新回归修复前 0/1，现在同时检查受影响绑定的 deliveryBlocked，拒绝将永久在途调用误视为正常闭包。故障测试保留原清理屏障，不为测试强制清账。
2. 原环预检只登记必供输出，直接复用会漏掉等待可选输出的消费者。pending 改为真实可选输出后修复前 0/1；在同一声明记录增加可选输出，图合并两类输出，原必供环预检规则保持不变。已有真实绑定仍优先，避免未使用的可选声明捕获宿主服务的无关消费者。

调用路径复查还优化了一处职责边界：将提供者/消费者信息从常规 BindingSnapshot 移到专用 ServiceBindingSnapshot，仅依赖图查询遍历消费者，保持常规快照原布局与开销。调整后重新构建并验证最终矩阵，不沿用调整前的全量结果。

另对遗漏实际消费者、等待声明和子所有权边做三项负向控制，0/3；单独破坏真实绑定优先级，0/1。按原字节恢复后重建，17/17 通过。首轮负向注入中的常量条件被 /WX 拒绝；修改为可编译注入后，发现组合注入会相互遮蔽优先级场景，改为分组验证，不把编译拒绝或未失败场景记为功能回归。日志前缀 build/p5-m2c1-，分别为 delivery-*、optional-*、edges-* 和 precedence-*。

17 项场景包含 15 项无 Qt Core 检查与 2 项真实 C DLL 检查：动态未声明消费者、多级 Pending、可选输出、子实例、realm/major 隔离、多目标/同 Runtime 多实例、外部 Core 实际/等待消费者、手动原生消费者、已有宿主绑定、Loading/Unloading/Disposed、开放发布组、线程/参数/关闭门禁、在途调用与租约排空、联合依赖环及纯值 epoch/revision 快照。见 [验收映射](../tests/acceptance/p5-replacement.md#m2c1-实际图与只读替换闭包)。

## 最终验证

最终结构首轮 Release 为 785/786，p3.ui_services_failures 超时 243.94 秒，预算为既有 20 秒；同期一次请求 45 秒的工具等待实际记录为 266.45 秒。这提示执行环境停顿，但未据此确诊原因或忽略失败。源码和预算保持原样，单项连续 10 次通过，失败轮保存在 release-timeout-tests.log，复核为 timeout-repeat-tests.log；再完整重跑最终矩阵。

| 最终检查 | 实际结果 | 日志（build/p5-m2c1- 前缀） |
|---|---|---|
| 原目录 MSVC Release / Debug / ASan 全量 | 各 786/786 | {release,debug,asan}-{build,tests}.log |
| 无 Qt GCC 全量 | 286/286 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 317/317 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 248/248 + 1/1，包含全部 17 项闭包检查 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 并重建 | 80/80 P5 检查补验通过 | restore-{configure,build,tests}.log |
| 两编译器各三个独立 C++17 公开头 | 均通过；实际 Core 测试编译不含 Qt 路径 | {msvc,gcc}-header.log、*-header-probes.json、*-core-command.json |
| 普通绑定快照的声明与实现 | 与基线完全一致，不引入消费者遍历 | snapshot-boundary.json |
| P4 验收清单及构建依赖图 | 237 项标签、54 项引用及 1 项支持检查完整；Core 仅直接依赖 Threads::Threads | p4-acceptance.json、dependencies.dot |

最终 18 项源/构建输入哈希一致，8 个相关编译单元的实际 ASan 命令与本轮对象日志对应；17 项闭包场景逐项匹配验收引用，编码、修改区域格式、文档链接与只读边界审计通过，汇总为 build/p5-m2c1-audit.log。未发现阻止本小阶段提交的问题，按要求随本提交归档并同步后才继续后续 M2c。

M2c 后续的模块/闭包排他权、ReplacementOperation、定义/schema/目标版本核对、真实停止/创建及服务/目录整体切换尚未实现。M3 迁移/恢复、M4 白名单物理卸载、M5 UI 接入和 H01/H02/S09 与压力验收保持完整范围，不能将只读闭包当作运行替换完成。
