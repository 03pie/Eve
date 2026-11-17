# P5-M5 业务升级、工作台接入与最终验收

日期：2026-11-18。代码基线 cff1bac（M4-c 已审计、提交同步，工作区干净）。M5-0 架构与分段设计已通过审查，随本次提交归档；代码尚未实施。原说明书与开发指南仅作只读依据。

## 完成条件与分段

对照[说明书 §12、§14、§18–19](../Cordis插件平台-软件设计说明书.md#12-受控替换与恢复)和[开发指南 H01/H02](../Cordis-Cpp17-Qt5152-开发指南.md)，M5 交付可运行的实际业务升级、工作台操作与故障报告，以及同进程连续替换和随机压力。M1–M4 的真实夹具、完整事务、状态迁移和物理卸载是实现基础，不替代实际业务/原生桌面验收。

| 小阶段 | 职责与必须兑现的出口 | 状态 |
|---|---|---|
| M5-0 | 核对实际贡献、宿主与业务产物；记录依赖、拥有者、提交/恢复、名单和压力门禁；审查并提交同步 | 设计与独立审查通过，随本次提交归档 |
| M5-a1 | 私有 Context 发布桥与完整终态通知、页面/命令完整准备与隐藏呈现；原 Ela/工作台所有入口受同一发布状态控制；回归真实销毁与投递失败 | 待实施 |
| M5-a2 | 输出/通知 pending 值的有界保存、提交接纳、失败丢弃及纯历史锁存；整 UI 贡献发布/恢复组合审计 | 待实施 |
| M5-b | 实际 counter/reporter 与 notebook 旧/新产物；明确运行状态契约；复用现有具体宿主装配、安装/stage/ReplacementOperation 与配置共同提交 | 待实施；实施前补具体拥有者/故障和接口设计 |
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
