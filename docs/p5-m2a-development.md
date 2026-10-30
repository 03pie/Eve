# P5-M2a hash 目录安装事务

日期：2026-10-30。基线 94bd1e9。M2 的第一部分，先完成安装来源与磁盘事务，不把安装完成计作运行替换完成。完整出口继续见 [P5 计划](p5-development.md)。

## 架构与复审

先核对 VerifiedSource、唯一进程目录、ModuleManager、Core 输出发布及 Kernel/Session 的职责。安装为独立 Cordis::ModuleInstall，同步 IO 受文件预算限制，可由宿主 worker 调度；它不依赖或操纵模块装载、业务实例、服务发布和配置。安装凭据共享文件/目录保护，后续装载仍取得自己的来源验证与保护，见 [ADR-0042](adr/0042-module-artifact-installation.md)。

旧路径输入规则和祖先保护提取到私有 path_p.hpp，预检、装载和安装共同使用；原生文件路径支持扩展长度，缓存成员可明确禁止跟随重解析点。复审补强目标根获取祖先保护后的原生身份复核，以及拥有的句柄在分配异常时的 RAII 交接。安装不复制未声明的依赖或旁文件，不添加预留的包格式、迁移或运行协调接口。

两个关键负向控制：临时取消已存在 hash 目录的声明身份检查，以及回收前的文件身份检查。conflict / rollback_replaced 实际失败 0/2；finally 按原字节恢复生产源码、重建后安装专项 25/25。日志为 build/p5-m2a-guards-{red,restore}-{build,tests}.log。测试中被替换的文件只属于独立子进程夹具；生产回收没有递归删除或路径猜测。

最终路径复审另发现一项 P2 兼容性问题：直接添加 Windows 扩展长度前缀会使原先可接受的 `.` / `..` 绝对路径失效。真实 Win32 调用与新增 normalized_root 回归修复前失败 0/1；在添加前缀前保持词法归一化后，普通和显式扩展长度输入均通过，安装专项 26/26。日志为 path-{red,green}-{build,tests}.log；未以修改测试期望来放宽路径门禁。

## 场景与验证

新增 26 项真实文件系统检查，映射见 [验收表](../tests/acceptance/p5-replacement.md)：声明原文/自定义输入名、独立复制、多块读取、缓存复用、跨线程与无 QCoreApplication、非法路径和限额、坏 PE/摘要、来源共享冲突、互斥与并发、长路径/归一化/Unicode/根别名、重解析目录、硬链接、篡改、发布不覆盖、失败回收及外来内容保留、发布前后文件身份；另接真实 C ABI 与 Qt 候选装载。

原生接入场景保持旧业务 Started 并正常关闭；Qt 场景验证 root 和候选目录，不把元数据夹具当作真实业务页面升级。安装过程本身在源和目标均无映像映射，只有显式 stage 才执行后端。

| 最终检查 | 实际结果 | 日志（build/p5-m2a- 前缀） |
|---|---|---|
| 原目录 MSVC Release 全量 | 754/754 | release-{build,tests}.log |
| MSVC Debug / ASan 的 P4+P5 及 Session 支持 | 各 285/285 + 1/1 | {debug,asan}-{build,tests,support-tests}.log |
| 无 Qt GCC 全量 | 258/258 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 285/285 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容的 P4/P5 及支持检查 | 216/216 + 1/1，含 25 项安装检查 | static-{configure,build,tests,support-tests}.log |
| 原 Release 恢复 ON/ON 并重建 | 48/48 P5 检查补验通过 | restore-{configure,build,tests}.log |
| 六线程竞争同一 hash，在 Debug / ASan 各连续 20 次 | 各 20/20 | {debug,asan}-concurrent-repeat.log |
| 两编译器公开模块头独立语法检查 | 无 Qt include 目录通过 | {msvc,gcc}-header.log、header-commands.json |
| P4 验收清单 | 237 项标签、54 项引用及 1 项支持检查完整；不代替执行 | p4-acceptance.json |

19 项源码/构建输入哈希一致，11 条实际 ASan 编译命令、两编译器无 Qt 头、编码/格式/文档引用及只读边界通过审计，最终汇总为 build/p5-m2a-audit.log。pre-path-* 保留路径复审前的 753/284/215/47 等历史矩阵，不代表最终版本。并发安装重复检查不等于 H02 的 100 次运行替换，也没有新增原生 UI 或网络共享实测。

复审未发现阻止本小阶段提交的问题，按用户要求随本提交归档并同步，之后才进入下一段。M2 后续的运行闭包逻辑锁、候选输出发布边界和当前代原子切换仍未完成；M3–M5 的迁移/恢复、白名单卸载、H01/H02/S09 和完整压力验收保持原范围。
