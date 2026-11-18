# P5-M5-c 产品模块管理与升级交互

日期：2026-11-18。M5-b4-2 已随 0d2df2d 审计提交同步，工作区干净后开始。本记录先核对现有入口、状态源和拥有链，再按下列阶段实现。每段审查、修正、相称验证并提交同步后，才开始下一段；完整 100 次/随机和最终名单、桌面验收仍归 M5-d/e。

| 阶段 | 具体出口 | 状态 |
|---|---|---|
| c1 | 顶部实际模块入口、同一托管模块窗口、纯值模块目录和视图退出屏障；替换旧无用户入口的诊断路径 | 已完成审计与验证，随本次收口提交同步 |
| c2 | 候选文件选择、身份/影响路径预览、显式升级、原阶段/报告、取消与陈旧/双击门禁 | 待 c1 提交同步 |
| c3 | 高级物理重试及真实 H01/S09、捕获/停止/通知/回执故障产品报告；完整 M5-c 审计 | 待 c2 提交同步 |

## c1 问题与职责

现 modulesPage 是 host.notebook 的 Dialog 贡献，实际只在测试中直接创建同类诊断 widget，产品没有 navigate 调用。Dialog 不进导航和 Home，现 ElaShell 只提供配置文件/关闭宿主回调；把模块升级藏进配置文件页会混淆实例配置和代码产物。每个配置卡片不等于一个模块，多实例共享同一代码代次；本阶段先建立一个平台模块入口，不在 management.cpp、IConfigurationManagement 或插件服务堆入升级职责。

ElaShell 增加与 onConfigurationRequested 对称的 onModulesRequested 宿主非虚成员。handler 仅存在私有 ShellControl，线程/关门检查、复制回调后调用及 NavigationAccess 沿原约定，关闭清理。顶部文件菜单“模块管理…”为真实平台 QAction：无 handler 时禁用，晚绑定后可用，仍由原命令面板登记/触发。不能初始隐藏而假设后续补登记，原命令面板仅枚举构造时可见动作。新增读取可用性的 callback 只看 handler/open，不调用 provider；沿 b4-2 Home 单次权限来源，不添逐贡献外部回调或动作 registry。

实际模块窗口留在 examples/qt_workspace，复用原 modulesPage 的 PagePresentation::Dialog 与 effect/窗口拥有者，不反向依赖 Loader/Module 到 Cordis::Ui。现 diagnostics.cpp 整理为纯值诊断格式化，与新的具体模块视图分工；删除原跨可重入 DiagnosticsService::snapshot 后继续使用裸控件的刷新工厂，不留并行旧界面/兼容包装。旧实际测试改用托管窗口或真实纯值快照，不为清理失败另造不受托管的诊断窗口。

## 唯一模块值来源

Workspace.modulesSnapshot() 是 owner Result 查询：先 synchronizeUpgrade，再读取原目录/当前记录与当前 Session/Driver，返回纯 WorkspaceModulesSnapshot：

- current：alias、PluginId、GenerationId、RuntimeId 和属于该插件的配置路径；
- generations：原 ModuleGenerationSnapshot 全值诊断；
- configurationErrors：实际会话/运行/关闭错误，保留原因链；
- open：当前工作台仍接纳操作，关闭/队列故障不显示可执行状态。

这些结构位于示例宿主，不能含 Runtime、NativeModule、安装凭据、Task、Widget、回调或自行管理的当前/候选目录。当前身份仍来自 b4-2 原事务共同提交；配置路径取已接受目标，禁用/尚未实际创建的条目也应按目标列出。读取某一诊断源失败保留原错误，不用空表称排空；已关闭时能力 RuntimeId 可无效，真实模块/驻留历史仍可读。普通 legacy 入口只有 Notebook，不伪造三个模块。c1 不预留 canUpgrade、取消或回收接口；后续真实调用者需要时再设计。

## 模块视图与关闭的拥有链

模块 widget 仍归原 host Context 的 PageHandle/effect。视图只弱持 Workspace；QTimer 是 widget 子对象，只读值，不推进事务、不长期借用工厂。Dialog 关闭隐藏视图，不停止业务；不创建第二个 Shell/Kernel/Session 或自行接管模块生命。

新增具体 Workspace.withModuleView(action) 同步 owner 访问屏障：接纳时计数并持实际 Workspace，RAII 在正常/异常路径减少；第一次进入初始化本轮 idle Promise，最后退出结算。只在该屏障内投影/写模块视图，不保存 this/控件引用跨异步回执。业务升级仍是 b4-2 原执行所有者，c1 屏障不成为事务锁或插件资源。

Workspace.close 在原升级取消/等待及保存成功后，先关模块视图接纳，异步等待已有写栈退出，再进入 Session/Kernel 的破坏性关闭；没有 Session 的半装配路径同样经过屏障。不能阻塞 Qt 或在嵌套 processEvents 时提前删模块页。保存冲突发生在视图关门前，原窗口/队列继续可读，rebase 后重试关闭；已接受等待被丢弃沿原 deliveryBlocked/拥有链保留，不伪造 idle 或强制删除。正常退出仍等完整 Page/widget 析构。Kernel 真正关闭时 host Dialog 贡献照常退出；清理失败后保留的 Shell/纯模块及错误诊断不等于该已撤销 Dialog 仍存活。

视图输入/刷新只经该实际屏障，跨可能嵌套的文件选择在 c2 使用明确 parent 的非模态 QFileDialog，避免新增 exec 嵌套。withModuleView 不授予业务能力；所有业务动作仍由 Workspace/原 API 重验。外部直接销毁 widget、越过 Workspace 直接关闭其 Kernel/Shell 不属于该拥有约定，不能作为平台正常关闭路径。

## SDK 与旧实物

ElaShell 现无 virtual，仅七个 shared_ptr 数据成员；新增非虚 out-of-line 成员不改布局、既有符号或 vtable。新 handler 是私有 ShellControl 字段，Ui 当前自动导出符号，原 PageSpec、DiagnosticsSnapshot、Qt IID 和 C ABI 不改。当前精确工具链支持维持 SDK 0.1.5，不宣称稳定跨工具链 ABI；若实现需要改公开布局/契约须另评版本。

任何 c1 代码修改前已保存 0d2df2d 的真实 legacy Notebook（SHA256 175d27205feca24681ee11985958a57fac52455a8eab42fcdc50401562e5b3b6）、真实旧宿主测试 exe（SHA256 fe501feea926e02f033c9c4b537892578aa084ec62919849ad02ed8e7dcd8d99）、声明、旧 ElaShell 头及 Ui 导出/两旧实物导入清单到 build/p5-m5c1-sdk015-legacy。后续验证旧宿主/旧插件对当前 Ui 的真实装配和 normal 运行，不改元数据冒充兼容，不把所有私有自动导出符号误当公共 ABI。

## c1 验证出口

普通 Shell 无 handler 禁用，绑定/重绑、线程/空/关闭、菜单及原命令面板真实触发、回调关闭/泵事件；legacy 单模块与 qualification 三模块的同一托管窗口、纯目录来源/配置路径、重复点击不复制窗；模块表格/文本信号中实际 Workspace.close 并泵保存/清理，写栈退出前不破坏 Session/Kernel/页，之后完整退出；保存冲突可继续刷新/重试，实际丢弃不丢拥有者。更新旧两个诊断调用者，保留 cleanup failure 原因链可读。此前 b4-2 真实升级、Home/菜单/配置/发布门禁相邻回归及旧实物/头/导入导出、实际插桩/格式按最终修改风险验证，不预称通过。

## c2/c3 已核对的约束

预览只做文件/元数据检查，不能宣称已检测发现/业务导出；缺符号须实际走装载校验阶段，保留旧业务。候选 Notebook 1.3 启动失败用于真实停止后的恢复。S09 可用同源 tests-only resident Reporter 故障薄桥：第二次创建启动失败，或第二次及以后失败，分别见证提供者已 Active/Held 后完整恢复与 RecoveryFailed。复用原服务/cleanup/实例账目，无测试导出、setter、RPC 或第二实例表；不改正常 AB，四份 Native explicit 批准不扩张。故障变体必须限定独立进程和单 Reporter；详细方案在对应小阶段实施前补齐。

已有 failCleanup 配置、超限草稿和真实 discarded 另按实际阶段报告：未停止、停止失败、已完整恢复、恢复失败、已共同提交但回执/物理失败彼此区分。不把策略未批准拒绝当缺符号或停止后恢复，不清 pin 后继续压力计数。完整 H01/S09 出口在 c3，100 次/随机及最终名单/人工验收仍为 d/e。

## c1 收口结论

平台顶部“文件→模块管理…”及原命令面板已接到同一个托管 modulesPage Dialog；无 handler 禁用，晚绑定即时可用，重绑/异常/关闭重入沿原访问屏障。SDK 只加非虚宿主方法，handler 在私有 ShellControl，Ui 不引入 Module/Loader 依赖，也没有新增动作 registry 或把代码升级塞入实例配置管理。

原无产品入口且跨 provider 回调使用裸控件的独立诊断工厂已删除。实际模块视图弱持 Workspace，表格/文本使用 Ela 控件；当前身份、版本、策略、已接受配置路径和错误来自同一纯 modulesSnapshot，禁用/多实例路径仍保留。格式化与控件分开，关闭后的纯目录/原因链可读，不用独立测试窗口冒充生产故障视图。Dialog 隐藏保留业务，完整退出仍归原 Context/effect。

实际 withModuleView 同步写栈以真实 shared Workspace/深度/本轮 idle Promise 保活。原关闭在保存确认后关视图接纳、异步等待写栈退出，再走 Session/Kernel；保存冲突未关门，可以刷新/rebase 后再关。真实 Qt 丢弃 idle continuation 时原 Task 报 SchedulerClosed，原 Workspace/Kernel/Driver/Session 及页继续保留，不伪造 idle、静止或清 pin。

七项新场景覆盖实际菜单及原平台命令、重复打开同窗、legacy 单模块、三模块/禁用/多配置路径、真实 Notebook A→B 后 current/Runtime/locator、视图异常/错误线程，以及模型 dataChanged 内关闭/泵保存删除、保存冲突、真实 FIFO 丢弃。旧两个诊断场景改为托管窗口与实际纯错误链。交叉审查修正测试临时容器迭代器，未为测试增加插件导出、setter、第二事务或业务旁路。

首轮新七项加原诊断两项 9/9；十五项功能输入冻结后，MSVC Release/Debug/ASan 各受影响专项 226/226，Passed 与完整选择集逐项一致，whole_stage=false。最后审查确认 nativeMapped 只表示目录映像引用，物理离开验证仍可能失败；仅将 diagnostics.cpp 的四个显示字面量收窄为“映像引用/目录引用”状态并排版，其余十四输入不变。该最终十五输入在三配置重建，对相关九项各 9/9 复验；没有宣称文案修正后又完整重跑 226，也不把两组重复累加为测试数。精确差异与两轮输入记录在 build/p5-m5c1-label-audit.json。

十四项格式通过，最终三十一实际 ASan 对象及一百三十六源/头编译依赖核 flags、真实 Ninja deps、时间戳和前后 SHA；功能/最终对象只有 diagnostics.obj 变化。三十六实际产物为原 Native 二十四摘要保持与当前 Notebook 十二清单/metadata/moc/宏一致，正常业务源/状态/moc 未改。SDK 0.1.5 两旧/新独立头在 MSVC、GCC 声明检查通过；MSVC 实测均 112 字节、七个 shared_ptr、无 vtable。十五原公开 Shell 符号及旧实物实际 Ui imports 保留，新方法已导出。

保存的 0d2df2d 旧宿主和旧插件均在当前 Release 运行 normal，Toolhelp 见证加载的是当前路径/摘要的 Ui/Core；不是只看 PATH 或改 metadata。最后文案后已重跑，旧八项归档及 provenance SHA 保持。两个实际入口三配置共六次坏文件输入，均按预期退出 1、无未确认收尾。上述独立检查不计入 226/9 或桌面人工验收；GCC 只声明检查，不是 UI 运行支持证明。Core/Module/Loader/Replacement/Qt/Ela 未改，不重复未变的无 UI/无 Qt 全量。

独立源码、生命周期、SDK 和证据审查无剩余提交阻断，随本次提交同步。c1 交付实际模块入口和视图基础；候选选择、影响/执行、进度/取消属于 c2，高级回收及完整 H01/S09 属于 c3，生产名单仍空，Qt 驻留。没有预留不可用升级按钮或宣称整个 M5/P5 完成。
