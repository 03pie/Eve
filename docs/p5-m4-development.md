# P5-M4 白名单原生模块卸载

日期：2026-11-17。基线 b454d85（M3-c），已审计、提交并同步。M4 进行中；M4-0 和 M4-a 已提交同步，M4-b 物理终结实现、阶段审计与五配置完整验证完成，随收口提交归档；M4-c 集成未开始。

## 架构与分段门禁

已核对 Generation/ModuleCatalog、原 Native/Qt 映像所有者、固定产物、HostRegistry 缓存、ledger、Runtime/实例来源、admissions 和 replacement/readers。决定、取舍及官方 API 依据见 [ADR-0050](adr/0050-approved-native-module-unloading.md)。现有 resident/PIN、const artifactFile、长期函数表缓存及历史总量 64 上限不能合理承载新需求，先重构这些边界再交付卸载。

| 小阶段 | 实际改动边界 | 必须通过的出口 | 状态 |
|---|---|---|---|
| M4-0 | 策略/权限、发现兼容、映像/文件所有权、失败、历史/预算及测试设计 | 对照说明书 §11.4/§12、开发指南及真实源码逐项审计，本地引用检查；审计后提交同步 | 已随 eaef2dd 审计、提交同步 |
| M4-a | manifest 请求、宿主精确白名单、首次策略与 owner；发现 1.1 显式声明及原 1.0 resident 兼容 | 真实新旧 DLL；未授权映射前拒绝、旧代不能升级、跨 facade/别名/候选策略、Qt 拒绝、SDK 与布局、相应支持矩阵 | 已随 0f4f5f2 审计、提交同步 |
| M4-b | 同目录原子终结资格、缓存与映像/文件退役、Unloading/Unloaded、失败所有权、有界纯历史 | 真实物理卸载组合证据；全部执行引用阻碍、重入/并发/线程、系统失败及外部映射、再次装载、64 重资源和 128 历史边界 | 实现、阶段审计及五配置全量验证完成，随本次提交归档；提交同步后进入 M4-c |
| M4-c | 原替换终结后的退役代显式回收、完整故障及支持矩阵、整 M4 审计 | 新代继续服务、旧代完整恢复与保留、卸载失败不改写业务提交；全量验证、要求映射、审计提交同步 | 未开始 |

每段先记录具体数据来源、依赖、状态和释放方式，再实现与独立审查；修正实际问题、冻结源码/测试名称并验证，提交同步后才能开始下一段。没有必要的重复实现和未使用抽象不进入代码库。

## 数据与职责

- manifest 只表达请求；宿主白名单精确批准 C ABI 模块 ID/真实 SHA256，默认空。实际策略由首次固定产物选择并保存在原 Generation，后续复用不能变更。
- Core 和 Loader 不认识物理加载器。Module 管理目录、映像与实际资格；原 Native 适配器退役自己的业务/状态表，Qt 保持 resident；替换协调器仍只负责闭包、状态恢复和共同业务提交。
- 首次装载 owner 执行最终系统调用。租约析构、worker 最后释放和对象 facade 析构不卸载；Runtime、状态契约、服务、队列捕获和 cleanup 返回路径都必须先完整退出。
- Unloaded 只有纯值历史，不持 artifactFile、backend 或可调用表；同目录独立约束 64 个重代次及 128 条纯历史，再次装载使用新 ID。
- 失败保留所有权，不确定原因保持 pin；未知外部映射不由平台循环减引用。恢复仍可能用到的旧代不得卸载，物理结果不参与业务提交回滚。

## 具体兼容与实施约束

M4-a 才将 C++ SDK 从 0.1.3 升为 0.1.4，增加实际使用的请求/策略诊断。发现扩展保留已有 1.0 常量和结构布局，显式路径协商 minor 1；旧 resident 行为、业务 plugin.h 和状态 IID 不变。不能只改 descriptor.flags 判断就把旧 RESIDENT 重新解释为可卸载。

已在 SDK 更新前保存实际 0.1.3 Qt DLL 到 build/p5-m4-sdk013-artifact，SHA256 为 b0322f4c033b51fcb19cbdf5febb161847c4e97cb9826c3129733b0ee136acbd；元数据与来源提交一并保存。M4-a 以真实旧产物证明仅 SDK 字段变化时在映射前拒绝，不能只用改字符串的新版夹具代替。

Native 旧 facade 的入口要在解引用 api 前核对关闭/终结，避免缓存失效后空表调用；缓存清除不得以全局 host 锁包住 DLL detach。普通已知系统释放失败可保留真实引用并显式重试；未知外部引用取得拥有的探测引用后保守留驻，向同一 ledger 新增永久 unknownExternalMapping pin，不允许再次减引用，也不清除原 pin。具体私有系统调用注入边界在 M4-b 实施前补充记录，不提前公开测试能力。

## 整 M4 的验收矩阵

| 要求 | 必须取得的实际证据 |
|---|---|
| 权限与首次策略 | resident 仍 PIN；显式未批准在映射前拒绝；声明/摘要/后端/协商匹配；复用不能改 owner/策略，Qt/未知后端拒绝 |
| 真实物理结果 | 同一未退出进程内动态 detach 恰一次、映像范围离开、文件保护释放及可操作；重新 query/业务、新 ID、旧地址不再调用 |
| 全部执行引用 | 实例、setup/cleanup、Call、worker/返回路径、callback、queue payload、服务、QObject、外部句柄、Runtime/定义/状态契约实际持有时拒绝；释放实际对象后可推进 |
| 故障与未知 | 清理/协议 pin 不清除；系统失败保留原拥有引用；外部 LoadLibrary 导致仍映射时保守 Resident，有准确原因和所有权，无假 Unloaded |
| 原子与线程 | 首 owner；异线程 facade 拒绝；最后租约跨线程释放不卸载；同代重入/并发唯一，其他代独立；任何内部锁外执行系统释放 |
| 缓存/文件/历史 | 旧 facade 不解引用失效表；全部来源/缓存先退役；64 重资源不被空历史耗尽、128 纯历史按最旧淘汰，旧报告有效、不复用 ID |
| 替换与恢复 | 成功后旧退役代可回收且新业务继续；恢复期间旧代仍保留；恢复失败资源不卸载；物理失败不改写已成功提交 |
| 兼容与支持 | 默认 Native、Qt resident、原 C ABI/SDK 门禁、公开头与布局；Release/Debug/ASan、静态 Core 关闭示例、无 Qt GCC 的相应完整源码验证 |

优先复用 module_fixture/business_fixture/resource_fixture、lifecycle_fixture 的真实对象、队列捕获和 DLL 内析构证据，不复制实例目录或以 mock 替代真实卸载。探针自身的 NativeSource.session/Call 和函数指针能力必须在试卸载前退出，最后只保留宿主纯值见证。

M4 的专用夹具白名单和短重复装卸不是生产开放证明。开发指南 H01/H02、S09、服务与 Qt 页面在线升级、Windows 连续 100 次真实 ReplacementOperation 及随机/故障压力由 M5 收口；没有通过这些门禁前不向实际业务产物发布白名单。

## M4-a 实施设计（已完成）

M4-0 已随 eaef2dd 审计、提交同步，工作区干净后开始本段。此次先建立实际可验证的策略选择和协商，不添加 tryUnload/Unloading/Unloaded 或系统释放代码；原目录持有新路径取得的真实映射引用，直到后续显式终结或进程退出。

ModuleManifest 追加 ModuleUnloadPolicy（Resident/Explicit），严格接受 resident/explicit，默认和旧声明语义不变，qt-ui+explicit 在解析/后端拒绝。加载入口追加默认空的 NativeUnloadWhitelist 参数，放在现有 PreflightLimits 后，原调用源码兼容，不增加重载歧义；每条 NativeUnloadApproval 只含 moduleId/sha256，C ABI 后端由类型与入口限定。它是宿主纯值允许集合，匹配重新固定的真实 manifest 身份与摘要；无效/不匹配内容不能命中，不另造授权服务或可变注册表。

原 loadGeneration 先核对 owner、固定产物及后端，再选择 Resident/ApprovedNative 实际策略，显式未匹配在 reserveGeneration 前拒绝。reserveGeneration 接收选定策略，Generation 构造时保存不可变策略和首次装载 owner；诊断只派生实际策略值，不把 nativePinned=false 当作授权。复用必须匹配已固定策略，不能更改原 owner。相同产物/别名的声明比较包含 unloadPolicy，不能通过改 manifest 或另一 facade 升级已驻留代；不同候选仍逐个独立批准。

身份取舍：sameCatalog 继续只比较模块 ID/后端/PluginId 集合，允许两代采用不同策略。不同物理副本即使二进制摘要/版本相同，也可仅因显式策略声明不同成为新候选，但必须重新批准、取得新 Generation 并独立映射，原 resident 代不改变；同物理文件或硬链接不能如此换策略。安装器同 hash 目录的声明冲突仍拒绝，不能覆盖既有产物。此取舍使支持两种发现请求的同一审核二进制可以显式选择新映像策略，而非撤销旧 PIN。

mapGenerationImage 只按保存的策略决定是否登记 residentPolicy 和执行永久 PIN：Resident 保留原路径，ApprovedNative 使用同一受限 LoadLibraryEx、固定文件及实际映像身份检查，保留本方映射引用，不执行 PIN。映射/身份/协商任何失败仍沿原 failGeneration 的 loadRejected pin、引用/文件保护及诊断保留，不能因尚未 PIN 就自动释放未知 DLL。

发现常量 CORDIS_MODULE_QUERY_MINOR 继续为 0；增加 CORDIS_MODULE_QUERY_EXPLICIT_UNLOAD_MINOR=1 和 CORDIS_MODULE_EXPLICIT_UNLOAD=2。Resident 请求/严格检查 minor 0+RESIDENT，ApprovedNative 请求/严格检查 minor 1+EXPLICIT_UNLOAD，request.flags 仍为零、runtime ABI 仍为 1.0；结构大小和偏移不变。专用真实夹具可支持两种请求，旧夹具默认行为不变。Qt map 只接受 Resident，不添加授权参数。

本段同步提升 C++ SDK 0.1.4，增加实际旧 0.1.3 Qt 产物及新版错误声明的映射前拒绝证明。仅扩展发现协商，不改 plugin.h、状态扩展或 Qt 既有 IID。验证包含解析、真正未授权零映射/零目录副作用、默认 PIN、新显式未 PIN 且持有映像、协商失败保留、策略复用/别名/候选、错误线程/装载重入、Qt 拒绝和全部旧回归。独立审查后按支持矩阵冻结验证、提交同步，才能进入物理终结。

首版过程记录（最终结果见下方收口结论）：沿同一目录/映像入口接入策略，22 项真实 DLL 用例、解析/预检/布局联合 Release 53/53，通过后重建的实际预约/安装可执行文件回归 243/243；未运行未重建的旧 SDK 插件冒充当前证据。10 组 MSVC/GCC 独立公开头通过，原业务/状态/Qt 契约四份声明不变；发现头的语义扩展及原大小/偏移由专门 C/C++ 布局断言核验。首版独立生产差异复查未发现阻断项；当时整阶段支持矩阵和最终审计尚未完成，未提交。

本段验证范围：SDK/Native/Qt 装载链变化需要四组 MSVC 配置完整重建及全量测试。无 Qt GCC 不编译 manifest/Native/Qt 装载实现，Core 算法源码未改，仅构建版本与公开声明/发现常量受影响，因此验证当前公开头、C/C++ 布局、账目、SDK 契约及实际版本示例；保留 M3-c Core 全量 440/440 作为未变算法基线，不把该旧完整结果计作 SDK 0.1.4 的全量。整 M4-c 收口仍执行当前源码全部配置的完整矩阵。

## M4-a 收口结论

2026-11-17。实现与独立生产差异复查完成。首次策略沿原 ModuleManager→固定来源→唯一目录→同一映像入口接线；实际 const policy/首 owner 只在构造时确定，快照派生诊断。原无条件驻留选择已被声明/宿主匹配替换，默认/Qt 的 resident/PIN 及失败保留继续复用；没有新增权限目录、动态开关、释放函数或未使用的卸载接口。

| 当前源码配置 | 验证结果 | 范围 |
|---|---|---|
| MSVC Release、共享 Core/Ela | 1295/1295 | 完整重建与全量串行测试 |
| MSVC Debug、共享 Core/Ela | 1295/1295 | 完整重建与全量串行测试 |
| MSVC ASan、共享 Core/Ela | 1295/1295 | 完整重建与全量；8 个实际关键对象及前后哈希/异常模型一致 |
| MSVC Release、静态 Core、关闭示例/Ela | 921/921 | 本配置完整重建与全量测试 |
| 无 Qt GCC Debug、静态 Core | 8/8 | 当前受影响布局、ledger、SDK/版本专项，实际示例输出 0.1.4；不是完整配置测试 |

19 项源码/构建输入冻结后逐项匹配每组实际通过名称；GCC 专项单独保存选择范围，未把 M3-c 的 440 项旧完整结果合并进当前计数。53 项首轮策略/解析/预检/布局及 243 项重建可执行文件的相邻回归只作过程证据。日志、选择清单、输入与验证结果保存在 build/p5-m4a-*；10 组公开头、C/C++ 布局/常量、4 份原业务/状态/Qt 协议不变、14 项格式与差异检查通过。

| 出口 | 实际证据 |
|---|---|
| 声明不是授权，未知/Qt 策略拒绝 | p4.manifest_unload_policy；独立预检保留 Explicit，但不加载；错误类型/未知值/重复字段/qt-ui+explicit 拒绝 |
| 精确重新授权，拒绝零副作用 | p5.load_policy_unauthorized/wrong_id/wrong_hash/invalid_entry/tampered_preview；既无映像也无新目录行，旧预检后真实 PE/摘要改变须重新批准 |
| 新旧策略及发现协商 | approved/resident/capable_resident/wire；真实 minor 0+flag1 仍 PIN、minor 1+flag2 未 PIN 且持有映像；原布局和 1.0 常量保留 |
| 不兼容结果保留 | legacy_reject/legacy_flags/explicit_flags/explicit_minor；协商失败保留 loadRejected、实际映像和文件保护，不调用失败代 getter 或自动释放 |
| 策略/owner 不变与别名 | reuse/immutable_resident/immutable_approved/alias_resident/alias_approved/threads；复用同代、不二次 query、不修改实际策略或首 owner |
| 候选、同 hash 副本与原准入 | candidates/same_hash_copy/reentry；两种策略候选独立批准、预约时 stage 拒绝；不同物理副本新 ID/映像，同文件/硬链接不能重分类 |
| 未提供卸载能力 | ApprovedNative 的 drain 只关闭准入，实际映像与文件保护仍在；默认及 Qt 路径保持驻留 |

实际 SDK 0.1.3 Qt DLL 的原哈希 b0322f4c033b51fcb19cbdf5febb161847c4e97cb9826c3129733b0ee136acbd 未变，工具链描述与新宿主仅 sdkVersion 不同；新版进程在映射前拒绝且目录无副作用，记录为 p5-m4a-sdk013-verified.json。当前 C++ SDK 0.1.4，宿主/同工具链插件须一起重建；发现 1.1 是明确协商扩展，原业务 ABI 与状态 IID 保持不变。

最终要求映射、所有权/兼容和整体结构复查未发现剩余提交阻断项。实现与审计随本次提交归档，提交同步后开始 M4-b；当前仍无 tryUnload、Unloading/Unloaded、缓存/文件退役或物理系统释放能力。真实业务白名单、H01/H02 连续 100 次及 UI/压力门禁仍归 M5。

## M4-0 文档审计结论（历史记录）

已对照实际源码、说明书/开发指南和官方 Windows API 核对职责、依赖、状态与资源所有权。独立审阅澄清四项：外部映像重新取得拥有引用仍须永久 pin；正常卸载后无 probe 与无法判断范围的未知状态分开；装载选择跳过 Unloaded 但历史诊断保留；DLL 原子写入固定宿主槽、宿主返回后读取，进程退出 detach 不计证据。Current 终结不隐式提升候选。

M4-0 当时仅提交文档，未改源码、SDK、ABI、测试或运行行为，以 M3-c 的 b454d85 五配置结果作为当时实现基线，未重复运行未改变的矩阵。本地引用、阶段状态与差异检查通过，已随 eaef2dd 提交同步；当时未交付物理卸载、SDK 0.1.4 或 M4-a/b/c 的实际验证，不能以设计代替功能。M4-a 的后续实际结果见上方收口结论。


## M4-b 实施设计（已完成）

M4-a 已随 0f4f5f2 审计、提交同步，工作区干净后开始本段。先检查真实目录、账目、Native 缓存和关闭路径，确认需要重构三个所有权边界：账目 snapshot→beginDraining 不能原子拒绝并发 acquire；关闭 root 先释放 session 后摘 active 存在资格窗口；永久函数表及 const artifactFile 无法退役。按下述方案替换原实现，不在 drain 末尾追加系统调用。

ModuleManager::tryUnload(GenerationId) 为唯一显式物理入口，返回错误或成功；Unloading/Unloaded 追加到原 ModuleState，快照追加独立 unloadFailure，不改业务提交和 businessFailure。C++ SDK 升为 0.1.5，原 ABI/IID、默认驻留和 Qt 不变。现有工作台诊断补齐两终结态与独立卸载错误，避免将新状态误报为驻留；不增加 UI 卸载按钮或业务白名单。原 SDK 0.1.4 真实 Qt DLL 已在更新前保存在 build/p5-m4-sdk014-artifact，SHA256 f4ec368234dd809e003517d6d7085060e31b19e2f61d0f376f655e55429991c6；后续证明映射前拒绝，不能覆盖该产物。

owner 采用原 std::thread::id 加私有、进程单调不复用的 TLS uint64 身份；计数耗尽明确失败。Generation 与 ModuleManager 首次构造固定同类身份，原 manager 线程检查统一使用私有 ownerThread()，物理入口还核对 Generation 身份。不同线程即使复用系统/std ID 也不能冒充原 owner；只有值，不增加线程目录、句柄或自动卸载调度。

目录→代次→账目锁下检查真实首次策略、协商、映像/文件所有权、健康、loading/admissions、同模块 replacement/readers、runtimeOrigins/instanceOrigins；GenerationLedger 新增仅 Generation 可调用的私有 sealEmpty，在同一账目锁中检查 records/pins 并永久关闭 accepting。成功后设置 Unloading，全部锁退出才退役 Native 缓存和消费系统引用；失败资格检查不关闭业务准入。Unloading/Unloaded 的生命周期关闭准入也必须拒绝，不能因旧清理入口不检查健康而重新入场。最后租约只记账，worker 不触发卸载。

复用 Generation.backend 强拥有真实 NativeBackendResidency/原 BusinessRecord；HostRegistry.modules 只保留原目录的弱索引，退役摘除，避免第二份强缓存。BackendResidency 提供实际使用的 retire 入口，Native 实现核对协商与 active 为空、移除弱索引后清业务/状态表；原 Qt backend 默认拒绝且不进入卸载。ModuleNative 不反向链接 Host。finish 先摘 active、清 root 的 query/queryState，之后释放 session；关闭 facade 的 create/状态/幂等 close 先判断完成状态，再访问表或 scheduler。实际实例/请求/服务/捕获/状态仍复用既有租约，不追加引用体系。

物理实现在独立 unload.cpp/private header，原目录仍是唯一资格与状态源。私有 NativeUnloadSystem 只替换 FreeLibrary/GetModuleHandleEx/VirtualQuery 三个真实系统调用边界，生产入口使用原 API；不公开故障注入，也不绕过任何资格/身份检查。普通释放失败保留原 image/ref 和文件保护，Resident + unloadFailure，准入不重开，可显式重试。缓存退役拒绝保守 pin，无系统释放。消费成功后原 image 立即置空，随后用拥有引用的 FROM_ADDRESS probe 和实际文件身份/VirtualQuery allocation 证明原映像离开；仍为同产物则接管 probe 并永久 unknownExternalMapping pin，不循环消费外部引用。探测/范围不确定则永久保留文件保护和诊断，不伪造 owned image。另一映像的 probe 只释放本方新引用；若该释放失败，单独保留 observationImage 与永久 pin，不能将别的映像冒充原 image。

只有确认离开才清 backend/artifactFile，发布 Unloaded/Retired，并在同一目录保留纯值历史。所有选择、别名、复用和准入跳过 Unloaded；历史快照和按 ID 的诊断仍可见。原 64 限额改为重代次计数，Unloaded 历史最多 128，超额只淘汰最旧 Unloaded；ID 不复用，不建第二历史容器。系统释放、backend 清理与文件句柄释放均在内部锁外；锁内只移动所有权/发布宿主值。

验证先取得真实动态 detach（仅 lpvReserved==nullptr 的 DLL_PROCESS_DETACH 原子增加宿主预分配槽）、映像 allocation 离开、文件可独占写入/改名/删除和重装新 ID 的联合证据；探针 session/Call 在尝试前退出。覆盖真实 Native 会话/实例、Runtime/状态、异步请求/捕获/服务与清理故障，以及旧 facade、首次 owner、跨线程最后租约、并发原子关门、重入、预约/readers、系统失败、外部映像、再次装载和目录边界。不得用进程退出、短目录循环或单次 ledger 快照冒充物理结果。M4-c 才完成替换终结集成与整 M4 五配置全量；M5 业务名单与压力门禁不提前开放。


M4-b 首轮复查：生产库编译通过后，进一步发现 createBound 的 scheduler->isOpen/context guard 可重入，最初前置关闭检查仍不足以保护后续 api 访问。已把实际表/插件目录检查移到原第二次 root 锁内、关闭检查之后，保留真实准入与实例预算边界；增加独立 create_reentry 场景，在 isOpen 内关闭并实际卸载后返回 true，验证外层创建安全拒绝。映像路径查询复用原私有文件辅助；不复制同一系统查询实现。格式工具首次误用默认 LLVM 的整文件重写已撤销，备份代码等价核对后，仅用原显式四空格/函数换行样式格式化改动范围。


2026-11-18 首轮实际卸载 32/32；补齐业务重装、候选/Current/兄弟预约及真实 reader 后，首测 36/37。reader 夹具在建立读占用后才 close consumer，被原生命周期读门禁正确拒绝；现改为先 close 会话、仍保留真实 Runtime 来源来建立借用，再释放该 Runtime。未修改生产读门禁，最终 37/37。错误结果现在携带 phase/message，原失败日志保留。测试不手改角色、来源或 reader map；资源/来源确已空而读占用仍在时 tryUnload 拒绝，解除真实预约后物理卸载。

证据范围：probe_other 分支注入另一真实 DLL 的拥有引用，验证引用归属和释放失败，不声称系统实际复用原地址；owner_gone 验证首次线程退出、新身份值不同及拒绝，不声称系统已实际复用线程 ID。worker/QObject 是宿主显式登记加真实 join/QObjectResource 和 DLL 回调，不代表自动跟踪插件线程或允许 Qt 插件卸载。setup/cleanup/service 证明实际资源及最终退出，当时还同时持有会话/实例，未声称各类别独立隔离。上述边界须保留在最终收口。

本段完整验证范围：原子 seal 改变共用账目/排空路径，C++ SDK 和 Native 缓存/系统终结均有变化，因此执行 Release、Debug、ASan、静态 Core 关闭示例/Ela及无 Qt GCC 的当前源码全量矩阵。无 Qt GCC 已并行开始重建，冻结后再次检查构建依赖是否仍最新；只以当前实际产物和通过清单归档，不把 M4-a 的专项或旧完整结果计入本段。


## M4-b 收口结论

2026-11-18。先按实际状态与资源所有权记录设计，再重构原子账目/首 owner、Native 缓存及文件所有者，沿唯一目录实现显式 tryUnload。普通失败保留原引用可重试；同产物外部映射、无可靠观测及外来 probe 释放失败均保留准确拥有者、文件保护和永久 pin，不循环消费其他引用，不把 unknown 伪装成 Unloaded。默认 Native/Qt 与失败驻留兼容路径保留，原业务/状态 ABI 和 Qt IID 不变，C++ SDK 为 0.1.5。

| 当前源码配置 | 完整验证结果 |
|---|---|
| MSVC Release，共享 Core/Ela | 1333/1333 |
| MSVC Debug，共享 Core/Ela | 1333/1333 |
| MSVC ASan，共享 Core/Ela | 1333/1333 |
| MSVC Release，静态 Core、关闭示例/Ela | 959/959 |
| 无 Qt GCC Debug，静态 Core | 440/440 |

冻结的 30 项源码/构建输入对应五组全量结果，各组实际 Passed 名称与当前配置清单逐项一致；无 Qt GCC 冻结后再检查构建依赖，ninja 确认无工作，结果为当前完整配置，不沿用 M4-a 的 8 项专项。37 项卸载用例已包含在各适用完整配置中；首轮 32/32、补强时 36/37、修正后 37/37 仅作过程记录，不叠加统计。11 个实际 ASan 关键对象、插桩标志、Module 的 /EHs 异常模型及全量前后哈希一致。

最终独立审计指出 QFile::open(ReadWrite) 允许共享，尚不能证明独占，依据 [Qt 5.15.2 官方实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/io/qfsfileengine_win.cpp#L104-L123)。已在原 gone 联合证据中补 CreateFileW 的 GENERIC_READ|GENERIC_WRITE、share=0，实际读出并写回同一字节，RAII 关闭后再进行后续文件操作。唯一变化是 tests/qt/module_unload_p.hpp，重新冻结 30 项输入后，四组适用配置各重建并复验 37/37；生产输入及其他可执行文件 SHA256 不变。最终覆盖由原全量与受影响复验共同归档，37 项不重复累加；并非在补强断言后重跑所有未受影响用例。基线清单、差异及复验分别保存在 before-exclusive、baseline-verified、exclusive-* 和最终 verified 记录中。

10 组 MSVC/GCC 独立公开头、C/C++ 布局/常量及 5 份原发现/业务/状态/Qt 声明不变通过；24 项改动代码格式和差异检查通过。实际旧 SDK 0.1.4 Qt DLL 的原 SHA256 f4ec368234dd809e003517d6d7085060e31b19e2f61d0f376f655e55429991c6 未变，保存的同工具链描述仅 SDK 字段与新宿主不同；当前宿主在映射前拒绝且目录无副作用。宿主和同工具链插件须一起重建。源码清单、完整选择、原失败及通过日志、对象/头/SDK/路径结果均保存在 build/p5-m4b-*。

| 本段出口 | 实际证据与范围 |
|---|---|
| 物理结果及再次装载 | physical/file/reload/closed_facade：同进程动态 detach 恰一次、完整原 allocation 范围离开、本方文件保护释放；实际独占打开/改名/删除，重装新 ID、重新发现和业务 query/create/start/关闭 |
| 首次策略及未知保留 | resident/rejected/release_retry/external/query_failure/probe_other/probe_release_failure：默认 PIN/失败拒绝，真实原引用未消费可重试，真实外部 LoadLibrary 永久 pin，观测与外来 owned probe 的正确引用归属 |
| 真实资源退出 | session/instance/setup/cleanup/runtime/state/call/worker/qobject/capture/stale_capture/cleanup_failure/service：实际会话、实例、任务、工厂/状态契约、DLL 调用、完整 join/QObjectResource、DLL 捕获及服务生命周期；故障保持资源和 pin |
| 线程及原子门禁 | owner/owner_gone/last_lease/seal/reentry/create_reentry：首次真实身份、线程退出后拒绝、最后租约跨线程不卸载；实际 acquire/reset 竞争、成功后原子封门，同代重入拒绝且独立代可推进，调度器重入关闭后不读失效表 |
| 角色、预约及读者 | lineage_candidate/current/reservation/sibling/reader：真实 stage/预约/borrow，候选重装、新 Current 不提拔旧候选；Unloading 兄弟阻止新预约，来源与账目空后真实 reader 仍阻止卸载，解除后物理离开 |
| 资源与历史有界 | heavy/history：64 重代上限，真实卸载归还重资源空位；130 次真实装卸后保留 128 条纯历史，按目录的代次创建顺序淘汰最旧 Unloaded，失败代保留，ID 不复用、外部已复制值有效 |
| 兼容及边界改善 | 原 PIN/Qt 路径、五配置旧回归和旧 SDK 实物；原强缓存/永久文件持有已替换，重复路径查询删除并复用，没有第二目录、清 pin、最后租约自动卸载或未实现的预留入口 |

上述探测注入、线程 ID 和显式资源管理的证据边界按前文保留，未扩大为实际地址复用、自动线程追踪或 Qt 物理卸载。独立审查及最终要求映射未发现剩余提交阻断项；实现、验证和审计随本次提交归档，提交同步后才进入 M4-c。实际 ReplacementOperation 终结后的 Retired 回收、恢复期间保留和物理失败不改写业务提交由 M4-c 接续；M5 业务白名单、H01/H02、100 次真实替换及 UI/压力门禁仍未交付。
