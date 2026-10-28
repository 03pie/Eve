# 开发进度

最后更新：2026-10-28（北京时间）。当前工作区：`D:/01_C++/Eve`。

本文件是项目进度的统一入口，记录实际状态；设计细节见说明书，任务完成条件见工作单，测试依据见验收规范和验证报告。代码行数、文档篇幅和测试数量不作为项目完成百分比。

## 当前状态

**P1/P2/P3 及 UI 专项已完成（当前内置插件/Windows 支持矩阵）。P4-M1 已提交为 59b7c76。M2a 的真实 DLL 装载、独立只读描述 ABI、进程目录与默认驻留已完成；下一步为 M2b 业务 ABI，M3 Kernel/请求桥、M4 UI DLL 尚未实现，P5 未开始。**

2026-10-28 当前增量见 [M2 开发记录](p4-m2-development.md)及 [ADR-0030](adr/0030-native-module-discovery.md)。提交前审查修复过早发布 Loaded 及异常选项处理；MSVC Debug/Release 各 490/490、GCC 217/217，ASan 与关闭示例的 P4 专项各 31/31；CLI、22 项源快照及边界审计通过，按用户要求随本提交归档。不把只读描述握手当作外部业务插件已激活，也不提前冻结不完整的 HostTable。下列 M1 及更早验证保留为各提交的历史。

2026-10-28 本轮范围、阶段和测试见 [P4 开发记录](p4-development.md)及 [ADR-0029](adr/0029-module-preflight-ledger.md)。审查修复缺盘符路径误接受、预检按字符串重开来源，以及关闭示例时 junction 回归缺失；新增回归修复前 0/2、修复后 2/2。最终原目录 MSVC Debug/Release 各 473/473、无 Qt GCC Debug 215/215、ASan P4 专项及关闭示例的 Qt Debug 各 14/14 通过；13 项源/构建/脚本快照及依赖/文档审计通过，按用户要求随本提交归档。下文各轮“未开始”“未提交”和测试数量为当时历史，不覆盖本节最新结论。

UI 专项 [UI-S0–S4 五个阶段](ui-special-stages.md)已全部收口：统一 Ela 组件、Microsoft YaHei 字体、ElaAwesome 图标、真实状态与独立反馈/偏好，并完成 VS Code 结构对照及四项交互增量。最终 Release 454/454、ASan UI 114/114；Debug 完整轮次 453 项通过，150% 多截图项独立连续五次通过。最新原生检查 32/32 通过，用户已分别确认基线体验、Windows 系统 150% 和四项新增交互及两个窗口正常关闭。剪贴板错误 5 已解除，见 [增量验证与完成审计](ui-structure-verification.md)。原先未提交的 UI 基线随本提交归档，P4/P5 仍未开始。

2026-10-28 用户追加 Home 管理收纳需求：插件管理/配置管理改为卡片按钮打开的托管窗口，平台添加入口放在顶部菜单，仅添加已注册配置实例。实现及审查已完成：Debug/Release 各 459/459、ASan UI/Session/YAML 136/136、原生 18/18，独立 SDK 头及源/依赖边界审计通过；与上一轮 UI-S4 的人工确认分开，见 [开发与审查记录](home-management-development.md)。本轮按用户新指令随本提交归档，包含此前未提交的 Ela UI 基线及本次卡片窗口/配置实例添加。

Q2g 回归：Ela/MSVC Debug、Release 各 428/428，GCC Debug、Release 各 210/210，ASan connection/worker/快照及真实配置驱动专项 109/109；11 项前缀/原生 watcher/配置身份与最新目标检查在 Debug、ASan 各连续 20 次通过。两编译器均接受原生前缀，按预期诊断拒绝六类非法契约。见 [Q2g 记录](p3-development.md#q2g-原生-qt-信号前缀与验收映射)、[ADR-0027](adr/0027-native-signal-prefix.md)及 [P3 验收映射](p3-acceptance.md)。验收映射区分自动化证据、id 变更语义和人工待验，没有将 P3 整包标为完成。

本轮提交前审核：Ela Debug/Release 各 418/418，GCC 各 209/209，ASan 专项 68/68；17 项信号/释放预算检查及示例 Debug/ASan 各连续 20 次通过。预算回归修复前真实失败、修复后通过；完整范围见 [审核记录](code-review.md#q2eq2f-与桌面环境增量提交前审核)。下文 401/417 等数量及未提交状态保留为各轮历史。

本轮 Q2f：QtConnection::observeValues 冻结数值/枚举及标准/Qt 字符串，参数寿命覆盖业务栈和返回 Task；复制中的票据与待释放数据也受预算及关闭屏障管理。新增 15 项真实 Qt 场景和 1 项联合退出示例。Ela/MSVC Debug、Release 各 417/417，GCC Debug、Release 各 209/209；ASan connection/worker/快照专项 67/67，16 项新检查 Debug/ASan 各连续 20 次通过。GCC/MSVC C++17 独立头/工厂语法检查通过，QObject* 数据在两编译器均按预期拒绝；不作为 GCC/Qt 二进制组合验收。见 [Q2f 记录](p3-development.md#q2f-类型化-qt-信号连接)、[ADR-0026](adr/0026-typed-qt-connections.md)。

本轮新增 Q2e：QtValueWorker<T> 将后台数据冻结为不可变快照，成功结果继承原生线程/TLS、捕获/宿主与 effect 退役门禁，支持不可复制类型；结果数据寿命与资源关闭分别管理。新增 10 项线程场景、1 项计算示例及 1 项无 Qt 快照单测。Ela Debug/Release 各 401/401，GCC Debug/Release 各 209/209，ASan worker/快照专项 34/34；新场景与示例 Debug/ASan 各连续 20 次通过，见 [Q2e 记录](p3-development.md#q2e-类型化-worker-结果)和 [ADR-0025](adr/0025-typed-worker-results.md)。环境入口为 tools/desktop.ps1。初次桌面授权超时后按用户要求重试成功，完成文件监听/冲突/退出及进程 150% 明暗布局的[原生代理复核](q4d3-desktop-verification.md)，未代填用户人工确认。

本轮新增 Q2d：QtWorker 将一次后台工作纳入所属 Context/epoch，提供协作取消、Windows 原生线程/TLS 退出证明、owner 捕获释放与独立资源关闭屏障。审查后共 21 项场景及 1 项联合退出示例，修复自动退役干扰延续和暂态门禁错误过早保留；Ela Debug/Release 各 389/389，GCC 各 208/208，ASan 相关 236/236，22 项 worker 检查 Debug/ASan 各连续 20 次通过，见 [审查记录](code-review.md#p3-q2d-托管-worker-提交前审核)及 [开发记录](p3-development.md#q2d-托管工作线程)。

上一轮 Q2c：QtConnection 将信号作为零参数触发，业务排队到所属 Context/epoch；关闭等待原生调用快照、异步任务和捕获释放。已有 16 项真实 Qt 场景和 headless 联合退出示例；审查补修断连重入、取消误报及既有 Home 测试失效指针。Ela Debug/Release 各 367/367，GCC 各 208/208，ASan 相关 214/214，见 [审查记录](code-review.md#p3-q2c-托管-qt-连接提交前审核)及 [开发记录](p3-development.md#q2c-托管-qt-信号连接)。

上一轮完成 Q4d3：配置文件在私有线程串行读写，退出等待真实 join；原生 watcher 恢复原子替换/删除重建后的监听并按版本消重。配置页提供显式载入磁盘原文，不自动覆盖编辑或应用目标，保存继续检查冲突。新增 12 项 I/O/watcher 和 1 项 UI 场景，详见 [开发记录](p3-development.md#q4d3-配置后台-io-与文件监听)及 [ADR-0022](adr/0022-configuration-background-io-watcher.md)。Release 仍为 build/ela-msvc-release/bin/cordis_config_workbench.exe。 上一轮审查修复编辑重入、监听游标/回退/重连和退出等待五项问题；Ela 各 350/350，GCC 各 208/208，ASan UI/Loader 197/197，见 [审查记录](code-review.md#p3-配置子树会话与后台读写提交前审核)。

前一轮 Q4c2c2 已完成真实父子归属、域继承/覆盖、自动重建、失败及退出，新增 11 项驱动场景及 YAML 父子示例；当时完整回归为 Ela 各 305/305、GCC 各 201/201，ASan Loader 88/88、Core 相关 27/27，见 [子树记录](p3-development.md#q4c2c2-显式配置子树协调)。上述未提交状态为当时记录，后续开发与审查修复随本次一并归档。

已提交的审查修复内部投递故障挂起、忙碌开关状态及已移动 remount 来源崩溃；提交前原目录 Ela/MSVC Debug/Release 各 287/287、GCC 各 183/183，ASan UI/Loader 141/141、Core 10/10。Q4c2a/b 包含实例级依赖联合、契约变更重建及部分应用再规划，Runtime 定义不随配置膨胀，见 [审核记录](code-review.md#p3-kernel-配置驱动与-home-生命周期操作提交前审核)。

本轮新增真实配置执行、依赖排序/恢复、失败重试、realm 重建和资源排空，以及 13 项真实 Kernel 单测和 1 项示例。原目录 Ela/MSVC Debug/Release 各 273/273、无 Qt GCC 各 172/172、Loader ASan 70/70 通过，见 [Q4c2a 记录](p3-development.md#q4c2a-顶层内置配置驱动)。工作台/Home 先前审查和 UI ASan 61/61 证据保留于 [审核记录](code-review.md#p3-ela-工作台与-home-提交前审核)。

后续按用户要求将 Home 卡片改为状态展示和单个左右滑动开关，移除卡片打开跳转及浮层；支持停止、以新身份加载和失败重试，尺寸改为宽 280、最低高 144。电源操作初版完整回归为原目录 Ela/MSVC Debug/Release 各 276/276、GCC 各 173/173，UI ASan 63/63 和 Core remount ASan 1/1；左右开关的定向复核另见 [Home 记录](p3-development.md#home-状态卡片与电源操作)。此前桌面验收确认保留，本次 Home 变更单独待人工复核，不重新阻塞已开始的 Q4 工作。

Qt 5.15.2 Core/Widgets/Test 已就绪，本机入口见 [开发环境](development.md)，历史工具链见 [P3 记录](p3-development.md)。配置后台 I/O/watcher、零参数连接、类型化信号值、Windows 一次 worker 及类型化结果已接入。P3 当前支持矩阵已按场景收口，其他资源扩展另验；任意自定义信号值、其他 worker 后端/多任务、模块管理及跨 ABI 未由本轮实现或证明。

上一阶段跨组件验收：Ela Debug/Release 各 432/432；四项真实装配检查 Debug/ASan 各连续 20 次通过，覆盖活动父子树拒绝错误配置、显示名/身份/页面撤销、部分激活与真实后台写盘失败、缺依赖管理与原生 worker 阻塞退出。见 [开发记录](p3-development.md#p3-跨组件装配验收收口) 和 [P3 验收映射](p3-acceptance.md)。该阶段已审核提交为 766db1e；其后用户确认与原生补验完成，当前结论见 [P3 收口审计](p3-closeout.md)。

## 状态规则

| 状态 | 含义 |
|---|---|
| 未开始 | 尚无该工作包的有效实现 |
| 进行中 | 正在实现，有具体剩余项 |
| 待验证 | 实现已写入，验收或必要检查尚未完成 |
| 已完成 | 交付存在、完成条件满足、验证结果有记录 |
| 阻塞 | 依赖外部条件，注明原因、影响与解除条件 |

部分完成时在工作包内列出已完成子项，整包仍保持进行中；不把占位接口、待实施测试规范或编译通过当作功能验收通过。

## 里程碑

| 阶段 | 状态 | 当前证据与剩余项 |
|---|---|---|
| P0 研究、设计与编码准备 | 已完成（内核起步范围） | 设计说明书、契约、工程及环境检查齐备；桌面工具链在对应阶段前补齐 |
| P1 内置插件与基础内核 | 已完成（首阶段内置范围） | W1–W3/W6/W7 与 A01–A10 内核证据；生产适配、全局事件 schema/post 另验 |
| P2 响应式服务依赖 | 已完成（首阶段内置范围） | W4/W5/W7 联合闭环通过；跨 ABI、物理卸载及替换不在本轮 |
| P3 Qt / Loader / Ela | 已完成（当前支持矩阵） | L01–L04/Q01/U01/S05/S08/S10 及真实装配检查通过；Q4d3 已获本次用户确认，Home 手动路径/原生 150% 代理复核通过，见 [收口审计](p3-closeout.md)；不补造系统 DPI/跨屏证据 |
| UI 设计与优化（独立专项） | 已完成（当前支持矩阵） | UI-S0–S4 与 UI-00–07 已完成，原生 32/32、三次用户确认和最终审计归档；跨屏未覆盖，见 [阶段计划](ui-special-stages.md)及[增量验证](ui-structure-verification.md) |
| P4 原生插件模块 | 进行中 | M1/M2a 已完成；M2b 业务 ABI、M3 请求与 Kernel 集成、M4 UI DLL 待实施，见 [阶段记录](p4-development.md) |
| P5 受控替换 | 未开始 | 新旧代替换、恢复和卸载专项验证待实现 |

## UI 设计与优化专项

2026-10-27 按用户要求新增。目标为 VS Code 工作台分区与 Ela 组件/交互统一；保留已确认的插件内部资源树、可全部关闭的中央页签、空画布、前进后退、默认收起底部以及 Home 单个左右启停开关。专项不重编号 P3/P4/P5，也不撤销 P3 历史验收。

本表是专项工作包状态的统一入口；范围、优先级和完成条件见 [专项计划](ui-optimization-plan.md)，逐项验收见 [专项验收单](../tests/acceptance/ui-optimization.md)。

按用户最新要求，阶段为主要推进顺序，原工作包用于保留职责和证据：

| 阶段 | 名称 | 状态 | 当前交付与下一步 |
|---|---|---|---|
| UI-S0 | Ela 基线与既有改动收口 | 已完成 | 组件/字体、状态/偏好/键盘实现及两套完整构建、ASan、原生检查与两次用户确认已记录；随本提交归档 |
| UI-S1 | VS Code 全结构分析与 Cordis 对照 | 已完成 | [结构对照](vscode-ui-structure.md)覆盖主要区域、公共交互和独立能力边界，四项增量行为及 UI-A21–24 验收映射明确 |
| UI-S2 | 工作台框架与上下文操作 | 已完成 | 页签批量关闭、代次快照、输出工具及分隔条主题已实现；复制/清空两档原生检查和本次人工确认通过 |
| UI-S3 | 宿主页、检索与反馈 | 已完成 | 设置中英文检索、空状态、Ctrl+F/Escape 和通知历史入口已实现；自动化、原生与本次新增体验确认通过 |
| UI-S4 | 联合验证、体验与最终收口 | 已完成 | 最终 Release 454/454、ASan UI 114/114，Debug 453 项及截图项独立五次通过；原生 32/32、用户新增确认及源快照/审计归档 |

| 编号 | 内容 | 状态 | 已有交付与下一动作 |
|---|---|---|---|
| UI-00 | 设计规范与页面盘点 | 已完成 | [组件/字体与页面盘点](ui-design-system.md)、VS Code 结构分析及增量设计/验收映射已交付 |
| UI-01 | 工作台框架 | 已完成 | 菜单、响应式搜索、状态栏、页签/面板、上下文关闭与输出工具已验证；默认收起、空画布及插件所有权保持 |
| UI-02 | Home 插件中心 | 已完成 | Ela 品牌卡片、真实状态图标、长名称/空状态、左右启停及稳定刷新已实现并回归，基线用户体验已确认 |
| UI-03 | 插件管理与诊断 | 已完成 | 手动/配置表的类型化图标、密度、详情、权限与禁用原因已实现，最终管理/配置管理回归通过 |
| UI-04 | 配置编辑与反馈 | 已完成 | 分区、Ela 字体、路径、编辑/运行/磁盘独立反馈及失败恢复已实现；最终配置回归、原生入口及用户体验通过 |
| UI-05 | 设置、关于与界面偏好 | 已完成 | 偏好保存/恢复/失败/隔离、设置检索及关于页已实现；自动化、原生和新增体验确认通过 |
| UI-06 | 键盘、缩放与细节 | 已完成（当前支持矩阵） | 键盘/焦点、明暗/宽窄与进程 150% 已验证，用户系统 150% 及新增交互分别确认；单屏环境未覆盖跨屏 |
| UI-07 | 联合回归与收口 | 已完成 | 基线与新增自动化、原生 32/32、用户确认、修复审查和最终归档完成；不扩大为外部插件或模块卸载认证 |

UI 专项已收口，基线、追加实现及验收文档已提交于 31e52ab；当前按主计划推进 P4，最新状态见本文件首节与 [P4 阶段记录](p4-development.md)。

## 内核工作包

W0–W7 沿用 [现有工作单](p1-work-items.md) 编号，其中服务工作包覆盖总体里程碑 P2。此表维护状态，工作单维护范围和完成条件。

| 编号 | 内容 | 状态 | 验证/交付依据 | 下一动作 |
|---|---|---|---|---|
| W0 | 工程、基础类型和构建检查 | 已完成 | [准备记录](preparation-status.md)、[环境验证](environment-verification.md) | 保持构建可用 |
| W1 | Result、调度、Task/Promise、取消 | 已完成 | [W1 验证记录](w1-verification.md) | 供 W2 使用，保持契约回归 |
| W2 | EffectScope、清理屏障 | 已完成 | [W2 验证记录](w2-verification.md)；A05 仅 Effect 层 | 供 W3 使用，保持屏障回归 |
| W3 | 实例、Context、epoch | 已完成（无服务范围） | [W3 验证记录](w3-development.md)；包含 CleanupContext 访问窗口 | 供 W4 使用，保持生命周期回归 |
| W4 | Binding、realm、服务租约 | 已完成 | [W4 验证记录](w4-development.md)；包含输出契约、域视图及异步调用租约 | 供 W5 使用，保留服务回归 |
| W5 | 子实例及依赖环 | 已完成 | [W5 交付记录](w5-development.md)；D05、S02 及 A07 父子清理 | 保持子树与环诊断回归 |
| W6 | EventBus 五类分发 | 已完成 | [W6 交付记录](w6-development.md)；E01/E02 首阶段场景通过 | 保持事件回归 |
| W7 | 插件闭环示例与验收 | 已完成 | [联合验收](w7-verification.md)；恢复、事件关闭屏障、失败残留 | 进入 P3 前置检查 |

### W1 完成项

- [x] Result<T> / Result<void> 和错误访问契约。
- [x] IScheduler、FakeScheduler、投递拒绝及队列步数限制。
- [x] Task/Promise 的一次完成仲裁及多个观察者。
- [x] thenOn 始终排队，包括已经完成的 Task。
- [x] 失败 continuation 和 scheduler 关闭路径。
- [x] CancellationSource：取消请求与底层完成分离。
- [x] 为上述真实行为添加单测，完成适用工具链验证及记录。

这些条目已有真实实现与测试。W2 也已完成 Effect 控制块、清理屏障、FakeClock 与超时观察，见 [交付记录](w2-verification.md)。W3–W7 已验收，P3 的 Qt/Ela 自动化基线及 Q4a/Q4b 的内置配置解析、schema 和预览独立完成；这些无运行副作用前置工作不替代 Q3c 的人工桌面验收。

## 验证台账

| 检查范围 | 最近验证日期 | 结果 | 证据 |
|---|---|---|---|
| GCC Debug/Release 基础构建、测试、示例 | 2026-10-23 | 通过，各 1/1 基础检查 | [环境报告](environment-verification.md) |
| MSVC x64 Debug/Release 基础构建、测试、示例 | 2026-10-23 | 通过，各 1/1 基础检查 | 同上，MSVC 19.44 / v143 |
| W1 Result、调度、Task、取消及异步示例 | 2026-10-23 | GCC/MSVC Debug/Release 各 26/26；3 个竞态用例各重复 20 次通过 | [W1 验证记录](w1-verification.md) |
| 精确 Qt 5.15.2 前置检查 | 2026-10-23 | 不满足：未找到目标 kit | 同上，现有 Qt 6.11.0 被正确拒绝 |
| 4 份参考仓库的固定版本与关键文件 | 2026-10-23 | 已核对 | [准备记录](preparation-status.md)、[研究证据](../research-evidence.md) |
| W2 Effect、清理屏障、时钟与观察 | 2026-10-23 | GCC/MSVC Debug/Release 各 60/60（含 W1 回归）；4 个 W2 头文件独立包含通过 | [W2 验证记录](w2-verification.md) |
| W6 事件协议、作用域及控制台示例 | 2026-10-24 | GCC/MSVC Debug/Release 各 110/110；13 项事件场景和 1 项示例；event.hpp 独立包含检查 | [W6 交付记录](w6-development.md) |
| W3 实例、Context、配置、epoch 和 Kernel | 2026-10-23 | 四配置各 64/64；10 组既有行为加 3 个独立清理上下文用例；不含 realm | [W3 开发记录](w3-development.md) |
| W4 服务、描述契约、域视图与调用租约 | 2026-10-23 | GCC/MSVC Debug/Release 各 86/86；含 W1–W3 回归及 22 项 W4 测试 | [W4 验证记录](w4-development.md) |
| W5 可选子实例、父子清理与联合环诊断 | 2026-10-24 | GCC/MSVC Debug/Release 各 96/96；共 10 项 W5 测试及 3 个公共头独立包含检查 | [W5 验证记录](w5-development.md) |
| W7 联合闭环及首阶段验收汇总 | 2026-10-24 | GCC/MSVC Debug/Release 各 113/113；新增恢复、事件在途关闭及失败残留三项联合场景 | [W7 联合验收](w7-verification.md) |
| P3 Q0 Qt 5.15.2 工具链与事件循环原语 | 2026-10-24 | Qt/MSVC Debug/Release 各 114/114；无 Qt GCC Debug 113/113；Qt6 拒绝与环境恢复通过 | [P3 记录](p3-development.md) |
| P3 Q1 QtScheduler、关闭仲裁及 Kernel 异步停止 | 2026-10-24 | Qt/MSVC Debug/Release 各 122/122；8 项真实循环调度场景；无 Qt GCC Debug 113/113 | [P3 Q1 记录](p3-development.md#q1-qtscheduler)、[ADR-0004](adr/0004-qt-scheduler.md) |
| P3 Q0/Q1 提交前审查、调度器移动与析构重入 | 2026-10-24 | Qt/MSVC Debug/Release 各 126/126；无 Qt GCC Debug/Release 各 115/115；两个头独立包含通过 | [审核记录](code-review.md)、[P3 Q1 记录](p3-development.md#q1-qtscheduler) |
| P3 Q2 Timer/QObject 资源屏障与 Kernel 集成 | 2026-10-24 | Qt/MSVC Debug/Release 各 139/139；13 项真实 Qt 资源场景；无 Qt GCC Debug 115/115；两个新公共头独立包含通过 | [P3 Q2 记录](p3-development.md#q2-timerqobject)、[ADR-0005](adr/0005-qt-resource-barriers.md) |
| P3 Q2 提交前审查与资源生命周期检查 | 2026-10-24 | Qt/MSVC Debug/Release 各 140/140；无 Qt GCC Debug/Release 各 115/115；14 项资源场景 ASan 通过 | [审核记录](code-review.md)、[Q2 审查记录](p3-development.md#q2-提交前审查) |
| P3 Q3a Ela 页面贡献、服务恢复与窗口退出 | 2026-10-24 | Ela/MSVC Debug/Release 各 149/149；无 Qt GCC Debug 115/115；9 项 UI 场景 ASan 通过；两个新公共头独立包含通过 | [Q3a 记录](p3-development.md#q3a-ela-页面贡献)、[ADR-0006](adr/0006-ela-page-contributions.md) |
| P3 Q3a 提交前审查、关闭/输入门禁及键盘导航 | 2026-10-24 | Ela/MSVC Debug/Release 各 151/151；无 Qt GCC Debug/Release 各 115/115；11 项 UI 场景 ASan 通过；两个公共头独立包含通过 | [审核记录](code-review.md#p3-q3a-提交前审核)、[Q3a 审查记录](p3-development.md#q3a-提交前审查) |
| P3 Q3b1 命令屏障、服务恢复与纯值通知 | 2026-10-24 | Ela/MSVC Debug/Release 各 162/162；无 Qt GCC Debug 115/115；22 项 UI 场景 ASan 通过；三个公共头独立包含通过 | [Q3b1 记录](p3-development.md#q3b1-命令与通知)、[ADR-0007](adr/0007-ui-commands-notifications.md) |
| P3 Q3b1 提交前审查、注册/Qt 初始化重入及错误展示 | 2026-10-24 | Ela/MSVC Debug/Release 各 164/164；无 Qt GCC Debug/Release 各 115/115；24 项 UI 场景 ASan 通过；三个公共头独立包含通过 | [审核记录](code-review.md#p3-q3b1-提交前审核)、[Q3b1 审查记录](p3-development.md#q3b1-提交前审查) |
| P3 Q3b2 基础管理页、只读诊断与受控操作 | 2026-10-24 | Ela/MSVC Debug/Release 各 178/178；无 Qt GCC Debug/Release 各 117/117；36 项 UI 场景 ASan 通过；三个公共头独立包含通过 | [Q3b2 记录](p3-development.md#q3b2-基础管理页)、[ADR-0008](adr/0008-management-diagnostics.md) |
| P3 Q3c Ela 基础库、原生导航与工作台 | 2026-10-24 | 审查后 Ela/MSVC Debug/Release 各 187/187；GCC Debug/Release 各 117/117；45 项 UI 场景 ASan 通过；浅/深色 offscreen 图像已查看 | [Q3c 记录](p3-development.md#q3c-ela-组件原生导航与工作台)、[ADR-0010](adr/0010-maintained-ela-navigation.md)、[审查](code-review.md#p3-q3c-提交前审核) |
| P3 Q3c Example 交互与插件内部布局 | 2026-10-24 | Ela/MSVC Debug/Release 各 250/250，52 项 UI ASan 通过；取消全局资源树，树归插件内部；人工验收待办 | [本轮记录](p3-development.md#q3c-example-交互与插件内部布局)、[ADR-0010](adr/0010-maintained-ela-navigation.md) |
| P3 Q3c 多行卡片高度修复 | 2026-10-24 | Debug/Release 各 252/252，含 54 项 UI 检查；欢迎/设置/关于明暗及 150% 缩放布局回归通过，笔记卡片同修 | [修复记录](p3-development.md#多行卡片高度修复) |
| P3 Q3c 插件直达、空工作区与 Home | 2026-10-24 | 原目录 Debug/Release 各 255/255，57 项 UI ASan 通过；所有页签可关闭，零页签空白，插件卡片/悬浮详情和默认目标动态同步 | [交付记录](p3-development.md)、[ADR-0010](adr/0010-maintained-ela-navigation.md) |
| P3 工作台/Home 提交前审查 | 2026-10-25 | 原目录 Ela Debug/Release 各 259/259，GCC 各 158/158；61 项 UI ASan，通过四项缺陷回归与格式/公开头检查 | [审查记录](code-review.md#p3-ela-工作台与-home-提交前审核) |
| P3 Q4a 配置结构候选与差异预览 | 2026-10-24 | Ela/MSVC Debug/Release 各 194/194；GCC Debug/Release 各 124/124；7 项 Loader 场景 ASan 通过，含子树、预算和规范化 | [Q4a 记录](p3-development.md#q4a-配置结构候选与差异预览)、[ADR-0011](adr/0011-configuration-preview.md) |
| P3 Q4b 受限 YAML / 内置定义与文件预览 | 2026-10-24 | Ela/MSVC Debug/Release 各 204/204；GCC Debug/Release 各 134/134；17 项 Loader 场景 ASan 通过，C/C++ 解析实现均插桩 | [Q4b 记录](p3-development.md#q4b-受限-yaml-与内置定义校验)、[ADR-0012](adr/0012-restricted-yaml-validation.md) |
| P3 Q4c1 目标接受与串行协调基础 | 2026-10-24 | Ela/MSVC Debug/Release 各 214/214；GCC Debug/Release 各 144/144；27 项 Loader 场景 ASan 通过，其中协调协议 10 项 | [Q4c1 记录](p3-development.md#q4c1-目标接受与串行协调基础)、[ADR-0013](adr/0013-configuration-coordinator.md) |
| P3 Q4a/Q4b/Q4c1 提交前审查 | 2026-10-24 | Ela/MSVC Debug/Release 各 215/215；GCC Debug/Release 各 145/145；28 项 Loader 场景 ASan 通过；新增规划期间关闭门禁回归 | [审查记录](code-review.md#p3-q4aq4bq4c1-提交前审核)、[Q4 审查验证](p3-development.md#q4aq4bq4c1-提交前审查) |
| P3 Q4d1 配置文件事务基础 | 2026-10-24 | Ela/MSVC Debug/Release 各 226/226；GCC Debug/Release 各 145/145；39 项 Loader ASan 通过，含 11 项真实文件场景 | [Q4d1 记录](p3-development.md#q4d1-配置文件事务基础)、[ADR-0014](adr/0014-configuration-file-transactions.md) |
| P3 Q4d2 Store 状态、单写队列及退出刷新 | 2026-10-24 | Ela/MSVC Debug/Release 各 242/242；GCC Debug/Release 各 157/157；55 项 Loader ASan 通过，含新增 12 项通用协议与 4 项真实 Qt 文件集成 | [Q4d2 记录](p3-development.md#q4d2-store-状态单写队列和退出刷新)、[ADR-0015](adr/0015-configuration-store.md) |
| P3 Q4d1/Q4d2 提交前审查 | 2026-10-24 | Ela/MSVC Debug/Release 各 243/243；GCC Debug/Release 各 158/158；56 项 Loader ASan 通过，新增 Unchanged 基线一致性回归 | [审查记录](code-review.md#p3-q4d1q4d2-提交前审核)、[审查验证](p3-development.md#q4d1q4d2-提交前审查) |
| P3 Q3c 原生桌面人工交互 | 2026-10-25 | 用户在本次会话确认完成；未另行记录系统/分辨率/缩放明细 | [人工验收单](../tests/acceptance/p3-ui.md) |
| P3 Q4c2a 顶层内置 Kernel 驱动 | 2026-10-25 | Ela/MSVC Debug/Release 各 273/273，GCC Debug/Release 各 172/172；Loader ASan 70/70；新增 13 项真实 Kernel 场景及 1 项示例 | [开发记录](p3-development.md#q4c2a-顶层内置配置驱动)、[ADR-0016](adr/0016-kernel-configuration-driver.md) |
| P3 Q4c2b 动态 inject 契约 | 2026-10-25 | Ela/MSVC Debug/Release 各 285/285，GCC 各 182/182；最终驱动四配置各 20/20；ASan Loader 76/76、Core 10/10 | [开发记录](p3-development.md#q4c2b-动态-inject-契约)、[ADR-0017](adr/0017-instance-inject-contracts.md) |
| Q4c2a/b 与 Home 提交前审查 | 2026-10-25 | Ela/MSVC Debug/Release 各 287/287，GCC 各 183/183；ASan UI/Loader 141/141、Core 10/10；修复前 0/3、修复后 3/3 | [审查记录](code-review.md#p3-kernel-配置驱动与-home-生命周期操作提交前审核) |
| P3 Q4c2c1 父代上下文与子实例桥 | 2026-10-25 | Ela/MSVC Debug、Release 各 292/292，GCC Debug、Release 各 188/188；专项 5/5、ASan 45/45 | [开发记录](p3-development.md#q4c2c1-父代上下文与子实例桥)、[ADR-0018](adr/0018-activation-context-bridge.md) |
| Q4c2c1 提交前审查 | 2026-10-25 | Ela/MSVC Debug、Release 各 294/294，GCC Debug、Release 各 190/190；ASan 47/47；重入回归修复前 0/2、修复后 2/2 | [审查记录](code-review.md#p3-父代上下文与子实例桥提交前审核) |
| P3 Q4c2c2 内置显式配置子树 | 2026-10-25 | Ela/MSVC Debug、Release 各 305/305，GCC Debug、Release 各 201/201；ASan Loader 88/88、Core 相关 27/27；驱动/示例 32/32 | [开发记录](p3-development.md#q4c2c2-显式配置子树协调)、[ADR-0019](adr/0019-configuration-subtrees.md) |
| P3 Q4e1 配置会话与可选 Ela 配置页 | 2026-10-25 | Ela/MSVC Debug、Release 各 321/321，GCC Debug、Release 各 208/208；ASan UI/Loader 168/168；新增 16 项检查，明暗及 150% 布局通过 | [开发记录](p3-development.md#q4e1-配置会话与-ela-配置页)、[ADR-0020](adr/0020-configuration-session-ui.md)；2026-10-25 用户确认人工验收通过 |
| P3 Q4e2 Home/管理页配置路由 | 2026-10-25 | Ela Debug/Release 各 334/334，GCC 各 208/208，ASan UI/Loader 181/181；新增 13 项，重入场景 Debug/ASan 各连续 8 次通过 | [开发记录](p3-development.md#q4e2-home-与管理页配置路由)、[ADR-0021](adr/0021-configuration-management-routing.md)；本次用户确认人工验收通过 |
| P3 Q4d3 配置后台 I/O 与监听 | 2026-10-25 | Ela Debug/Release 各 347/347，GCC 各 208/208，ASan UI/Loader 194/194；新增 13 项 | [开发记录](p3-development.md#q4d3-配置后台-io-与文件监听)、[ADR-0022](adr/0022-configuration-background-io-watcher.md)；新增体验人工复核待验 |
| Q4 配置集成提交前审查 | 2026-10-25 | Ela Debug/Release 各 350/350，GCC 各 208/208，ASan UI/Loader 197/197；三项新回归修复前 0/3、修复后 3/3，目录恢复另有重复证据 | [审查记录](code-review.md#p3-配置子树会话与后台读写提交前审核)；随本次归档 |
| P3 Q2c 托管 Qt 信号连接 | 2026-10-26 | Ela Debug/Release 各 365/365，GCC 各 208/208，ASan 相关 212/212；连接专项 Debug/ASan 各连续 20 次通过 | [开发记录](p3-development.md#q2c-托管-qt-信号连接)、[ADR-0023](adr/0023-managed-qt-connections.md) |
| Q2c 提交前审查 | 2026-10-26 | Ela Debug/Release 各 367/367，GCC 各 208/208，ASan 214/214；新增两项连接回归修复前 0/2、修复后专项 17/17，Home 失效指针诊断确认并修正 | [审查记录](code-review.md#p3-q2c-托管-qt-连接提交前审核) |
| P3 Q2d 托管工作线程 | 2026-10-26 | Ela Debug/Release 各 385/385，GCC 各 208/208，ASan 相关 232/232；Qt-only 18/18，worker 检查 Debug/ASan 各连续 20 次通过 | [开发记录](p3-development.md#q2d-托管工作线程)、[ADR-0024](adr/0024-managed-workers.md) |
| Q2d 提交前审查 | 2026-10-26 | Ela Debug/Release 各 389/389，GCC 各 208/208，ASan 236/236；worker 专项 22/22，Debug/ASan 各连续 20 次通过；新增三项门禁回归真实失败后修正 | [审查记录](code-review.md#p3-q2d-托管-worker-提交前审核) |
| 本机桌面环境恢复及 Q2e 类型化 worker | 2026-10-26 | Ela Debug/Release 各 401/401，GCC Debug/Release 各 209/209；ASan worker/快照专项 34/34，新场景 Debug/ASan 各重复 20 次；Q4d3 原生代理复核及退出码 0 | [开发环境](development.md)、[Q2e](p3-development.md#q2e-类型化-worker-结果)、[桌面复核](q4d3-desktop-verification.md)；未提交，未补记用户人工确认 |
| Q2f 类型化 Qt 信号 | 2026-10-27 | Ela Debug/Release 各 417/417，GCC 各 209/209；ASan 专项 67/67，新 16 项检查 Debug/ASan 各重复 20 次；编译期拒绝指针值 | [Q2f](p3-development.md#q2f-类型化-qt-信号连接)、[ADR-0026](adr/0026-typed-qt-connections.md)；未提交，未改变 P4/P5 状态 |
| Q2e/Q2f 增量提交前审核 | 2026-10-27 | 修复释放中数据预算提前归还；Ela 各 418/418、GCC 各 209/209、ASan 专项 68/68，17 项 Debug/ASan 各重复 20 次 | [审核记录](code-review.md#q2eq2f-与桌面环境增量提交前审核)；随本次提交归档，P3/P4/P5 状态不变 |
| Q2g 原生 Qt 信号前缀与验收映射 | 2026-10-27 | Ela 各 428/428、GCC 各 210/210、ASan 专项 109/109，11 项 Debug/ASan 各重复 20 次；GCC/MSVC 正向及六类拒绝检查 | [Q2g](p3-development.md#q2g-原生-qt-信号前缀与验收映射)、[P3 验收映射](p3-acceptance.md)；本轮未提交，人工确认待登记 |
| Home 状态卡片、电源停止/加载/重试 | 2026-10-25 | Ela/MSVC Debug/Release 各 276/276，GCC 各 173/173；UI ASan 63/63、Core remount 1/1；明暗及 150% 布局通过 | [Home 记录](p3-development.md#home-状态卡片与电源操作)；本次 Home 专项人工复核待办 |
| Home 左右滑动开关替换 | 2026-10-25 | 原目录 Ela/MSVC Debug/Release 专项各 34/34，ASan 同范围 34/34；同步位置不触发操作，连续点击去重通过 | [Home 记录](p3-development.md#home-状态卡片与电源操作)；仅私有 UI 接口变更 |
| P4-M1 模块预检及宿主驻留账目 | 2026-10-28 | MSVC Debug/Release 各 473/473；GCC Debug 215/215；ASan P4/关闭示例配置各 14/14；真实路径/共享回归及源/边界审计通过 | [开发与验证记录](p4-development.md)，审查后随本提交归档 |
| 原生模块/ABI、物理卸载和替换 | — | P4 后续工作包及 P5 待实现或验证 | [设计说明书](../Cordis插件平台-软件设计说明书.md) |

“最近验证”是当时已检查的版本；后续代码变化不自动继承通过状态。新验证需注明覆盖范围、工具链和失败/跳过项。临时构建日志位于 build，持久结论保存在本台账及对应报告。

## 阻塞与风险

| 编号 | 条件 | 影响 | 解除条件 | 当前处理 |
|---|---|---|---|---|
| B01（已解除） | 原生桌面人工验收已获用户确认；缺 Qt kit 的旧条件此前已解除 | Q4 Kernel 运行树/管理入口可继续推进 | 2026-10-25 用户确认完成 | [验收单](../tests/acceptance/p3-ui.md) 按用户确认登记，不补造环境明细 |
| B02 | 原 v142 组合未安装或验证 | 若坚持精确 v142 或进入插件 ABI，需另验 | 补齐原工具集或冻结经专项验证的选定组合 | 当前 Qt5 msvc2019_64 + MSVC19.44/v143 已通过 Debug/Release 探测；不构成 DLL ABI 验收 |

首阶段无 Qt 内核工作已收口，Qt/Ela、配置子树、会话、文件/Store 及可选配置工作台已有证据。本机 TEMP/TMP 与依赖前缀由 desktop.ps1 隔离设置，详见开发环境。配置工作台及统一路由已获人工确认；监听/后台读写已完成原生代理复核，并于 2026-10-27 获用户确认。其他插件资源适配、外部模块产物/ABI 另验，Ela 全局对象仍按进程生命周期驻留。

## 工作记录

| 日期 | 工作 | 结果/证据 | 剩余项 | 提交 |
|---|---|---|---|---|
| 2026-10-23 | 完成研究、开发指南和软件设计说明书 | 固定源码版本、核实/设计/待验分层 | 运行时待实施 | 尚未提交 |
| 2026-10-23 | 建立工程和首阶段契约 | W0 完成，类型与编译链接检查可运行 | W1–W7 | 尚未提交 |
| 2026-10-23 | 迁移到 Eve 并修复旧构建路径 | 当前目录 Debug/Release 验证通过 | 桌面环境另验 | 尚未提交 |
| 2026-10-23 | 完成 GCC/MSVC 四配置环境验证 | 基础检查和示例通过 | B01/B02 | 尚未提交 |
| 2026-10-23 | 建立统一进度台账 | 记录真实基线、任务、验证及阻塞 | 开始 W1 | 尚未提交 |
| 2026-10-23 | 完成 W1，细化投递回执和生产者终结语义 | 四配置各 26/26 通过，竞态重复验证通过；ADR-0002 | W2；生产 Qt 与计时适配未实现 | 尚未提交 |
| 2026-10-23 | 完成 W2 验收；修复时钟步数耗尽丢失回调错误，补充屏障超时与子组失败测试 | GCC/MSVC 四配置各 60/60；[W2 记录](w2-verification.md) | W3；Qt/生产计时适配待实施 | 未提交本轮改动 |
| 2026-10-23 | W3 单次激活、弱 Context、停止屏障与实例隔离 | GCC/MSVC 四配置各 61/61；[W3 记录](w3-development.md) | Kernel、epoch、配置更新、retry | 未提交本轮改动 |
| 2026-10-23 | W3 配置收敛、每代作用域、旧上下文门禁、失败重试 | 四配置各 61/61，七组 W3 场景；[W3 记录](w3-development.md) | Kernel、全局标识、完整查询、CleanupContext | 未提交本轮改动 |
| 2026-10-23 | W3 Kernel 注册/归属/关闭、进程标识、查询，分离 retry 与配置修订 | 四配置各 61/61，九组 W3 场景；两个公共头独立包含通过 | CleanupContext；W4 服务依赖 | 未提交本轮改动 |
| 2026-10-23 | 提交前审核：修复移动后 Runtime、关闭后命令修改账目及 MSVC 依赖前缀检测 | [审核记录](code-review.md)，四配置各 61/61，十组 W3 场景 | CleanupContext；W4 服务依赖 | 随本次提交归档 |
| 2026-10-23 | 完成 W3 清理能力边界，新增 CleanupContext 及三个独立用例 | GCC/MSVC 四配置各 64/64；instance.hpp 独立包含通过 | W4 服务、realm、CleanupLease | 本轮未提交 |
| 2026-10-23 | W4 服务索引、Staged/Published/Retiring/Released、realm 键隔离、同步调用屏障 | 四配置各 69/69；service.hpp 独立包含及返回 Task 拒绝编译检查 | 实例服务输出与依赖传播、committed/CleanupLease | 本轮未提交 |
| 2026-10-23 | W4 Context.consume、committed 保留、旧绑定清理访问及在途调用退出屏障 | 四配置各 73/73；新增 4 项租约测试；两个公共头独立包含通过 | 提供者输出、自动 Pending/恢复与依赖传播 | 本轮未提交 |
| 2026-10-23 | W4 托管 provide、声明依赖、Pending/恢复、自动退役传播及消费者优先清理 | 四配置各 79/79；新增 6 项闭环测试；3 个公共头独立包含通过 | 必供输出、继承式 realm 视图；W5 父子联合图 | 本轮未提交 |
| 2026-10-23 | 完成 W4：必供/可选输出、minor/capability、RealmMap 派生视图、异步调用及诊断 | 四配置各 85/85；新增 6 项收尾测试；3 个公共头独立包含通过 | W5 可选依赖、父子与联合等待图 | 本轮未提交 |
| 2026-10-23 | W4 提交前审查：修复连续更新绕过消费者清理屏障，统一文档状态 | 四配置各 86/86；回归修复前失败、修复后通过；[审核记录](code-review.md) | W5 可选依赖、父子与联合等待图 | 随本次提交归档 |
| 2026-10-23 | 完成 W5：Context::inject、子 Pending/恢复、父代屏障、实际及声明联合环路径 | GCC/MSVC 四配置各 95/95；新增 9 项场景及独立包含检查；[W5 记录](w5-development.md) | W6 EventBus；任意 Task 等待不在可证明图内 | 本轮未提交 |
| 2026-10-24 | W5 提交前审查：修复 Pending 后失败未创建新操作的问题 | 环/版本两条回归修复前均失败，修复后四配置各 96/96；[审核记录](code-review.md) | W6 EventBus | 随本次提交归档 |
| 2026-10-24 | 完成 W6：EventBus 五协议、Observer 隔离、effect/epoch 门禁、快照与异步屏障、事件示例 | 四配置各 109/109；12 项场景、1 项示例及独立包含检查；[W6 记录](w6-development.md) | W7 联合示例与验收汇总 | 本轮未提交 |
| 2026-10-24 | W6 提交前审查：修复用户负载复制异常导致分发悬挂，整理规范与异常安全 | 四配置结果见台账；新增复制异常回归；[审核记录](code-review.md) | W7 联合示例与验收汇总 | 随本次提交归档 |
| 2026-10-24 | 完成 W7：服务/子树/事件联合示例、恢复、正常关闭、失败残留及 A01–A10 证据映射 | 四配置各 113/113；三项联合场景；[联合验收](w7-verification.md) | P3 Qt kit 前置检查与调度适配设计 | 本轮未提交 |
| 2026-10-24 | W7 提交前审查：补强恢复后的事件实际写入和关闭资源断言，防护诊断读取 | 四配置各 113/113；24 个证据测试名已核对；[审核记录](code-review.md) | P3 前置检查 | 随本次提交归档 |
| 2026-10-24 | P3 Q0：按授权安装 Qt5.15.2 qtbase、接入 Qt 预设/运行探测、记录调度关闭契约 | Qt Debug/Release 各 114/114，GCC Debug 113/113；Qt6 拒绝及环境恢复通过；[P3 记录](p3-development.md) | Q1 QtScheduler 实现与真实循环验收 | 随本次提交归档 |
| 2026-10-24 | P3 Q1：Cordis::Qt 与 QtScheduler、宿主 FIFO、lifetime 持有队列、执行/丢弃仲裁、异常诊断及退出兜底 | Qt Debug/Release 各 122/122，GCC Debug 113/113；8 项真实事件循环测试；[P3 Q1 记录](p3-development.md#q1-qtscheduler) | Q2 Timer/QObject 资源屏障 | 随本次提交归档 |
| 2026-10-24 | P3 提交前审查：修复两个调度器持锁移动用户代码、FakeScheduler 析构重入，保留 Qt 关闭控制块并补回归 | Qt Debug/Release 各 126/126，GCC Debug/Release 各 115/115；修复前失败证据见 [审核记录](code-review.md) | Q2 Timer/QObject 资源屏障 | 随本次提交归档 |
| 2026-10-24 | P3 Q2：QObjectResource 完整析构确认、QtClock 原生计时/取消/关闭及 Kernel 清理集成 | Qt Debug/Release 各 139/139，GCC Debug 115/115；13 项真实 Qt 资源场景及头文件隔离；[P3 Q2 记录](p3-development.md#q2-timerqobject) | Q3 Ela UI，connection/worker/watcher 按集成补齐 | 随本次提交归档 |
| 2026-10-24 | P3 Q2 提交前审查：修正测试断言掩盖和通知捕获，补充清理中释放句柄场景，核对规范与资源边界 | Qt Debug/Release 各 140/140，GCC Debug/Release 各 115/115，14 项资源场景 MSVC ASan 通过；[审核记录](code-review.md) | Q3 Ela UI | 随本次提交归档 |
| 2026-10-24 | P3 Q3a：固定 Ela 依赖、页面/分类命名空间、完整清理、服务恢复、输入门禁与窗口示例；复现原生导航 UAF 后采用宿主导航 | Ela Debug/Release 各 149/149，GCC Debug 115/115，9 项 UI 场景 MSVC ASan 通过；[Q3a 记录](p3-development.md#q3a-ela-页面贡献)、[ADR-0006](adr/0006-ela-page-contributions.md) | Q3b 命令/通知/管理页及原生桌面交互 | 随本次提交归档 |
| 2026-10-24 | P3 Q3a 提交前审查：修复关闭请求后仍可操作、快捷键/输入法漏拦及键盘导航，整理规范和诊断 | Ela Debug/Release 各 151/151，GCC Debug/Release 各 115/115，11 项 UI 场景 MSVC ASan 通过；[审核记录](code-review.md#p3-q3a-提交前审核) | Q3b；原生桌面交互、其他专用资源及 ABI 另验 | 随本次提交归档 |
| 2026-10-24 | P3 Q3b1：命令 effect、在途调用/谓词屏障、排队撤销与服务恢复；有界纯值通知、宿主命令列表及示例接入 | Ela Debug/Release 各 162/162，GCC Debug 115/115，22 项 UI 场景 MSVC ASan 通过；[Q3b1 记录](p3-development.md#q3b1-命令与通知)、[ADR-0007](adr/0007-ui-commands-notifications.md) | Q3b2 管理页，原生桌面人工交互，Q4 Loader/Store | 随本次提交归档 |
| 2026-10-24 | P3 Q3b1 提交前审查：修复回调移动和 Qt 插入关闭重入、空错误提示；增强并发排空与规范检查 | Ela Debug/Release 各 164/164，GCC Debug/Release 各 115/115，24 项 UI 场景 MSVC ASan 通过；[审核记录](code-review.md#p3-q3b1-提交前审核) | Q3b2；其他专用资源、原生桌面及 ABI 另验 | 随本次提交归档 |
| 2026-10-24 | P3 Q3b2：弱 Kernel 绑定、只读诊断、精确依赖、受控停止/重试、操作历史与关闭重入屏障；示例包含缺依赖/失败状态 | Ela Debug/Release 各 176/176，GCC Debug/Release 各 117/117，34 项 UI 场景 MSVC ASan 通过；[Q3b2 记录](p3-development.md#q3b2-基础管理页)、[ADR-0008](adr/0008-management-diagnostics.md) | Q4 Loader/Store、配置/模块管理与原生桌面人工交互 | 随本次提交归档 |
| 2026-10-24 | P3 Q3b2 提交前审查：修复管理页导航关闭重入崩溃、取消选择后仍可操作；统一导航入口并核对代码规范与质量 | Ela Debug/Release 各 178/178，GCC Debug/Release 各 117/117，36 项 UI 场景 MSVC ASan 通过；[审核记录](code-review.md#p3-q3b2-提交前审核) | Q4 Loader/Store；原生桌面人工交互及其他专用资源另验 | 随本次提交归档 |
| 2026-10-24 | P3 Q3c：宿主/演示 Ela 组件迁移，视图与数据模型、主题透明背景和标签容器适配，禁止标签拖出独立窗口 | Ela Debug/Release 各 180/180，GCC Debug/Release 各 117/117，38 项 UI 场景 ASan 通过；[Q3c 记录](p3-development.md#q3c-ela-组件原生导航与工作台) | 原生桌面人工验收后推进 Q4 Loader/Store | 本轮未提交 |
| 2026-10-24 | Q3c 后续：按授权纳入 Ela 源码维护、修正原生导航生命周期，采用 VS Code 分区的 Ela 工作台，保持 Core/SDK 边界 | Ela Debug/Release 各 184/184，GCC Debug/Release 各 117/117，42 项 UI 场景 ASan 通过；[ADR-0010](adr/0010-maintained-ela-navigation.md) | 原生桌面人工验收；Q4 Loader/Store | 本轮未提交 |
| 2026-10-24 | Q3c 提交前审查：修复紧凑菜单残留、已删除分组缓存、原生导航关闭重入 UAF 和通知序号重入；整理重复逻辑与包含依赖 | Ela Debug/Release 各 187/187，GCC Debug/Release 各 117/117，45 项 UI 场景 ASan 通过；[审查记录](code-review.md#p3-q3c-提交前审核) | 原生桌面人工验收；Q4 Loader/Store | 4b6139b |
| 2026-10-24 | Q4a：独立 Loader 库、结构候选、规范 EntryPath/依赖、父禁用级联、资源预算和纯目标差异预览 | Ela Debug/Release 各 194/194，GCC Debug/Release 各 124/124，7 项 Loader 场景 ASan 通过；[ADR-0011](adr/0011-configuration-preview.md) | Q3c 人工验收；Q4b 文本/schema、执行和保存 | 7864de9 |
| 2026-10-24 | Q4b：固定 LibYAML 事件解析、预算/重复键/严格字段、规范文本往返、内置定义/schema 与依赖联合、有界文件预览示例 | Ela Debug/Release 各 204/204，GCC Debug/Release 各 134/134，17 项 Loader 场景 ASan 通过；[ADR-0012](adr/0012-restricted-yaml-validation.md) | Q3c 人工验收；Q4c 目标接受/CAS、运行协调与保存 | 7864de9 |
| 2026-10-24 | Q4c1：owner CAS 接受、单动作/最新目标、实际状态再规划、部分失败/Pending、关闭/投递门禁及独立目标诊断 | Ela Debug/Release 各 214/214，GCC Debug/Release 各 144/144，27 项 Loader 场景 ASan 通过；[ADR-0013](adr/0013-configuration-coordinator.md) | Q3c 人工验收；Q4c2 Kernel 驱动，Q4d Store | 7864de9 |
| 2026-10-24 | Q4a/Q4b/Q4c1 提交前审查：修复规划期间调度器关闭后仍执行动作，补充关闭/异常回归，核对规范、依赖来源及质量 | Ela Debug/Release 各 215/215，GCC Debug/Release 各 145/145，28 项 Loader 场景 ASan 通过；[审查记录](code-review.md#p3-q4aq4bq4c1-提交前审核) | Q3c 人工验收；Q4c2 Kernel 驱动，Q4d Store | 7864de9 |
| 2026-10-24 | Q4d1：独立 Qt 文件原文/SHA256 基线、协作锁、冲突来源保留、QSaveFile 提交和失败重试 | Ela Debug/Release 各 226/226，GCC Debug/Release 各 145/145，39 项 Loader ASan 通过；[ADR-0014](adr/0014-configuration-file-transactions.md) | Q3c 人工验收；Q4c2 Kernel 驱动，Q4d2 Store 状态/写队列 | 随本次提交归档 |
| 2026-10-24 | Q4d2：目标/历史保存分离与 dirty、有界单写队列/请求共享、失败跳过/显式 rebase、退出刷新及真实 Qt 写后端 | Ela Debug/Release 各 242/242，GCC Debug/Release 各 157/157，55 项 Loader ASan 通过；[ADR-0015](adr/0015-configuration-store.md) | Q3c 人工验收；Q4c2 Kernel 驱动，Q4d3 watcher 和后台 I/O | 随本次提交归档 |
| 2026-10-24 | Q4d1/Q4d2 提交前审查：修复 Unchanged 回执越过磁盘基线，补齐协议回归、错误阶段及规范检查 | Ela Debug/Release 各 243/243，GCC Debug/Release 各 158/158，56 项 Loader ASan 通过；[审查记录](code-review.md#p3-q4d1q4d2-提交前审核) | Q3c 人工验收；Q4c2 Kernel 驱动，Q4d3 watcher 和后台 I/O | 随本次提交归档 |
| 2026-10-25 | 用户确认 Q3c 桌面人工验收完成，解除 B01；Q4c2a 接入真实顶层 Kernel 驱动、固定依赖/realm、配置重启、失败重试和关闭屏障 | Ela Debug/Release 各 273/273，GCC Debug/Release 各 172/172，Loader ASan 70/70；[ADR-0016](adr/0016-kernel-configuration-driver.md) | 显式配置子树、动态契约、管理页/保存装配，watcher 和后台 I/O | 本轮未提交 |
| 2026-10-25 | Home 卡片取消跳转/浮层、调整尺寸，单电源图标管理停止/加载/失败重试；已停止实例以原配置/realm 创建新身份 | Ela Debug/Release 各 276/276，GCC 各 173/173，UI ASan 63/63、Core remount 1/1；[管理决策](adr/0008-management-diagnostics.md#home-电源操作增补2026-10-25) | 本次 Home 人工复核；Q4 后续工作保持 | 本轮未提交 |
| 2026-10-25 | 按后续偏好把 Home 电源图标换成 Ela 左右开关，按期望启停状态执行，程序刷新屏蔽业务信号 | 原目录 Debug/Release 专项各 34/34，ASan 同范围 34/34，明暗/150% 布局通过 | 本次 Home 人工复核；Q4 后续工作保持 | 本轮未提交 |
| 2026-10-25 | Q4c2b：Core 实例级附加契约，Loader 类型目录/联合/重建，修正部分应用被替代后的旧图排序，示例按 YAML inject 真正等待服务 | Ela Debug/Release 各 285/285，GCC 各 182/182，最终驱动四配置各 20/20，ASan Loader 76/76、Core 10/10 | 显式配置子树、管理页/保存装配及 watcher/后台 I/O | 本轮未提交 |
| 2026-10-25 | 审查全部 Kernel 驱动/Home 改动，修复内部投递故障、忙碌开关和已移动 remount 来源，整理规范及质量 | Ela 各 287/287，GCC 各 183/183，ASan UI/Loader 141/141、Core 10/10；[审查记录](code-review.md#p3-kernel-配置驱动与-home-生命周期操作提交前审核) | 继续显式配置子树 | 8befcbd |
| 2026-10-25 | Q4c2c1：宿主成功代次观察、弱 Context 与类型化子实例注入，父更新等待旧子清理后重建示例 | Ela/MSVC 各 292/292，GCC 各 188/188，ASan 45/45；[ADR-0018](adr/0018-activation-context-bridge.md) | Loader 显式子树协调、管理页保存及 watcher/后台 I/O | 02a2ed7 |
| 2026-10-25 | 审查父代上下文/子实例桥，修复通知重入 stop 遗漏取消、update 后新观察回放旧代；核对规范与质量 | Ela/MSVC 各 294/294，GCC 各 190/190，ASan 47/47；回归修复前 0/2、修复后 2/2；[审查记录](code-review.md#p3-父代上下文与子实例桥提交前审核) | Loader 显式子树协调，管理页保存及 watcher/后台 I/O | 02a2ed7 |
| 2026-10-25 | Q4c2c2：真实内置配置子树、继承域、父恢复自动重建、等待父代诊断及失败/退出；修正等待记录变更/移除和旧图再规划 | 最终 Ela/MSVC 各 305/305，GCC 各 201/201，ASan Loader 88/88、Core 相关 27/27；新增 11 项场景和父子 YAML 示例；[ADR-0019](adr/0019-configuration-subtrees.md) | 管理页配置编辑/启停/保存装配、watcher/后台 I/O 与专用资源 | 本轮未提交 |
| 2026-10-25 | Q4e1：独占配置会话、不可伪造预览/CAS、期望启停、Store/关闭跟踪及可选 Ela 配置页/真实文件工作台；冲突保留窗口、主题/重入销毁修正 | Ela/MSVC 各 321/321，GCC 各 208/208，ASan UI/Loader 168/168；新增 7 项会话、7 项真实 Qt/文件 UI、2 项明暗示例；[ADR-0020](adr/0020-configuration-session-ui.md) | 统一 Home/管理路由、新入口人工验收、watcher/后台 I/O | 本轮未提交 |
| 2026-10-25 | 用户确认 Q4e1 配置工作台验证通过；Q4e2 接入 Home/管理页路径路由、期望启停及重新协调，隔离手动实例并补齐 CAS/失效/关闭防护 | Ela 各 334/334，GCC 各 208/208，ASan UI/Loader 181/181；完整证据见 [Q4e2](p3-development.md#q4e2-home-与管理页配置路由) | 新路由人工复核；Q4d3 watcher/后台 I/O | 本轮未提交 |
| 2026-10-25 | 用户确认 Q4e2 路由验收通过；Q4d3 新增配置后台读写/线程 join、原生监听恢复及自身版本消重，UI 显式载入磁盘原文 | Ela 各 347/347，GCC 各 208/208，ASan UI/Loader 194/194；[ADR-0022](adr/0022-configuration-background-io-watcher.md) | 新增体验人工复核；任意插件 connection/worker 屏障与联合退出、P4 模块 | 本轮未提交 |
| 2026-10-25 | 审查 Q4c2c2/Q4e1/Q4e2/Q4d3；修复编辑重入、监听初始化/回退/重连、退出隐式等待五项问题，整理格式、依赖和表格更新 | Ela 各 350/350，GCC 各 208/208，ASan UI/Loader 197/197；[审查记录](code-review.md#p3-配置子树会话与后台读写提交前审核) | Q4d3 原生体验、任意插件资源屏障及 P4 模块继续待验 | 64c0a4f |
| 2026-10-26 | Q2c：托管零参数 Qt 信号连接、Context/epoch 归属、异步业务/原生快照/捕获释放和独立关闭屏障；补齐联合退出示例 | Ela 各 365/365，GCC 各 208/208，ASan 212/212；[开发记录](p3-development.md#q2c-托管-qt-信号连接) | 类型化信号参数、通用插件 worker；Q4d3 人工复核及 P4 | 随本次审查归档 |
| 2026-10-26 | Q2c 提交前审查：修复断连通知重入提前释放捕获、已取消通知误报故障和 Home 测试旧按钮失效，整理快照/头依赖/格式 | Ela 各 367/367，GCC 各 208/208，ASan 214/214；[审查记录](code-review.md#p3-q2c-托管-qt-连接提交前审核) | Q4d3 原生体验、worker/类型化参数和 P4 继续待验 | 随本次提交归档 |
| 2026-10-26 | Q2d：一次插件工作线程、代次/本地协作取消、原生 TLS 退出证明、捕获/宿主释放和联合退出示例 | Ela 各 385/385，GCC 各 208/208，ASan 232/232；worker 检查 Debug/ASan 各连续 20 次通过 | 其他线程后端、类型化数据、多任务；Q4d3 人工复核及 P4 | 随本次审查归档 |
| 2026-10-26 | 审查 Q2d：修复自动退役阻断 ready/result 延续及过早保留暂态门禁错误，补充 Core 退役确认和投递失败终结 | Ela 各 389/389，GCC 各 208/208，ASan 236/236；[审查记录](code-review.md#p3-q2d-托管-worker-提交前审核) | Q4d3 原生体验、其他线程后端/类型化结果及 P4 继续待验 | 随本次提交归档 |

本轮补充工作记录：2026-10-26 / 按用户选择先收口 P3，恢复本机环境、完成 Q2e 类型化 worker 及 Q4d3 原生代理复核 / Ela 各 401/401、GCC 各 209/209、ASan 专项 34/34、新场景 Debug/ASan 各重复 20 次、原生退出码 0 / 用户人工确认、类型化信号与其他资源适配、P4/P5 / 未提交。

本轮补充工作记录：2026-10-27 / Q2f 受限类型化信号、独立字符串冻结、复制/排队/在途统一预算及数据退出屏障 / Ela 各 417/417、GCC 各 209/209、ASan 专项 67/67、新 16 项检查 Debug/ASan 各重复 20 次 / P3 完整验收与用户确认、后续原生模块/ABI / 未提交。

本轮补充工作记录：2026-10-27 / 按用户要求审核全部未提交增量并提交，修复释放批次提前归还预算、更新启动说明 / Ela 各 418/418、GCC 各 209/209、ASan 专项 68/68，预算修复前实际失败、修复后通过 / P3 完整验收及用户确认、后续模块/ABI / 随本次提交归档。

本轮补充工作记录：2026-10-27 / Q2g 显式 Qt 信号值前缀、真实 watcher 联合退出、配置重排/改 id 身份与 A/B/C 最终值断言、P3 验收映射 / Ela 各 428/428、GCC 各 210/210、ASan 专项 109/109，11 项 Debug/ASan 各重复 20 次，双编译器契约检查通过 / Q4d3 用户确认、真实 DPI 与后续模块/ABI / 未提交。

本轮补充工作记录：2026-10-27 / 跨组件 P3 装配验收，代码审核后提交 / Ela 各 432/432、新四项 Debug/ASan 各连续 20 次通过 / Q4d3 用户确认、Home 手动路径及真实 DPI；P4/P5 未开始 / 随本阶段提交归档。

本轮收口记录：2026-10-27 / 用户确认 Q4d3，原生补验 Home 手动路径与 150% 明暗、两次正常退出 0，逐项核对 P3 完成条件 / Ela 各 432/432、GCC 各 210/210，阶段审核/提交及测试引用核对通过 / P3 已完成，P4/P5 未开始 / 收口记录随本阶段审核提交归档。

本轮专项建立记录：2026-10-27 / 新增 UI 设计与优化专项，建立 UI-00–07 工作包、视觉与交互基线、独立偏好边界及 UI-A01–20 验收矩阵 / 当时仅补充文档，状态呈现/偏好和人工确认尚待完成，后续实现见下条 / 未提交。

本轮 UI-S0 记录：2026-10-27 / 完成 Ela 组件与字体、类型化状态、配置独立反馈、偏好保存与失败恢复、键盘/焦点/布局及审查 / 最终 Ela Debug/Release 各 444/444、ASan UI 104/104；用户确认新版体验及系统 150%，源快照/依赖/文档审计通过，见 [验证记录](ui-optimization-verification.md) / 基线已完成，尚未提交。

本轮阶段调整记录：2026-10-27 / 用户要求分析 VS Code 每个 UI 结构并分专项阶段；建立 UI-S0–04 阶段与逐结构对照初版 / 官方 UI/UX 文档与现有代码已核对，阶段映射/文档检查通过 / 当前 UI-S1 进行中，后续 UI-S2–04 增量待实施和验证；P4/P5 未开始 / 未提交。

## 更新约定

1. 开始工作包时改为进行中，注明具体子项；实现完成但没验收时为待验证。
2. 每次完成可评审工作、发现阻塞、改变设计或结束开发工作时更新本表；纯询问状态不虚增开发成果。
3. 记录改动、相关文件/测试、实测结果、未覆盖范围和下一动作；有 Git 提交后才填写实际 hash，不编造提交。
4. 功能通过对应验收才标已完成；失败和跳过都保留。接口变更同步契约，架构决策同步 ADR。
5. 新聊天继续项目时先阅读本文件及进行中的工作包，不依赖聊天记忆推断完成情况。
6. 本文件人工随开发更新，不是后台定时监控；当前未创建自动化任务。

后续工作记录采用同一行格式：日期 / 工作包与实际变化 / 验证依据 / 剩余项 / 已存在的提交。详细故障可另写报告并链接，避免把全部终端输出复制进台账。
