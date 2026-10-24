# 开发进度

最后更新：2026-10-25（北京时间）。当前工作区：`D:/01_C++/Eve`。

本文件是项目进度的统一入口，记录实际状态；设计细节见说明书，任务完成条件见工作单，测试依据见验收规范和验证报告。代码行数、文档篇幅和测试数量不作为项目完成百分比。

## 当前状态

**W0–W7 首阶段工作单已完成。P3 Q3c 工作台、Q4a/Q4b 配置校验及 Q4c1 协调协议已提交。Q4d1 文件事务及 Q4d2 Store 已通过提交前审查：Ela/MSVC Debug/Release 各 243/243，无 Qt GCC 各 158/158，56 项 Loader ASan 检查通过。Q3c 原生桌面人工验收待办；Q4c2 Kernel 运行树按此前置条件接入，后续推进 Q4d3 watcher。**

本轮补齐 Ela 菜单、搜索、前进后退和 Home 插件卡片中心，资源树归插件内部。左侧单击直达页面，所有页签可关闭，零页签为空工作区，底部面板默认收起；多行卡片高度已修复。提交前审查另修复刷新关闭重入、失效页签回落、查询重入和浮层区域生命周期四项问题。原目录 Ela/MSVC Debug/Release 各 259/259，无 Qt GCC 各 158/158，UI ASan 61/61 通过；人工验收仍待办，见 [审核记录](code-review.md#p3-ela-工作台与-home-提交前审核)。

Qt 5.15.2 Core/Widgets/Test 已就绪，路径与当前 MSVC 组合见 [P3 记录](p3-development.md)。QtScheduler、Timer/QObject、贡献服务及基础管理页已通过真实循环/offscreen 测试，配置文本/schema、文件事务和 Store 已有独立验证；后台 I/O、connection/worker/watcher、真实配置执行/管理页保存/模块管理、完整子树、原生桌面交互及跨 ABI 仍待验收。

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
| P3 Qt / Loader / Ela | 进行中 | Q0–Q3c 自动化基线、Q4a/Q4b 校验/预览、Q4c1 CAS/协调、Q4d1 文件事务和 Q4d2 Store 已实现；原生桌面人工验收、Kernel 驱动/真实运行、后台 I/O/watcher/管理页保存及其他资源适配待完成 |
| P4 原生插件模块 | 未开始 | ABI、ModuleManager 和驻留账目待实现 |
| P5 受控替换 | 未开始 | 新旧代替换、恢复和卸载专项验证待实现 |

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
| 完整 UI、原生桌面人工交互、ABI、物理卸载和替换 | — | 待实现或验证 | [设计说明书](../Cordis插件平台-软件设计说明书.md) |

“最近验证”是当时已检查的版本；后续代码变化不自动继承通过状态。新验证需注明覆盖范围、工具链和失败/跳过项。临时构建日志位于 build，持久结论保存在本台账及对应报告。

## 阻塞与风险

| 编号 | 条件 | 影响 | 解除条件 | 当前处理 |
|---|---|---|---|---|
| B01 | 原生桌面人工交互尚未验证；缺 Qt kit 的旧条件已解除 | P3 桌面验收及 Q4 Kernel 运行树/管理页接入；不阻止独立配置、协调、文件及 Store 前置工作 | 完成原生 Windows 交互专项验收 | Qt/Ela 自动化基线就绪；人工项未代填，独立前置组件按各自验证记录推进 |
| B02 | 原 v142 组合未安装或验证 | 若坚持精确 v142 或进入插件 ABI，需另验 | 补齐原工具集或冻结经专项验证的选定组合 | 当前 Qt5 msvc2019_64 + MSVC19.44/v143 已通过 Debug/Release 探测；不构成 DLL ABI 验收 |

首阶段无 Qt 内核工作已收口，Qt/Ela 自动化、内置配置校验、通用协调、文件事务及 Store 已有证据。D8050 的 TEMP/TMP 与中文 showIncludes 覆盖沿用既有记录。按 [Q3c 桌面验收单](../tests/acceptance/p3-ui.md) 验收原生交互，再接入 Q4c2 Kernel 驱动和真实运行协调；后台 I/O/watcher、管理页保存、外部模块产物/ABI 另验。当前受控驱动测试不作为真实插件执行证明；Ela 全局对象仍按进程生命周期驻留。

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


## 更新约定

1. 开始工作包时改为进行中，注明具体子项；实现完成但没验收时为待验证。
2. 每次完成可评审工作、发现阻塞、改变设计或结束开发工作时更新本表；纯询问状态不虚增开发成果。
3. 记录改动、相关文件/测试、实测结果、未覆盖范围和下一动作；有 Git 提交后才填写实际 hash，不编造提交。
4. 功能通过对应验收才标已完成；失败和跳过都保留。接口变更同步契约，架构决策同步 ADR。
5. 新聊天继续项目时先阅读本文件及进行中的工作包，不依赖聊天记忆推断完成情况。
6. 本文件人工随开发更新，不是后台定时监控；当前未创建自动化任务。

后续工作记录采用同一行格式：日期 / 工作包与实际变化 / 验证依据 / 剩余项 / 已存在的提交。详细故障可另写报告并链接，避免把全部终端输出复制进台账。
