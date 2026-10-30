# P5-M1 候选代目录

日期：2026-10-30。基线 d9d4f05。先核对模块唯一目录、业务/资源归属及旧装载契约，再引入同一目录的 Current/Candidate 与明确 predecessor；没有更改 Core、配置协调或普通插件权限。

## 实现与审查

ModuleManager::stage 和 stageQtModule 复用原装载路径。共同目录检查当前前驱、后端/插件目录、内容身份和物理文件归属，再预留候选；query/root 构造仍在锁外。普通 load 永远选择 Current。相同候选复用，失败候选继续驻留且不改变旧代；目录预算包含候选。

审查由当前代理完成，未使用子代理。真实 C ABI 旧实例保持 Started，暂存后仍由原 DLL 正常关闭；Qt root 构造重入只见 Validated/Candidate，重复暂存被拒绝，原 Current 仍可获取。Qt 元数据工具链 JSON 从两个已有生成处提取为同一 CMake 函数，新增版本夹具也实际使用，避免第三份声明复制。

22 项场景覆盖两后端的并存、复用、owner/前驱/无变化门禁、目录/身份拒绝、候选失败和当前代失败、已知业务协商失败的当前代/候选缓存；另验 C ABI 已 drain 拒绝、旧业务存活、候选占用预算、硬链接及失败物理别名，以及 Qt 构造重入。硬链接首轮把同一映像的别名误当成未映射路径，测试失败 1/20；已改为断言它确为旧映像别名，并继续严格检查 stage 在目录层拒绝、无新记录且 query 只执行一次，未放宽生产门禁。

临时恢复“选择最后一个同名代次”的错误搜索，两后端 coexist 均按预期失败（0/2），证据为 build/p5-m1-selector-red-{build,tests}.log。finally 按原字节恢复源码、重建，再运行全部候选场景 20/20；恢复日志为 selector-restore-*。初轮 16 项与边界补验分别保留在 first-*、review-*、boundaries-*。

最终复审发现：C ABI 发现通过但业务协商失败时，失败记录只在 businessFailure，旧复用检查仅看装载失败和运行信号，会把已知业务失败的当前代或候选当成可暂存/复用。两项真实缺业务入口 DLL 回归修复前 0/2，见 business-red-*；按 Discovery/Staging 区分内部复用目的后，暂存拒绝已知业务失败，普通发现复用保持原契约，22 项通过，见 business-green-*。该修复不要求未协商的元数据夹具已经具备业务入口，只拒绝已被证实的失败。

## 最终验证

| 检查 | 实际结果 | 日志（build/p5-m1- 前缀） |
|---|---|---|
| 原目录 MSVC Release 全量 | 728/728 | release-{build,tests}.log |
| MSVC Debug / ASan 的 P4+P5 标签及 Session 支持 | 各 259/259 + 1/1 | {debug,asan}-{build,tests,support-tests}.log |
| 无 Qt GCC 全量 | 258/258 | gcc-{build,tests}.log |
| 原 Release 关闭示例的 P4/P5 及支持检查 | 259/259 + 1/1 | no-examples-{configure,build,tests,support-tests}.log |
| 静态 Core 兼容路径的 P4/P5 及支持检查 | 191/191 + 1/1，含 14 项原生候选检查 | static-{configure,build,tests,support-tests}.log |
| 恢复原 Release ON/ON 配置并重建 | 22/22 候选检查补验通过 | restore-{configure,build,tests}.log |
| P4 验收清单 | 237 项标签、54 项引用及 1 项支持检查仍完整；清单校验不代替执行 | p4-acceptance.json |
| 源码与编译审计 | 14 项源/构建哈希一致；8 项实际 ASan 编译命令；MSVC/GCC 公开模块头无 Qt 目录编译 | source-sha256.json、asan-commands.json、header-commands.json |

三种构建的旧/新 Qt 嵌入元数据除 moduleVersion 外的 moduleId、plugins 和 toolchain 一致，版本分别为 1.0.0 / 2.0.0，真实生产声明也使用同一生成函数。编码、修改区域格式、文档引用、只读参考边界及 git diff --check 通过；最终汇总见 audit.log。Debug/ASan 记录的是上述专项范围，不冒记为两套全量重跑。

未发现阻止本小阶段提交的剩余问题，按用户要求审查后提交同步，再开始 M2。本阶段不宣称已经切换运行、完成 H01/H02/S09、迁移状态或允许物理卸载；完整出口见 [P5 计划](p5-development.md)。

pre-health-* 保存业务门禁复审前的 726/257/189/20 等矩阵和源码快照，仅为过程历史；本节最终结果对应修复后的源码。
