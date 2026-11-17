# P5-M3 版本化业务状态迁移与恢复

日期：2026-11-06。实现基线：M2c5 `c8ae9a1`，已提交并同步。M3 尚未完成，不计入 M4 白名单卸载或 M5 UI/H02 压力验收。

## 架构先行

已核对真实 Runtime、InstanceControl、NativeInstanceControl、QtBusiness、Scope 冻结/释放及宿主事务入口。决定和取舍见 [ADR-0049](adr/0049-versioned-replacement-state.md)。本次 M3-0 只交付架构基线、阶段出口及要求映射；没有新增迁移接口或改变桌面运行行为。

## 顺序与提交门禁

| 小阶段 | 改动边界 | 必须通过的出口 | 当前状态 |
|---|---|---|---|
| M3-0 | 架构基线与验收设计 | 对照说明书 §12、实际源码和 M2c5 结果，审查责任/生命周期/兼容及恢复语义；审计后提交同步 | 已完成，0a9a095 已提交同步 |
| M3-a | Core 通用状态契约、每 epoch 参与者与调用屏障 | 真实实例注册/释放、身份与预算、错误线程/过期 Context/重复注册、重入停止/Kernel 关闭、无状态兼容；公开头与 SDK 兼容核验 | 实现与阶段审计完成，随收口提交归档 |
| M3-b | Native 与 Qt 的可选扩展及真实适配 | 两后端真实 DLL 的描述/导出/校验/导入；原生纯 C 表与缓冲区门禁、Qt 独立 IID；缺失扩展旧插件兼容、协议故障与对象/引用清理 | 实现、阶段审计及五配置完整验证通过，随本次提交归档 |
| M3-c | 原宿主操作的快照/迁移/恢复管线 | 根/配置/动态子树/消费者按真实成员对应；提前拒绝、候选失败完整恢复、恢复失败保留、关闭/取消/来源故障；两后端及共享/静态支持矩阵；整 M3 需求逐项审计 | 待实施 |

M3-a 不把试验接口写进示例作为已完成迁移；M3-b 不自行创建第二个替换协调器；M3-c 必须删除适配过程中被替代的临时分支。三个实现阶段都必须先更新本记录中的具体设计再修改代码，代码审查、修复、实际验证、提交与远端同步完成后才能进入下一段。

## 完整 M3 的证据清单

以下是完整 M3 的出口；Core 已有证据见本页 M3-a 收口结论，后端及整体事务仍待完成，不能引用 M2c5 的旧配置恢复用例代替。

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
