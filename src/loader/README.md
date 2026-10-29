# Loader

Q4a 已实现独立 C++17 库 Cordis::Loader：配置结构校验、不可绕过校验构造的 DocumentCandidate、规范 EntryPath、父禁用级联，以及无副作用差异预览。公共接口见 sdk/include/cordis/loader/configuration.hpp，决策见 [ADR-0011](../../docs/adr/0011-configuration-preview.md)。Core、Qt 和 Ela 均不是本库依赖。

DocumentCandidate 只证明结构有效，不证明插件、schema、文件路径或模块产物有效。PlanPreview 比较两份目标结构，不等于可执行计划；保留目标变化与运行意图的区别，禁用条目修改仍可在预览中识别。

Q4b 新增 Cordis::ConfigYaml，私有链接仓库内固定 LibYAML C 库；通过有界事件接口解析 UTF-8 YAML，拒绝引用、tag、重复键和未知结构字段，输出可往返的规范 JSON/YAML 文本与来源位置。validateCandidate 检查内置插件定义/schema，联合声明依赖，结果不保留检查回调。外部模块在 P4 路径/产物验证接入前拒绝。具体语义见 [ADR-0012](../../docs/adr/0012-restricted-yaml-validation.md)。

Q4c1 新增 Cordis::ConfigCoordinator：owner CAS 接受目标、至多一个在途动作、最新目标替代、部分失败/Pending 报告、投递失败门禁和异步关闭。驱动负责从实际状态规划；运行查询失败也不会隐藏 desired 快照。见 [ADR-0013](../../docs/adr/0013-configuration-coordinator.md)。

Q4d1 新增可选 Cordis::ConfigFileQt：有界读取原始文件、路径绑定 SHA256 基线、协作写锁及 QSaveFile 提交；冲突保留外部原文，不自动采用，失败保留旧文件。此层同步执行，不管理目标修订或写队列，见 [ADR-0014](../../docs/adr/0014-configuration-file-transactions.md)。

Q4d2 新增无 Qt 的 Cordis::ConfigStore：跟踪已接受目标、独立历史保存修订与 dirty、有限 FIFO/同修订请求共享、失败停止/显式 rebase 及退出刷新。文件值类型移到 file.hpp；可选 Cordis::ConfigStoreQt 提供真实 QtConfigurationWriter，同步执行文件 I/O，回执由 Store 排队处理。见 [ADR-0015](../../docs/adr/0015-configuration-store.md)。

Q4c2 的 Cordis::ConfigKernel 已接入真实 Kernel：宿主类型目录、实例级依赖联合、精确服务排序及完整退出；显式 builtin children 经父代 Context.inject 创建，继承/覆盖 realm，父恢复后自动重建已登记子树。等待父代的记录不会伪造实例标识，后台失败通过实际快照报告，历史计划保持不变。见 [ADR-0019](../../docs/adr/0019-configuration-subtrees.md)。

Q4e1 新增无 Qt 的 Cordis::ConfigSession，独占连接候选、Coordinator 和 Store：预览无副作用，候选绑定会话与基准修订，接受后同栈跟踪保存目标；期望启停修改配置路径，保存/退出明确区分运行和持久化。可选 Cordis::ConfigUi 和配置工作台在 UI 层消费此接口，普通 Ui SDK 不依赖 Loader。见 [ADR-0020](../../docs/adr/0020-configuration-session-ui.md)。

Q4e2 已通过宿主协议统一 Home/管理路由，路径启停经 Session 而不直接停止配置实例，见 [ADR-0021](../../docs/adr/0021-configuration-management-routing.md)。Q4d3 已提供可选 Cordis::ConfigIoQt：私有串行后台读写、真实线程 join、文件/父目录监听恢复和自身版本消重；通知只提供外部原文，不自动接受目标。见 [ADR-0022](../../docs/adr/0022-configuration-background-io-watcher.md)。Q4c1 的受控驱动只验证协调协议，Q4c2 的真实 Kernel 场景单独验证资源/依赖闭环；候选、目标、运行和保存状态继续分离。

P4-M3d1 新增宿主冻结的 PluginModule 目录：PluginDefinition 与 KernelPlugin 缺省仍为 builtin；原生模块由宿主先验证/装载，再固定插件标识、别名和 manifest 定位文本。校验器和驱动分别核对所有声明（包括未使用声明），预览不读取或执行 DLL。KernelPlugin.available 与 maximumDependencies 在启用/挂载前检查，失效模块仍可显式停用；Loader 不增加 Module/Qt 依赖。真实 NativeModule/Kernel/Session 夹具验证与使用边界见 [M3d1 记录](../../docs/p4-m3d1-development.md)；Q4b 的外部模块拒绝描述为其历史状态。
