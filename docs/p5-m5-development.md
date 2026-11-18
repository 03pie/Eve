# P5-M5 业务升级、工作台接入与最终验收

日期：2026-11-18。M4-c cff1bac、M5-0 4ca3b48、M5-a1 d936580、M5-a2 b1f7d4c 已审计提交同步。M5-b1 a263c0d 已审计提交同步；M5-b2 1c9a1b4 已提交同步；b3 3e74d52 已提交同步；当前 b4-1 三模块装配/关闭完成审计与四配置专项，随收口归档；后续按 [具体分段](p5-m5b-development.md)推进实际业务版本与整体升级。M5 尚未完成。原说明书与开发指南仅作只读依据。

## 完成条件与分段

对照[说明书 §12、§14、§18–19](../Cordis插件平台-软件设计说明书.md#12-受控替换与恢复)和[开发指南 H01/H02](../Cordis-Cpp17-Qt5152-开发指南.md)，M5 交付可运行的实际业务升级、工作台操作与故障报告，以及同进程连续替换和随机压力。M1–M4 的真实夹具、完整事务、状态迁移和物理卸载是实现基础，不替代实际业务/原生桌面验收。

| 小阶段 | 职责与必须兑现的出口 | 状态 |
|---|---|---|
| M5-0 | 核对实际贡献、宿主与业务产物；记录依赖、拥有者、提交/恢复、名单和压力门禁；审查并提交同步 | 已完成，4ca3b48 已提交同步 |
| M5-a1 | 私有 Context 发布桥与完整终态通知、页面/命令完整准备与隐藏呈现；原 Ela/工作台所有入口受同一发布状态控制；回归真实销毁与投递失败 | 已完成，d936580 已审计提交同步 |
| M5-a2 | 输出/通知 pending 值的有界保存、提交接纳、失败丢弃及纯历史锁存；整 UI 贡献发布/恢复组合审计 | 已完成，b1f7d4c 已审计提交同步 |
| M5-b | 实际 counter/reporter 与 notebook 旧/新产物；明确运行状态契约；复用现有具体宿主装配、安装/stage/ReplacementOperation 与配置共同提交 | b1 a263c0d 已提交同步；b2 1c9a1b4 已提交同步；b3 3e74d52 已提交同步；b4-1 单一装配/关闭完成审计验证，b4-2 事务待实施；见 [工作单](p5-m5b-development.md) |
| M5-c | 管理/配置升级入口、预检/影响路径、双击/冲突门禁、完整进度/报告、高级回收诊断；实际 H01/S09 业务与页面故障 | 待实施 |
| M5-d | 同一 Windows 进程 100 次成功真实 Native 替换；逐轮业务、引用、句柄/对象基线；有 seed 的 mount/update/withdraw/stop 与升级/故障压力 | 待实施 |
| M5-e | 对应产物精确名单资格、最终支持矩阵、故障报告、需求映射、原生桌面升级与退出验收；整 P5 审查提交同步 | 待实施 |

每个小阶段先检查当前职责、状态、依赖和拥有者，再写具体实施记录；完成实现、代码审查、修正与适量验证后提交同步，工作区干净才开始下一阶段。若某段过大，按有实际调用者的边界细分，不以半成品或新增代码量替代出口。被替代、重复和过时实现须删除，兼容路径保留理由须明确。

## 已核对的实际缺口

UiPages/CommandService 已有正确 effect/epoch、退出和对象屏障，但直接登记可见导航/命令；Output/Notification 直接改变可见历史。开放候选组中的 Context 合法，现 eventGuard 不能区分“允许初始化”和“向用户公开”。先按 [ADR-0051](adr/0051-ui-contribution-publication.md)接同一 PublicationControl，不先在旧管理入口堆积升级分支。原管理/配置保护、页面撤销、命令在途调用和窗口访问栈继续使用。

真实 native_services 的 counter/reporter 仍仅 resident 发现/声明、没有状态扩展和新版本；Demo 的 manager 值成员、unique Session、局部 Runtime/Definitions 不适合逐代显式回收。notebook 没有 Qt 可选状态扩展，编辑草稿仅存在 QWidget 中；配置 text 不是用户当前草稿。当前没有实际 Windows 句柄/GDI/USER 基线采集。以上均需实现与实测，不能把已有夹具能力写成产品能力。

## 职责、数据与拥有者

Core 保持无 Module/Loader/UI 依赖。Module 原目录/来源/ledger 负责映像资格；ReplacementOperation 原 Execution 负责闭包、快照、恢复与唯一共同提交；Loader 原 Driver/Session/Store 负责定义、目标版本与显式保存。UI 只投影值和发送宿主操作，不绕过 CAS、预约或代次门禁，不把 ModuleManager/配置能力注册给普通插件。

宿主装配负责候选安装/预检、打开真实工厂、组成 ReplacementRequest、保留正在执行的操作和有界纯报告。每模块当前/候选槽明确拥有 Generation、NativeModule/Qt 工厂、Runtime 与配置定义；一项 Request 只替换一个 moduleId 的一对代次，不能宣称两个独立模块原子升级。manager/Session 使用真实 shared ownership，禁止 no-op deleter 假共享。

先沿现有 native_services 具体宿主拆出薄 main 和可由测试调用的装配/关闭对象，保持原五个示例场景；不另复制升级宿主、解析器或事务算法。GUI 复用该实际装配能力时才抽取必要组件，不能为将来使用预造通用升级管理器。最终同进程场景保持单一 scheduler、Kernel、Driver、Session；不是每轮创建一套平台来掩盖残留。

正常提交后先替换宿主持有的定义/工厂槽，明确关闭旧 Native 会话，释放旧 Runtime/目录/请求/探针及安装凭据；跨 owner 队列尾部屏障后，查真实来源/账目/预约/readers，再由首次 owner 显式 tryUnload。纯 report/Task/旧 Disposed Instance 可保留，报告不持安装文件、工厂或可执行闭包。恢复失败保留原 Execution/作用域/预约；协议/清理不确定保持真实对象和 pin。物理失败独立显示，不回滚 Applied、角色或配置。

## 实际业务与状态契约

- counter 是需保留的运行状态：提供真实版本化纯值状态扩展，严格处理 uint64，不能经 JSON double 丢精度。兼容 A/B 版本可用相同 schema 验证代码换代；形状/版本转换另用有限真实场景验证。
- reporter 的缓存是派生值：重建后通过原 C ABI 真实异步服务调用读取 counter，不要求为了覆盖而实现无必要的状态扩展。清理也可能调用 counter.next；验收围绕冻结时捕获值、导入值和实际 start 调用定义期望，不把清理期递增误报为迁移丢失。
- notebook 升级保留当前编辑草稿，使用真实 Qt 状态 IID/值契约，导入先于激活建立编辑器；不能用原配置 text、旧 widget 未删除或一次磁盘保存冒充草稿迁移。页面和命令仍须完整销毁重建，旧句柄不可复活。

不要求所有插件实现状态扩展；无状态业务保持兼容，声明与需求不符时按 M3 原准入拒绝。实际版本、hash、发现响应和 manifest 必须一致。WriteModuleManifest 只增加当前调用者需要的参数，默认 1.0/resident 保持；真实显式能力严格区分发现 minor0/Resident 与 minor1/Explicit，不改变业务 ABI。

Native counter→reporter 已是真实提供者/消费者链。100 次 Native 替换时，独立活跃 notebook 页面保持可操作；不为人为混接 UI→C ABI 服务引入尚无业务要求的公共 RPC 桥或借用私有 NativeServiceEndpoint。Qt 代码升级另做有限次数的真实 stage/ReplacementOperation、草稿和页面/命令恢复。该范围不宣称跨后端业务服务消费桥已经提供。

## 管理、故障与退出

升级入口属于宿主，候选选择和影响预览不执行破坏性业务；预检失败保留当前运行。提交时重新检查实际目标修订、产物与预约，编辑可继续，冲突应用/双击不得产生第二个事务。展示真实 OperationId、阶段、当前/候选身份、受影响实例/路径、Applied/Restored/Cancelled/Failed、RecoveryFailed、保留资源与单独 unloadFailure；不用一个“失败”标签混同完整恢复和未确认清理。

H01 必须用实际业务候选缺符号、激活失败证明预检不破坏旧链或停止后完整恢复；S09 必须在提供者已准备但真实消费者失败时，整体和 UI 同为非成功。候选 UI 不得提前可见，失败贡献须退出，只有完整恢复的旧贡献可重新公开。关闭期间停止接纳新操作，沿原操作取消/恢复、Session 保存、Kernel、UI 完整析构和模块退役顺序退出，最后关闭调度器。

物理尝试只放高级诊断，复用 tryUnload 正常拒绝和准确原因，不成为普通启停开关，不清 pin，不解除别人的资源。M5 原生人工验收必须使用完成后的具体 Release 窗口；此前 P3/UI 专项通过不替代新增升级交互。

## H02、随机压力与名单门禁

成功压力在同一未重启 Windows 进程内，复用同一平台实例，以兼容 A/B 实际版本交替受控替换 counter/reporter（包含回切，不宣称版本单调递增），至少完成 100 次成功 Applied 的完整 ReplacementOperation（每次一个模块）。counter 轮真实重建 reporter 消费者，reporter 轮借用活跃 counter；每轮实际版本/hash/代次/实例/绑定、状态、配置共同提交及旧映像回收都要兑现。两类轮次的清理/start 递增不同，期望值由实际调用次序计算，不能统一假定每轮加一。重复预检、候选映射、失败恢复、短目录装卸和配置重建不能计入 100。

Qt 固定 resident，Retired lineage 不复用且重代次仍受 64 限额。有限 Qt 代码升级单独解释 root/PIN/映像增长；不得提高上限、重新启动进程、自动卸载 Qt 或把 100 个独立 Qt DLL 当作可循环回收。Native 循环保持活跃 Qt 页，不累计 100 套窗口、Scheduler receiver 或宿主工厂。

先预热页面/命令/服务，记录相同稳定业务状态的分类 ledger、runtime/instanceOrigins、预约/readers、Call/Request/Capture 与任务队列、实际页面/命令/QObject 数，以及 GetProcessHandleCount、GetGuiResources 的 USER/GDI 值和模块驻留清单。每轮业务基线可包含解释清楚的活跃非零资源；停止后再核对清理基线。固定纯历史与已知驻留须分类说明，拒绝无法解释的增长，不能只比较总内存。

句柄采集按 [GetProcessHandleCount 官方契约](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount)处理成功/错误；GUI 对象分别采 [GetGuiResources 的 USER/GDI 计数](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguiresources)，区分零对象与调用失败，不把峰值当当前基线。另记录实际重算次数、重载停机、队列延迟和清理耗时，与相同业务状态的基线比较并解释异常，不预设未经测量的 SLA。

物理证据使用实际已批准产物的完整原映像范围离开、文件可独占操作、再次发现/业务和新代次；未设置原子 detach 见证的真实业务 DLL 不冒称直接计数了 DllMain。若添加见证，测试与批准必须是同一 hash，不能带探针测完换掉产物。

随机压力保存 seed、动作、OperationId、故障位置与完整失败轨迹，组合实际 mount/update/withdraw/stop、配置/升级和关闭；通过显式 gate 驱动交错。失败时缩减为最小可重放序列并复现，不能只留 seed 或最长日志。永久 pin/未确认资源故障分进程记录，不污染正常 100 次基线；不得清 pin 后继续计数。随机一次通过不能代替确定性 H01/S09 和真实 100 次成功链。

资格测试宿主仅对明确构建的 counter A/B、reporter A/B 的实际 moduleId/预期 SHA256 传临时精确批准，不从任意所选 manifest 自动批准，不修改应用默认名单。H01/H02、资源审计及最终门禁通过后，才归档对应工具链/配置/摘要的业务名单并接入产品；产物变化需重新批准。Qt 始终不进入物理名单，不增加通用启用开关。

缺符号等故障产物在独立资格进程中按 resident 或单独精确批准测试，不自动混入正常四产物/生产名单。H01 须真正到达符号/业务失败路径，不能把显式产物仅因未获批准提前拒绝当成缺符号恢复证据。

## M5-0 验证范围

M5-0 仅文档，不修改生产代码、公开 SDK 0.1.5、原 C ABI、Ela、测试或业务名单。不重复运行未变的代码矩阵；M4-c 当前基线为 Release/Debug/ASan 各 1344/1344、静态 Core 关闭示例/Ela 970/970、无 Qt GCC 440/440。设计须经独立要求/源码边界审查、本地引用和差异检查后提交同步，再开始 M5-a1。后续任何“通过”须对应实际产物、当前输入和完整选择范围，不能把设计当能力。

## M5-0 审计结论

三路只读审查覆盖原要求、实际 UI/发布通知及业务宿主所有权。修正设计中的 Aborted 无 Binding 通知缺口，以及提交后 owner 提前退出时 pending 历史的终态判断；补齐可见模型索引、最小可重放失败序列和性能基线，明确 A/B 包含回切、故障授权不能冒充缺符号验收。计数/草稿保留为本示例明确契约，不强制所有插件迁移状态，不新增跨 ABI RPC 要求。

独立复查未发现剩余文档提交阻断项。5 份本段文档、414 个本地引用路径、当前仅文档差异及空白检查通过；来源/输入记录保存在 build/p5-m50-docs-verified.json。M5-0 随本次提交归档并同步后，才进入 M5-a1 具体设计和实现；UI 发布、真实业务版本和最终压力/名单/桌面验收仍未交付。

## M5-a1 实施设计

M5-0 4ca3b48 已审计提交同步且工作区干净后开始。再次核对实际 Context、原模型索引、命令过滤及 Home 来源，按以下边界替换原直接呈现路径，不增加另一发布状态表。

Core 在 Context 私有区新增 nested Presentation（纯 visible 查询和 shared ScheduledJob observation），由私有 presentation(scheduler, changed) 登记。登记检查真实业务/epoch/同 scheduler，普通上下文无需观察。查询只读登记线程、弱实例/epoch/退役值与原发布组状态/标志，不调用 business、isOpen、scope.snapshot、插件或 Qt；原 eventGuard 仍负责实际业务入口。UI 原记录强持观察令牌，job 只弱持记录，退出时释放。暂不为 M5-a2 增加无调用者的终态字段。

PublicationGroup::abort 固定 Aborted 后完整请求各成员停止，再通过原 notifyRegistry 发终态通知；失败使用原 deliveryBlocked 和 SchedulerClosed，幂等调用不清故障，不为通知失败提前跳过停止。原 dependencyGraph 的 publicationHeld 扩义为组尚非 Published、仍 notifying 或 deliveryBlocked，阻止 Aborted 残留/发布未完成时再次开闭包；同步注释和错误文案。Core-only 回归仅用真实公开 Kernel/PublicationGroup/graph，不定义假 UI friend 或测试访问 API，私有桥由真实 UI 验证。

Ela 的 addExpanderNode/addPageNode 新增携带纯 presentation predicate 的重载（空表示原行为），沿 Window→Bar→Model 在节点插入前固定；原 node/children/nodesMap/stack 仍唯一拥有资源。可见行缓存只保存原节点指针，在合法模型通知窗口重算；getRow/index/parent 同步使用该投影。提供 refreshNavigationPresentation、稳定 key 的 getNavigationNodeIndex 和 live isNodePresentable 转发。主树、紧凑菜单、原生建议、首选、route、直接切页与新窗口都重查同源 gate。Held 首项/移除当前页先切安全空白，不能让 QStackedWidget 自动选 Held。工作台原本使用同步 None；涉及受控源/目标的 Ela 切页保持同步，普通无 predicate 动画保留，但旧延迟动画不得晚到覆盖受控目标或已删除页，不扩大为无关动画重写。

编码前接口复查补充兼容取舍：公共 Window/NavigationBar 保留原六个方法签名及默认参数，作为转发到空 predicate 的薄入口；新重载的最后 predicate 必填，避免默认参数造成歧义。不能直接用新增默认参数替换旧签名，否则会移除共享 Ela 的原导出。兼容入口仍有现有普通调用者且只转发一次，节点登记/投影算法保持唯一；不为此改变 SDK 或复制旧算法。

原 CentralStackedWidget 的取消修订/稳定目标状态放在私有 QObject 子对象，不改变公开类字段布局。首次重建前另保存实际旧 SDK 0.1.5 notebook DLL（SHA256 254f71b5ec8fdfd278a997ef65e48b3ee8a360e736a31628f6bd746c01a9305f）及原声明/嵌入元数据到 build/p5-m5a1-sdk015-artifact，后续用当前宿主验证真实旧产物运行；不以新版改字符串代替，也不覆盖该实物。

PageControl 在真实 effect setup 前取得发布桥；工厂、Dialog/节点/堆栈、原工作台 Entry 和清理完整准备后 ready 兑现。新增纯 gate 同时叠加 shell/open、贡献撤销和投递诊断；navigate、输入、原生导航及选择再次检查。删除 PageControl/Group 的最后一行/长期持久索引假设，按稳定 key 重取所需索引。原 PageSnapshot 保留真实 widget 与资源诊断，navigationPresent/current 只反映对外投影，不改变公开布局。

CommandControl 在原 rows/records 内保存 gate/观察令牌；行在 append 前准备，现有 QSortFilterProxyModel 扩为纯 gate 过滤，不把 std::function 放进 QVariant 或注册元类型。enabled/execute 与宿主 activate 在谓词、日志或标题呈现前拒绝 Held；原在途调用/捕获/清理屏障不变。Workbench 原 Entry 接默认空 predicate，register 完整登记一次，refresh 只更新搜索/Home/历史/页签投影。ElaSuggestBox 补通用 suggestion filter，查询及 click/Enter 都复查稳定 key，避免缓存建议越权。

Home 的管理快照来源也包含 mountHeld 根，不能仅过滤贡献 Entry；宿主私有 helper 每次刷新从原 dependencyGraph 取得未发布实例值，正常卡片筛除，诊断仍保留真实候选记录。不新增公开 snapshot 字段或并行实例表；已发布/无组普通条目不变。页面/命令的普通注册和关闭路径保留，替换原直接公开与旧索引假设；输出/通知仍按 M5-a2 后续处理，不宣称 a1 已完整隔离所有 UI 副作用。

先验证真实 Qt/Ela 上 holdPublication/mountHeld 的 ready/隐藏、同组多页/命令、搜索/紧凑菜单/路由/首选/页签/Home、缓存建议、commit/abort/取消及关闭重入；Core 验证无输出终态与 graph/拒投。补原 ReplacementScope 的准备/提交/恢复组合，检查真实工厂、QObject 和命令捕获完整退出；真实双版本业务 DLL 的最终组合仍由 M5-b/c 交付。现计划保持 SDK 0.1.5、原公开布局/ABI 和 Qt resident；若实现发现需改公开契约，先记录取舍再调整版本。Core/准入与 Ela/Ui 变化需当前 Release/Debug/ASan、静态 Core 关闭 Ela/示例、无 Qt GCC 全量，以及冻结输入、实际插桩对象、公开头/协议和格式检查后审计提交同步。

首轮交叉审阅已纠正测试装配把裸 Runtime 交给 Kernel.mount 的前置错误，普通测试现先真实登记，Scope 候选仍由 prepareDefinitions 拥有。首轮 GCC 编译发现误向不可赋值 Result<void> 重新赋值，已改为独立末次检查；MSVC 实际构建发现兼容薄转发的空花括号可匹配另一 int/enum 重载，已明确 std::function<bool()>{}。此前独立命令环境缺失未启动 MSVC 编译，与生产构建失败分开记录。修正后 GCC、MSVC 发布组专项各 17/17（含新增四项）通过；完整 UI 构建及矩阵仍未完成。原日志保存在 build/p5-m5a1-*，不把专项当全量，也不提前提交。

后续实际旧 UI 联合 167/168，原 search_normalization 暴露新版本检查过早退出：模型 reset 内重入的 Beta 查询被模型拒绝，外层不能留下 Beta 文本/Alpha 结果。已保留原断言、恢复 reset 后真实文本收敛，并保留选择内部 setText 重入时拒绝旧 Alpha 的新检查。独立审阅另发现取消普通 Scale/Flip/Popup 后删除无关页会让合法当前页持续隐藏，现只在窗口层复查合法目标后恢复显示；被删除/不可呈现目标仍转安全页，新增三模式实际状态回归，不放开 Held。

最终补齐 Held Dialog 的独立完整删除、真实 Qt 已接受观察作业被丢弃，以及 PageControl→实际 CordisPageSearch/Home/页签的门禁接线。最后一项只增强现 pages_held 的断言：候选搜索结果为零且无兜底卡片/页签，发布后原两条建议出现、Return 打开现有页且工厂仍只调用两次。当前 20 个 UI 用例加原查询规范化共 21/21，完整 Core 专项 17/17；两组不重复累加为完整矩阵。

39 项源码/构建输入已冻结。Release 1368/1368 完整基线后，最终审计只增强 tests/qt/page_presentation.cpp，重新构建并复验 UI/查询 21/21；其余 182 个实际运行产物 SHA256 和全部生产输入不变。最终 Release 覆盖由该全量和更强受影响复验共同归档，并非最后断言后又跑了所有未受影响测试，重叠名称不累加。静态 Core 关闭 Ela/示例全量 974/974、无 Qt GCC 全量 444/444 已通过，两者不编译该 UI 用例；Debug/ASan 已完整构建后更新最后测试 TU，随后各全量 1368/1368 通过。输入、原失败、基线/补强日志与产物比对保存在 build/p5-m5a1-*；38 项格式、20 组独立公开头、5 份原协议不变、实际旧 SDK 0.1.5 notebook normal 运行通过，29 个实际 ASan 对象的插桩、Module 异常模型及运行前后哈希一致。

## M5-a1 收口结论

本段将完整准备与对外呈现分开，复用原 PublicationControl、服务观察、贡献记录及资源屏障；Core 保持无 Qt/UI/Module 反向依赖，没有第二发布状态表或事务。移除了页面最后一行/持久索引假设，合并 Ela 重复节点登记算法；现有 Ela 导出薄转发因普通调用者和二进制兼容保留，公开 CentralStack 布局与 SDK 0.1.5 不变。旧 SDK 实物兼容结论仅限已保存的 notebook DLL 在当前宿主 normal 场景，不扩为任意 C++ 二进制兼容保证。

独立结构、拥有者、重入/失败、兼容和最终证据复查未发现剩余提交阻断项。实际 Qt 用例覆盖共同提交前后门禁、完整 ready、搜索/卡片/页签、原生路由/紧凑导航、同组多页、Dialog 完整销毁、关闭重入、缓存建议、真实观察作业丢弃和 ReplacementScope 提交/恢复；Core 覆盖无输出终态、拒投与发布期间闭包门禁。五配置清单与唯一 Passed 名称、冻结输入及实际产物逐项匹配，Release 的组合验证范围按上段准确保留。

验收映射见 [M5-a1 清单](../tests/acceptance/p5-replacement.md#m5-a1-页面与命令发布边界)。本段审计提交并同步远端、工作区干净后才进入 M5-a2。输出/通知 pending 历史、真实双版本业务 DLL、完整 H01/S09、同进程 100 次与随机压力、业务白名单资格和新增原生桌面升级验收仍未交付，不用本段测试替代。

## M5-a2 实施设计

d936580 已审计提交同步、工作区干净后，重新检查原输出/通知接纳、两个序号域、窗口访问栈与 Shell 关闭顺序。当前输出把校验、接纳和 Qt 呈现绑在一起，通知又混在命令控制块且直接写状态栏；先拆职责，再接候选值，保留平台日志和已公开命令结果的即时宿主入口。不能仅按 owner 值推测候选身份。

私有 Presentation 增加 publication 纯查询，返回 Result<optional<bool>>：Open/正在 notifying 为未决定，安全 Published 为 true，Aborted 为 false，Published 且 deliveryBlocked 为 SchedulerClosed；查询校验登记线程但不读 owner 生存期。它只暂时强持原只读 PublicationControl：原 registry/replacement 和 members 的 Core 回调均只持弱引用，不会保活 Instance/Runtime/DLL。页面 visible 的弱 owner/epoch 门禁保持独立；不新建回执目录、发布状态或 observer 列表。原组尚 Open 时，单独 stop 不是 group.abort；值可在有界 pending 中等原组终态或宿主关闭，不伪造成功或放弃决定。

两类值沿各自原控制块保存 pending，共用有两个实际调用者的宿主私有 PendingValues<Value>：每 owner/epoch 复用一份来源与原 watch，条目只保存复制值/目标及字节账目，watch 仅弱持后端，discarded 只写来源原子故障。它只负责预算和取出已有终态的值，不拥有可见历史、不决定发布、不执行 Qt 或插件。Output pending 的单外壳总预算为 1000 条、1 MiB 字符串及 64 个精确目标；Notification 为 100 条、256 KiB 文本及 100 个归属目标，独立于已发布历史。固定记录开销由条数限定，来源数不超过条数；超限拒绝，不淘汰已有 pending 或可见历史，不建 channel、不改选择/状态栏、不镜像日志。

登记路径复查发现，满额的新 owner 先登记原 watch 再被预算拒绝，会留下已过期弱槽；连续拒绝而没有业务通知时，原 notifyRegistry 的清理不会运行。因此原 ServiceRegistry::watch 登记时也清理已过期弱槽，与通知时同属原索引资源管理，不增加目录或预算回调，也不令候选满额阻断普通已发布日志。

write/post 校验同调度器、业务/epoch、级别、UTF-8 和原严格长度后，在成功接纳时保留本服务的单调序号；普通与 Held 使用同一个序号域。拒绝不占号，Aborted 可留下空洞，清空不复用号。Published 值按原保留号有序插入可见历史，再按原条数/目标规则保留；不能晚到重新编号，也不能以旧通知覆盖较新的状态栏。通知镜像逐条取本次 incoming 纯值，只在通知接纳后沿原输出内核写一次；其输出序号属于独立输出域，在镜像接纳时分配。

输出拆为一份纯值准备/校验、一份纯值接纳/淘汰和原 Qt 呈现。保留插件过长拒绝、平台过长按 UTF-8 截断的现有差别，不复制两套算法。纯值接纳记录 dirty，批次全部入历史/镜像后才进原 WindowAccess；重入前移出 pending、释放来源/watch，不跨 Qt 信号保留容器引用。原 watch 主动结算并刷新；写入、快照、复制和清空在 owner 上先纯值结算已有终态，快照/复制不触 Qt 或插件，避免 commit 后新写/clear 被迟到旧值复活。排队通知仍会刷新 dirty 投影，不把只读查询改成执行插件的轮询。

清空只覆盖当时已安全提交到该输出目标的值，尚 Open 的候选与尚未产生的通知镜像属于未来接纳，不由清空隐式取消原组。通知镜像的接纳时刻固定为通知公开时；clear 后新公开的通知仍可产生新日志，不能宣称清除了尚不存在的镜像。

通知存储、校验、pending、历史及状态栏呈现移到 notifications.cpp 的 NotificationControl，由原 ServicesControl 实际拥有；公开 NotificationService 的 weak ServicesControl、对象布局及友元不改，方法保留薄委派。命令调用/模型/原 effect 屏障仍归 services.cpp，删除原混合的通知 history/append 算法。新通知呈现也必须走原 WindowAccess，不能直接 setText 绕过关闭访问栈。

Shell 同步关呈现门后、closeOutput 之前，先纯值结算两个后端已安全 Published 的 pending（含通知镜像），随后立即关命令/通知入口、丢弃 Open/Aborted/故障 pending、释放来源/watch，再关闭输出/工作台/管理等控件。不能把服务关门放在 closeWorkbench 之后：禁用 QAction/模型的同步信号仍可能重入 post/add。纯值结算不创建或刷新控件；已接纳历史关闭后仍能读。已经接受的观察作业被实际 Qt 丢弃，或原发布通知阻断时，不再公开对应 pending；服务仍 open 时 snapshot/后续插件提交用既有 Result 返回 SchedulerClosed，关闭则只保留已接纳纯历史，不增加公开快照字段或清除原组故障。

首轮编译通过。独立交叉审阅进一步追到上述 QAction 关闭重入窗口，新增真实 changed 回调回归在修复前 0/1；现按统一入口关门时序前移 closeUiServices，保留起始栈和原关闭回执屏障，再统一复验。原失败日志保存在 build/p5-m5a2-close-before-fix.log，不把首次构建成功当作运行正确。

修正后 UI 发布 33 项、原服务 13 项及输出面板 9 项联合 55/55，名称集合与清单一致。18 项源码/构建输入冻结，16 项格式、24 组独立公开头及 5 份原协议不变已核对；当前源码随后完成下述五配置全量，不沿用 a1 结果。实际 ASan 构建的 20 个对象插桩、Module 异常模型及全量前后哈希一致。

首次 a2 重建前保存的实际旧 SDK 0.1.5 notebook DLL（SHA256 45267e3e698a259daf8e5bdb6187abcdb0f291a91f530cf7f6349d28d902eaf0）已在当前 Release 宿主执行 normal 通过，原嵌入元数据与摘要未变。兼容脚本最初传相对路径被入口准确拒绝，改绝对路径后执行成功；两份日志分别保留，不混为产品失败或成功兼容。该结论只覆盖该实物及场景。Scope 组合只证明新贡献完整恢复并可重新进入，不宣称自动恢复活动页签、焦点或 Notebook 草稿，后者继续归实际业务阶段。

验证须用真实 Context/原发布组与 Qt 队列：Held 零可见副作用；普通/候选序号交错、拒绝不占号与 Abort 空洞；条数/字节/精确目标限额不损旧历史；通知镜像恰一次及旧序号不覆盖新状态；提交后立即 stop/丢原句柄，以及未推进 observer 即 shell.close 的历史/镜像保留；commit→新写→clear→迟到 watch 不复活；无输出且清理失败的真实 Abort；拒投和已接受后 discarded；窗口/镜像信号重入 post/close；整页面/命令/值贡献的 Scope 提交/恢复组合。原 ABI、SDK 0.1.5、Qt resident 与默认空名单不变；完成代码审查及风险相称矩阵后提交同步，才进入 M5-b。

## M5-a2 收口结论

两类原后端现在完整区分接纳、pending、纯历史与呈现，发布决定仍唯一来自原组。已删除 ServicesControl 的旧通知存储/append 及 raw 日志 fallback，输出只有一份接纳/淘汰算法，PendingValues 有两个实际调用者；Core 无反向 Qt/UI/Module 依赖。私有终态桥和编译期容量常量不改变公开对象布局、原业务/状态 C ABI 或 SDK 0.1.5；内部 Core/Ui 一起重建，保留既有公开 facade 签名和真实调用者所需入口。

| 配置 | 当前冻结源码的完整结果 |
|---|---|
| MSVC Release，共享 Core、Qt/Ela 与示例开启 | 1381/1381 |
| MSVC Debug，相同功能范围 | 1381/1381 |
| MSVC ASan，相同功能范围 | 1381/1381；20 个实际对象运行前后记录一致 |
| MSVC 静态 Core，关闭 Ela/示例 | 974/974 |
| 无 Qt GCC Debug | 444/444 |

各日志通过名称唯一并与完整清单匹配，全部在关闭顺序最终修正及当前 18 项冻结输入后完整执行。55 项联合只是过程证据，不与全量重复累加；a1 的组合验证限定仍作为历史保留，不套用到本段。格式、公开头、原协议、当前产物与旧 SDK 实物 normal、本地引用及差异检查通过。完整来源、原失败及最终报告保存在 build/p5-m5a2-*。

独立要求追溯、结构/所有权、线程/重入、兼容及证据复查未发现剩余提交阻断项。实际用例和范围见 [M5-a2 验收映射](../tests/acceptance/p5-replacement.md#m5-a2-输出与通知发布边界)。本段随收口提交并同步后，工作区干净才进入 M5-b 实际业务版本和宿主装配；本段不宣称自动恢复活动页签/草稿、真实 DLL H01/S09 全范围、100 次/随机压力、生产名单或新增原生桌面升级验收已经完成，整 P5 仍进行中。
