# P5-M3 版本化业务状态迁移与恢复

架构基线日期：2026-11-06；收口日期：2026-11-17。实现基线：M2c5 `c8ae9a1`，已提交并同步。M3 实现、阶段审计与五配置验证完成，随本次收口提交归档；M4 白名单卸载和 M5 UI/H02 压力验收继续。

## 架构先行

已核对真实 Runtime、InstanceControl、NativeInstanceControl、QtBusiness、Scope 冻结/释放及宿主事务入口。决定和取舍见 [ADR-0049](adr/0049-versioned-replacement-state.md)。本次 M3-0 只交付架构基线、阶段出口及要求映射；没有新增迁移接口或改变桌面运行行为。

## 顺序与提交门禁

| 小阶段 | 改动边界 | 必须通过的出口 | 当前状态 |
|---|---|---|---|
| M3-0 | 架构基线与验收设计 | 对照说明书 §12、实际源码和 M2c5 结果，审查责任/生命周期/兼容及恢复语义；审计后提交同步 | 已完成，0a9a095 已提交同步 |
| M3-a | Core 通用状态契约、每 epoch 参与者与调用屏障 | 真实实例注册/释放、身份与预算、错误线程/过期 Context/重复注册、重入停止/Kernel 关闭、无状态兼容；公开头与 SDK 兼容核验 | 实现与阶段审计完成，随收口提交归档 |
| M3-b | Native 与 Qt 的可选扩展及真实适配 | 两后端真实 DLL 的描述/导出/校验/导入；原生纯 C 表与缓冲区门禁、Qt 独立 IID；缺失扩展旧插件兼容、协议故障与对象/引用清理 | 已完成，ebc2392 已审计、提交同步 |
| M3-c | 原宿主操作的快照/迁移/恢复管线 | 根/配置/动态子树/消费者按真实成员对应；提前拒绝、候选失败完整恢复、恢复失败保留、关闭/取消/来源故障；两后端及共享/静态支持矩阵；整 M3 需求逐项审计 | 实现、阶段审计和五配置验证完成，随收口提交归档；提交同步后进入 M4 |

M3-a 不把试验接口写进示例作为已完成迁移；M3-b 不自行创建第二个替换协调器；M3-c 必须删除适配过程中被替代的临时分支。三个实现阶段都必须先更新本记录中的具体设计再修改代码，代码审查、修复、实际验证、提交与远端同步完成后才能进入下一段。

## 完整 M3 的证据清单

以下是完整 M3 的出口；Core、后端和整体事务的实际证据分别见本页 M3-a、M3-b、M3-c 收口结论。旧配置重建不能代替业务状态恢复，阶段审计按下表逐项核对。

| 要求 | 必须取得的实际证据 |
|---|---|
| 版本化纯值与预算 | schema 标识/版本、空值与无状态、无效 UTF-8/JSON、单份与事务预算边界、数字原文；宿主缓冲区输出及无跨模块内存释放 |
| 一致快照 | 实际计数/内容修改后换代保持值；Busy 导出在停止前拒绝且旧树仍能工作；多个同 PluginId 实例保持不同状态 |
| 候选校验/转换 | 同版本仍校验、跨版本成功转换、schema 拒绝、转换失败；原始快照未被候选修改 |
| 真实导入屏障 | Native start/Qt activate 首次观察到迁移值；未导入、重复导入、错误成员/epoch、候选无状态吞掉旧值均拒绝 |
| 拥有树与配置 | 根、显式配置、动态子树、新增/明确删除成员、同代受影响消费者；候选/恢复使用各自阶段许可，拥有者不能跳过实际状态兑现 |
| 完整恢复 | 新代校验/导入/激活/消费者失败后，真实旧工厂和旧配置恢复原始状态与全部消费者；报告为 Restored，目录/目标/服务仍为旧状态 |
| 恢复失败 | 旧代导入/激活/消费者恢复失败分别可见，RecoveryFailed 不被提供者 Active 掩盖；闭包停止，资源与代次保留原因可核查 |
| 重入与中断 | 导出/校验/导入中取消、关闭 Kernel/Session、实际来源故障、拒绝投递及晚到回调；实例不在回调返回前销毁，阶段结果不越权提交 |
| 兼容及支持 | 旧无状态 Native/Qt、实际 SDK 版本拒绝、独立公开头、原 C ABI 布局；Release/Debug/ASan、静态 Core 关闭示例、无 Qt GCC 的相应当前源码验证 |
| 架构与清理 | Core 无上层依赖、无第二实例账目/解析器/提交点，适配回调随 epoch 清理，报告无可执行引用；审查应删除的旧实现及必要兼容路径 |

H01 的缺符号/激活失败也须保留实际证明；H02 连续 100 次、白名单物理卸载、UI 升级演示和最终随机压力继续由 M4/M5 完成，不提前宣称 P5 完成。

## M3-0 审计范围

检查对象为本工作单、ADR-0049、P5 总计划和进度入口，以及 ADR-0048 的过时“未提交”句。依据是当前真实源码及已同步的 `c8ae9a1`，本阶段不改 C++、CMake、SDK 版本或测试。文档审计须核对本地链接、阶段状态、与说明书要求的一致性及最终 diff；不以重复运行未改动的 M2 测试证明 M3 功能。

## M3-a 实施设计（已实施）

采用独立 `migration_state.hpp` 的纯值 StateSchema/StateSnapshot、不可变 StateContract 和每 epoch 的 StateParticipant，保留原 `state.hpp` 的诊断枚举。`Runtime::stateful` 复用原 Runtime 构造校验，避免新的重载歧义；`Context::state` 仅允许在 Loading 登记一次实际参与者，收到输入时先校验/转换、再导入，然后业务激活才能成功。参与者缺失或被忽略的导入错误必须成为激活失败，不能由用户返回 completedTask 掩盖。

初始状态作为可选的挂载输入，在 prepareMount 建立所有权时保存，不能在 stage 返回后再补写，避免依赖调度时序。直接 Instance 挂载、候选 Scope/Builder 和子实例创建共用这一输入机制；普通调用默认无状态输入。状态导出仅由 Core 私有访问器对 Frozen Scope 中的真实成员/epoch 执行，不给普通业务公开越过冻结的读取接口。

通用协议校验与参与者状态分离到 Core 私有实现，InstanceControl 只负责真实 epoch、调用进入/退出及延迟退役接线；清理不能在迁移回调尚未返回时释放资源。回调返回后先重验关闭/epoch/作用域状态，再接纳结果。参与者在主资源静止后、末尾所有者销毁前释放，永久停止时同时释放契约捕获。Core 只做结构/大小检查，不复制上层 JSON 解析器；单事务 16 MiB 总预算在 M3-c 宿主持有全部快照时实施。

公开 Runtime 布局和挂载签名改变，源码 SDK 从 0.1.1 升为 0.1.2；宿主及同工具链 C++/Qt 插件共同重建，原生 C ABI 不变。后端实际对象保护及协议错误仍由 M3-b 完成，本段不宣称已有两后端迁移。

分阶段兼容门禁：M3-a 新增的有状态实例不能沿尚未接入迁移的旧 M2 宿主操作被无输入重建。宿主预检通过 Core 私有只读访问器检查真实成员的状态契约，遇到有状态旧成员就在冻结/停止前明确拒绝并撤销预约；不从可能缺失的动态工厂快照猜测。此拒绝仅保护 M3-c 集成前的入口，M3-c 必须以真实导出/导入管线替换并将对应测试改为状态保持测试，不能把拒绝算作最终迁移能力。低层 Scope/Builder 仍允许受控调用方显式携带纯值状态。

## M3-a 收口结论

2026-11-17。Core 状态协议、每 epoch 参与者和重入清理屏障已完成实现、复查及当前源码验证。首版为 `26230ac`，本次收口修正与其一起纳入审计；阶段提交同步完成后才能开始 M3-b。完整 M3 仍须完成两后端扩展和宿主迁移/恢复管线。

| 当前源码配置 | 完整测试结果 | 范围 |
|---|---|---|
| MSVC Release，共享 Core/Ela | 1109/1109 | 串行全量，含 UI 与两后端宿主保护 |
| MSVC Debug，共享 Core/Ela | 1109/1109 | 全量 |
| MSVC ASan，共享 Core/Ela | 1109/1109 | 全量；核对实际 ASan 编译标志和关键对象 |
| MSVC 静态 Core、关闭示例/Ela | 802/802 | 当前配置全部可用测试，含原生宿主保护；无 Qt 业务后端 |
| 无 Qt GCC Debug、静态 Core | 413/413 | 当前配置全部测试 |

每组均逐项比对完整测试名称、通过记录及冻结的 20 项改动源码/构建输入，无缺项或重复计数，包含 21 项 Core 状态专项。GCC/MSVC 各 6 个公开头独立编译通过；原生 C ABI 文件与 M3-0 基线相同。ASan 的 instance.cpp、state.cpp、replacement/preflight.cpp 实际对象已核对编译命令、时间和 SHA256，全量构建/测试后哈希不变。清单、日志和哈希记录保存在 build/p5-m3a-*，审计映射见 [代码审查](code-review.md#p5-m3-a-收口复查通过)。

| M3-a 出口 | 实现与实际证据 |
|---|---|
| 不可变契约、实例独立值、版本转换、原值保持 | Runtime::stateful / EpochState；state_schemas、state_roundtrip |
| 结构和单值预算；无状态区别于 JSON null | mount 前检查、state.cpp；state_inputs、state_capture、state_converted_budget；JSON 语法和事务总预算由后续宿主承担 |
| 导入先于业务部分；错误不能由 completedTask 掩盖 | Context::state / 激活就绪检查；state_missing、duplicate、invalid_participant、restore_fail/throw、validate_fail/throw |
| 真实实例/epoch、线程与权限 | Frozen Scope 私有访问、prepareMount 初始输入；state_guards、state_children、两后端 operation_state_guard |
| 重入关闭不提前释放资源 | 实例唯一调用屏障与延迟 retire；state_reentry_capture/restore/validate/register |
| 参与者与契约生命周期 | 主资源静止后、末尾 finalizer 前解除参与者；永久停止清空未用输入；state_lifetime、state_waiting、state_unstarted |
| SDK 兼容与旧无状态行为 | 0.1.2 全目标重建、legacy_state_sdk/legacy_sdk 预检和五组全部旧回归；额外以真实旧 0.1.1 DLL 证明仅 SDK 字段不符时映射前拒绝 |
| 架构边界与删除 | 通用协议在 state.cpp，真实实例仍为唯一身份账目；三处重复异常处理归并到唯一调用边界；不新增 JSON 解析器或第二个提交点 |

审计修正：未激活的永久停止实例显式清空 stateInput；导入异常被就绪检查覆盖的问题严格断言修复前 0/1，现保留首个 provisionError；旧宿主入口保护真实两后端修复前 0/2，现冻结前拒绝并核对旧实例/配置和预约解除。测试同时修复局部变量遮蔽及把子实例误作 Kernel 顶层实例查询的问题。兼容保护将在 M3-c 被真实状态事务替换，不能作为最终迁移能力。

Release 并行全量曾有三项失败：kernel_driver_lifecycle、observation_deadline_race 超时，workbench_card_area_lifetime 可见性断言失败。保留原失败日志；相同二进制和原超时设置下，三项串行复测通过、各重复 10 次通过，随后完整串行 1109/1109 通过。未改超时、删测或修改生产代码来掩盖结果；共同根因未被证明，不将重跑成功写成并行首轮通过。其他四组各自完整轮次通过。

## M3-b 实施设计（进行中）

基线 `3c534ca` 已提交并同步。原生在实际映射代次中查找独立可选 `cordis_query_state_v1`，缺失保持无状态；存在但协商失败不能静默降级。新表不改已有 plugin/host V1 的大小或 capability。每 PluginId 的 describe 返回固定容量 schema（可见 ASCII 标识与非零版本），UNSUPPORTED 明确表示该插件无状态；校验/转换和导出写入宿主一次分配的至多 1 MiB 缓冲区，不能通过两次导出探测长度。输入只在调用期间借用；PENDING、未知状态、输出指针/容量/头被改写、越界长度或非法 JSON 均为协议故障。正常 Busy/业务拒绝与协议故障分开处理，失败输出长度必须为零。

Qt 使用独立可选工厂/实例 IID，复用同一 C POD schema/buffer 和有界值校验，不传旧 QObject 或 STL 状态对象；现有 IQtPluginV1/IQtInstanceV1 不追加虚函数。缺失扩展的旧 0.1.2 Qt 插件保持无状态；独立新增接口不改变已有 Core/Qt V1 布局，当前无需再次变更 SDK 二进制契约版本。

值边界代码加入已有 ModuleRequests 的 ABI/JSON 私有支持层，原生/Qt 适配共用，不另建解析器。Core 的纯值校验声明从 EpochState 私有头分离到 state_value_p.hpp 供其复用，仍沿既有 Module→Core 依赖，不暴露 Core 实例控制或新增公共 API。具体后端调用必须持原 Call 引用，并在原 NativeInstanceControl/QtBusiness 中登记短期迁移调用，直接关闭也不能在调用返回前销毁对象；离开回调后重验关闭和来源健康。不能只持 Core 的迁移屏障或 generation Call 租约就宣称对象存活。

原生在 createBound 后、start 前，Qt 在实际 QObject 收养/接口核对后、activate 前登记 Context::state；Core 同步导入成功后才执行真实业务入口。契约校验闭包及参与者分别保留真实工厂/实例来源引用，清理仍复用原请求、effect、实例来源和 QObjectResource 屏障。协议失败沿原 quarantine/fault 及替换来源通知处理，不伪报已销毁。M3-c 的旧宿主保护门禁本段继续保留。

本段必须以真实 C DLL/Qt 插件验证扩展描述、导出/导入顺序、缺失扩展兼容、缓冲区和协议故障、重入直接关闭及引用释放，再按支持矩阵审计提交同步。以下保留各轮验证范围，最终阶段出口以收口记录为准，未提交前不进入 M3-c。

值边界首轮 MSVC Release 26/26 通过：21 项 Core 状态回归、5 项 state_wire 测试，后者同时编译独立 C11 的大小/偏移断言。覆盖可选表的版本/必需函数、固定 schema 往返、一次宿主缓冲区输出、1 MiB 边界、整数原文保持、非法 UTF-8/JSON、输出指针/容量/头篡改、越界长度、PENDING/未知状态、异常和普通 Busy/拒绝。状态扩展使用独立版本与预算常量，不意外绑定业务主表的版本演进；当前新声明不改现有 V1 表。此证据只证明值层，不替代真实 DLL 和两后端生命周期验证。

原生接线细化：独立查询来自 acquireNativeSource 的实际映射，扩展表与每个实际 PluginId 的描述在首次 open 协商窗口中一并复制到原 BusinessRecord，复用 negotiating/Call 保护，后续 Runtime 只读取本代不可变描述，不反复调用 describe 或增加实例目录。StateContract 与激活闭包共享原 RuntimeOriginControl；参与者由私有 NativeStateAccess 从实际 NativeInstance 构造，既用于生产 Runtime，也允许真实原生实例的适配测试，不公开新低层状态业务 API。

同步状态调用使用宿主栈上的短期门禁，同时持有原 root/instance 与 Call 租约。root 的关闭完成等待状态调用返回；instance 的 close 先关准入，整个取消/清理入口延迟至状态回调返回，不能仅延迟 destroy 而允许 stop/cancel 在回调中重入。原 close 剩余逻辑收敛到 beginClose，由普通关闭和状态调用退出共用，避免复制两条关闭路径。协议故障保留原 quarantine/fault 来源通知；普通 Busy/拒绝不隔离。工厂校验在 Core 的初始状态准备中执行，创建后绑定参与者并同步导入，成功后才 start。真实 DLL 将验证导入先于 start、原始值导出、失效结果拒收及直接 close 的完整等待。

原生首版已接线，真实 C DLL 新增 29 个用例：覆盖首次协商与描述缓存、旧无符号 DLL、每插件声明无状态与查询拒绝的区别、真实异常/PENDING/无效输出、旧 schema 拒绝、大整数原文、导入先于 start、工厂/实例/root 的直接关闭、错误线程与重入，以及两个工厂闭包共享来源引用后真实释放。关闭回调中直接检查真实 DLL 的 stop/destroy 计数尚未增长，返回后才完成关闭；协议错误检查具体 nativeStateUncertain/nativeInstanceUncertain 保留原因、实际映射与 businessFailure，不能用默认驻留 pin 冒充故障保留。

本轮指定目标重建后，Release 选中兼容/原生回归 240/240、ASan 160/160，通过名称与当前选中清单一致，两组均包含 29 个新真实 DLL 用例；过程清单、日志及输入哈希保存在 build/p5-m3b-*。这不是整阶段五配置全量矩阵，Qt 状态扩展及其对象存活适配尚未实现，M3-b 继续，不提交、不进入 M3-c。

Qt 接线细化：可选 IQtStatePluginV1/IQtStateInstanceV1 采用独立 IID，方法只借用现有状态 C POD；描述随原工厂首次协商缓存到 QtResidency，校验闭包与激活闭包共享原 QtDefinition/RuntimeOrigin。Qt 状态适配独立放入 qt_state.cpp，复用原代次健康检查、Call 账目与协议故障通知；QtBusiness 只负责在收养对象后、activate 前绑定参与者，不增加状态对象目录。

Qt 对象寿命不能仅靠延迟 QtBusiness::finalize：aboutToQuit 也会直接调用 QObjectResource 的关闭入口。为此在原 ObjectControl 上增加仅宿主私有的栈式 ObjectCall 保护；进入必须为 owner、对象存活且尚未关闭，关闭立即失效，但 deleteLater 只在最后一个调用退出后投递。现有完整析构确认路径保持唯一，公共资源句柄布局不变。状态调用同时持此保护与真实 generation Call，禁止同工厂状态重入；回调返回后重新检查调度器、root、资源关闭/存活和代次健康。外部 delete/deleteLater 仍属原契约禁止行为，不把检测到的意外销毁伪报为正常清理。真实 Qt DLL 必须覆盖嵌套事件、Core 停止和应用退出请求期间的存活，以及协议故障保留与普通拒绝清理。

Qt 首版及复查：新增 30 项测试，其中 29 项使用实际 Qt DLL，另一项直接检查原 QObjectResource 的多调用保护和唯一完整删除回执。覆盖独立 IID/描述缓存、旧无扩展工厂、状态输入先于 activate、多个实例实际不同值、普通拒绝和协议失败、状态方法中停止/退出并嵌套处理延迟删除、错误线程、跨实例状态重入，以及正常停止后 RuntimeOrigin 释放。复查发现失败状态码不能跳过对象健康复验：真实 DLL 在 capture 中违规 delete this 并返回 Busy，修复前 0/1；现连同 parent/线程核对及正常失败后的来源重验一起修复，不能把对象已失效误报为可重试 Busy。Native 同步使用此规则，新增直接关闭并返回 Busy 的回归；协议故障仍优先保留原故障。

两后端专项和相邻旧 Qt 工厂回归 Release 94/94 通过（Native 30、Qt 状态 30、值层 5、旧 Qt runtime/reentry 29），未以该选中集合替代整阶段验证。随后按冻结的源码/构建输入完成五配置整阶段矩阵，结果见下节；M3-c 状态事务尚未接入。

## M3-b 收口结论

2026-11-17。实现、代码结构、错误处理和资源所有权复查及五配置验证已完成，本提交归档；提交同步后进入 M3-c。31 项改动源码/构建输入在整阶段构建前冻结；C++/C 格式和差异空白检查通过。原生与 Qt 只共用纯值边界，各自实际对象的关闭机制仍由对应后端持有。Core 只分离原有纯值声明，没有引入 Module/Loader 依赖；旧无状态路径用于兼容无扩展插件，M3-c 前的有状态整体替换保护仍须保留。

| 配置 | 当前源码完整测试 | 范围 |
|---|---|---|
| MSVC Release、共享 Core/Ela | 1174/1174 | 全部可用测试，含 Native/Qt 各 30 项状态测试 |
| MSVC Debug、共享 Core/Ela | 1174/1174 | 同上 |
| MSVC ASan、共享 Core/Ela | 1174/1174 | 同上；7 个实际关键对象的地址检查和异常模型已核对 |
| MSVC Release、静态 Core、关闭示例/Ela | 837/837 | 全部可用测试，含 Native 状态；无 Qt 业务插件后端 |
| 无 Qt GCC Debug、静态 Core | 418/418 | 本配置完整测试，包含共用状态值边界与 Core 回归 |

五组均逐项比对 CTest 完整名称、实际通过记录及冻结输入，不合并早期选中回归冒充整阶段验证。6 组公开头独立编译及 4 项既有协议文件不变检查通过；ASan 的 state_wire/native_state/qt_state、host/runtime/qt_runtime 和 QObjectResource 实际对象在测试前后哈希一致，Module 的 C ABI 异常边界保留 /EHs。过程与汇总保存在 build/p5-m3b-*。本段只交付两后端适配，事务总预算、全部成员状态兑现、候选转换与原值恢复仍由 M3-c 完成。

复现完整矩阵时分别对上表构建目录执行 cmake --build <目录> --parallel 2，再执行 ctest --test-dir <目录> --output-on-failure -j 1。原无状态功能和 M3-a 的 21 项 Core 状态回归全部保留；未修改 UI 行为，不新增人工桌面验收门禁。

## M3-c 实施设计（进行中）

基线 ebc2392 已提交同步。核对现有 Execution、DefaultBuild、IReplacementOwner、Context::inject、实际 InstanceControl/EpochState、Scope 的 staged 账目和共同提交路径后，确认只给 ReplacementConstruction 添加快照再在末尾检查 Active 不足以证明正确成员已导入；拥有者还可能省略输入、错配 key、重启到另一 epoch，或沿普通 Context::inject 冷启动。此次先建立创建准入和导入回执，再接入宿主快照管线；不新增替换协调器或 instance→对象目录。

职责与数据来源：Execution 的迁移存储只持 Frozen Scope 实际成员的独立原始快照，以真实 InstanceId/epoch 对应原计划的 previous；保留原值供候选和恢复共用，候选校验不覆盖原值。Core 只持纯值输入、阶段准入及字节预算，不解析 JSON、不识别 Module/Loader。Native/Qt 继续使用 M3-b 的原工厂/对象门禁。历史 ReplacementReport 不包含快照、工厂或准入引用。

在 freeze 与 stop 之间增加明确的 Capturing 阶段。逐个实际成员同步导出，校验结构、UTF-8 JSON 与预算；对已知候选和恢复工厂提前执行同版本/跨版本校验，确保原值及单次转换能共存于预算。每次外部调用后重验取消、Kernel/Session、投递门禁和候选/恢复来源。普通 Busy/不兼容或候选独有失败在停止前释放 Frozen Scope，旧树继续工作；旧来源故障或关闭则沿原真实停止/保留路径处理，不能假装旧树健康。所有快照成功后才进入原 Stopping 顺序。

创建采用原 ReplacementBuilder 的按成员 key 方法，分别覆盖根 stage 和实际父 Context 下的 inject。宿主每次打开候选/恢复发布组后，按本阶段完整计划安装 Core 纯值准入：key、parent key、可选原始输入和预算；这些是未来创建权限，不保存业务对象或另一套实例。准入与原 Scope/发布组绑定，每个 key 只可消费一次，过期、跨阶段、错误父成员和重复使用在业务创建前拒绝。实际实例只在原 InstanceControl 上记弱准入身份及首次 epoch；导入成功和就绪仍由原 EpochState 兑现。Core 准备提交和宿主映射核对均检查真实实例、成员 key、epoch 和状态兑现，不能由拥有者口头报告替代。

有状态整体操作要求每个成员走上述准入，普通 stage/Context::inject 不能绕过输入。无状态操作保留旧拥有者与低层显式 StateSnapshot 的兼容路径。IReplacementOwner 的 preflight 增加明确的状态重建支持声明；整体闭包含有状态旧成员时，每个参与的拥有者均须支持，否则在停止前拒绝，包括自身只管理无状态成员的拥有者。声明只用于提前拒绝旧拥有者，真正兑现仍由 Core 创建和回执检查保证。默认根/显式配置重建由平台统一改用按 key 方法；动态拥有者必须使用相同方法，新增成员使用明确的空输入，候选明确删除的旧成员不要求重建，恢复计划仍覆盖全部旧成员。

状态预算与所有权：一个操作共享 16 MiB 的已持有 JSON 字节预算，原始快照及尚未完成导入的转换值一起计数，单份仍为 1 MiB；schema 和固定描述不计为 JSON 字节。原值用宿主独立拥有的不可变存储共享到候选/恢复，不复制一份随计划流转。Core 在转换后保留结果前取得预算，成功导入、失败退役或销毁时释放；适配器单次最多 1 MiB 的临时线缓冲区为有界调用开销，不重复当成持有快照。纯字节预算/租约私有实现位于 Core，宿主与 EpochState 共用，避免两个计数器。正常终结释放全部值，RecoveryFailed 随真实执行保留原值与必要引用，不伪报预算清空。

宿主快照附带固定生命周期的纯值格式谓词（原 ModuleRequests 的 validAbiJson 函数地址），供 Core 在持有转换值前再次调用；不增加 JSON 解析器或 Core 对 Module 的链接依赖。该谓词只由私有宿主入口提供，不持有业务闭包，普通 Core 挂载保留原结构/大小检查。这样未知动态内置工厂返回坏 JSON 时，也在业务激活前拒绝，而不只依赖 Native/Qt 的线边界。

构建和失败恢复继续使用原 sequence、发布组、阶段许可、来源观察和最终 Core/Module/Driver 共同提交。实际候选 Runtime 在激活前再次校验原值并导入，未知动态工厂必须在创建路径验证，不能用 preflight 契约替代。候选失败先完整 discard，再以旧工厂、旧配置和原始快照重建全部成员及消费者；恢复任一校验/导入/激活/消费者失败均保留 RecoveryFailed。旧有状态兼容拒绝门禁只在上述管线接入并有真实回归后删除。

公开拥有者声明和阶段枚举涉及 C++ SDK 契约，源码版本提升到 0.1.3，宿主和同工具链插件一起重建；继续保留原 C ABI 与 Qt 状态 IID。阶段出口包含真实两后端多实例状态、动态子树/配置/消费者、总预算边界、提前拒绝后旧业务、完整恢复及恢复失败、关闭/取消/来源/投递故障，最后执行支持矩阵、整 M3 要求映射和审计，提交同步后才进入 M4。下面各轮仅记录实施进展，不能提前标记 M3-c 完成。

Core 首轮进展：共享预算、不可变原值、转换持有期和固定纯值格式检查已接入原 EpochState，提前 Runtime 校验共用同一值验证函数；按成员 key 的 stage/inject 接到原创建路径，实际 InstanceControl 只记录弱准入和首次 epoch。对外用于提交核对的回执只含 operation/key/epoch 纯值，不返回原始快照或许可指针；Core 准备提交按当前发布组检查全部实际成员。普通无状态路径保持原行为，操作发布并释放后可以继续正常 inject。

首轮复查修正三点：提前校验回调释放自身 Runtime 会使契约捕获提前析构，新增 factory_lifetime 修复前 0/1，现持局部契约引用至调用结束；原 staged 账目包含前一轮已丢弃实例，状态检查改为仅使用当前发布组并在新组重建准入；释放后普通 inject 被新准入误拒绝，roundtrip 新增发布后创建回归修复前 0/1，现已解除本操作结束后的限制。原释放路径只增加纯值准入清理，未复制取消/实例/提交账目。

Core 首轮 Release/ASan 各 170/170、GCC 42/42 通过：新增预算 9、成员准入 12，保留原 Core 状态 21；前两组还包括共用值层 5、Native/Qt 状态各 30、旧 Qt runtime/reentry 29，以及 Builder/Stage/Commit/Inputs 34。这一轮尚未接入宿主管线，不能当作整阶段支持矩阵。

随后宿主首版已接入：原拒绝全部有状态成员的临时分支删除，改为实际 schema 收集、拥有者 stateAware 提前核对，以及有状态旧成员到无状态已知工厂的明确拒绝。冻结后保存不可变原值并提前校验两条路线，停止后为原默认重建/拥有者安装同一成员准入，末尾按真实回执核对映射。普通提前拒绝释放 Frozen Scope，旧来源故障沿真实停止并保留；原 discard/recover/共同提交路径保持唯一。原两后端 operation_state_guard 已替换为 operation_state_preserved，检查真实配置子成员的导入函数及原值；不再把拒绝迁移当作功能。

SDK 已升为 0.1.3，Release 宿主操作与预检首轮 176/176 通过（157 项操作、19 项预检）。另保存升级前实际 SDK 0.1.2 Qt DLL，SHA256 为 25c95a2d6b2a6bbbcb89fdda2ccc6a5e1ea41f4ebc4061fa2d616e7a393b0190；用新版预检进程实际验证其在映射前拒绝，原产物及描述保存在 build/p5-m3c-sdk012-artifact。当前仍缺真实有状态 Native/Qt DLL 的整体迁移/恢复故障、多实例与动态子树/消费者、总预算和中断出口的完整证据；最终五配置矩阵、整 M3 审计及提交同步尚未完成，M3-c 未提交。

SDK 0.1.3 指定目标重建后，Core/后端适配/整体操作/预检联合选中 Release、ASan 各 346/346，通过名称与选中清单逐项一致，34 项当前源码/构建输入哈希核对通过，记录在 build/p5-m3c-first-*。该集合的整体操作仍主要使用旧无状态 DLL，新增的两项 state_preserved 验证实际内置状态子成员；两后端状态 DLL 的 30+30 是 M3-b 适配回归，不能拿这些数量替代版本化有状态 DLL 的整体替换/恢复证明。下一步补实际版本化状态产物和上述完整出口，再进行最终矩阵与阶段审计。

真实产物验证设计：复用原 C/Qt 状态夹具的同一个实例实现，默认仍为 schema 2；独立候选产物声明 schema 3 和模块版本 2.0.0，整数迁移以原文追加零实现，明确检查实际 start/activate 首次看到转换值，恢复必须重新看到 schema 2 原值，不能把提前校验结果再当旧值转换。每个实际映像使用独立故障模式/探针，探针沿原代次取得 Call，不建立测试替身实例目录。验证多实例不同值、提前拒绝后旧实例继续工作、候选失败原值完整恢复、恢复失败保留、重入取消/关闭和协议故障；Qt 同时检查完整 QObject/子对象销毁。

动态树与预算验证使用原整体操作入口及宿主拥有者：状态属于真实 Core 子成员，拥有者按计划 key 通过原 Builder 重建；分别覆盖声明不支持、声明后绕过、错配父/key、明确删除/新增、消费者恢复和总 JSON 预算边界。此类夹具证明协调器的树/预算规则，与两后端有状态 DLL 用例互补，不替代后者。生产管线另作独立结构与故障路径复查；所有改动汇合并冻结后才进行构建，阶段审计和提交同步仍由完整 M3-c 出口决定。

删除边界补充：核对 planConstruction 后确认，省略动态描述会作为遗漏契约被拒绝，已有 API 实际不能表达明确删除。ReplacementOwnerPreflight 因而增加仅用于候选的 removed key 列表；只允许删除已有的不透明动态子成员，不允许用它删除平台根/配置成员或新成员，重复 key、与未来描述冲突、删除父节点却保留其后代均拒绝。原计划归一化函数统一处理此声明，不增加一条构建或提交路径；恢复路线不接受删除并继续保留全部原始快照。SDK 0.1.3 当前尚未发布，该公开声明与 stateAware 一起纳入最终兼容验证。

独立复查补充三项边界修正设计：状态准入以整个操作为边界，拥有者支持声明的检查也必须以整个闭包为边界，避免混合无状态旧拥有者在停止后才发现两条创建路线都不可用。捕获检查点须分别复验候选与恢复来源，因为外部借用代次可以只在恢复计划中出现；捕获期间不复用只处理 Building/Recovering 的异步来源观察器来证明健康，错误仍由原 rejectCapture 判断旧来源能否继续工作。原值占满 16 MiB 时，任何非空转换均无法与原值同时保留，停止前应尝试取得并立即释放最小 1 字节转换空间；这只排除必然失败的边界，未知动态转换的实际大小仍须在真实创建时逐项计费，不推断完整恢复一定成功。

最终要求复核仍须直接证明两个组合边界：沿原配置会话夹具，将实际有状态子成员的 capture/restore 回调接到同一 Session.close，检查会话屏障等待回调退出，并沿原取消/恢复顺序结算；不增加关闭控制器。沿原动态树夹具令候选消费者和恢复消费者各失败一次，证明旧提供者即使已导入原值并 Active，也不能替恢复消费者成功，结果须保留 RecoveryFailed；只增加夹具的明确失败次数，不复制重建流程。

整体验证首轮：40 项真实 Native/Qt 双版本状态 DLL 操作及 cold_epoch 通过。混合拥有者提前拒绝两后端修复前 0/2；修正后两项通过。预算测试名称最初含数字，被已有入口的 case 名规则拒绝，已改为 boundary/full/over；随后满预算与仅恢复外部来源故障两后端严格断言修复前 0/4，日志保存在 build/p5-m3c-e2e-before-*、budget-before-*、source-budget-before-*。早期闭包内 peer 用例在初始 collectBorrowed 已同时标记两路线，不能作仅恢复来源的反例；新增闭包外 provider 用例直接核对生产 requiresGeneration 的候选 false/恢复 true，再触发实际 DLL 协议故障。当前上述三项生产修正已实施，仍待统一复测。

Qt 大树测试暴露测试等待器吞吐限制：原每次 processEvents 只处理当时事件，随后 Win32 Sleep(1) 实测 100 次约 1.295 秒；QtScheduler 每个唤醒只推进一项，正常 15 子树在 5 秒任务等待内被测试休眠限速。等待器改用有界 1 毫秒事件泵以处理期间新投递事件，仍保留原 5 秒任务限制与 CTest 限制，调度器及业务逻辑未改。相同二进制逻辑重建后 15 子树预算成功从超时变为通过，完整场景约 1.27 秒；满预算失败此时能到达实际业务断言，不能把原测试入口/等待器错误算作生产缺陷证据。

完整构建暴露测试入口的结构边界：Release 与静态 Core 构建通过，Debug 的 module_reservation.cpp 在纳入新增状态树模板后超过 COFF 对象节数限制。状态树场景不再继续堆入原预约/输入/配置入口的同一翻译单元：提取原真实 Scene 到共用测试头，新建状态树实现翻译单元，由非模板函数桥接原分派；装载目录、实际 ModuleManager 和 Kernel 创建路径保持共用，不复制 Scene，不改变测试名称、断言、超时或生产代码。此结构调整优先于给整个目标增加 /bigobj；调整后重新冻结输入及构建验证。

## M3-c 收口结论

2026-11-17。成员准入、独立原值、共享预算及原宿主捕获/迁移/恢复管线完成实现和独立结构复查；新增组合用例首轮 Release 75/75 通过。测试翻译单元拆分后，修正原夹具依赖隐式传入 iostream、atomic、QtConfigurationWriter 和 NativePlugin 完整定义的问题，依赖改由使用它们的头显式包含。最后冻结 51 项源码/构建输入，以下均为当前源码整配置构建及串行全量测试，清单名称与实际通过记录逐项一致。

| 配置 | 全量结果 | 支持范围 |
|---|---|---|
| MSVC Release，共享 Core/Ela | 1271/1271 | Core、Native/Qt、配置、工作台及全部可用回归 |
| MSVC Debug，共享 Core/Ela | 1271/1271 | 同上；拆分后的测试入口正常编译 |
| MSVC ASan，共享 Core/Ela | 1271/1271 | 同上；11 个实际关键对象的地址检查/异常模型及前后哈希一致 |
| MSVC Release，静态 Core，关闭示例/Ela | 897/897 | 本配置全部测试，含 Native 状态整体操作；无 Qt 业务插件后端 |
| 无 Qt GCC Debug，静态 Core | 440/440 | 本配置全部测试，含 Core 状态、预算、成员准入及共用值层 |

完整 M3 的要求映射如下；以实际断言证明行为，未用新增用例数量代替需求完成。

| 要求 | 当前实现与实证 |
|---|---|
| 值、schema、JSON/UTF-8、单份与总预算 | state_schemas/inputs、state_wire_*、state_budget_*；两后端 state_tree_budget_boundary/full/over 验证 15 份原值与转换恰达 16 MiB、满额/超额停止前拒绝 |
| 实例独立值、转换和原值恢复 | state_operation_native/qt_roundtrip；真实两个同 PluginId 实例保存 11 和 9007199254740993，schema 2→3 原文转换，首次 start/activate 观察新值；恢复重新导入原始 schema 2 值 |
| 停止前拒绝和恢复失败 | 两后端 busy/validate_reject、候选 restore/start/activate 失败与 recovery_validate/restore/start/activate；核对旧 ID/epoch/可用性、真实映射、角色、具体故障 pin 和调用/实例租约 |
| 实际成员和首次 epoch 兑现 | state_admission_* 覆盖普通创建绕过、遗漏/重复/错误父、过期许可；cold_epoch 通过真实依赖退役/重新发布进入 epoch 2，观察冷启动值 0，拒绝旧回执和提交 |
| 配置、拥有树、删除与新增 | 两后端 operation_state_preserved、state_tree_roundtrip/delete/delete_recovery；配置子成员实际导入 42、不同动态成员保存各自值；明确删除 beta、新 gamma 无输入启动，恢复仍重建 beta；非法父/配置删除拒绝 |
| 消费者完整恢复与恢复失败 | state_tree_recovery、recovery_consumer；候选消费者失败后原值和依赖一起恢复；恢复消费者再次失败时，旧提供者即使已导入并就绪仍报告 RecoveryFailed、无映射、保留预约 |
| 取消、会话/Kernel/调度关闭与拒投 | 真实 DLL 的 cancel_*、kernel/close/scheduler_capture，operation_session_close_state_capture/restore；回调内检查屏障未越过、退出后清理，复用 owner_delivery 及晚到回调门禁 |
| 来源故障 | 状态协议故障/异常、state_tree_source/source_recovery；后者核对闭包外真实来源的候选 false/恢复 true，再触发实际协议故障并在重建前保留失败 |
| 兼容、布局、H01 | 旧无扩展 Native/Qt、legacy_migration_sdk、实际保留的旧 SDK 0.1.2 DLL；11 组独立公开头，4 份既有协议声明不变；native_incompatible/host_missing 和真实激活失败保留缺符号/失败证明 |
| 架构、所有权与删除 | Core 无 Module/Loader/Qt 依赖；共用原值校验与唯一字节预算，真实 InstanceControl/EpochState 为唯一身份；历史报告无执行引用；临时状态迁移拒绝分支已删除，必要无状态兼容有明确边界；测试共用 Scene，状态树独立编译 |

11 组公开头独立编译通过，原 plugin.h、Qt 工厂 V1、migration_state.hpp 和 state.h 与 M3-b 一致。实际旧 SDK 0.1.2 DLL 的 SHA256 仍为 25c95a2d6b2a6bbbcb89fdda2ccc6a5e1ea41f4ebc4061fa2d616e7a393b0190；与当前工具链描述仅 sdkVersion 不同，新宿主在映射前拒绝且目录无副作用。C++ SDK 为 0.1.3，宿主和插件一起重建，原 C ABI/状态 IID 不变。

最终 41 项 C++/C 格式、差异空白、本地文档路径、51 项输入及实际 ASan 对象复验通过。整配置日志、完整名称、哈希和验证结果保存在 build/p5-m3c-{release,debug,asan,static,gcc}-*，ASan 对象记录为 p5-m3c-asan-objects.json，SDK 旧产物验证为 p5-m3c-sdk012-final-verified.json。早期失败和选中回归日志保留，不能将它们改写为首轮全量通过。

未发现阻止 M3-c 提交的剩余问题；实现与阶段审计随本次提交归档，提交同步完成后进入 M4。M4 的白名单物理卸载、M5 的 UI 升级演示、H02 连续 100 次及最终压力验收继续，M3 收口不代表 P5 已完成，也不新增未改动 UI 的人工验收门禁。
