# Loader

Q4a 已实现独立 C++17 库 Cordis::Loader：配置结构校验、不可绕过校验构造的 DocumentCandidate、规范 EntryPath、父禁用级联，以及无副作用差异预览。公共接口见 sdk/include/cordis/loader/configuration.hpp，决策见 [ADR-0011](../../docs/adr/0011-configuration-preview.md)。Core、Qt 和 Ela 均不是本库依赖。

DocumentCandidate 只证明结构有效，不证明插件、schema、文件路径或模块产物有效。PlanPreview 比较两份目标结构，不等于可执行计划；保留目标变化与运行意图的区别，禁用条目修改仍可在预览中识别。

Q4b 新增 Cordis::ConfigYaml，私有链接仓库内固定 LibYAML C 库；通过有界事件接口解析 UTF-8 YAML，拒绝引用、tag、重复键和未知结构字段，输出可往返的规范 JSON/YAML 文本与来源位置。validateCandidate 检查内置插件定义/schema，联合声明依赖，结果不保留检查回调。外部模块在 P4 路径/产物验证接入前拒绝。具体语义见 [ADR-0012](../../docs/adr/0012-restricted-yaml-validation.md)。

Q4c1 新增 Cordis::ConfigCoordinator：owner CAS 接受目标、至多一个在途动作、最新目标替代、部分失败/Pending 报告、投递失败门禁和异步关闭。驱动负责从实际状态规划；运行查询失败也不会隐藏 desired 快照。见 [ADR-0013](../../docs/adr/0013-configuration-coordinator.md)。

Q4d1 新增可选 Cordis::ConfigFileQt：有界读取原始文件、路径绑定 SHA256 基线、协作写锁及 QSaveFile 提交；冲突保留外部原文，不自动采用，失败保留旧文件。此层同步执行，不管理目标修订或写队列，见 [ADR-0014](../../docs/adr/0014-configuration-file-transactions.md)。

Q4d2 新增无 Qt 的 Cordis::ConfigStore：跟踪已接受目标、独立历史保存修订与 dirty、有限 FIFO/同修订请求共享、失败停止/显式 rebase 及退出刷新。文件值类型移到 file.hpp；可选 Cordis::ConfigStoreQt 提供真实 QtConfigurationWriter，同步执行文件 I/O，回执由 Store 排队处理。见 [ADR-0015](../../docs/adr/0015-configuration-store.md)。

尚无 Kernel 运行树驱动、真实依赖执行顺序、配置 UI、后台 I/O 或 watcher。当前协调测试的受控驱动不作为产品默认运行驱动；候选、目标、运行和保存状态继续分离。
