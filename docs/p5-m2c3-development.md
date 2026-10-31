# P5-M2c3 模块预约与实际创建准入

日期：2026-10-31（北京时间）。基线 e5e6899；M2c2 已审核、提交并同步后开始。本段范围与取舍见 [ADR-0046](adr/0046-module-replacement-reservation.md)，完整交付仍按 [P5 计划](p5-development.md)。

## 架构与实现

先检查唯一进程目录、两后端实际工厂、GenerationLedger、原生关闭和 Core 作用域。复用原目录的同步边界，增加模块预约索引与可移动唯一权限，不在管理器、UI 或 Kernel 另建模块目录。公开权限门面不持管理器地址；准入计数覆盖实际同步创建/登记窗口，真实资源仍使用原 ModuleLease，不能按准入归零推断静止或卸载。

预约核对真实当前/候选身份、前驱、后端/插件目录、健康及选定候选无业务实例；同模块其他候选的装载/准入也参与排他。已取得的 NativeModule、Runtime 或 Qt 定义不能绕过创建检查，原生宿主打开与新定义同样检查。暂存与 drain 在同一目录锁内拒绝冲突，普通当前代发现复用不变，已有业务和清理继续。原生关闭已经要求先关闭托管 Core 并发布 Draining，本次复用，未增加平行关闭状态。

## 审查与回归

由当前代理审查，未使用子代理。复现并修复一项 P2：只核对选定两代时，另一同模块候选仍在装载，真实 Qt root 构造重入会误取得预约。独立回归修复前 0/1；补齐同模块全部代次的 loading/admissions 后通过。另增加未选中的兄弟候选正在工厂协商的检查。

20 项真实 DLL 场景覆盖 9 项 C ABI 和 11 项 Qt：参数/线程/移动、跨管理器与线程排他、管理器先析构、worker 释放、双版本实际业务、旧定义迟到创建、既有业务与清理、候选有实例时拒绝、失败候选不阻塞健康候选、独立模块并行、两个线程争同一预约、工厂 query/create 与兄弟代装载/协商重入。双版本 C DLL 同时提供真实业务表，Qt 两版本均有真实工厂；不将发现通过冒认为业务已协商，见 [验收映射](../tests/acceptance/p5-replacement.md#m2c3-模块预约与创建准入)。

最初测试中的三项断言混淆了接口契约：原生托管激活未传实际服务注册表、用首次 ready 历史任务观察重试，以及要求 Qt 无效元数据绕过预检直接报预约冲突。已改为显式索引、retry.target 和合法第三候选，未放宽生产代码以迁就断言；初轮失败日志保留。预约不是业务验证步骤，未实现工厂的合法元数据候选仍须由后续上层验证拒绝。

删除共同创建准入、暂存排他、排空排他，负向控制各 0/2；遗漏在途准入检查，query/create/兄弟候选协商共 0/3。各组按原字节恢复并重建，专项 20/20。首版注入脚本因匹配到两处相似条件而在变更前停止，缩小到预约分支后才执行第四组，不把脚本断言计作功能证明。日志前缀为 build/p5-m2c3-。

## 最终验证

| 最终检查 | 实际结果 | 日志（build/p5-m2c3- 前缀） |
|---|---|---|
| 原目录 MSVC Release / Debug / ASan 全量 | 各 837/837 | {release,debug,asan}-{build,tests}.log |
| 无 Qt GCC 全量 | 313/313 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 368/368 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 288/288 + 1/1，包含全部 9 项原生预约检查 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 并重建 | 131/131 P5 补验通过 | restore-{configure,build,tests}.log |
| 两编译器各两个独立 C++17 公开头 | module/manager、module/replacement 均通过，编译仅依赖 SDK 标准头 | {msvc,gcc}-header.log、*-header-probes.json |
| 原账目与层次边界 | Core 源码、C ABI 声明未改，ledger.hpp/ledger.cpp 与基线字节相同；Core 仅直接依赖 Threads::Threads | source-sha256.json、dependencies.dot、audit.log |
| P4 验收清单 | 237 项标签、54 项引用及 1 项支持检查完整 | p4-acceptance.json |

最终 16 项源/构建输入哈希一致；10 个相关编译单元的实际 /fsanitize=address 命令、本轮对象日志与对象文件相对应，覆盖目录/预约、原生宿主/Runtime、Qt Runtime、真实检查与新版本 DLL 的 C/Qt 代码。20 项预约检查逐项匹配验收引用，13 个 C/C++ 文件修改区域格式、UTF-8/LF、文档链接及只读边界审计通过，汇总为 build/p5-m2c3-audit.log。未发现阻止本小阶段提交的问题，按要求随本提交归档同步，之后再开始下一段。

本段不改变代次角色或提交运行升级；完整 ReplacementOperation、定义/schema/目标及工厂协同、迁移/恢复、白名单物理卸载和最终 UI/压力验收仍待后续阶段。
