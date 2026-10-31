# P5-M2c4 配置与运行的共同定义来源

日期：2026-10-31（北京时间）。基线 fad636a；M2c3 已审核、提交同步后开始。本段先完成完整换代的定义来源前提，完整交付不变，见 [P5](p5-development.md)及 [ADR-0047](adr/0047-configuration-definition-identity.md)。

## 架构与实现

先检查 Session、Coordinator、真实 Kernel 驱动、两后端宿主和 Runtime 身份。将 schema/来源/Runtime/可选类型化依赖/可用性/预算合并到不可变 ConfigurationDefinitions，驱动内部五份映射收敛成一个目录。Session 经配置驱动接口读取同一目录，三个实际宿主采用共同装配；普通 Core 和模块账目没有增加反向依赖。

候选只保留无执行引用的身份，完整目录仍由驱动/校验栈拥有。schema 校验先强持数据，候选及 Store 长期保存不延长工厂寿命。目录构造不执行 schema 或激活，拒绝缺失 schema、无效 Runtime、弱化固定依赖及没有兼容类型化契约的要求；驱动构造再检查 Runtime 实际属于 Kernel。公开旧静态构造保留，新路径采用命名工厂避免 `{}` 重载歧义；完整驱动拒绝另一份 schema。

Coordinator 在可重入 preflight 前后核对来源，Kernel 驱动的 preflight/plan/apply 共用身份门禁，Session 在所有外部调用后核对来源和关闭状态，再发布草稿；已失效草稿不能进入 desired CAS 或 Store。ConfigYaml 仅提取私有规范化实现，继续没有 Core/Runtime 依赖。

## 审查与回归

由当前代理审核，未使用子代理。审查复现一项 P2：Session 在 driver.snapshot 之前完成最后检查；快照同步回调切换目录或关闭会话后，仍会发布旧草稿。两项独立回归修复前 0/2；最后检查移到所有外部回调之后，修复后通过。另验证 getter 异常、schema/预检/读取重入及门面释放，保持实际所有权到调用栈结束。

15 项无 Qt 场景验证目录规范化、类型化契约、实际 Kernel 归属、直接驱动入口、Session 应用/添加/启停/保存/关闭、旧构造兼容、错误目录和草稿来源、Coordinator 前后门禁、schema 释放最后门面、快照关闭与查目录异常。协议替身只注入目录变化，不代表业务重建。

两项真实 DLL 场景分别使用两个 C ABI/Qt 版本、真实 Runtime、Kernel/Session 与 QtConfigurationWriter，在各自实际文件中验证绑定、激活、保存读回及完整关闭。Current/Candidate 角色保持原样；它们验证独立定义来源，未冒充完整 ReplacementOperation 或在线升级。

三组可编译的负向控制分别移除直接驱动门禁、Coordinator 预检后置核对，以及把 Session 最后检查提前到快照之前，实际结果依次为 0/1、0/1、0/2。每组按原字节恢复，重新构建后定义专项 17/17；没有把编译拒绝计为功能检出。

首轮测试遗漏 formatVersion，真实文件场景又把 module.manifest 映射写成标量，已修正测试输入；编译期局部变量遮蔽、GCC 单测聚合初始化遗漏字段也已修正，不计为运行缺陷。GCC 首轮失败后主动停止尚未完成的 MSVC 矩阵，补齐测试字段并重新冻结输入，最终结果只使用最终源码的重建轮次；中断日志另保留。所有实际红/绿和构建日志保存在 build/p5-m2c4-*。

## 最终验证

| 检查 | 实际结果 | 日志（build/p5-m2c4- 前缀） |
|---|---|---|
| 原目录 MSVC Release / Debug 全量 | 各 854/854 | {release,debug}-{build,tests}.log |
| ASan 全量 | 854/854 | asan-{build,tests}.log |
| 无 Qt GCC 全量 | 328/328 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 385/385 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 304/304 + 1/1，包含原生实际定义绑定 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 并重建 | P5 148/148 | restore-{configure,build,tests}.log |
| 两编译器各五个独立 C++17 头 | yaml、definitions、definition_driver、kernel_driver、session 通过；旧 Kernel 驱动 `{}` 调用可编译 | {msvc,gcc}-header.log、*-header-probes.json |
| 实际 ASan 编译与对象 | 10 个变更编译单元均带 /fsanitize=address，日志与本轮对象对应 | asan-commands.json、asan-build.log |
| 层次与来源边界 | Core 仅直接依赖 Threads；ConfigYaml 仅依赖 Loader/libyaml；ConfigDefinitions 依赖 ConfigYaml/Core；Core、C ABI 和原 ledger 未改 | dependencies.dot、source-sha256.json |
| P4 验收清单 | 237 项标签、54 项引用和 1 项支持检查完整 | p4-acceptance.json |

最终 21 项源/构建输入哈希一致，10 个变更编译单元有实际 ASan 命令和本轮对象。17 项定义专项逐项匹配验收引用，18 个 C++ 文件修改区域格式、UTF-8/LF、文档链接、依赖及只读边界检查通过，汇总为 build/p5-m2c4-audit.log。未发现阻止本小阶段提交的问题，按要求随本提交归档同步，之后再开始下一段。

本段不提供可变目录替换接口。完整事务、冻结目标版本、授权候选创建和服务/定义/模块角色整体提交仍由下一段统一完成；迁移/恢复、白名单物理卸载、UI 贡献可见性及 H01/H02/S09、Windows 连续 100 次真实替换与随机压力全部保留。
