# P5-M5-d 连续替换与压力验证

最后更新：2026-11-19（北京时间）。M5-c 73cc08e、d0 a9f4230、d0b 1aa9e79、d1 f41860c、d2a 44202f9已审查提交同步。d2b完成实现、代码审查和三配置专项，随本次归档；M5-e待实施，M5、P5均未完成。

## 分段与职责

| 小段 | 出口 | 状态 |
|---|---|---|
| d0 | 原服务索引真实激活依赖评估批次的只读统计、语义与兼容性验证 | 已完成，a9f4230 已审查提交同步；见 [ADR-0052](adr/0052-dependency-evaluation-observation.md) |
| d0b | 核对并兑现原指南的按服务键定向通知，保留声明图/发布/替换门禁的必要通知；与 d0 原口径比较 | 已完成，1aa9e79已审查提交同步；见 [ADR-0053](adr/0053-keyed-activation-notifications.md) |
| d1 | 同一真实 Windows 平台进程预热后 100 次成功 Native 原事务；逐轮业务、配置保存、引用和物理回收，冻结资源基线与实测性能 | 已审查提交同步，f41860c；见 [ADR-0054](adr/0054-native-workspace-endurance.md) |
| d2a | 固定 seed、有限真实动作解释器与落盘轨迹 | 已审查提交同步，44202f9；见 [ADR-0055](adr/0055-workspace-action-traces.md) |
| d2b | 独立进程严格回放、无效轨迹拒绝与真实删除最小缩减证明 | 实现、代码审查及三配置专项通过，随本次归档；见下文及ADR-0055 |

每段先记录职责和拥有者，再实现、审计与验证，提交同步且工作区干净后进入下一段。d0 是后续对照与 d1 的实测依赖，不用队列数冒充重算次数；d1/d2 复用原 Workspace、ReplacementOperation、Module 目录与 Driver/Session/Store，不增加第二事务或假业务 RPC。

## d0 实施设计

先检查原通知、start 门禁、声明图预检、ServiceRegistry 私有共享控制块与公开 SDK 布局。采用 ADR-0052 的原位累计，公开查询只有一个真实来源；计数不拥有实例、作业或插件，不参与执行决定。保留原广播/合并行为，准确标注尚非完整反向索引，不在观测阶段同时改动被测算法。

实施前保存 73cc08e 的真实头/导出/正常业务与旧 SDK 实物哈希。新测试通过真实公开路径驱动生命周期，不定义测试友元或伪造注册表；检查空执行和诊断不会增加计数。溢出不得改变业务路径，不为不可穷尽分支新增测试入口。最终输入、构建/选择用例、旧符号与布局、实物兼容、ASan 实际对象证据独立归档；若验证失败，保存原始日志并修复后重新冻结。

私有控制块有 src/module/host_services.cpp 这一真实平台消费者；必须随 Core 由原编译依赖共同重建并检查其 ASan 对象，不能只替换 Core DLL 后混用旧内部平台产物。旧 SDK 实物兼容仅针对公开头/接口的既有调用者，不包含任意版本内部私有头混搭。

## d0 验证与收口

最终五项源码/构建输入已冻结，四个 C++ 文件格式及差异检查通过。五配置均完整构建成功；三套共享配置的 Core/Loader 与实际工作台、业务替换、发布和故障选择清单各 584/584，静态 Core 关闭 Ela/示例的选择清单 448/448，无 Qt GCC 全量 453/453。前四配置均为受影响专项，不是各配置全量；Debug 首六项探测不重复累加。

原/新公开头在 MSVC/GCC 独立编译，实际 MSVC x64 Release 布局仍为两份 shared_ptr、32 字节/8 字节对齐且无虚函数；三 Core 原公开 ServiceRegistry 方法符号保留，新增 getter 唯一导出。原 SDK 0.1.5 宿主、Notebook DLL 两个真实 normal 进程均通过，核对实际加载的当前 Core/Ui 路径与摘要；原八份实物及 provenance 未变。该结论仅限已保存的同工具链实物，不扩写为任意 C++ 二进制兼容。

30 个真实 ASan 对象与 147 项实际 Ninja 编译依赖已核对，包含 Core 和直接使用私有控制块的 HostServices。原 36 份业务产物对照中，24 Native 摘要不变，12 Notebook 随平台重链变化并单独记录新摘要；业务源、协议、元数据与编译 entry 保持。核对脚本曾混用 Native/Notebook 派生记录 schema 而报 KeyError；真实 manifest 的 unloadPolicy 均存在且严格验证，修正未引入默认兜底，原脚本及实际失败复现另存。

本段没有被替代的旧统计实现；现广播机制明确保留到独立 d0b 兑现原逆索引要求，不把观测接口当作通知重构。溢出只作静态审查，没有测试 setter、假友元或动态覆盖宣称。证据保存在 build/p5-m5d0-*；独立证据复核、精确暂存清单与提交同步完成、工作区干净后才能进入 d0b。M5-d/e 与 P5 仍未完成。

## d0b 实施设计

a9f4230 已审查提交同步、工作区干净后，重新核全部八类通知来源与两个实际 watch 调用者。按 ADR-0053 把唯一激活作业弱引用放入原声明，仅新增完整 key→InstanceId 索引；通用呈现观察者保留。登记/移除在原 owner 同步边界内完整准备/删除后再通知；先收集稳定作业再投递，兼容拒投和 stop/forget 重入。

原通知算法整体移入 Core 私有 service_notifications.cpp，完整/按键共用一次实现，原 service.cpp 中的旧实现删除；索引登记/移除保持原注册表职责，生命周期方法不积累新的路由分支。只为两个实际通知路径抽出具体私有实现，不建立无调用者的通用抽象。

普通 publish/retire 定向，全部 release、声明、发布组和替换门禁维持完整广播；不复制联合依赖图，不新增“拓扑是否真的变”的分支。先用新判据记录 d0 旧算法的无关 Pending 重算负例，再修改生产实现。验证必须保留 Staged-x 释放显露 A/B/C 环的反例，以及完整键、合并、呈现、Held/恢复、停止和拒投。私有签名变化要联动重建平台，旧公开布局/符号与实际旧 SDK 实物单独复核。

原 a9 Core 对照已实际记录：初轮7/9中额外发现新测试把逐次声明的初始评估误作3次；保留初始失败后仅修正该非业务计数假设。复跑9项为8通过、仅无关键预期失败，真实计数1→2，旧Core摘要不变；三实例环释放正例通过。修正后的源、实际测试exe和原日志在后续增加回归前归档，不把负例当成产品回归或最终通过。

交叉审查另确认现有通知拒投/重入用例主要触发结构广播。为直接覆盖按键投递，复用原 PublicationScheduler 的具体实现，抽到 tests/support 的通知调度 helper，供原 publication 与新 dependency_evaluation 两个实际测试消费者使用并删除原类定义；不复制 wrapper 或建立框架。补 publish/retire 拒投、post 栈内 stop→forget，以及原真实 Qt observer_discarded 改由无激活兴趣的外部 key publish 触发，证明通用 presentation watch 仍接收通知。

## d0b 验证与收口

最终11项源码/构建输入冻结，9个C++文件格式通过。原通知投递实现已从 service.cpp 删除，新的具体私有源由完整/按键两个实际路径共用；原激活通用 watch 登记已删除，测试原调度器类也删除并由两个实际消费者共用 helper。私有登记参数/导出变化要求平台联动，旧公开6符号、公开布局和 SDK 0.1.5 保持；旧宿主/插件两实物 normal 在本轮 Core/Ui 上通过，原8件未动。

五配置构建按真实选择用例的消费者收集目标，共享各59目标、静态44、无Qt GCC42，均已成功；不是默认全目标构建。原三共享构建封装把JSON数组嵌套成一个Ninja参数，在编译前失败；原日志/包装器保留后改用结构化 subprocess argv 重跑。原9项负例源/exe/log与三个维度的新计数1→1、queued0的实际记录分别归档，12直接回归在Release/Debug/ASan均通过，不与后续选择清单累加。

Debug首次选择591项为590通过，唯一新强化 observer_discarded 将触发放在Published首呈现已退订之后；这时无关发布不排作业符合产品生命周期。保持产品代码与全部原故障判据，只将新触发移至Held稳定阶段。原11输入/失败exe/log/32实际ASan对象/151依赖完整复制核SHA后，新冻结仅该Qt测试CPP变化；三共享只重编该对象与UI测试exe，静态/GCC无编译工作。Debug复验全部33项UI发布通过：按未变化exe摘要保留558项非UI原通过，与33项当前UI形成不重叠591覆盖；不宣称修正后重新运行了591项。Release已按最终输入完整执行所选591/591。

最终32实际ASan对象/151实际编译依赖重新记录并复核，只有该Qt对象相对原记录改变。正常36业务源/协议/编译entry保持，24Native摘要不变、12Notebook平台重链后的新摘要另记；实际旧SDK运行、公开头/符号也已核对。ASan按最终输入新运行591/591，静态455/455、GCC417/417；所有选择均为专项，不是配置全量，probe不累加。原失败、修正前后输入和当前结果在 build/p5-m5d0b-*，未覆盖d0证据。

跨审确认原实例依赖和RealmMap在实例寿命内固定，索引使用原解析键，不添加无调用者的重建入口；失败清理保留原声明/资源，不清PIN或伪报静止。登记分配失败回滚仅静态审查，无测试写入口或动态覆盖宣称。源码、兼容与现有运行结果无剩余代码审查阻断；最终独立证据审计、精确暂存、提交同步并确认工作区干净后进入d1，不把本段当同进程100次、随机压力或整P5收口。

## d1 实施设计

1aa9e79审查提交同步、工作区干净后按ADR-0054记录职责。主用例、Core/目录纯值采样、Windows/Qt纯值采样分开，复用原Scene/事务/显式保存/Image，不新增生产API或业务测试导出。原真实私有catalog仅供诊断复制，短期强引用与锁在collector返回前全部释放；窗口仅首次测量前创建实际HWND，collector不泵事件、不绘制、不预约Core。

固定8预热+100测量四相位，逐轮真实业务、Notebook不受影响、应用/磁盘/保存和旧映像物理证据齐全；按最终预热相位冻结资源形状与实测区间，不在后续轮次放宽。记录实际依赖评估批次与请求/保存/FIFO/可观察阶段时间，不拿任务数或阶段轮询当精确内部耗时。具体纯值接口由共同测试头约束，当前源码以通过实际三配置运行后再收口。

## d1 验证与收口

最终7项源码/构建输入冻结，6个C++文件格式通过。原共享支持、事务、显式保存和Image共用，新增main/round、目录与Windows采样四个具体TU；无SDK/Core/Module/Loader/Replacement/UI生产改动，无新业务导出、friend或预算放宽。局部checked包装替代会丢ErrorInfo字段的测试便捷入口，只转发原preview/upgrade/wait，不复制推进算法。时间报告补明确成功请求窗口内旧清理/业务不可用的保守0..request界限，不是内部精确时长/SLA。

Release/Debug/ASan最终每配置一个真实windows QPA进程，各完成8预热+100正式原Applied：108唯一原OpID、111代次、4相位固定基线、最终业务163。每份JSONL219事件含108开始/108完成；实际HWND本进程/线程有效且OS隐藏，QObject只为可遍历拥有树。业务/来源、dirty旧磁盘→原显式save→新磁盘、Notebook实例/epoch/runtime/widget/命令保持及逐轮HMODULE/MEM_IMAGE离开与文件独占均核。正式Win资源未超过预热上界，固定对象/账目形状保持；关闭沿原Resident Native/Qt root，未强制全部卸载。

三份最终输入新运行均通过p5.workspace_endurance_native，资格只限原生Windows的共享Qt/Ela三配置，不扩写为配置全量、无Qt/无Ela运行或桌面人工验收。旧字段old_image_and_file_absent有误读为删除文件的风险，原三份成功源/exe/trace完整归档后删旧key，改为old_image_absent_file_exclusive，仅重编round并在三配置重新完成整次资格；旧成功不重复累计。文件通过OPEN_EXISTING独占可访问，未删除。

26个实际ASan对象/134实际编译依赖覆盖4新TU、2个真实STATIC支持及因果链生产者；新公头和共享Image依赖、插桩/mtime/SHA与运行后复核通过。36正常DLL/manifest均等于d0b最终摘要，原source/protocol/producer entries、12metadata/moc、64SDK头及原8SDK实物/provenance保持；生产代码/策略未改，Native生产名单仍空、Qt驻留。具体证据在build/p5-m5d1-*。

代码和已有证据跨审未见剩余阻断；独立逐轮报告/对象/产物审计、精确暂存与提交同步完成、工作区干净后才进入d2。H02固定原产物成功资格不替代随机操作/故障组合、保存回放/缩减、最终名单/全支持矩阵及新增桌面人工验收，M5/P5未完成。

## d1/d2 已识别的约束

下面是实施前核对结果，尚非已完成验证。d1 开始前再固定详细方案及真实计数口径。

同进程平台和目录预算保持原值；建议先完成 8 次预热，按 counter→B、reporter→B、counter→A、reporter→A 固定四相位建立资源基线，再完成 100 次成功原事务。Counter 与 Reporter 业务计数增量不同，必须根据真实 start/cleanup 验证。显式区分应用与保存，不用隐式保存 helper 掩盖 dirty/磁盘先后顺序。旧 Native 每轮立即验证原映像消失与文件独占访问；同一路径以后可作为新代次再次载入，不能用历史路径最终一律不存在作断言。

Windows 自动验证使用真实 windows QPA 和有效本进程 HWND，可隐藏且不激活，不把 offscreen WId 当 HWND。QObject 仅报告可遍历的应用/Widget/Window 树并去重，不宣称是全进程对象计数。当前句柄/GDI/USER 的真实基线在测量前冻结，不临时放大容差。最终 Current Native 按原 close/drain 语义保留 Resident，Qt 保留原 loader root，不为测试改变生产退出契约。

随机压力必须执行原配置所有权路径，落盘 seed、动作参数、实际接受的 revision/OperationId 和原错误原因树；所有 uint64 标识用无损字符串。回放执行保存的动作，前置条件失效应报告无效轨迹，不能静默跳过。缩减在新进程重放候选序列，避免原 PIN 或 Qt 代次谱系污染下一次；真实 65537 字节 Notebook 草稿拒绝迁移可用于验证缩减链，不伪造业务失败。详细动作状态机在 d2 实施前记录。

## d2a 实施设计

f41860c 已审查提交同步且工作区干净后，先按 ADR-0055 明确有限动作、原回执、观察与轨迹职责。将 d1 的 directory() 及两个纯值结构原样提到共享测试 helper，原位置删除；d1 validator 不增加压力分支。新观察器按实际 desired/path、Core/Driver/Coordinator、完整服务键/租约、Native/UI/Module 来源及资源 kind/label 交叉核验，支持禁用、撤回、多实例和真实缺服务 Pending，不将任意 Failed 当作成功。采样不执行插件、Scope 捕获或 pump，不保留代次/Runtime/控件能力。

动作文件用原 Session/YAML helper 编辑，撤回/realm 从原目标重建并保留 config 数字原片段、module/inject/children/local disabled。普通动作收敛；Prepared 组合内部不 pump：拒绝相关合法 failCleanup 探针，连续接受两个无关 Notebook 修订，再按本次原 OpID 取消，核 Superseded/Applied。Building 通过真实命令源 rowsInserted 与候选 Held/来源命中后取消，窗口失效报 InvalidTrace。保存冲突以真实外写→原 Conflict→显式 rebase→原 save，身份不变。

固定 SplitMix64/1、seed "1" 与 "18446744073709551615"，确定覆盖骨架加有界合法随机尾部。动作先完整保存为规范 JSON，再从该文件读取逐条解释；每动作开始 flush 后才执行，记录原报告、前后状态和完整 ErrorInfo。仅接受工具规范字节，避免重复字段被 QJson 折叠；uint64 均严格规范十进制字符串。异常现场在 Scene 仍活时尽力保存，观察失败单列，不改原错误出口。d2a仅提供生成并执行入口，外部独立回放和缩减须待d2b，不提前宣称完成。

## d2a 验证与收口

13 项最终源码/构建输入、12 个 C++ 格式和差异检查通过。初次三配置编译发现 Qt5 QStringList 比较触发旧 STL 警告，改逐字段 size/contains；第二次发现升级快照不可赋值，改一次聚合构造完整观察。均保留原日志/输入后修复，不抑制警告、不改生产接口。最终三配置两个具体测试目标构建成功，包含共享目录 STATIC；不是默认全目标构建。

Release/Debug/ASan 当前输入各 3/3：原 d1 同进程8预热+100正式Applied、108唯一操作/111代次/最终业务163保持，资源基线未放宽；另两个 seed 各一个真实 windows QPA 进程，均34动作/70事件。核默认 Counter 停用后 Reporter 真 Pending、再启用 Active，第二 realm 成对加入/撤回，Native四次 Applied 及旧映像离开/文件独占，Prepared相关拒绝/两个排队修订/候选映像离开，seed1 Notebook Applied 与MAX seed真实Building Cancelled/recovered，草稿恢复、Conflict/rebase/save和最终原close通过。随机尾部两seed不同，三配置同seed动作一致；不是配置全量、任意seed穷举或桌面人工资格。

47个实际ASan对象/169项实际编译依赖涵盖新helper/六stress TU、原d1及真实STATIC支持和因果生产者，实际插桩/deps/mtime/SHA、两个exe链接同一目录STATIC通过。正常36 DLL/manifest、12metadata/moc、原生产命令/业务源/协议、Core3、64 SDK头及旧SDK8件/provenance保持；本段没有生产或SDK改动，未重复宣称旧SDK新运行。旧d1原报告和exe先归档，提取后重新运行结果另属本段。

逐动作证据及对象/产物独立复核通过，无剩余代码阻断；证据在build/p5-m5d2a-*。新输入专项通过后，最终精确暂存、提交同步且工作区干净才能进入d2b。d2b的fresh-child严格回放/无效输入和真实删除最小证明、M5-e精确名单/全支持矩阵/需求映射/新增原生桌面验收仍待完成，M5/P5未完成。

## d2b 实施设计

44202f9审查提交同步、19提交blob与原暂存一致且工作区干净后，沿ADR-0055实现具体proof入口。现动作解释器和唯一schema不复制；CLI仅增加读取已有动作文件的replay，以及正常回放、无效输入和真实缩减三个有限资格入口。父进程不建立Scene/ModuleManager，子进程通过本次实际exe绝对路径启动，同一源产物根但独立临时store/Scene；参数用QProcess结构化argv，不经shell。每次实际PID、输入/输出/产物摘要、正常退出与终态均保留。启动/超时/crash/HarnessError明确失败，不能当作产品复现或InvalidTrace。

具体子进程runner单独管理进程、有限时间预算及原JSONL读取，供三个实际proof消费者使用，不建立通用fuzz框架。正常资格在新进程生成/执行两个seed，再按同一文件各独立重放，核动作参数、原回执语义与收敛状态；数值ID只作本次证据，不要求各进程相等。前置条件、schema/版本/字段/uint64/重复键/产物摘要不合法须InvalidTrace；超限草稿配Building取消时不能到达指定窗口，也必须InvalidTrace，不能自动改成capture故障目标。

独立回放/删除候选可能保留缺服务目标跨多个动作，d2a入口仅按“本动作是否停用Counter”选择Plan期望不能继续使用。删除该固定骨架假设，当前状态仍按原desired/依赖图/来源完成verify；历史Plan单独核原entries的Applied/Pending及对应outcome，本段普通配置不接受Failed。不能用当前Driver状态反推或改写历史Pending，不复制执行计划器、不放行未知Failed或无缺依赖Pending。

缩减从含噪声的真实Draft(65537 ASCII)→notebookUpgrade开始，在不同子进程复现两次。删除块候选始终保存为原规范动作文件、再原replay；只有同一原capture错误签名且未stop/身份/epoch/page/全文不变的真实事实才保留删除，InvalidTrace不算复现。最终逐删每个剩余动作另开进程，证明仅两动作且任删一个不复现；65536字节另外通过。原目标是插件实际边界拒绝，不伪造异常、不修改边界、不清理PIN或重建manager以绕过限制。只宣称删除最小，不宣称全局最短。

手工故障/无效资格轨迹显式标为authored/1，与正常seed生成的splitmix64/1区分；缩减保留其来源标签。seed作为无损原输入身份保留，不能声称手工轨迹由该seed随机生成，也不能在回放/缩减时重新使用PRNG。

实施前归档d2a的当前exe、动作/JSONL和报告，避免后续运行覆盖原资格；生产/SDK/正常业务产物不应变化。实现后审查父子拥有者与错误分类、实际独立scratch/PID及全部候选证据，Release/Debug/ASan相关入口串行运行。三配置构建可并行，源码冻结后才能构建，运行结束再复核真实ASan对象/依赖和产物。之后精确提交同步且clean，再进入M5-e最终名单/全支持矩阵/需求映射与新增桌面人工验收。

proof每次在指定输出根下原子创建独立run目录并保留，实际路径写入本次CTest stdout；子runner拒绝覆盖任何已有回执/日志/输入。这样同一CTest可重复执行，前次原始证据不会被覆盖；目录随机名仅分配证据位置，不参与业务动作生成或回放。

## d2b 验证与收口

18项有效源码/构建输入冻结，其中9项是本段改变的源码/构建文件，另9项保留d2a原输入。8个改变的C++文件格式与差异通过，三配置原stress目标首轮构建全部成功，无警告抑制、预算放宽或编译后源码修正。共享runner由三个实际proof消费者使用，删除原main中只适用固定骨架的Counter动作推断，历史Plan/当前状态分开核；没有第二Scene/ModuleManager/事务或无调用预留接口。

Release/Debug/ASan当前输入各所选5/5，分别约21/90/182秒，属于专项而非全量。两个原seed仍各34动作；normal proof每配置6个真实子进程：两个seed先生成执行，再按各自同一落盘文件分别回放两次，共4次回放，纯语义一致且不比较跨进程能力ID。无效proof每配置20子进程，严格字段/类型/版本/uint64/重复键/产物摘要及前置条件拒绝均raw exit21；指定Building窗口被真实超限capture阻断时保留原错误而终态仍InvalidTrace，未改成目标失败。

缩减每配置10个实际新子进程，4次目标Failed与6次Passed：原7动作含噪声轨迹完整复现两次，按原规范文件删除块重放后只剩id5 Draft(65537 ASCII)与id6 Notebook升级。保留候选需同一真实module.state/CORDIS_E_LIMIT原因树、原未提交/恢复、empty映射及同child实例/epoch/页面/全文/服务/目标保持；不是只比较固定签名。两次最终单删和65536字节对照均实际Passed，证明删除最小，不宣称全局最短。所有候选文件、原JSONL、实际PID/独立scratch、结构化argv、退出码及输入/exe/报告摘要保留。

独立原始证据审计按每配置复制的CTest LastTest实际stdout定位本次3个run目录，不猜latest；逐child核实际回执/事件和保留链，未把parent passed当唯一依据。另Release同一invalid CTest重复1/1，通过新目录且原83文件SHA保持；该重复只证明可重复性，不累加成原5项或更多业务覆盖。d2a原19提交blob、exe/轨迹/报告等73件已提前归档并核SHA，未覆盖旧资格。

51个实际ASan对象/174项实际编译依赖、新头的实际消费者及STATIC链接通过运行后复核。36正常DLL/manifest、12metadata/moc、生产source/producer命令、Core3、64SDK及旧SDK8件/provenance与d2a/d1/d0b保持；本段无生产或SDK改动，也未声称新旧SDK运行。新D1未重跑：其源、依赖、实际exe及生产产物保持，原d2a三配置8+100资格继续作为已有证据，最终全矩阵另属M5-e。

最终代码与证据审查无剩余阻断，证据在build/p5-m5d2b-*；精确暂存、提交同步并确认clean后进入M5-e。M5-d范围已兑现，仍不替代最终精确产物名单接入、全支持矩阵、需求映射、整P5审计及新增原生桌面升级/退出人工验收，M5/P5未完成。
