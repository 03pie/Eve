# 提交前代码审查记录

## P5-M2c1 实际服务图与替换闭包提交前审核

日期：2026-10-31。基线 e92fd5e。由当前代理审核，未使用子代理。先核对实际所有权、依赖方向、声明/committed 生命周期和原生身份，再在同一服务索引上增加纯值图与独立闭包查询。Kernel 使用实际子句柄核对归属；Core 接纳标记和原生编号不能代替 Kernel 所有权证明。未增加第二套注册表、模块反向依赖或替换执行占位入口。

复审修复两项 P2：服务调用投递阻塞未反映在实例健康中，闭包误接受；只复用必供输出的原环预检记录，漏掉等待可选输出的消费者。delivery、pending 均修复前 0/1，修复后通过。可选输出只补充图元数据，原预检语义保持。另将消费者遍历从常规 BindingSnapshot 移到专用 ServiceBindingSnapshot，普通快照声明和实现与基线完全一致，避免扩大每次服务调用的诊断开销。

遗漏实际/等待/子实例边的负向控制 0/3，破坏实际绑定优先级 0/1；原字节恢复后 17/17。最终结构重建后的 Release/Debug/ASan 全量各 786/786，GCC 286/286；关闭示例 P4/P5 317+1、静态兼容 248+1，恢复原 Release ON/ON 后补验 P5 80/80。首轮 Release 一项既有 UI 测试超时，保持源码/20 秒预算单项连续 10 次通过，随后完整重跑通过；失败和环境停顿提示分别记录，未确诊或掩盖原因。调整前的布局验证不计作最终结果，详见 [M2c1 记录](p5-m2c1-development.md)。

18 项源/构建哈希、8 条实际 ASan 命令与对象、两编译器各三个独立头、无 Qt 编译/实际依赖图、验收引用、编码/格式/文档及只读边界审计通过。未发现阻止本小阶段提交的问题，随本提交归档同步；排他事务、定义与目录切换、迁移/恢复、白名单卸载及 H01/H02/S09 仍按 [P5 原范围](p5-development.md)继续，见 [ADR-0044](adr/0044-replacement-dependency-closure.md)。

## P5-M2b Core 服务发布组提交前审核

日期：2026-10-31。基线 42269bd。由当前代理审核，未使用子代理。先检查索引、键冲突、依赖图、激活与清理、Kernel 归属及原生桥所有权，再引入同一索引的受控发布组。私有结构抽到 service_p.hpp，组提交/放弃位于独立 publication.cpp，Core 仍仅直接依赖 Threads；没有第二套 Registry、realm 或模块依赖。

核对 Held 可见性、组内真实调用与失败门禁、子实例成员和顶层重挂载归属、旧键不抢占、异步调用/committed/清理失败保留。原生桥按实例持有组内视图，模块共享规范索引不携带实例权限。复审修复两项 P2：通知投递失败误报成功、通知重入重赋句柄后写错发布组；真实回归修复前失败，修复后通过。公开状态与通知错误分别保留，操作强持控制块，重复提交不能掩盖交付失败。

取消视图/成员门禁及恢复模块共享视图错误写入的负向控制 0/3，原字节恢复后 15/15。最终 Release/Debug/ASan 全量各 769/769，GCC 271/271；关闭示例 P4/P5 300+1、静态兼容 231+1，恢复原 Release ON/ON 后补验 P5 63/63。23 项源/构建哈希、8 条实际 ASan 编译与对象、两编译器无 Qt 头、实际依赖图、验收引用/编码/格式及只读边界审计通过。未发现阻止本小阶段提交的问题，见 [M2b 记录](p5-m2b-development.md)及 [ADR-0043](adr/0043-controlled-service-publication.md)。

本段是服务发布前提；完整模块/闭包锁、定义与目录切换、迁移恢复、白名单卸载、UI 贡献和 H01/H02/S09 保持 [P5 原范围](p5-development.md)。没有把组内 Active、通知受理或已有人工确认冒记为整体替换成功。

## P5-M2a hash 安装事务提交前审核

日期：2026-10-30。基线 94bd1e9。由当前代理审核，未使用子代理。先复核来源验证、唯一目录、Core 服务发布和 Kernel/Session 边界，再把磁盘安装放到独立 ModuleInstall。安装复用 VerifiedSource 与共同路径设施，不执行后端或操作运行状态；InstalledArtifact 只持有目标的只读保护，不承载业务或可执行引用。

逐项核对固定句柄复制、限额、原始声明、同 hash 锁、临时验证、不覆盖目录发布、发布后原生身份确认、已有缓存重验和移动后失效。失败清理仅按已记录文件身份回收，外来条目/替身/占用失败保留并进入 causes；发布后无法确认的目录保留证据。复审补强获取父目录保护后的根身份核对和句柄分配异常时的 RAII 交接。未引入第二套 Registry、Core 反向依赖、DLL 包扫描或迁移占位接口。

发现并修复一项 P2：扩展长度前缀使原先可接受的 . / .. 绝对路径失效；真实 Win32 调用及新增回归修复前 0/1，前缀前先归一化后，普通/扩展长度输入均通过。另临时取消缓存声明身份和回收文件身份门禁，两项真实文件系统检查失败 0/2；finally 恢复原字节后专项通过。最终 26 项安装检查覆盖并发、别名、长路径/Unicode、篡改、外来内容、发布不覆盖及两后端候选接入，原生旧业务仍 Started 并正常关闭。

最终 Release 全量 754/754，Debug/ASan P4+P5 各 285/285 及支持各 1/1，GCC 258/258；关闭示例 285+1，静态兼容 216+1，原 Release 配置恢复并补验 P5 48/48。同 hash 六线程竞争在 Debug/ASan 各连续 20 次通过。19 项源/构建哈希、11 条实际 ASan 编译命令、两编译器无 Qt 头、编码/格式/引用及只读参考边界通过审计。未发现阻止本小阶段提交的问题，见 [M2a 记录](p5-m2a-development.md)与 [ADR-0042](adr/0042-module-artifact-installation.md)。完整运行替换、迁移/恢复、物理卸载及 H01/H02/S09 继续按 [P5 计划](p5-development.md)推进，未把安装压力冒充运行替换验收。

## P5-M1 当前/候选代目录提交前审核

日期：2026-10-30。基线 d9d4f05。由当前代理审核，未使用子代理。先检查 P4 单代身份假设与资源/锁归属，在同一目录增加 Current/Candidate 及明确 predecessor；两个后端复用预留、产物验证和装载机制，未增加第二套 Registry、实例系统或替换占位接口。

普通 load 只匹配 Current，显式候选接口检查前驱、后端、插件目录与物理身份。目录锁先于代次锁，外部 query/root 构造在锁外；相同候选复用，失败候选不会改变旧代。临时恢复“最后一个同名代次”的错误选择，两后端 coexist 均按预期失败 0/2；原字节恢复并重编译后 20/20（业务门禁补验前）。补验硬链接、失败物理别名、当前代失败、旧原生业务运行及 Qt 构造重入，保持预算与默认驻留；首轮硬链接测试对 Windows 映像别名的错误预期已修正，生产门禁未放宽。

最终复审另发现 C ABI 的 businessFailure 与装载/运行失败信号不同：旧检查会接受已知业务协商失败的当前代或复用失败候选。两项真实缺业务入口 DLL 回归修复前 0/2；内部复用区分 Discovery/Staging 后，只收紧暂存门禁，保留普通发现复用，22/22 通过。

最终 Release 全量 728/728，Debug/ASan P4+P5 各 259/259 及支持检查各 1/1，GCC 258/258；关闭示例 259/259+1/1、静态兼容 191/191+1/1，恢复原 Release 后 22/22。14 项源码/构建快照、8 项 ASan 编译命令、两编译器无 Qt 头检查、元数据一致性与 P4 清单/编码/格式/引用/只读边界通过审计。未发现阻止本小阶段提交的问题，见 [M1 记录](p5-m1-development.md)及 [ADR-0041](adr/0041-module-candidate-generations.md)。P5 的运行替换、迁移/恢复、白名单物理卸载和 H01/H02/S09 压力仍按 [计划](p5-development.md)继续。

## P4-M4c3 产物与收口审核

日期：2026-10-30。基线 b9b889a。先对照 A01/A02、S06/S07 及业务服务/UI DLL 出口，再核对原生用户确认、退出与实际产物。由当前代理审查，未使用子代理。

原生操作由用户完成，原文“已全部验证”“主窗口已经关闭了”；进程随后实际退出 0，空应用日志和合法保存结果已核对。本轮没有代理点击或截图，不补造系统 DPI/跨屏证据。13 项 M4c2 C++ 源/头与已验基线一致。

产物审查发现新工作台 EXE 没有 Eve 图标组；原两个入口匹配七帧，新入口红检查失败。第三个入口接入时提取共同 CMake RC 打包函数，三个入口使用同一正式 ICO/模板，未修改原稿或运行 C++。修复后 Debug/Release/ASan 共 9 份 EXE 的 63 帧均逐字节匹配。新工具只解析 PE，不装载或执行待检 EXE。

机器可读矩阵从当前 CTest 清单校验 237 项 P4 标签、19 个唯一组件、54 项场景引用和 1 项支持检查；缺失引用、漏掉名称为 p3 的 P4 标签、禁用及重复匹配四项负向控制均拒绝。清单校验与执行结果分开记录，三套 P4/支持范围实际各 238/238。无 Qt GCC 重新配置/构建通过且无重编译工作，M4c2 的全量历史不冒记为本轮重跑。

源码、文档链接、LF、正式资源不变及只读边界审计通过。没有发现阻止当前支持矩阵 P4 收口的剩余问题；物理卸载/替换仍属未开始的 P5。证据索引见 [M4c3](p4-m4c3-development.md)和 [P4 收口](p4-closeout.md)。

## P4-M4c2 业务页面 DLL 与配置装配提交前审核

日期：2026-10-30。基线 1b5899d。由当前代理审核，未使用子代理。先核对共享 SDK、Core 所有权、目录与配置边界，再实施真实笔记 UI DLL、独立宿主、模块诊断和 Session 追加入口；用户授权完成后提交并同步。

实际复现并修复三项问题：外部插件添加时误用 builtin，目录校验拒绝；示例退出在磁盘保存冲突前就关闭 Session，无法继续处理冲突；遗漏保护宿主配置/诊断 Runtime，使其可被手动管理。分别有 module-add-red、review-red 和 host-guard-red 日志，见 [开发记录](p4-m4c2-development.md)。修复复用冻结定义、保存屏障和现有管理保护；未扩大 DLL 权限，也未复制生命周期/模块目录。按钮经 CommandHandle 执行，长输出有真实失败反馈；诊断只读复制实际记录，清理失败保留引用，正常退出仍驻留 root/映像。

最终 Debug/Release/ASan 全量各 706/706，GCC 258/258。关闭示例的 P4 名称检查 236/236、补标签检查 1/1；静态兼容名称检查 177/177（含 Session 追加）、补标签检查 1/1；恢复原 Release 的 ON/ON 开关后重建并检查新增场景 13/13。源码/构建/配置 18 项哈希一致、9 项 ASan 编译命令和三套产物摘要及实际导入检查通过；文档、编码、只读边界及差异检查通过，没有修改 Core/Qt 适配器/第三方或既有超时预算。

未发现阻止本小阶段提交的剩余问题。本次场景通过不等同于 M4c3 的真实桌面确认或整个 P4 完成；详见 [ADR-0040](adr/0040-configured-qt-workspace.md)。

## P4-M4c1 共享 Qt/UI 运行库提交前审核

日期：2026-10-30。基线 4866657（M4b2 已提交并推送）。由当前代理审核，未使用子代理。检查跨 DLL 的依赖方向、静态状态、页面工厂闭包与对象所有权，真实复现 QtScheduler 私有事件类型因静态链接被复制，造成 DLL 接受投递却不执行；负向调度回归 0/1、页面对照 3/3。收敛共享 Qt/UI 后四项通过，没有向调度器增加后端分支或复制页面系统。

QtUiPluginSdk 仅聚合现有 Core/Qt/Ui/Ela 目标，Module/Loader 仍属宿主。实际 DLL 导入确认具体类型 post 和页面 add 来自共享库，Debug 的 Core/Qt/Ui 均使用 d 后缀；Qt 不依赖 Ui/Module。真实页面多实例/换代检查子对象、工厂捕获先于业务对象析构；停止后释放业务引用，loader root 仍按策略保留。

最终原 MSVC Release/Debug/ASan 全量各 693/693、GCC 257/257；关闭示例 P4 225/225、静态兼容 P4 177/177，恢复原 Release 后五项 5/5。7 项源码/构建快照、17 项 ASan 编译命令（含 1 项生成资源）、编码/格式/引用及只读边界审计通过。未发现阻止本小阶段提交的问题，见 [M4c 记录](p4-m4c-development.md)及 [ADR-0039](adr/0039-shared-qt-ui-runtime.md)。M4c2 业务 UI DLL/配置/诊断和 M4c3 原生验收仍待完成，P4 保持进行中。

## P4-M4b2 Qt 工厂与业务实例提交前审核

日期：2026-10-30。基线 cd5bf2a。由当前代理审核，未使用子代理。先检查静态 Core 身份复制和并行清理的所有权缺口，再采用共享 Core、实际 Core 身份虚调用握手及通用 afterCleanup 末尾阶段；保留单一模块目录、Core 实例/服务系统和 QObjectResource 完整析构屏障。Core 不依赖 Qt/Module，插件 SDK 不链接模块宿主实现。

审查复现并修复输入租约过早释放、创建中失活仍执行旧代 activate，以及跨 scheduler 重入工厂。真实 gate 与 DLL 回归分别有 0/1、0/3 到修复后通过的证据。末尾清理按 LIFO 串行且失败即停，管理重试/配置检查点包含完整末尾账目；根工厂不手动 delete/unload，未知所有权保留引用。正常拒绝与激活失败按 Core 回滚，不将每次业务失败都扩大成全局故障。

真实静态 Core 错链 DLL 的生产握手在 query/create 前拒绝，正常 SDK 恢复后通过；并行末尾清理负向控制失败，源码逐字节恢复后通过。SDK 与共享 Core 的 /MT 编译均按明确诊断拒绝，Debug DLL 使用 d 后缀。没有以元数据声明或 Qt IID 字符串替代真实 C++ 接口/运行时归属。

最终三套 MSVC 全量各 689/689、GCC 257/257；关闭示例 P4 221/221，关闭共享 Core 兼容路径 P4 177/177，并恢复原 Release。21 项源码/构建哈希、11 项 ASan 编译命令、独立 host 头、实际 DLL 导入及格式/编码/引用/只读边界检查通过。9 项在 Debug/ASan 各连续 20 次通过，结果随 [开发记录](p4-m4b2-development.md) 归档；未发现阻止本小阶段提交的剩余问题。M4c 的真实页面 DLL、配置与原生验收仍待完成，P4 不提前收口。

## P4-M4b1 共同目录与 Qt root 装载提交前审核

日期：2026-10-29。基线 99863b8。按架构原则先检查职责/依赖/状态/所有权，将原生管理器中的唯一进程目录、代次保留与受限映像映射提取，原生与新 Qt 后端实际共用；共同失败信号不依赖装载实现，未让 Core/Loader/普通 UI 反向依赖 Qt 模块。由当前代理审核，未使用子代理。

核对原文件/目录句柄保护、身份/摘要/PE、64 代预算、锁外外部调用、原生兼容、root 构造重入、加载器与根对象的真实所有权/引用、异常驻留及不收养外部映像。NativeModule.open 增加明确后端门禁。Qt root 仅证明装载层完成，不以 IID 字符串替代实际工厂协商，descriptorAccepted/businessAccepted 均保留 false。root 存活时真实 QObject 账目保留并拒绝 drain，不手动 delete/unload 或清空记录。

新增独立 Qt 装载入口和真实 DLL 场景，复用已有安装设施、元数据构建依赖和进程退出后的安全清理包装器，未扩展原生测试主文件。提前发布 Loaded 的负向控制 0/1，finally 逐字节恢复并重编译后重入 1/1；有效元数据的错误构造夹具同时证明只读预检仍无映射/实例副作用。未发现阻止本段提交的剩余问题。

最终原 Release 全量 648/648、Debug/ASan P4 各 180/180、无 Qt GCC 245/245、关闭示例 P4 180/180 并恢复 ON。四项在 Debug/ASan 各连续 20 次，16 项源/构建哈希、18 项 ASan 编译命令、MSVC/GCC 无 Qt 独立头及格式/编码/链接/依赖边界审计通过。手动头探针初次遗漏 /utf-8 的 C4819 记录保留，补齐项目编码选项后通过。详见 [开发记录](p4-m4b1-development.md)。M4b2 业务工厂/实例、M4c UI DLL 与 P4 收口继续，不提前计为完成。

## P4-M4a 架构收敛与 Qt 预检提交前审核

日期：2026-10-29。生产基线 8cd2f60，用户要求的架构原则单独提交为 0213414。先审查职责/依赖/所有权，再提取现有来源保护/二次核对/PE 为 VerifiedSource，由 C ABI 后端和 Qt 只读预检共同消费；同时复用测试产物设施、建立独立 Qt 测试入口。没有往原生管理器添加 Qt 装载分支，没有第二套目录、通用框架空实现或页面工厂占位。由当前代理审查，未使用子代理。

复核原始文件句柄/祖先保护寿命、原生代次接管映像、公共 manifest loader 缺省兼容与后端显式拒绝、目录集合语义、Qt 编译/运行版本及工具链契约、预检线程与快照时效。首轮把框架 major/minor 当完整 patch 导致合法 Qt 被拒，核对本机 moc 后改为分层比较；全 patch 仍由自定义契约限制。增量验证复现 JSON 更改而 DLL 元数据仍旧，现以明确 JSON→moc→DLL 依赖解决，未用碰时间戳或清理重建掩盖图缺陷。修复后错误元数据 0/1、恢复后 good/order 2/2，详细日志见 [开发记录](p4-m4a-development.md)。

最终原 Release 全量 630/630、Debug/ASan P4 各 162/162、GCC 245/245；关闭示例 P4 162/162 并恢复 ON。good/order/backend/thread 在 Debug/ASan 各连续 20 次通过。16 项源/构建快照、17 个变更源对应 ASan 编译行、两公开头在 MSVC/GCC 不带 Qt 目录独立编译、编码/修改区域格式/差异/依赖/引用与只读边界审计通过。Core/Loader/Ui/第三方未改。

未发现阻止 M4a 提交的剩余问题。Qt 元数据检查不调用 load/instance，不创建 root/页面/代次，也不证明任意 C++ ABI 互通；M4b/c 工厂与页面资源装配继续按 [ADR-0037](adr/0037-qt-module-boundaries.md) 和 [阶段计划](p4-m4-development.md)实施，P4 仍进行中。

## P4-M3d2b 取消与 DLL 捕获验收提交前审核

日期：2026-10-29。基线 6c5931f。仅增加 lifecycle C++ DLL 夹具、真实 Qt 请求/托管检查及测试构建接入，生产 Module/Core/Loader/UI、SDK 和两个业务 DLL 未改。由当前代理审查，未使用子代理。

S06 分开核对生产 RequestChannel 的 Stop 取消/晚到完成与生产 HostTable 的 PENDING stop；前者使用测试 shim 转交 completion，明确不冒充完整 HostTable，也未新增公开 stop-cancel API。S07 以 Core epoch 立即失活、真实 DLL lambda 和其 Capture 析构、一个已接受宿主任务副本的 Callback/QueuePayload 租约及跨线程最后释放验证，活动和失败对照避免只观察最终计数冒充安全退役。所有跨 DLL 入口持 Call/Instance 等真实代次引用。

负向检查临时去掉 dispatchOne 执行前 allowed 门禁后，stale_capture 按预期 0/1；在 finally 中逐字节恢复并刷新时间戳重新编译，生产源与 HEAD 相同。首轮缺私有请求头的编译失败已补显式包含；复核 Qt 局部事件循环超时/异常、线程 join/异常交回、ABI 表、一次取消、完成值复制、捕获失败保留以及夹具退出后的独立目录清理，没有放宽原有超时或警告。未发现阻止本段提交的剩余问题。

最终原 Release 全量 617/617、Debug/ASan P4 各 149/149、GCC 245/245，关闭示例 P4 149/149 并恢复 ON。新五项在 Debug/ASan 各连续 20 次通过；4 项测试/构建哈希与 3 个 C/C++ ASan 编译行、格式/编码/差异/引用及生产/只读边界审计通过。完整证据见 [开发记录](p4-m3d2b-development.md)。M3 当前 Windows x64 范围已收口；M4 UI DLL、P4 整体和物理卸载不由本次测试完成。

## P4-M3d2a 业务 DLL 与真实 Qt 链路提交前审核

日期：2026-10-29。基线 02d2ca9。范围为两个业务 DLL、公共 C ABI 支持、实际产物声明生成、Qt 配置宿主及五项端到端检查。由当前代理审核，未使用子代理；Core/Loader/Ui/SDK/第三方未改。

复核查询写入容量/必需能力和静态描述寿命、纯 C ABI 传值、DLL 内有界存储、单调身份与计数溢出、effect 先登记和发布门禁、服务两端请求/ACK 身份、旧绑定异步清理、失败实例保留与 Qt 回调异常边界。修正 Windows MODULE 的输出目录（RUNTIME/LIBRARY 均设置），不放宽根路径校验；增加真实持久化内容比对、队列零待处理/零丢弃/零异常检查、Unicode 参数和 manifest byproduct。首轮五项找不到声明的失败与修复后五项通过均保留日志，见 [阶段记录](p4-m3d2a-development.md)。

最终原 Release 全量 612/612、Debug/ASan P4 各 144/144、无 Qt GCC 245/245；DLL 两个源另过 GCC 严格 C++17 语法检查，不冒充跨编译器 ABI 互通。关闭示例后原 Release P4 144/144 并恢复 ON，五项在 Debug/ASan 各连续 20 次通过。8 项源/构建哈希、3 个新增 ASan 编译行和三配置共 6 个 DLL 声明/产物摘要一致；格式/编码/差异/依赖/文档引用审计通过。

未发现阻止 M3d2a 提交的剩余问题。提供 counter/next 与 reporter/read 接口，本轮端到端观察启动结果，read 分支未独立请求；正常退出不能替代 S06/S07，M3d2b 与 M4 待实施，P4 保持进行中。故障演示返回成功只表示不静止/驻留符合预期，不宣称资源清理成功；默认驻留、桌面 UI 和工作台配置不变。

## P4-M3d1 独立复审：创建拒绝与借用输入

日期：2026-10-29。审查基线 ed2471d，覆盖本段 Core/Kernel/Loader 和原生生命周期/服务/资源适配，按用户要求检查 bug、规范和质量后提交。由当前代理复审，未使用子代理。

发现并修复一项 P2（src/module/host.cpp，NativeModule::createBound）：托管模式在进入 DLL create 前借用 Core 已提交的输入。DLL 满额且同步返回 LIMIT/空句柄时，原检查把这些宿主借用记录视为 DLL 遗留资源，错误隔离整个模块代次，阻止健康实例继续使用及后续重试。新增 native_managed_create_failure 使用真实 DLL 四槽容量，验证第五次拒绝、依赖计数回落、释放一槽后重新创建及完整退出；修复前 0/1，见 build/p4-m3d1-followup-red-tests.log，修复后 1/1，见 green-tests.log（相同 p4-m3d1-followup- 前缀）。

修复仅在已经确认失败且空句柄的托管 create 分支清除借用输入引用，Core 继续持有并按本代清理释放租约。没有绕过真实 effect/服务/排队资源遗留的隔离检查，也没有改变低层 create 协议。复核了同 scheduler/索引归属、精确 realm、输出声明与统一发布、已接受操作/ACK 的最后捕获、服务键保留、目录冻结及配置门禁；未发现其他阻止本次提交的问题。格式、编码与差异规范通过，未为风格重写无关代码。

最终原目录 Release 全量 607/607、Debug/ASan P4 各 139/139；新增创建失败/恢复在 Debug/ASan 各连续 20 次通过。三个变更源/构建文件 SHA256 与验证版本一致，两个受影响 C++ 编译命令均含 ASan，日志为 build/p4-m3d1-followup-{release,debug,asan}-{build,tests}.log、{debug,asan}-repeat.log、source-sha256.json、asan-commands.json 及 audit.log（均沿用前缀）。本轮没有修改 Core/Loader/SDK/可移植实现，GCC 和独立头证据沿用 ed2471d 的已验证版本；未把此前完整 Core/P4 数量记作本次重跑。sources/、research/、third_party/ 和 UI 未改，Release 仍为原目录且示例 ON。

本次收口 M3d1 复审，M3d2 业务 DLL/真实 Qt/S06/S07 与 M4 UI DLL 仍待实施，P5 未开始。开发及验收映射见 [阶段记录](p4-m3d1-development.md)和 [验收单](../tests/acceptance/p4-module.md#m3d1-core-托管与冻结配置目录)。

## P4-M3d1 Core 托管与配置目录提交前审核

日期：2026-10-29。基线 d6d003f。范围为 Core 私有适配门禁、NativeModule.runtime、Kernel 身份登记、冻结模块目录及 17 项真实 DLL/4 项无 Qt 回归；沿用远端低层 open/create 和 NativeServices。由当前代理审核，未使用子代理。

复现并修复六类问题：低层启动回执被新关闭语义误改；Core 停用后排队回调仍进入 DLL；异步清理提前释放 Core 服务键；提供者代次故障未阻止健康实例继续受理业务；普通 invoke/dispatch 配额耗尽误隔离健康模块；同步 ACK 额外申请异步请求导致满额时不能交付。回归分别验证低层兼容、执行前 epoch 门禁、Core 最终释放键、代次受理/执行双检查、LIMIT 拒绝与恢复，以及实际 CallLease/捕获屏障下不占请求预算的同步 ACK。红测日志及各项修复映射见 [M3d1 记录](p4-m3d1-development.md)。

质量检查覆盖输出声明及元数据回滚、初始 setup 发布/取消/失败、只借用 Core 已提交输入与旧绑定清理、realm/owner/scheduler/索引约束、Session 预览不执行 DLL、模块目录不能被配置重指向、失效模块仍可停用。新测试头显式包含基础夹具，避免格式化改变包含顺序后编译失败；未修改警告或测试超时。Core/Loader 无 Module/Qt/Windows 反向实现依赖，C++ 宿主描述不跨 C ABI。

最终原目录 MSVC Release 606/606、无 Qt GCC 245/245、Debug/ASan Core/P4 各 260/260、原 Release 关闭示例 P4 138/138 并恢复 ON。cleanup/session/key/generation/incoming_budget/ack_capacity 在 Debug/ASan 各连续 20 次，共各 120 次通过。29 项源/构建哈希、15 个受影响 C/C++ ASan 编译命令、8 个 SDK 头的两编译器独立编译、格式/编码/差异/文档/依赖审计通过。sources/、research/、third_party/、UI 和既有测试预算未改。

未发现阻止本段提交的剩余问题。测试使用真实 DLL 夹具与 FakeScheduler、受控内存 writer；业务 DLL 示例、真实 Qt 循环及 S06/S07 联合退出属于 M3d2，M4 UI DLL 仍待实施。本次不新增人工桌面验收要求，不将夹具通过标记为整个 P4 或物理卸载完成。

## 远端优先合并验证

日期：2026-10-29。本地 a788c51 与远端 5e71888 分叉。按用户“以远端为主”处理 19 个冲突，M3c2 服务桥、关联 SDK/Core 接口、夹具与测试整体采用远端版本；本地另一套服务桥专属接口、测试及 ADR 随之移除，原提交保留在合并历史中。远端 Eve 品牌资源、工作区面板及隔离输出完整保留。

代码相对远端仅保留三个兼容文件的独立增量：json.cpp/json_p.hpp 的有界字符串数组解码，以及 requests.cpp 中 Unicode/转义重复项/预算断言。没有混用两种 NativeModule 服务桥接口。合并冲突标记与引用检查通过。最终原 Release 全量 585/585、无 Qt GCC 全量 241/241、ASan P4 117/117 通过，分别见 build/remote-merge-release-tests.log、gcc-tests.log、asan-tests.log；208 项源码/构建/资源 SHA256 一致，两个保留增量的 C++ 编译行包含 ASan。没有新建 Release 目录或关闭证书验证。

git fetch 的默认 Windows TLS 后端遇到握手失败；使用命令级 OpenSSL 后端（保持证书校验）成功获取，最新 origin/master 与 MERGE_HEAD 均为 5e71888。未改 Git 持久设置或关闭 TLS 校验。

## P4-M3c2 原生服务桥提交前审核

日期：2026-10-29。基线 aa22275。范围：Core 内部异步清理门禁、NativeModule 显式共享索引、生产服务注册/撤回/调用、资源退出屏障、两个真实 C DLL 及 19 项回归。用户要求修改完成后提交并同步。审查由当前代理完成，未使用子代理。

复现并修复两项 P2：受理超限后遗留新 committed 租约，导致被拒绝的调用也钉住提供者；提供者取消入口返回非法 PENDING 时，遗漏隔离提供者。新增 service_budget / service_cancel_protocol 修复前 0/2，见 build/p4-m3c2-review-red-tests.log；现在失败受理撤销新租约、取消协议异常隔离两端并保留账目。修复后服务专项 19/19，见 build/p4-m3c2-review-green-tests.log。

审查核对 start 与真实 effect ready 双发布屏障、精确 realm/minor/能力、转义能力判重、Core 实际依赖环、旧绑定受限清理、提供者等待消费者、两端独立请求及真实取消。补测 ACK 后投递副本仍存在和最后捕获跨线程释放，清理失败不释放租约。共享索引只能在同 scheduler 使用，活动会话不能切换；调用方保证活动 InstanceId 唯一。夹具改为按 generation 选择真实 DLL，不依赖全局快照第一项。

最终 MSVC Debug/Release 各 585/585、无 Qt GCC Debug 241/241、ASan P4/W4/W5 152/152、原 Release 关闭示例 P4 117/117 并恢复 ON。ASan 首轮目标未齐的失败另存 p4-m3c2-asan-incomplete-targets.log，补齐全部构建后重跑，不计为通过。16 项源/构建文件哈希和 8 个受影响 ASan 编译命令一致；编码、差异、文档和依赖边界检查通过，没有修改 UI/Loader、第三方/只读参考或既有测试预算。

未发现阻止本段提交的剩余问题。实现和完整日志索引见 [服务桥记录](p4-m3c2-development.md)及 [ADR-0035](adr/0035-native-services.md)。M3d 的 Kernel/Session 与两个业务服务 DLL、M4 UI DLL 仍待完成；本次夹具通过不等同于整个 P4 或物理卸载安全。

## 底部工作区与输出通道增量审核

日期：2026-10-29。基线 d917f64。范围：当前/非当前页签配色、底部命令/输出/问题工作区、ui.output/v1 源码 SDK、笔记插件实际接入及九项新增真实 Qt 回归。用户明确选择执行已注册平台/插件命令，并要求输出可按目标选择，随后要求代码审核、提交并同步远端。由当前代理完成审查，未使用子代理；本增量按用户要求随提交归档，不覆盖此前 UI 专项的用户确认。

命令经过过滤模型映射回原始实例/epoch 记录，保留原 enabled、队列、Task 和 effect 屏障；平台动作复用现有 QAction。异步结果在面板中显示，过程/结果另入所属输出通道，通知历史保持独立。输出仅保存有界值，按实例/代次及名称隔离，旧 Context/错误线程拒绝写入，平台身份仅由宿主入口产生；目标切换和清空不串改其他记录。问题来自 diagnostics 真实值，具有筛选、详情和身份/路径定位，恢复后移除，源失效不假报恢复。

审查修复了工具栏角控件与页签 size hint 不一致导致的裁切、非当前目标日志重置当前视图、追加日志丢失阅读选区、重入追加未显示最新记录、长诊断截断破坏 UTF-8，以及实例/effect 汇总重复计数。关闭重入回归以嵌套事件循环验证窗口访问屏障，未放宽任何既有超时或清理断言。通知和管理旧检查按新的目标隔离语义显式选择输出来源。

提交前复审另发现并修复三项 P2：清空动作先发出文本信号再删除当前目标记录，重入切换目标可误清另一通道；复制只读编辑器的 500 行窗口，遗漏后台保留的历史；问题树在阻断信号后恢复原选中项，但详情仍保留旧快照。新增三项检查修复前 0/3，见 build/panel-review-red-tests.log，修复后相关检查 12/12。进一步复现从命令面板执行清空时，平台前置日志也可改变清空目标；分支修复前 0/1，见 panel-platform-clear-red-tests.log。现由后台先捕获/清空目标，平台调用日志在实际动作之后写入；复制读取全部当前目标历史，显示在进入 QTextDocument 前裁剪至 5000 行；恢复选中项时同步刷新详情，并保持窗口访问屏障。

实现阶段 Ela/MSVC Debug、Release 曾各 563/563；过程日志为 build/panel-reviewed-debug.log、panel-reviewed-release.log，100%/150% 主题截图保留在 build/。初次三项显示/旧目标断言失败保留在 panel-debug-ui-tests.log。GCC C++17 对 output.hpp 的无 Qt 包含路径独立编译通过，独立头探针为 build/panel-output-header.cpp。提交前修复的最终验证另列于本节末尾，历史结果不代替最终源码的验收。

本轮自动化和真实 Qt 截图通过，没有代填新增体验的用户确认。sources/、research/、third_party/ 未改；Core 仅增加输出适配器的私有 friend 声明，没有修改其实现。新接口不声称冻结原生二进制 ABI，P4-M3c2/M3d/M4 状态不变。使用与接入见[底部工作区说明](workbench-panel-development.md)。

提交前最终源码的 Ela/MSVC Debug、Release 各 566/566（含 UI 各 128 项），ASan 面板专项 9/9。最终日志为 build/panel-submit-final-debug.log、panel-submit-final-release.log、panel-submit-final-asan-tests.log；ASan 受影响目标重新构建，未修改检查预算或编译警告。编码/文档链接/只读边界与差异检查通过，源码摘要保留于 build/panel-submit-final-source-sha256.json；未发现阻止本轮提交的剩余问题。

## Eve 图标与 P4 分支合并审核

日期：2026-10-29。范围：将远端六项 P4 提交（59b7c76 至 e9c89dd）合入本地图标提交 9d1ccda，合并提交为 96b650a。冲突仅在本文件和进度文档：保留两侧审核/验证记录，以远端已提交的 M3c1 为当前状态，图标记录作为独立已提交增量；不再用图标分支的“P4 未开始”覆盖最新进度。

整合检查确认 P4 模块、ABI 和测试代码与 e9c89dd 一致；Qt 示例同时保留模块诊断入口及两个桌面 EXE 的 RC 资源，ICO 的 OBJECT_DEPENDS 仍有效。八项正式素材与只读原稿及 provenance 哈希一致，四个 Debug/Release 桌面 EXE 的七尺寸 RT_ICON 帧逐字节匹配原 PNG。未改 sources/ 或第三方源码，未新增功能。

合并后 Ela/MSVC Debug、Release 构建及全量测试各 557/557 通过。Debug 首轮及重试分别遇到两个桌面入口的既有 PDB 损坏（LNK1285）；仅删除对应生成文件后重新链接通过，未修改编译选项或源代码。初始失败与最终日志保留为 build/merge-p4-eve-debug*.log、merge-p4-eve-release.log，PE 图标审计为 build/merge-p4-eve-icons.log。冲突标记、差异格式、文档本地链接检查通过；本次整合审核未发现阻止合并的剩余问题。

## P4-M3c1 排队操作与 effect 提交前审查

日期：2026-10-29。基线 f32191b。范围：生产 DISPATCH/EFFECTS、私有宿主控制/资源拆分、独立请求和 ACK、Core effect、捕获/字节退役及 21 项真实 C DLL 场景。由当前开发代理审查，未使用子代理；M3c2 服务实现必须在本段审核提交后推进。

发现并修复三项 P2：主动异步清理期间实例关闭提前 stop；ACK 已完成但合法投递副本仍持有操作数据时过早宣布静止；scheduler 对象释放后包装器丢失 owner 身份导致资源诊断失败、聚合可抛异常。三个真实回归修复前各 0/1；现在先等待在途操作/清理，最后捕获析构并在 owner 确认退役，诊断身份独立保留且聚合显式传播错误。资源参数字节也在实际退役后返还，避免索引删除后超用真实内存，见 [M3c 记录](p4-m3c-development.md)。

核对跨边界值复制、消费者/目标请求不混用、取消不假完成、同步 ACK 禁止 PENDING、资源归属与一次 ready、注册逆序清理及失败闭包保留。Core effect 调度拒收/丢弃/异常转为不静止故障，Call/Callback/QueuePayload/Setup/Cleanup 租约按实际寿命登记。关闭不提前 seal，也不在持锁时执行 DLL。未用协商哨兵冒充生产实现，服务能力仍关闭。

最终 Release 557/557、GCC 241/241，Debug/ASan/关闭示例 P4 各 98/98；12 项冻结源/构建文件、8 个 ASan 编译行及编码/格式/文档/依赖/diff 审计通过，原 Release 示例开关已恢复。此前矩阵和红测试保留，末次 Windows 修复后重新构建三种 MSVC 配置，GCC 相关共同源码未变。未发现阻止本段提交的剩余问题；Core/UI/Loader、只读参考、第三方及原有 UI 预算未改。M3c2/M3d/M4 仍须实现并分别审核提交。


## P4-M3b 原生宿主提交前审查

日期：2026-10-29。基线 1ffa0ff。范围：NativeModule/NativeInstance、稳定 HostTable、ModuleGeneration 业务状态、JSON/UTF-8 共用检查、真实 C/C++ DLL 及 21 项 Host 场景。由当前开发代理审查，未使用子代理；按要求先审核修复、验证并提交，再进入 M3c。

修复两项 P2：关闭回调门禁后的已知 context 日志只返回 CLOSED，可误报静止；启动请求已发布并退役、生命周期观察器仍排队时，关闭会误取消不存在的请求。两个新增真实回归修复前各 0/1；修复后包含在 21 项 Host 场景中。另验证移动/复制投递捕获、宿主 shim 仍执行时不得销毁，异步错误 JSON 在销毁后仍由宿主保留，见 [M3b 记录](p4-m3b-development.md)。

核对 DLL/调度/Promise 通知在锁外执行、稳定表和实例/Call/会话租约寿命、取消不假完成、同步 create 句柄契约、失败驻留及非 owner 门禁。工作线程只记录受锁保护的故障信号，owner 推进生命周期；基础 Host 能力为 0，没有将协商哨兵当作真实服务/effect 实现。

最终 Release 全量 536/536、GCC 全量 241/241；Debug/ASan/关闭示例 P4 各 77/77。16 项源/构建哈希、9 个受影响 ASan 编译行、格式/编码/文档/依赖及 diff 检查通过，Release 示例开关已恢复。无阻止本段提交的剩余问题，未修改 Core/UI/Loader、只读参考或第三方实现。后续 M3c/M3d/M4 仍须实现并分别审核提交，不将本段当作 P4 整体验收。


## P4-M3a 生产请求组件提交前审查

日期：2026-10-28。基线 fed5a51。范围：RequestChannel、JSON 扫描、请求/投递租约、构建与 18 项回归及文档。由当前开发代理审查，未使用子代理；按用户要求，无未解决问题后提交，再进入 M3b。

审查发现并修复两项 P2：close 仍允许清理，却先发布 drained，终结屏障可被后续请求推翻；结果已发布但最后投递捕获仍在生产者侧时，cancel 仍登记新取消意图。现在 close 与不可逆 seal 分开，seal 后不再新增任何请求；已发布结果返回取消 false。两个真实回归各在修复前 0/1，修复后包含在 18 项通过检查中，见 [M3 记录](p4-m3-development.md)。

核对 worker 与入口返回的互斥仲裁、一次接受、取消不假完成、旧 context/线程门禁、字节/请求/深度限额、整数原文与 UTF-8/转义。scheduler、Promise 通知均在控制锁外执行，丢弃/拒收/抛错可诊断且保留账目。以显式 gate 验证 post 返回前运行队列副本时，最后生产者捕获仍交回 owner 退役；任务结果与资源静止不混为一谈。未终结控制块的自保留是有界故障策略，不将其写成已成功清理。

最终 Release 全量 515/515、GCC 全量 241/241，Debug/ASan/关闭示例 P4 各 56/56；7 项最终源/构建哈希、3 个 ASan 编译单元、格式/编码/文档/依赖及 diff 审计通过。没有修改 Core/UI/Loader、原有 UI 超时或参考/第三方实现。未发现阻止本段提交的剩余问题；随后继续 M3b 的真实 HostTable/DLL 生命周期，跨边界 CallLease、线程退出、服务/effect 与 Kernel 联合验收仍按原计划实施。

## P4-M2b 完整业务 ABI 提交前审查

日期：2026-10-28。范围：249a425 后全部 M2b 增量，包括独立 C11 业务头、宿主私有协商器、C 样例/布局/真实 DLL 检查及文档。由当前开发代理审查，未使用子代理，不声称独立第三方审计；按用户要求先审核提交，再进入下一阶段。

核对所有表槽的签名、显式调用约定与线码、全字段偏移、容量截断、major/minor 前缀兼容、能力依赖/空槽及精确 manifest 身份。宿主先校验再进入 DLL，未知状态和 query 异常被拒绝；有界元数据另存宿主副本，私有函数表仍受 DLL 与 HostTable 寿命约束。测试宿主使用进程寿命表，跨线程完成前后通过显式取消/线程 join 协调，不把夹具当作已实现的生产竞态或资源桥。

复核最终源码快照与实际日志：MSVC Debug/Release 各 497/497、GCC 223/223，ASan 与关闭示例 P4 各 38/38。8 个新增/受影响编译行有 ASan，两个 C 头在 MSVC/GCC 下分别以 C11/C++17 同时包含通过；12 项源/构建哈希、编码/格式、依赖、文档及 diff 检查通过。首轮两个既有 UI 超时及未改源码/预算的串行复验均保留，见 [M2b 记录](p4-m2b-development.md)。

未发现阻止本阶段提交的剩余问题。本次仅归档 M2b 协议/协商层，随后进入 M3；不把直接调用驻留 DLL 的测试当作生产 ModuleLease、Kernel/Session、服务/effect 或 M4 UI DLL 验收。未修改 Core/UI/Loader 实现、只读参考材料或第三方源码。

## P4-M2a 原生装载与描述 ABI 提交前审查

日期：2026-10-28。范围：59b7c76 后全部 M2a 增量，包含 SDK、真实 DLL 装载、只读 C ABI、文件句柄提取、诊断工具、构建、测试及文档。由当前开发代理审查，未使用子代理，不声称独立第三方审计；用户已要求修复并提交。

发现并修复两项问题：

- P2：描述 query 返回前就发布 Loaded，其他线程创建的管理器能观察到“Loaded 但描述尚未通过”。现在映射/PIN 期间仍为 Validated，成功后在同一锁中发布 Loaded、描述通过和完成标志。真实 C DLL 使用原生事件阻塞 query，验证并发快照、Call 租约、重复请求/排空拒绝及完成后的状态；修复前真实失败，修复后通过。
- P2：异常选项使用子串替换，会将合法 /EHsc- 改为关闭展开的 /EHs-，且没有处理 Debug 等配置专属标志。现在模块与 ABI 测试复用目录级函数，只替换完整选项，保留负修饰和其他模型，处理标准/自定义配置；不修改缓存或 Core/Qt/UI 的模型。脚本检查选项及作用域，并用实际自定义编译选项验证违规异常仍被宿主拒绝。

代码规范与质量检查覆盖公开头隔离、C11/C++17 布局、原生句柄 RAII、目录锁外执行 DLL、进程有界驻留、物理身份冲突和失败保留。共用私有文件对象，没有暴露 HMODULE、DLL 可调用指针或复制句柄；未关闭严格编译警告。另将文档中的“目录冲突”明确为插件目录内容，避免混淆部署路径与逻辑身份复用。

最终 MSVC Debug/Release 各 490/490、GCC 217/217，ASan 与关闭示例的 P4 专项各 31/31；源快照、编码/格式、边界及文档检查通过，未发现阻止本轮提交的剩余问题。新增回归、CLI 和完整证据见 [M2 开发记录](p4-m2-development.md#提交前最终验证)。原生夹具在子进程退出后删除，不改只读参考或第三方源码。实现和本审查记录按用户要求随本提交归档；M2b 业务 ABI、M3 Kernel/请求桥、M4 UI DLL 及 P5 替换/卸载仍未实现，不将描述握手作为完整 P4 验收。

## P4-M1 模块预检与宿主账目提交前审查

日期：2026-10-28。范围：31e52ab 后全部 P4-M1 增量，包括 SDK、原生预检、账目、命令行工具、构建/测试和文档。由当前开发代理审查，未使用子代理，不声称独立第三方审计；用户已要求审核、修复并提交。

发现并修复三项问题：

- P2：QDir 的绝对路径判断仍接受依赖当前驱动器的 /path。现在根必须带盘符或完整 UNC 共享；声明路径同样拒绝驱动器相对写法，合法相对根路径、中文及扩展路径保留。
- P2：最终路径查询关闭句柄后，又用 QFile 按字符串重开；路径检查与实际读取对象可能脱节，可写来源也会被接受。现在不可复制的原生 RAII 对象持有同一句柄，统一做路径、身份/大小/修改时间和字节校验，仅允许读取共享，并在返回前复核。预检返回仍只是快照，不冒充未来装载许可。
- P3：junction 回归依赖 CORDIS_BUILD_EXAMPLES，关闭示例会静默少验一项。现在测试使用现有诊断目标，或建立复用相同源码的测试探针，两种配置均保留完整 P4 专项。

两个新增真实回归在修复前 0/2 通过，修复后 2/2，P4 专项 14/14；红/绿日志保留为 build/p4-review-red-tests.log / green-tests.log。账目审查覆盖唯一租约、移动/自移动、排空与并发释放、永久 pin 和预算；未把记录为空当作卸载许可。公开接口保持 Qt/Windows 类型隔离，原生句柄禁止复制且所有返回路径由 RAII 关闭，未关闭严格警告。

最终原目录 MSVC Debug/Release 各 473/473，无 Qt GCC Debug 215/215，ASan P4 14/14；关闭示例的 Qt Debug P4 14/14，检查后恢复原开关。六个新增编译单元的 ASan 参数、13 项源/构建/脚本快照、公开头/依赖边界、编码/格式、文档链接及 git diff --check 均通过。末次将 PowerShell 规范为仓库要求的 BOM/CRLF，脚本解码内容一致，随后四种入口的 junction 定向复验通过；其余文件保持 UTF-8/LF。日志为 build/p4-review-*-tests.log / script-*.log，审计为 build/p4-review-audit.log。未发现阻止本轮提交的剩余问题，实现和审查记录随本提交归档；P4-M2 真实 DLL/ABI 尚未实施，不将 M1 当作整阶段完成。

## Eve 正式软件图标接入审核

日期：2026-10-28。范围：31e52ab 后的 Eve 素材、Qt 内嵌资源、Windows EXE 图标、内部品牌工厂及 ICO 封装工具。采用用户指定设计合集 README 标明的最终纯色蓝主稿 #2864C5，不采用历史渐变或字标组合。八个正式素材的 SHA-256 与原始文件及归档 manifest 一致；sources/ 保持只读。

审核确认 PNG 原样内嵌至静态 Ui 库，通过全局资源初始化函数保证链接并只注册一次；图标对象按应用使用，不额外静态持有 GUI 图标。默认应用、窗口/任务栏和导航品牌位共用七尺寸 PNG；宿主已显式设置的应用图标保持有效。两个桌面 EXE 另编入 ICO，Core/普通源码 SDK 不增加依赖，不引入 QtSvg、运行时外部文件或 Python 依赖。RC 模板使用带引号的配置路径和 UTF-8 code page。

ICO 工具只用标准库验证 RGBA 尺寸并封装既有 PNG，无重采样/着色；重复生成 SHA-256 一致。四个 Debug/Release 桌面 EXE 的 RT_GROUP_ICON/RT_ICON 数据逐帧与原 PNG 相同，涵盖 16/24/32/48/64/128/256px。Debug/Release 各 459/459 通过；最终 RC 模板单独重建检查通过。日志为 build/eve-brand-<debug|release>.log、eve-brand-final-resources-<mode>.log 和 eve-brand-audit.log。

原生 Release 窗口观察到标题栏和左侧品牌位为正式 Eve 轮廓且透明镂空正确，工作台保持运行供查看。首次原生状态捕获提示 no screenshot targets found，按原窗口重新选取后恢复，未据此重启或重复启动进程。没有修改应用名称、配置路径、插件业务图标或保存状态；未改第三方源码。差异、来源及文档检查通过，本轮审核未发现阻止提交的问题。

## Home 卡片窗口与配置实例添加提交前审查

日期：2026-10-28。范围：此前尚未提交的 Ela 基线/偏好/结构优化，以及本次 Home 管理窗口、配置实例添加和 SDK 展示方式增量。由当前开发代理审查；用户已要求修改完成后提交。

管理窗口复用原控制器，卡片身份固定到实例或配置路径，不占活动栏/中央页签。页面对话框继续使用本代 effect、输入门禁和 QObjectResource 删除整个容器；窗口关闭只隐藏，插件停用才撤销。未应用草稿不被定时刷新或切换对象丢弃；失效对象也不阻止平台通知历史。

添加/修改配置经过完整 Session 校验及 CAS，不安装外部模块，不立即写盘；已有但未应用的磁盘目标先要求检查/应用。受限 Loader 值树保持兄弟/子树、字段和大整数/小数原文，避免 Qt JSON DOM 的浮点转换。保存以独立 Promise 传播业务失败和投递失败，不拿 thenOn 投递成功替代 SaveReport 的真实结果。明暗编辑区透明样式随 Ela 正确绘制；没有配置会话时平台动作禁用并解释原因。

新增五类真实 Session/Kernel 场景覆盖定位、添加、嵌套编辑/精度、CAS、保存冲突/恢复及对话框退役；旧导航测试改为验证窗口打开时中央历史保持和关闭重入。最终 Debug/Release 各 459/459，ASan UI/Session/YAML 136/136，原生 18/18；最后纯呈现修正另经三构建和原生两档补验。SDK 独立头、32 项源快照、依赖/格式/文档审计通过，详见 [开发记录](home-management-development.md)。旧 UI-S4 用户确认不代填本次体验；本段变更随本提交归档。


## UI-S1–S4 结构增量审核

日期：2026-10-28。由当前开发代理检查，范围为 VS Code 结构对照、页签菜单、输出工具、设置检索、通知入口、分隔条主题与对应检查。尚未提交。

修复旧菜单误关重开同名页签的边界，使用键/代次快照；事件回调持有窗口访问屏障，检索重入收敛后再应用最新查询，关闭使旧菜单动作失效。通知来自独立服务快照、显示为纯文本，不在未连接 Kernel 时清空。全局动作和局部按钮复用 QAction，默认收起和插件生命周期契约保持。

审查截图发现深色分隔条仍为浅色；初版容器样式造成 management_theme 的后代调色板回归，改为只设置 QSplitterHandle 后定向通过。失败证据保留，没有改第三方源码或放宽文字颜色断言。

最终 Release 454/454、ASan UI 114/114；Debug 完整轮次 453 项通过，150% 多截图超时项独立连续五次通过。依据实测 15–19 秒，仅给两项多截图检查配置 60 秒预算，其他检查不放宽。源快照、字体/控件、依赖/只读边界及文档审计见 [增量验证](ui-structure-verification.md)。

最新原生检查在输出复制处受 Windows OpenClipboard 错误 5 限制；后台测试退出后仍可复现系统拒绝访问，未伪报为通过。上述为历史阻塞：本次系统剪贴板恢复成功，两档复制原生补验通过，最新原生合计 32/32；用户明确回复四项新增交互及两个入口正常关闭“已体验，全部通过”。源码快照保持一致，UI-S4 阻塞解除并收口；UI-S0 历史确认独立保留。


## UI-S0 Ela 基线审查

日期：2026-10-27。范围：1afd44b 之后当前未提交的 UI 基线、可选偏好路径、两桌面入口、专项回归和文档。由当前开发代理审核，未使用子代理，不声称独立第三方审查；本次尚未提交。

处理的主要问题：状态图标在换肤后保留旧色，现随主题立即更新；长通知可撑高状态栏，现限制单行/高度并提供完整提示与输出；Ela 开关缺少键盘操作，第一方继承保留 Ela 绘制/动画，补齐 Space/Enter、自动重复门禁及焦点环。配置页面分别显示编辑、运行和磁盘，真实部分执行失败后保存成功也不会把运行误显示为成功。

偏好文件独立于业务配置，只保存五个有版本且有范围的宿主值；有界读取、损坏/未知版本不自动覆盖，原子保存失败保留旧文件，并支持故障解除后的重试。读取/恢复不创建实例或自动展开底部。设置主题信号可重入关闭，窗口访问屏障及新 reentry 检查验证当前调用返回前不删除窗口；报告回调复制后调用。

GCC/MSVC C++17 独立包含 ElaShell SDK 通过，不引入 Ela/Qt 头路径。普通 Ui 没有新增 Loader 依赖，Core/参考材料/第三方实现未修改；正文统一为 Ela 应用字体，图标使用 ElaAwesome。最终原目录 Ela Debug/Release 各 444/444、ASan UI 104/104，源快照已比对；原生/用户确认及限制详见 [基线验证](ui-optimization-verification.md)。预编译 Qt/系统库未插桩，未作 DLL 卸载认证。

早期 smoke 依赖旧文案、测试 Qt5 容器比较的弃用告警、窗口尚未激活的 Tab 准备失败均已修正，保留日志；没有为通过而关闭警告或移除实际行为检查。用户已确认新版体验与系统 150%，后续新增 VS Code 结构分析和分阶段改进另行推进，不沿用本次确认作为新交互验收。

## P3 收口记录审核

日期：2026-10-27。范围：766db1e 后的验收确认、Home 原生代理补验及收口文档；没有产品/测试代码修改。

核对用户本次“已手动验证通过”明确对应 Q4d3，不再保留该人工确认阻塞，也不伪造用户逐项操作、系统 DPI 或分辨率。Home 手动路径另由当前代理使用 computer-use 的真实原生窗口操作验证，记录停止 #1、加载新身份 #5、失败 #4 重试、总卡片数保持 4、普通/进程 150% 明暗和两次退出码 0；该部分没有写成用户亲自操作。

完成条件逐项对应设计的 P3 交付与 L01–L04/Q01/U01/S05/S08/S10，核对现有实现、432/432 双配置、210/210 GCC 双配置、ASan/重复专项、原生报告、用户确认和阶段提交。证据支持当前内置插件/Windows 支持矩阵的 P3 完成；未发现遗漏实现或验收阻塞。进程缩放不冒充系统设置切换，额外硬件/跨屏验证及 P4/P5 模块边界继续明确。

最终文档链接/锚点、验收测试引用及 git diff --check 通过，未改只读材料、第三方源码或用户配置。阶段提交 e3491ae、766db1e 已存在，当前仅收口记录经本次审核提交；没有代码变化，沿用对应最终构建与测试记录，不虚增测试轮次。

## P3 跨组件装配验收提交前审核

日期：2026-10-27。范围：e3491ae 后新增装配验收目标、四个场景及进度/验收文档。由当前开发代理审核，不声称独立第三方审查；本阶段没有修改产品实现。

核对测试使用真实 Session/Kernel、后台文件执行器、watcher、Ela 配置页与窗口；语法/schema 失败后逐项比较活动树、实例/epoch、页面和磁盘 hash。显示名修改与显式 id 修改分别验证；部分激活失败检查实际/期望仍是新配置，并以真实 Windows 禁止共享删除句柄触发后台原子提交失败。保存成功、运行失败及历史已保存修订彼此独立，不以控件文案代替结果。

退出场景实际发出窗口关闭请求，使用显式 future gate 保持 worker 在运行；Qt 回调必须观察取消、会话未完成及窗口/调度器/I/O 仍存活后才能释放 gate。最终成功要求后台 join、worker 原生/捕获/宿主、连接、Kernel、业务页/配置页/窗口均完成，再关闭调度器和应用。无 fixed sleep、伪造监听信号或生产测试钩子。首轮断言通道错误和 ASan 运行库环境遗漏的失败记录保留，见 [开发记录](p3-development.md#p3-跨组件装配验收收口)。

审核补齐直接 Qt include，并增加部分失败后的实际/期望配置断言。最终 Ela Debug/Release 各 432/432、GCC 各 210/210，新四项 Debug/ASan 各连续 20 次通过。ASan 插桩本轮 Core/Loader/Qt/UI/Ela 和验收目标，预编译 Qt/系统库未插桩。验收引用、文档本地链接及 git diff --check 通过，未发现阻止本阶段提交的问题。人工确认与真实系统 DPI 继续如实待验，P4/P5 未开始；本阶段审核后提交归档。

## Q2g 原生信号前缀提交前审核

日期：2026-10-27。范围：179aca9 后全部增量，包括 SDK 模板、原生 watcher 示例、八项前缀场景、配置身份/最新目标断言及验收文档。审核由当前开发代理执行，不声称独立审查。

确认前缀在去除 cv/ref 后逐项精确匹配原信号，空/过长前缀及隐式转换被拒绝，选中数据继续服从白名单；完整入口没有自动丢弃尾参数。两入口共用同一注册实现，票据、字节预算、旧代、异步数据寿命和关闭屏障没有新增分支。未选参数不进入冻结 tuple，原生桥不持业务回调。配置身份检查明确区分重排与修改 id，并对真实最终运行配置和 desired 同时断言。

验证期间发现并修正原生通知测试输入的不确定性：快速改写可能没有改变 Qt Windows 后端观察的修改时间；初始临时文件现显式设置旧修改时间。首次重复失败记录与固定版本 Qt 依据见 [Q2g 记录](p3-development.md#q2g-原生-qt-信号前缀与验收映射)。没有改适配器来伪造通知，也没有延长超时或添加重试。

最终 Ela Debug/Release 各 428/428、GCC 各 210/210、ASan 专项 109/109；11 项 Debug/ASan 各连续 20 次通过。GCC/MSVC 正向及六类拒绝契约检查、本地文档 271 条文件/锚点链接和 git diff --check 通过。复核未发现阻止本阶段提交的问题。Qt/系统预编译库未插桩，人工确认、真实 DPI 和模块 ABI 不由这些结果替代。Q2g 随本阶段审核提交归档，随后继续 P3 集成验收。

## Q2e/Q2f 与桌面环境增量提交前审核

日期：2026-10-27。范围：145c6c1 之后全部未提交文件，包括类型化 worker、信号数据、Task 快照转发、桌面入口、测试/示例及文档。按用户要求审核、修复并提交；以下为当前提交归档结论，之前自检的未提交描述保留为历史。

发现并修复 P2 预算一致性问题：正常 drain 和入口门禁拒绝路径先从 pending/active 移除票据，再释放数据，并发发射可在旧数据仍存活时重用容量。新增 Retirement 批次在锁内接管数据并保留条数/数据账目，在 owner 锁外析构完成后才归还预算；snapshot 和关闭判定也覆盖释放栈。新增私有控制块回归，不扩展公开类型白名单或生产测试钩子，在真实释放栈中重入 reserve，验证释放期间拒绝、释放后可恢复。修复前 0/1（drain 路径实际失败），修复后同时覆盖 drain/门禁失败两条路径，连接专项 34/34；日志为 build/review-data-before.log 和 build/review-data-after.log。

其余审核核对复制/取消/旧代/原生快照、异步参数及 worker 结果寿命、任务完成仲裁、错误退出保留、头文件依赖与构建隔离；未发现其他阻止本次提交的问题。README 原启动命令仍引用旧 E:/Qt，已改为桌面入口，直接运行示例也明确临时 Qt PATH。入口成功/缺 kit 失败时逐项比对调用方环境保持；原生 stderr 警告退出 0 被接受，退出 7 被拒绝。

最终 Ela/MSVC Debug、Release 各 418/418，GCC Debug、Release 各 209/209，ASan connection/worker/快照专项 68/68；17 项信号/释放预算检查及示例在 Debug、ASan 各连续 20 次通过。日志为 build/review-data-<debug|release|asan>.log、build/review-data-core-<debug|release>.log、build/review-data-repeat-<debug|asan>.log；头/工厂正向与拒绝检查沿用本轮记录。文档本地链接及 git diff --check 通过。未改 sources/research/第三方代码或用户配置，未补记人工确认；Qt/系统预编译库未插桩，未把这些证据外推到模块 ABI/物理卸载。

## P3 Q2f 类型化 Qt 信号工作区自检

日期：2026-10-27。范围：本轮 observeValues、票据/数据寿命、类型约束、测试及示例；本轮自检，不声称独立审查或已提交。

核对原生桥仅持宿主租约及上限，不捕获业务函数；白名单排除指针/视图/自定义类型，Qt 字符串独立冻结，且保留空值及嵌入零字符。先登记票据再复制，复制中和待释放记录也占预算；节点转移保持 pending/active 的原子容量边界，消息丢弃只标记，数据在 owner 锁外释放，Task 观察者跨线程只写宿主结果。关闭等待复制完成、原生租约、全部业务及数据，再删除 QObject；应用先退出继续保守保留。模板 thunks 和 tuple 不提供 ModulePin 或 ABI 卸载保证。

验证中修正测试把资源 close 与 effect 退役混用，以及空参数包的 GCC 未使用变量警告。最终 Ela Debug/Release 各 417/417，GCC 各 209/209，ASan connection/worker/快照专项 67/67；16 项新增信号检查/示例 Debug、ASan 各连续 20 次通过。独立 GCC/MSVC 允许工厂语法检查及指针载荷编译期拒绝通过；不混称 Qt/GCC 运行或 ABI 验收。依赖 Qt/系统库未插桩，未注入分配器耗尽，也未改变 UI 或人工验收状态。完整证据见 [Q2f](p3-development.md#q2f-类型化-qt-信号连接)。

## P3 Q2e 与本机桌面入口工作区自检

日期：2026-10-26。范围：145c6c1 后本轮类型化结果、内部快照转发、测试/示例、桌面环境入口和文档；本轮自检，不声称独立审查或已经提交。

确认类型化成功仅由 QtWorker 完整 result 门禁触发；后台只写一次冻结快照，owner 发布时不调用 T 的构造函数；退出协议提前失败不读取仍在执行的工作数据。任务快照与模块资源寿命分别约束，支持不可复制值，但不为借用 GUI、跨 ABI 或物理卸载背书。新增冻结移动失败和发布禁止移动场景，普通失败/取消/旧代/应用先退出均终结数据任务；失败保留路径沿用原生 worker。

环境自检发现并修复：Windows PowerShell 重定向 stderr 导致 CMake 警告误报失败、UTF-8 无 BOM 中文注释吞行、MSVC 仅装中文资源时错误使用英文依赖前缀。对旧依赖账目为 0 的对象执行 Ela 两配置干净重建；新编译器依赖记录已包含实际头文件。VS 开发环境加载器还会改写空变量，桌面入口改为隔离子进程，成功与失败时均逐项比对调用方环境保持。run 通过原生退出码报告失败，原生窗口验证已应用未保存目标的退出刷新返回 0。

最终 Ela/MSVC Debug、Release 各 401/401，GCC Debug、Release 各 209/209；ASan Core/Qt/worker/快照专项 34/34，新类型化场景及示例 Debug、ASan 各连续 20 次通过。GCC C++17 无 Qt include 路径独立实例化新头与不可复制工厂通过；源 SDK/内核仍不引入 Qt 类型。Qt 及系统预编译库未插桩。原生桌面代理复核证据见 [Q4d3 记录](q4d3-desktop-verification.md)，用户人工确认仍单独待办。

## P3 Q2d 托管 worker 提交前审核

日期：2026-10-26。范围：ef088eb 之后全部未提交的 QtWorker、取消/诊断 SDK、Core 友元、示例、构建、测试及文档，包含新增文件；sources/research 和第三方组件源码未修改。

发现并修复两类 P2 时序问题：

- 自动退役阻断业务延续。快速工作在当前 owner 任务内完成，原实现从初始化观察者直接调用 effect.close，Core 清理先于已登记的 ready 延续进入队列；已完成 result 的晚订阅延续也可能排在本次清理窗口内。两种延续使用仍有效的 Context 都得到 ContextClosing。现在资源完成后先排队宿主退役通知，Core 再排队清理，ready 延续先运行；正常业务结果等待本 effect 清理确认，消除自身清理窗口对结果消费的干扰。
- 过早保留暂态门禁错误。原生完成时其他 effect 正在清理，原实现把此刻的 ContextClosing 固定为业务失败，即使真正交付结果前清理已经结束。现在原生完成只选定工作错误/协作取消，Context 门禁在真正发布时重新判断；旧代仍不能交付成功。

ready_context/result_context 修复前 0/2（build/review-worker-before.log）；cleanup_overlap 在初版修复后的旧门禁选定路径仍真实失败（build/review-worker-overlap-before.log）。修正后新增场景与既有 worker 专项 22/22，见 build/review-worker-targeted.log。另增 retirement_delivery，确定性在资源已关闭、Core 退役尚未确认时关闭 scheduler，确认等待中的结果会失败终结。所有时序来自实际私有 Qt 循环和 owner FIFO，不依靠 sleep。

资源和账目分别保留：close 继续是可独立于业务 scheduler 排空的物理资源屏障；正常 result 还等待本 effect 退役；retired/retirementBlocked 明确记录 Core 确认及交付故障。退役期间只保持一个可丢弃的宿主通知，投递失败不永久挂起结果，也不伪报 Kernel 静止。工作原有错误优先保留，Core 清理失败保留原因链。worker 内部通知不调用插件，原生线程/TLS、捕获 owner 析构、错误退出保留和 Core 无 Qt 边界保持。

规范与质量：核对每个新增具名类型/函数的中文 Doxygen、直接标准头依赖、原生句柄不外泄、跨线程只读/锁下字段、初始化/关闭重入及最后句柄异线程释放；删除未使用的 cstdint，统一新增代码及第一方改动行格式。SDK 和 ADR 补齐 ready/result/close 及物理/账目状态的区别。Windows/MSVC、一次工作、void 结果的范围保持，不扩大到其他原生线程后端、跨 ABI 或物理卸载。

审查后原目录 Ela/MSVC Debug、Release 各 389/389，无 Qt GCC Debug、Release 各 208/208，ASan UI/Loader/connection/worker 236/236；Qt-only Debug worker 专项 22/22，22 项 worker 检查在 Debug、ASan 中均各连续 20 次通过。完整日志为 build/review-worker-final-<debug|release|asan>.log、build/review-worker-core-gcc-<mode>.log 和 build/review-worker-qt-only.log；重复见 build/review-worker-repeat-<debug|asan>.log。

2 个公共头在 GCC/MSVC C++17 独立包含通过，不需要 Qt 头路径。14 个改动文件 UTF-8/LF、第一方改动范围格式、Core 7 个单元/普通 Ui 依赖隔离、3 个相关实现/测试/示例单元 ASan 插桩、本地链接/锚点及差异空白检查通过，见 build/review-worker-audit.log。预编译 Qt/系统库未插桩。未发现其他阻止本次提交的问题，本次将 Q2d 与审查修复一并提交。Q4d3 新增原生人工体验、其他线程后端、类型化结果和跨 ABI/物理卸载保持后续范围。

## P3 Q2c 托管 Qt 连接提交前审核

日期：2026-10-26。范围：64c0a4f 之后全部未提交的 QtConnection、Core 友元/内部任务身份访问、Qt 示例、构建、测试与文档，包含新增文件；复核中补修既有 Home 测试。sources/research 及第三方源码未修改。

发现并修复：

- P2，断连重入提前释放捕获。Qt 5.15.2 在调用 disconnectNotify 前可能已经释放 slot functor；通知再次调用资源 close，原实现只检查 nativeRetired，即会在外层原生断连仍运行时释放业务捕获。如果捕获拥有发送者或其协作资源，会缩短这些资源的有效窗口。现在托管原生断连统一经过 owner 栈深度保护，嵌套关闭不能推进捕获/接收对象销毁，最后一个断连调用返回后才释放。[固定版本实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qobject.cpp#L4679)
- P3，正常取消误报投递故障。close 已撤销票据后，scheduler 清空宿主空通知仍设置 deliveryBlocked，导致正常取消呈现错误诊断。drop 现在仅在确实移除有效票据时记丢弃和故障；真实未取消投递失败保持原语义。
- P2，既有 Home 启停测试使用失效控件。加载会以新身份重建卡片，原测试跨事件循环借用旧按钮裸指针并等待瞬态 disabled，完整 Debug 和单独重复均触发 SegFault。QPointer 诊断在同一位置真实确认旧按钮失效。改为守卫指针、同一输入窗口同步发送真实鼠标按下/释放，并等待新实例/新卡片稳定结果；原有 busy/gate 专项继续验证加载中的禁用状态，产品 Home 逻辑未修改。

两项新增连接回归修复前 0/2（build/review-connection-before.log），修复后连接专项 17/17（build/review-connection-fixed.log）。Home 原始失败保留于 build/review-connection-verified-debug.log、build/review-connection-power-debug.log；守卫诊断见 build/review-connection-power-probe2.log（button at 467）。此前 ASan 单独重复 30 次通过，不把它误写成复现或排除依据。

规范和质量：审查跨线程发射、所有权固定、注册期间关闭、旧代门禁、异步 Task 完成、回调/捕获析构重入、发送者销毁、容量、投递失败及应用提前退出；长快照聚合改为具名字段赋值，降低布尔/计数字段位置误配风险；用 exception 直接包含对应标准类型，补齐中文 Doxygen，只格式化第一方改动行及新增文件。Qt-only 示例不依赖 Ela，Core 不包含 Qt 头，连接资源屏障不代替 Kernel 总体静止。

审查后原目录 Ela/MSVC Debug、Release 各 367/367，无 Qt GCC Debug、Release 各 208/208，MSVC ASan UI/Loader/connection 214/214；Qt-only Debug 连接专项 17/17。日志为 build/review-connection-final-<debug|release|asan>.log、build/review-connection-core-gcc-<mode>.log 和 build/review-connection-qt-only.log。三项问题回归在 Debug、ASan 中均各连续 20 次通过；见 build/review-connection-repeat-<debug|asan>.log。

3 个公共头在 GCC/MSVC C++17 独立包含通过，其中 instance.hpp/task.hpp 不需要 Qt 头路径；GCC 对 connection.hpp 仅做语法检查，不声明与 MSVC Qt 库二进制兼容。17 个改动文件 UTF-8/LF、改动范围格式、Core 7 个单元/普通 Ui 依赖隔离、4 个相关实现/示例/测试单元 ASan 插桩、本地文档链接与差异空白通过，见 build/review-connection-audit.log。预编译 Qt 未插桩。信号仍限零参数触发，任意插件 worker、跨 ABI/物理卸载及 Q4d3 新增原生体验另验。本次将 Q2c 开发与本节审查修复一并提交。

## P3 配置子树、会话与后台读写提交前审核

日期：2026-10-25。范围：02a2ed7 之后全部未提交的 Q4c2c2 配置子树、Q4e1 会话/配置页、Q4e2 Home/管理路由、Q4d3 后台 I/O/监听、SDK、示例、构建、测试及文档，包含新增文件。sources/research 与第三方组件源码未修改。

发现并修复五项 P2 问题：

- 编辑重入错误采用旧候选：preview 的同步驱动预检重入编辑时，页面仍将旧输入的候选设置为可应用；apply 期间编辑被错误标记为已应用，启停期间编辑会被 useDesired 覆盖。现在各入口固定输入快照，预览期间变化则丢弃旧候选；应用/启停接受的目标保持真实结果，同时保留期间的新编辑并禁止基于它进行旧目标启停。局部候选副本保证不跨外部调用借用可被信号清除的 optional。
- 监听注册窗口误报自身提交：初始原文读入后、watcher 构造前完成一次自身提交，构造时查询的最新提交序号会使首轮复读把自身保存当作外部编辑。watcher 初始参数收紧为同一后端 read 返回的 ConfigurationDiskObservation，直接使用与初次读取配对的提交游标；历史自身内容的外部回退仍须通知，不能用永久 hash 白名单。
- 未观察提交的外部回退漏报：后台保存新内容后，外部在 watcher 复读前改回上次已观察的旧内容，原实现仅比较旧观察 hash，漏掉这次覆盖。现在新自身提交若与实际磁盘不符，即使磁盘等于旧观察，也发布外部通知。重复复读仍消重，不改变 Store 基线或目标。

- 退出检查隐式阻塞：初版把 QThreadPool.waitForDone(0) 当作非阻塞 join 检查，但 [Qt 5.15.2 源码](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/thread/qthreadpool.cpp#L261) 的 reset 仍可能无超时等待线程。改为单个惰性启动、保持至关闭的 QThread，加显式 FIFO/条件变量和 wait(0) 检查；不反复启动线程，不使用全局池、terminate 或 detach。
- 目录重建后丢失观察：读取期间父目录删除并重建，旧读取失败返回后虽然重新登记监听，却未再次读取已存在的新文件；若该修改发生在断连窗口，可能一直等不到下一次通知。现在维护检查同时核对实际存在性，重新登记后再复读。既有 directory 在重复/全量检查中真实失败，修正后 Debug 连续 100 次通过，见 build/review-config-directory-repeat.log。

新增 editor_reentry、late_watch、own_revert 三项真实回归，修复前 0/3，见 build/review-config-before.log；三项初始修复后 3/3，见 build/review-config-fixed.log。编辑测试通过真实 Kernel 驱动预检窗口注入 owner 重入；文件测试以 worker 完成条件暂停 owner 泵送，确定性覆盖提交/观察间隙，不靠 sleep。测试等待门改为共享状态，超时退出后晚到观察也不借用已销毁栈。

规范与质量：仅格式化第一方改动行和新增文件，核对参数对齐、直接 include、中文 Doxygen 与 UTF-8/LF。配置管理表复用原单元格，仅值变化时更新，减少周期性重建及模型信号；更新经模型接口，不跨信号保留可失效 item 指针。删除后台执行器未使用的头依赖。配置初始元数据、内容观察、外部通知和 Store 保存基线职责分开；普通 Ui/Core 不引入 Loader 依赖。复核父子清理、自动重建、CAS/关闭重入、弱来源、保护 Runtime、队列容量、读取/提交串行、线程 join、监听恢复与投递失败路径。

第一次全量还曾发生一次 late_watch 超时，保留于 build/review-config-ela-msvc-debug.log；原执行器的三项新增回归随后连续 100 次通过，因此不把该超时根因单独认定为线程池问题。提交前仍依据上述实现不符修正执行器，随后继续完整回归与重复测试。目录恢复失败另保留于 build/review-config-worker-repeat.log 和 build/review-config-final-asan.log。

审查后原目录 Ela/MSVC Debug、Release 各 350/350，无 Qt GCC Debug、Release 各 208/208，MSVC ASan UI/Loader 197/197（UI 88、Loader 133，其中 24 项重叠）。日志为 build/review-config-verified-ela-msvc-<mode>.log、build/review-config-core-gcc-<mode>.log 和 build/review-config-verified-asan.log。修正后的三个新增场景及目录恢复在 Debug/ASan 中各连续 20 次复核，另有目录恢复 Debug 100 次证据。

5 个公共头在 GCC/MSVC C++17 独立包含通过，其中 3 个无需 Qt 头路径。47 个变更文件 UTF-8/LF、Core 7 个编译单元/普通 Ui 的依赖隔离、15 个相关单元 ASan 插桩、本地文档链接/锚点及差异空白通过，见 build/review-config-audit.log。预编译 Qt 未插桩。未发现其他阻止提交的问题。本次将上述四轮开发及审查修复一并提交。Q4e1/Q4e2 的用户人工确认保留；Q4d3 新增原生体验、任意插件 connection/worker、ABI 和物理卸载仍按各自计划验证，不由本轮自动化代填。

## P3 父代上下文与子实例桥提交前审核

日期：2026-10-25。范围：8befcbd 之后全部未提交的 whenActiveAfter、类型化 Context.inject、示例、构建、测试及文档，包括新增文件。sources/research 和第三方组件源码未修改。

- P2，通知重入停止遗漏取消：发布成功上下文前把全部合格 Promise 从实例登记中移走，首项完成向调度器投递回调时重入 stop，后面的未完成观察不在取消集合中，stop 返回后仍可成功。现在完成通知期间保留原登记；stop 能同步取消其余任务，完成后只清除已终结记录。首项历史成功保持，不因后续停止改写。
- P2，新观察回放退役代次：在发布 ready/active/target 后才收集上下文观察，目标通知投递重入 update 并在退役期间登记新观察时，它被混入旧成功代次的完成集合，收到旧配置和已失效 Context。现在激活成功时先冻结观察集合及上下文快照，新登记只等待后续成功代次。
- 规范与质量：仅格式化第一方改动行和新增文件，整理参数对齐、直接 include、中文 Doxygen 及测试调度边界说明。冻结集合与保留可取消登记各自承担通知/生命周期职责，遍历快照不持有可被重入修改的容器引用，完成后统一清除。Runtime 不增加配置变体，Context 仍只弱持实例；Core 无 Loader/Qt/Ela 依赖。

新增 reentry_stop/reentry_update 使用真实 FakeScheduler FIFO，虚拟 post 边界只重入宿主，不提前执行任务。修复前 0/2，见 build/review-activation-before.log；修复后含既有观察及示例 7/7，见 build/review-activation-fixes.log。验证既有成功不改写、stop 返回前取消、退役期间登记不回放及新代配置/身份，不以业务回调被执行替代任务完成证据。

修复后原目录 Ela/MSVC Debug、Release 各 294/294，无 Qt GCC Debug、Release 各 190/190，MSVC ASan 相关回归 47/47。日志为 build/review-activation-<ela-msvc|core-gcc>-<mode>.log、build/review-activation-asan.log。instance.hpp 在两套 C++17 编译器中独立包含通过；12 个改动/新增文件 UTF-8/LF、Core 的 7 个编译单元依赖隔离、新增/修改单元 ASan 插桩、文档本地链接/锚点及差异空白通过，见 build/review-activation-audit.log。预编译 Qt 未插桩。

未发现其他阻止本次提交的问题。Q4c2c1、示例及上述修复已提交为 02a2ed7。提交时显式配置子树协调、管理页保存装配和其余 P3 工作仍待完成，本次不修改 UI 交互或人工验收记录。

## P3 Kernel 配置驱动与 Home 生命周期操作提交前审核

日期：2026-10-25。范围：4f743ed 之后全部未提交的 Q4c2a/b、实例附加依赖/重新加载、Home 状态卡片/左右开关、管理控制器、示例、测试及文档，包括新增文件。sources/research 和第三方组件源码未修改。

- P1，Core 内部投递失败可能永远等待：IScheduler 仍开放时，单次拒绝内部激活任务会使实例标记 deliveryBlocked，但驱动只观察未完成的稳定目标，动作和关闭可能一直等待，快照还显示正常 Pending。现在统一检查自有实例及 EffectScope 投递状态，挂载时发现阻断立即终结动作，业务和快照入口返回明确错误；关闭仍向其他自有实例广播停止，再返回失败并保守保留账目。internal_delivery 同时覆盖首次投递被拒绝、后续激活回执被拒绝和健康实例仍收到停止。
- P2，首页忙碌状态误显示为关闭：开关位置绑定 canStop，管理加载期间 busy 会使其变为 false，实际 Loading 实例显示关闭。现在把真实启用状态与操作权限分离，Loading/Active/Pending 显示开启，操作期间仍禁用；刷新屏蔽业务信号。power_busy 使用显式激活 gate 验证等待中的真实状态及没有多余管理请求。
- P2，重新加载已移动句柄崩溃：Kernel.remount 在读取来源快照前没有检查句柄，移动后空控制块导致空指针访问。现在返回 InvalidArgument；diagnostics_remount 增加已移动来源和保留副本验证。
- 规范与质量：仅格式化第一方改动行及新增文件，展开过度压缩语句，整理 include、直接依赖和中文 Doxygen。复用 Core 描述符校验、驱动健康检查及 Qt 状态等待辅助函数；明确停止缓存和操作历史各自的边界，不强行释放非静止实例。保留固定契约、有效依赖比较、部分状态再规划和真实稳定任务的分离。

三项回归在修复前 0/3，含真实崩溃；修复后 3/3。证据为 build/review-kernel-home-before.log、build/review-kernel-home-fixes.log。既有 delivery 回归同步要求阻断快照返回错误，并独立检查 Kernel 中资源仍然保留，不能把诊断错误解释成排空。

修复后原目录 Ela/MSVC Debug/Release 各 287/287，无 Qt GCC Debug/Release 各 183/183；MSVC ASan 的 UI/Loader 141/141（64 项 UI、77 项 Loader），Core 契约/remount 10/10。日志为 build/review-kernel-home-<ela-msvc|core-gcc>-<mode>.log、build/review-kernel-home-asan.log、build/review-kernel-home-core-asan.log。Kernel、ConfigKernel、管理 SDK 的无 Qt 独立包含、189 项本地文档链接、第一方 UTF-8/LF、差异空白及 Core 依赖隔离检查通过；修改单元的 ASan 插桩已核对，预编译 Qt 未插桩。

未发现其他阻止本次提交的问题。Q4c2a/b、Home 生命周期操作及上述修复已一并提交为 8befcbd；此前桌面人工验收确认保留，Home 变更仍待专项人工复核。显式配置子树、管理页保存装配、watcher/worker、ABI 与物理卸载继续按计划推进，ASan 不构成泄漏或物理卸载证明。

## P3 Ela 工作台与 Home 提交前审核

日期：2026-10-25。范围：88edada 之后全部未提交的工作台菜单/搜索/导航历史、插件直达与空工作区、Home 卡片、插件内部布局、Ela 本地修正、示例、测试及文档，包括新增文件。Core/Qt 适配实现、sources/research 未修改；公开 UI SDK 只更新页面贡献注释，未新增 Ela 类型。

- P1，首页刷新重入关闭后继续写控件：500 ms 刷新会发送卡片属性信号，回调可关闭外壳并推进事件循环，使根窗口/卡片先销毁，随后继续读取卡片详情。home_refresh_reentry 修复前被 ASan 报告 access-violation，栈位于 ElaPopularCard::getDetailedText。现在首页刷新与菜单操作通过宿主窗口访问屏障执行，关闭等待本轮写入栈返回；访问桥先复制再调用，刷新嵌套不重复进入。
- P2，关闭页签回落到失效页面：只尝试相邻页，导航门禁拒绝后仍删除当前页签，中央还显示已关闭视图。tab_fallback 修复前确认没有进入空工作区。现在按快照依次尝试已打开页，只有实际选择改变才删除原页签；均不可用时显示空画布，重入仍保持原页时保留其页签。
- P2，重入查询结果与输入不一致：textChanged 或模型复位回调规范化输入后，外层仍按旧文本发布建议，回车可打开错误页面。search_normalization 修复前真实失败。现在程序查询读取规范化后的实际文本，模型复位返回后再核对当前输入并收敛结果；平面建议模型对有效父索引返回零行。
- P2，卡片迁移后保留失效悬浮区域：卡片移到新容器、旧区域销毁后仍返回旧区域裸指针，按钮更新和绘制还依赖已销毁浮层。card_area_lifetime 修复前失败于悬空区域检查。现在区域/浮层均用 QPointer，缺少浮层时仍可更新/绘制卡片，重新指定区域可恢复浮层，悬浮定时器跳过失效区域。
- 规范与质量：展开新第一方文件中过度压缩的语句，统一缩进与 include 次序，补齐直接标准库/Qt 依赖，移除无用 include 和重复历史状态更新。卡片固定文案仅初始化一次，未增删卡片时不重复使流布局失效；提示文本转义，避免插件名称被当作富文本。第三方目录只修改相关本地补丁，不整体格式化上游文件。

前三项修复前 0/3 通过，证据为 build/review-ui-before.log；第四项见 build/review-ui-card-before.log。前三项定向修复验证见 build/review-ui-fixes.log。新增回归均检查真实 Qt 循环和完整关闭屏障，卡片迁移检查同时验证重新显示浮层。

修复后原目录 Ela/MSVC Debug/Release 各 259/259，无 Qt GCC Debug/Release 各 158/158，UI ASan 61/61 通过。日志为 build/review-workbench-ela-msvc-<mode>.log、build/review-workbench-core-gcc-<mode>.log、build/review-workbench-asan.log。新增首页/工作台、搜索及卡片/浮层等修改单元已核对 ASan 插桩，预编译 Qt 未插桩；pages.hpp 的无 Qt 独立包含检查及差异格式检查通过。

未发现其他阻止本次提交的问题。明暗/150% 缩放布局、空画布及示例自动化继续通过；原生 Windows 桌面人工清单保持待验，未代填为完成。Kernel 配置运行树、watcher、后台 I/O、管理页保存与 ABI 仍按原计划推进，ASan 不构成泄漏或物理卸载证明。

## P3 Q4d1/Q4d2 提交前审核

日期：2026-10-24。范围：7864de9 之后全部未提交的文件事务、Store、Qt 写后端、SDK 值类型/错误分类、构建、测试与文档，包括新增文件。sources/research 及 Core/Qt/UI/Ela 实现未改动。

- P2，Unchanged 回执缺少基线一致性校验：通用 Store 只检查磁盘值形状及目标文本，会接受“缺失文件变为存在”或 hash/原文与基线不一致的 Unchanged，错误推进 persistedRevision 并清除 dirty。现在要求此类回执的路径、存在性、hash 和原文均与提交前基线一致；非法回执形成 Failed，保留保存历史和 dirty，后续项 Skipped，关闭不能虚报刷新成功。
- 新增 p3.store_unchanged_protocol，覆盖缺失基线、hash 漂移、原文漂移、队列跳过及失败关闭。修复前实际失败于保存结果断言，见 build/review-q4d-before.log；修复后 13 项无 Qt Store 场景通过。缺陷位于通用 Store 对后端协议的校验边界，测试使用受控回执，不冒充真实磁盘写入证据。
- 规范与质量：集中基线快照比较；文件大小只查询一次，查询失败归 ConfigIo、超限归 ConfigInvalid；补齐 Qt 写后端空目标错误的 loader.file.target 阶段和标准库直接依赖。核对 UTF-8/LF、中文 Doxygen、公共头独立包含、队列容量、同修订共享、修订单调性、错误原因链、历史任务稳定性、关闭刷新与保守保留。
- 文件及依赖边界：核对原始字节/hash、缺失/空文件、路径绑定/链接拒绝、协作锁、二次基线比较、QSaveFile 禁止直接写回退及临时文件清理；真实文件/锁失败和 Windows 句柄导致的提交失败均保留旧文件。ConfigStore 保持无 Qt；ConfigStoreQt 复用文件事务，不新增第三方库或修改其源码。

修复后 Ela/MSVC Debug、Release 各 243/243；无 Qt GCC Debug、Release 各 158/158；MSVC ASan 56/56 Loader 场景通过，含 17 项 Store 场景。日志为 build/review-q4d-ela-msvc-<mode>.log、build/review-q4d-core-gcc-<mode>.log、build/review-q4d-asan.log。新库和测试均按现有配置插桩，预编译 Qt 未插桩。第一方暂存差异及新文件格式检查通过，未发现其他阻止本次提交的问题。

本次一并提交 Q4d1/Q4d2。Qt 写后端当前同步执行 I/O；后台 I/O、watcher、Kernel 运行树、管理页保存及全应用装配仍待后续实现。原生桌面人工验收未代填；非协作编辑窗口、网络/断电等文件边界沿用 ADR-0014，投递阻断保守保留资源，ASan 不构成泄漏或物理卸载证明。

## P3 Q4a/Q4b/Q4c1 提交前审核

日期：2026-10-24。范围：4b6139b 之后全部未提交的配置结构/预览、受限 YAML、内置定义/schema、通用协调器、SDK、示例、固定 LibYAML、构建、测试及文档，包含新增文件。sources/research、Core/Qt/UI/Ela 实现均未改动。

- P1，规划返回后的执行门禁缺失：驱动 plan 可同步重入并关闭 scheduler，原实现返回后仍调用 apply，导致无法投递回执时继续启动资源。规划抛错路径还可能先发布正常的失败报告，掩盖投递故障。现在集中检查投递能力，在同步规划返回、发布规划失败及下一步执行前重新核对；调度器已关闭时冻结协调，向未完成任务发布 SchedulerClosed。完成旧工作项后也重新检查关闭入口。
- 回归 p3.coordinator_planning_gate 在修复前真实失败（build/review-q4-before.log），确认关闭后仍产生驱动调用；修复后覆盖规划正常返回、抛错及重入协调器关闭，检查不启动动作、目标任务错误/取消结果及真实关闭证据。
- 规范与质量：修正公共类型和 safeToContinue 的过期/不准确注释，复用投递检查，保持 owner 状态与跨线程 Promise 门禁分离。为新增 C/YAML 类型及 LibYAML 许可固定 LF，避免 Windows 检出改变上游字节。核对 YAML 类型/重复键/预算、候选移动失效、schema 捕获释放、有效依赖联合、CAS 重入、最新目标、部分成功、Pending 历史及保守资源保留；核对公开头独立包含、C/C++ 工具链与静态库边界。
- 依赖来源：LibYAML 0.2.5 固定归档 SHA256 与 UPSTREAM.md 一致，10 个源码文件与本地原始解压目录逐一比对，均未修改；MIT 许可保留，构建不联网。导入原文已有空白不重排，第一方改动单独执行暂存格式检查。

修复后 Ela/MSVC Debug、Release 各 215/215；无 Qt GCC Debug、Release 各 145/145；MSVC ASan 28/28 Loader 场景通过，第一方库和 LibYAML C 库均插桩。日志为 build/review-q4-ela-msvc-<mode>.log、build/review-q4-core-gcc-<mode>.log、build/review-q4-asan.log。未发现其他阻止本次提交的问题。

本次完成 Q4a/Q4b/Q4c1，通用协议测试不代表 Kernel 运行树驱动或完整 L01–L04/S05 已验收。配置执行/保存、原生 Windows 人工交互、外部模块及 ABI 继续待办；非静止与投递阻断按契约保留资源，ASan 不证明泄漏或物理卸载安全。

## P3 Q3c 提交前审核

日期：2026-10-24。范围：8f15edd 之后全部未提交的 Ela 组件迁移、项目内基础库、原生导航与工作台布局、宿主模型、演示、测试和文档，包括新增文件。核对固定上游库与本地修改，保留 MIT 许可和来源；sources/research、Core 和公开 SDK 未改动。

- P1，紧凑菜单残留：菜单动作捕获节点裸指针，撤页未移除动作，分组菜单还以可复用的节点地址作键。compact_removal 在修复前确认旧动作仍存在。现在菜单/动作使用稳定 key，触发时重新查找节点，撤页同步删除旧动作、空菜单及已撤销分组的菜单；同组其他页面仍可导航。
- P1，恢复模式访问已删除分组：紧凑模式缓存展开节点裸指针，插件停止后恢复展开模式会解引用旧分组。removed_expanded_group 在修复前被 ASan 确认为 heap-use-after-free。现在只缓存分组 key，恢复时从当前模型解析，跳过已删除分组。
- P1，原生导航关闭重入：原生切页信号中可关闭外壳并处理 DeferredDelete，返回后仍访问导航私有对象和旧节点；程序导航还会用旧节点安排滚动。native_close_reentry 在修复前被 ASan 确认为 heap-use-after-free。现在切页/页脚信号返回后检查 QPointer 和模型归属，程序导航保存持久索引，窗口或节点消失时直接结束；鼠标释放路径也检查视图存活，宿主选择同步使用导航访问屏障。
- P2，通知返回序号：输出 textChanged 信号中重入发布通知会更新共享 sequence，外层返回后一个通知的序号。增强 notifications 在修复前实际失败。现在提交历史后保存本次序号，展示回调重入不会改变返回值，历史仍有界且为纯文本。
- 规范与质量：合并两个带分组的 addPageNode 重载中重复的四条菜单创建分支，集中处理动作创建与清理；拆开新增选择同步中的嵌套单行控制流，补齐 QMouseEvent/QPointer/QStringList 直接依赖并移除无用包含。核对模型项所有权、分组隔离、导航门禁、固定标签归属、主题适配、输出限额、关闭顺序、CMake 可选构建、原始许可与资源分发。

修复前证据：build/review-q3c-before.log（菜单残留和分组 UAF）、build/review-q3c-native-before.log（原生关闭 UAF）、build/review-q3c-notification-before.log（序号重入）。修复后 Ela/MSVC Debug、Release 各 187/187；无 Qt GCC Debug、Release 各 117/117；MSVC ASan 的 45/45 UI 场景通过，第一方与项目内 Ela 插桩，预编译 Qt 未插桩。完整日志为 build/review-q3c-ela-msvc-<mode>.log、build/review-q3c-core-gcc-<mode>.log、build/review-q3c-asan.log。未发现其他阻止本次提交的问题。

格式检查区分导入原文和本地改动：`git diff --cached --check -- . ':(exclude)third_party/ela/ElaWidgetTools/**'` 通过；Ela 相对固定上游目录的本地补丁没有新增空白告警。整个导入目录的暂存检查会报告上游已有的文件尾空行及少量行尾空格，保留原文以便后续比较，不为这些告警重排库源码；记录为 build/review-q3c-staged-check.log、build/review-q3c-vendor-check.log。

原生 Windows 缩放、主题动画和完整键鼠交互仍按 [桌面验收单](../tests/acceptance/p3-ui.md) 人工执行，未代填验收结果。清理失败/投递阻断按设计保留资源；ASan 结果不构成泄漏或 DLL 物理卸载证明。Loader/Store、模块管理和专用资源适配仍按后续工作包推进。

## P3 Q3b2 提交前审核

日期：2026-10-24。范围：4f3b808 之后全部未提交的管理页、诊断 SDK、Kernel/Instance 查询、外壳集成、示例、构建、测试与文档，包括新增文件。sources/research 未改动。

- P1，导航关闭重入：showManagement 切换堆栈会同步发出 Qt 信号；信号中关闭外壳并处理 DeferredDelete，可在切页调用返回前删除窗口，随后继续访问导航对象。navigation_reentry 在修复前实际 SegFault。现在程序入口与导航信号共用 selectManagement，以导航访问计数阻止窗口提前关闭；切页/选中信号返回后重查门禁，重入关闭返回 ContextClosing。回归验证信号栈内窗口仍存活、关闭未完成，调用返回后可完整删除。
- P2，取消选择仍可操作：QTableWidget.clearSelection 会保留 currentRow；原实现把当前单元格当作已选行，停止按钮仍可用，下一次刷新还会恢复旧选择。deselect 在修复前实际失败。现在只从 selectionModel 的唯一已选行解析 InstanceId，清除选择立即禁用操作，刷新保持无选择，重新选中后才开放操作。
- 规范与质量：管理页导航收敛到单一入口，补充 QItemSelectionModel 直接依赖及中文 Doxygen。核对 SDK 的值语义和无 Qt 头隔离、弱 Kernel 生命周期、同 scheduler/owner 门禁、依赖诊断不增加租约、请求去重/真实操作号/有界历史、纯文本展示、清理失败与投递阻断保留、UI 访问屏障和正常关闭顺序。未修改第三方代码。

修复前证据为 `build/review-q3b2-before.log` 和 `build/review-q3b2-navigation-before.log`；修复后 Ela/MSVC Debug、Release 各 178/178，无 Qt GCC Debug、Release 各 117/117。management.hpp、ela_shell.hpp、kernel.hpp 的 GCC C++17 独立包含及 git diff --check 通过。完整日志为 `build/review-q3b2-ela-msvc-<mode>.log`、`build/review-q3b2-core-gcc-<mode>.log`。

MSVC ASan 的 36/36 UI 场景通过，第一方与固定 Ela 库插桩，Qt 预编译库未插桩；日志 `build/review-q3b2-asan.log`。清理失败/投递阻断测试按设计保留未静止资源，该结果不构成泄漏或物理卸载证明。未发现其他阻止本次提交的问题；Loader/Store、配置/模块管理、完整子树、其他专用资源及原生 Windows 人工交互沿计划另验。

## P3 Q3b1 提交前审核

日期：2026-10-24。范围：0d9a5ac 之后全部未提交的命令/通知 SDK、宿主后端、effect 集成、窗口展示、示例、测试和文档，包括新增文件。sources/research 未改动。

- 注册门禁：MSVC std::function 的小对象移动可执行用户代码。旧 add 在检查宿主状态后移动 CommandSpec；若移动中重入 shell.close，随后仍创建命令 effect/列表项，此时宿主服务的关闭结果可能已经完成。现在先保留 Context 值副本，接管回调后重查宿主及本代 guard，再修改索引和创建 effect。新增 registration_reentry，固定在第二次移动触发关闭，确认请求被拒绝、索引和 effect 数量不变。
- 初始化屏障：列表项原先在 effect setup 返回后创建。Qt model 的 rowsInserted 信号中重入关闭并推进 owner 事件，清理会在列表构造返回前释放命令捕获。现在列表创建/登记全部纳入 effect setup，窗口及闭包必须等待初始化返回；token 返回后再补发宿主关闭请求。新增 row_reentry 验证嵌套循环期间捕获仍在、外壳未完成关闭，随后可完整排空。此问题通过生命周期断言复现，未声称已复现物理模块卸载或堆损坏。
- 错误展示：命令失败携带空 message 时，宿主将空文本转给通知服务，被空文本校验拒绝，界面没有失败提示。现在使用“命令执行失败”作为缺省文本。增强 interaction，确认禁用错误和空错误消息都形成可见错误通知。
- 质量和规范：补全 functional/memory/string 直接依赖；投递失败观察者只弱持 CommandControl 并写其原子标志，不再间接访问可在完成时移动清空的 CommandCall.record。增强 inflight，以两个独立 gate 逐个完成并发调用，确认第一个结束后停止仍 pending、最后一项结束后才释放捕获。核对中文 Doxygen、同 scheduler/epoch、错误线程关闭、谓词重入、排队撤销、worker 完成、关闭顺序、有界纯文本历史和无 Qt 构建隔离。

修复前两项失败见 `build/review-q3b-before.log`，列表初始化失败见 `build/review-q3b-row-before.log`。修复后 Ela/MSVC Debug、Release 各 164/164；无 Qt GCC Debug、Release 各 115/115。三个公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3b-ela-msvc-<mode>.log`、`build/review-q3b-core-gcc-<mode>.log`。

MSVC ASan 专项 24/24 UI 场景通过，第一方与固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3b-asan.log`。投递阻断场景按设计保留未静止资源，本检查不构成泄漏检查或任意等待环证明。未发现其他阻止本次提交的问题；管理页、原生 Windows 人工交互、Loader/Store、专用 connection/worker/watcher 及模块 ABI/物理卸载沿原计划另验。

## P3 Q3a 提交前审核

日期：2026-10-24。范围：b5b8bab 之后全部未提交的 Ela 构建、页面服务、外壳、Context.identity、测试、示例和文档，包括新增文件。sources/research 未改动。

- 正确性：PageHandle.close 原先只请求 effect 异步关闭，cleanup 排队期间仍可 navigate 和点击页面。现在 owner 关闭请求同步标记 revoked，输入及导航立即拒绝，retiring 快照也立即体现撤销；实际 QWidget 删除和 effect 完成仍沿原异步屏障。错误线程关闭不改变门禁。lifetime/input_gate 回归在修复前均失败。
- 输入边界：QShortcutEvent 的接收者是 QShortcut，旧过滤器仅识别 QWidget，会漏过停用页面的快捷键。现在沿 QObject parent 找到 QWidget 后判断页面归属。新增 shortcut_gate 验证 Active 能触发、停止后但删除前被阻断。中文输入法 QInputMethodEvent 也会绕过 KeyPress；input_gate 新增真实输入法提交验证，修复前确认修改了已关闭页面。过滤器补齐 InputMethod、MouseMove 和触摸后续事件，保留任意程序信号/外部 queued callback 仍需专用契约的边界。
- 导航：原先只连接 itemClicked，方向键改变选中行却不切换页面。改用 currentItemChanged，让键盘、鼠标与句柄使用同一导航门禁；新增 keyboard_navigation 检查方向键切页及关闭当前页后回安全页。
- 规范与质量：补全 utility/vector 直接依赖、移除未使用头；事件类型判断改为 switch；navigate 按值保留页面控制块，避免 Qt 调用重入时仅借用句柄成员；窗口关闭错误保留原 effect 的 causes；修正工厂释放与记录移除的注释。核对 Qt parent 所有权、工厂失败/关闭重入、同名贡献隔离、服务恢复、线程/epoch、窗口退出顺序、第三方固定 hash/许可、可选构建和脚本环境恢复。

修复前四项失败证据见 `build/review-q3-before.log`；输入法独立失败见 `build/review-q3-input-method-before.log`。修复后 Ela/MSVC Debug、Release 各 151/151；无 Qt GCC Debug、Release 各 115/115；两份新公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3-ela-msvc-<mode>.log`、`build/review-q3-core-gcc-<mode>.log`。

MSVC ASan 专项 11/11 UI 场景通过，第一方和固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3-asan.log`。开发阶段已经复现并规避的上游原生导航 UAF 见 [ADR-0006](adr/0006-ela-page-contributions.md)，本轮未修改第三方源码。未发现其他阻止提交的问题；命令/通知/管理页、原生 Windows 人工交互、connection/worker/watcher 及插件 ABI/卸载仍需后续验收。

## P3 Q2 提交前审核

日期：2026-10-24。范围：ce43cc6 之后全部未提交的 QObjectResource、QtClock、SDK、资源测试、构建登记与文档，包括新增文件。sources/research 未改动。

- 测试正确性：clock_close 原先在预期抛错的 discarded 回调内使用 CHECK。断言失败同样被隔离并计入 callbackErrors，可能被预期异常数量掩盖。现在回调只记录“重入 close 仍 pending”和“新计时被拒绝”的观察值，回调外独立断言，保留有意抛错以验证异常隔离。没有把这项测试缺陷记作生产关闭逻辑 bug。
- 回调所有权：waitTask 的通知标志改为共享值捕获，避免等待超时后栈已展开而排队通知仍访问局部变量。新增 p3.qt_resources_close_releases_handles，覆盖丢弃回调释放最后一个 QtClock facade 后继续完成整批通知，以及子对象析构释放最后一个 QObjectResource facade 后仍保持完整删除屏障。
- 规范与质量：补全直接使用的 memory/utility 标准头，拆开计时账目初始化；公共构造契约明确只有前置条件错误抛 invalid_argument，其他构造失败仍不转移所有权，并与 ADR 一致列出外部 deleteLater/WA_DeleteOnClose 限制。核对 owner 入口、令牌原子取消、已到期队列载荷、析构重入、异常隔离、退出兜底、Kernel 清理集成与无 Qt 构建边界。

Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 140/140；无 Qt GCC 13.1 Debug/Release 各 115/115。两份新公共头独立包含及 git diff --check 通过。完整日志：`build/review-q2-qt-msvc-<mode>.log`、`build/review-q2-core-gcc-<mode>.log`。

另以 MSVC /fsanitize=address、RelWithDebInfo、/MD、/O1 和 /EHsc 单独构建第一方 core/qt 与资源测试，14/14 资源场景通过，日志 `build/review-q2-asan.log`，重现命令见 [P3 Q2 记录](p3-development.md#q2-提交前审查)。Qt 5.15.2 预编译库未插桩，该结果不构成全程序内存安全或泄漏证明；未运行 TSan、connection 排空、worker join、Ela UI、ABI 或物理卸载验证。未发现其他阻止提交的问题。

## P3 Q0/Q1 提交前审核

日期：2026-10-24。范围：2e6efa2 之后全部未提交的 Qt 工具链配置、QtScheduler、SDK、测试与文档；并核对 FakeScheduler 的同类队列操作。sources/research 未改动。

- 正确性：QtScheduler 和 FakeScheduler 原先在队列锁内移动 ScheduledJob。MSVC std::function 的小对象存储会调用用户可调用对象的移动构造及移动后析构；若这些函数查询 isOpen，则重入同一把锁。现在在锁外创建 unique_ptr<ScheduledJob>，队列内只移动指针，执行和丢弃路径也不在锁内销毁用户对象。新增 p3.qt_scheduler_callable_reentry 与 w1.scheduler_callable_reentry，在修复前 MSVC Debug 两项均真实失败，见 `build/review-p3-before.log`；修复后均通过。
- 重入边界：FakeScheduler::runOne 在任务捕获析构前清除了 pumping，捕获的析构函数可提前运行后续任务。现在由后析构的 PumpGuard 恢复门禁，覆盖成功及异常返回。新增 w1.scheduler_destructor_guard；修复前 GCC Release 明确违反“后项仍排队”断言，见 `build/review-p3-guard-before.log`，修复后两个分支均通过。
- 所有权：QtScheduler 的 post/close 在进入用户代码前持有本地控制块，避免移动/丢弃回调释放 facade 与 Qt lifetime 后继续访问已释放控制块。新增 p3.qt_scheduler_close_releases_owner，验证丢弃回调释放全部所有者后抛错，仍完成后续通知；该场景为生命周期防护，未声称完成消毒器验证。
- 规范和质量：新增文件、类型及具名函数使用中文 Doxygen；重入探针由 Qt/Core 测试共用，避免只依赖编译器恰好使用堆存储。核对 owner 限制、锁外析构、关闭/投递竞争、异常隔离、账目与队列一致性，以及 CMake 可选目标、QT_NO_KEYWORDS 传播和开发脚本环境恢复。同步 ADR、接口契约和进度，保留初次验证与本轮审核的独立证据。

验证：Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 126/126；无 Qt GCC 13.1 Debug/Release 各 115/115。日志为 `build/review-p3-qt-msvc-<mode>.log`、`build/review-p3-core-gcc-<mode>.log`；QtScheduler/FakeScheduler 公共头仅使用 SDK include 的 GCC C++17 独立包含检查、git diff --check 通过。未发现其他阻止提交的问题。

本轮仍限 Q0 工具链与 Q1 调度；Q2 Timer/QObject 资源屏障、Ela UI、原生 ABI 与 DLL 卸载未实现或验收，未运行 ASan/TSan。

## W7 提交前审核

日期：2026-10-24。范围：25adfcc 之后全部未提交的联合示例、CTest 登记与验收/进度文档，包括新增文件。未改动核心 SDK 行为。

- 正确性与证据：原 recovery 只检查新 epoch/BindingId，没有实际分发恢复后的事件，无法证明新监听器连接到新服务。现在更换提供者前后均发送保存事件，检查恰好调用一次当前监听器、当前 Store 的实际文档内容和确认结果；旧 Store 必须在替换前过期，新监听器恰好一个。增强现有场景而非增加重复测试入口。
- 防御性检查：绑定读取集中到先验证数量的 binding()，避免空数组越界；清理失败快照先判断 optional 是否存在，弱引用只 lock 一次后读取，避免缺失诊断时空解引用。关闭检查复用不可变快照，并核对根、消费者及提供者的 Disposed、effects 和 committed 数量。
- 状态准确性：失败路径不再记录 consumer cleaned，改为 consumer cleanup failed，并断言提供者及根未运行清理。failure 示例返回成功只表示预期残留得到验证，不表示内核关闭成功。
- 规范与质量：核对所有新增文件、类型、具名函数的中文 Doxygen、标准头依赖和回调捕获所有权；统一资源查询与断言，整理进度工作记录表的空行，避免 Markdown 表格断裂。验收报告引用的 24 个唯一测试名均与 CTest 实际登记核对。

GCC 13.1、MSVC 19.44 x64 Debug/Release 各 113/113 通过，日志为 `build/review-w7-core-<compiler>-<mode>.log`；git diff --check 通过。未发现其他阻止提交的问题。未运行 ASan/TSan、Qt、ABI 或物理卸载验证，首阶段结论仅适用于文档明确的内置范围。

## W2/W3 提交前审核

日期：2026-10-23。范围：全部未提交的 W2 收尾、W3 Kernel/实例实现、SDK、测试及文档。

## 已修复问题

1. 移动后的 Runtime 保留原编号，但激活函数已为空。Kernel 仅核对编号会接受该句柄，随后访问空回调。现在移动后原句柄 id() 返回无效编号，Kernel 拒绝挂载；直接 Instance::mount 在登记资源前抛 invalid_argument。新增回归在修复前实际失败，日志为 `build/review-before-fix.log`。
2. 调度器关闭后 update/retry 可能先替换目标、分配操作，再投递失败或构造 EffectScope 抛错。现在在修改账目前返回 SchedulerClosed。回归同时检查 Active 更新和 Failed 重试被拒绝，操作编号、配置修订和状态保持原值。
3. 本机中文 MSVC 的 /showIncludes 前缀被 CMake 错误解码，Ninja 未记录头文件依赖。Release 增量构建因此混用旧 Kernel 目标和新 SDK。新增可选 CMake 缓存项 CORDIS_MSVC_SHOWINCLUDES_PREFIX，默认仍自动检测；本机使用 `注意: 包含文件:`。清洁构建后已核对依赖表，触碰 instance.hpp 会触发 instance.cpp、kernel.cpp 与 w3.cpp 重编译。

## 验证与边界

GCC 13.1、MSVC 19.44 x64 的 Debug/Release 各 61/61 通过。W3 共十组行为，共用一个 CTest 入口；新增测试不会增加 CTest 入口数量。日志为 `build/review-core-<compiler>-<mode>.log`；MSVC 清洁构建日志为 `build/review-build-core-msvc-<mode>.log`。`git diff --check` 通过。

本机 MSVC 配置需使用 W2 记录中的开发者环境及 TEMP/TMP 设置，再执行：

```powershell
cmake --preset core-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset core-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
```

修复旧依赖数据库后需使用 `cmake --build --preset <preset> --clean-first` 重建一次。不要只修前缀而继续使用缺失依赖记录的旧目标。此覆盖参数仅在检测错误时使用，值应与实际编译器输出匹配。

未发现其他阻止本次增量提交的问题。W3 仍未完成独立 CleanupContext，服务/realm 未实现；历史操作未裁剪，非合作清理及投递阻断会保守保留资源。上述已知边界见 W3 记录；未运行 ASan/TSan、Qt 或 DLL 验证。

## W4 提交前审核

日期：2026-10-23。范围：c3f3661 之后全部未提交的 CleanupContext、服务注册表、实例依赖收敛、输出声明、realm、异步调用、测试及文档；包含新增未跟踪文件。

发现并修复一项高优先级问题：`Instance::change` 在实例已退役时通过 `scope.close().tryResult()` 查询清理失败。提供者此时可能仍在等待消费者退出，调用 close 会提前启动提供者自身清理，破坏消费者优先清理的约束。现在只在快照表明 scope 已开始关闭时查询其结果，等待下游期间更新仅替换目标配置。

新增 `w4.dependency_update_draining`：消费者清理由 gate 阻塞，提供者连续更新两次，验证 gate 完成前提供者不执行清理、不启动新代，旧操作为 Superseded；消费者仍能通过 CleanupContext 访问旧绑定，之后才完成提供者更新及依赖恢复。修复前真实复现，GCC Debug 85 项通过、该项失败，见 `build/review-w4-gcc-debug-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 86/86 通过，日志为 `build/review-w4-core-<compiler>-<mode>.log`。同时修正文档中仍称服务依赖/realm 未实现的过期状态。`git diff --check` 通过，未发现其他阻止本次提交的问题。

已知范围：W5 可选依赖、父子所有权与联合等待图未实现；清理失败或投递阻断继续保守保留资源。未运行 ASan/TSan、Qt 或 DLL 验证；这些结果不构成物理卸载安全证明。

## W5 提交前审核

日期：2026-10-24。范围：748c8fb 之后全部未提交的子实例、父代清理屏障、实际/声明联合图、SDK、测试及文档，包括新增文件。

发现并修复一项操作结果一致性问题：实例已经返回 PendingMissingDependency 后，新的声明环或不兼容服务会使实例 Failed，但旧 target 已完成，再次 resolve(Failed) 无效，当前 operation 仍错误显示 Pending。现在激活前失败统一通过 failPreflight 收敛；若旧操作已稳定 Pending，先建立新 OperationId，再发布 Failed，配置修订和 epoch 不变，旧操作继续返回其历史 Pending 结果。

增强 `w5.children_declared_cycle` 检查自动失败操作与历史结果，新增 `w5.children_pending_failure` 检查晚到类型不兼容。修复前两项均真实失败（94/96 通过），见 `build/review-w5-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 96/96 通过，见 [开发进度](progress.md)，日志为 `build/review-w5-core-<compiler>-<mode>.log`。`git diff --check` 通过，未发现其他阻止本次提交的问题。审查涵盖父代广播停止、子清理失败保留、更新重建、realm 继承和环路径。已知图边界仍为同一托管注册表，候选提供者多义时不猜测连边；不覆盖任意 Task 等待。未运行 ASan/TSan、Qt 或 DLL 验证。

## W6 提交前审核

日期：2026-10-24。范围：462c594 之后全部未提交的 EventBus、Context/EffectScope 适配、测试、示例及文档，包括新增文件。

正确性：复制成功的源结果至 serial 返回任务或 parallel 汇总时，用户负载的复制构造可能抛错。原实现会使内部 continuation 失败，返回任务可能永久 pending、自保留控制块不能解除；parallel 还可能在已经减少 pending 后漏记该监听器结果。现在 serial 将复制异常转换为 CallbackFailed，parallel 每项复制失败记入对应 Result，组装报告本身失败则拒绝返回任务；这些路径都释放已完成的分发屏障。新增 `w6.events_result_copy` 使用可抛复制负载，同时验证 serial/parallel 结果、activeDispatches 归零与所属作用域清理成功。

规范与质量：补全标准库直接依赖，移除未使用的 algorithm；补充负载可移动的编译期约束。快照先成功存入记录再增加保留数，避免 vector 扩容失败留下未记账的保留；析构使用已有记录逐项释放并通知，不再额外分配 Promise 容器。修正“最后快照销毁闭包”的过强注释，明确最终回调所有权仍由 effect 清理闭包释放；拆开多个成员赋值，便于逐项检查。

审查同时核对 once/过滤重入、Next 窗口、注销快照、epoch 门禁、scheduler 关闭、异步投递阻断及实例清理路径。GCC 13.1、MSVC 19.44 x64 Debug/Release 各 110/110 通过；event.hpp 独立包含及 git diff --check 通过，未发现其他阻止提交的问题。矩阵结果见 [开发进度](progress.md)，日志为 `build/review-w6-core-<compiler>-<mode>.log`。未运行 ASan/TSan、Qt 或 DLL 验证；任意用户等待环、跨 ABI、全局 schema 与 post 仍不在本轮范围。
