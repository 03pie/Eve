# P5-M5-b 实际业务版本与宿主装配

日期：2026-11-18。M5-a2 b1f7d4c、M5-b1 a263c0d、M5-b2 1c9a1b4、M5-b3 3e74d52 已审计提交同步，b4-1 2036670 已提交同步；当前 b4-2 实际业务替换与有限验证已完成审计，随收口归档。沿 [M5 工作单](p5-m5-development.md)先核对实际代码与拥有者，再按下表推进；各段完成审查、修正和相称验证并提交同步后，才进入下一段。

| 小段 | 实际出口 | 状态 |
|---|---|---|
| b1 | Native 示例拆为可测试的装配/异步关闭库与薄运行器；manager/Session 真实共享拥有；保留原五个场景和故障资源语义 | 已完成，a263c0d 已审计提交同步 |
| b2 | counter/reporter 实际 A/B 产物、明确版本与发现声明；counter 严格 uint64 状态扩展，reporter 缓存重新读取；有限真实边界检查 | 已完成，1c9a1b4 已审计提交同步 |
| b3 | Notebook 实际 A/B 与真实编辑草稿状态接口；完整创建/销毁和导入顺序；原 Workspace 场景保留 | 已完成，3e74d52 已审计提交同步 |
| b4 | 同一平台装配两个 Native 服务及独立 Notebook；复用安装、stage、Session/Driver 和 ReplacementOperation 做实际替换/恢复 | b4-1 2036670 已同步；b4-2 实际替换/恢复及物理回收完成审计验证，见 [收口](p5-m5b42-development.md#收口结论) |

该拆分优先解决实际拥有者和职责，避免在原 Demo 或管理按钮中继续追加升级分支。b2/b3 的纯产物契约不等同于 b4 的整体事务；管理交互、H01/S09 全范围、100 次/随机压力、名单资格与新增桌面验收仍按 M5-c/d/e 收口。

## 已核对的边界

Native Demo 当前同时负责模块装配、schema/目录、五种 YAML 变体、断言、打印、保存、异步关闭和进程退出；manager 是值成员、Session 是 unique_ptr，无法交付 ReplacementRequest 所需真实共享拥有。配置可用性闭包还复制持有 NativeModule，不宜在换代时继续藏着旧会话。现 Notebook Workspace 的 manager/Session 已共享，但会独立创建 Scheduler/Kernel/Driver/Session，不能与 Native Demo 并置来冒称同一平台。

ConfigurationDefinitions::replacing 只替换已有 PluginId，且不公开可变目录。三个 Runtime 和 UI providers 须先准备，再一次创建完整目录与单一 Driver/Session。ReplacementRequest 只有一个 pluginRoot，当前 native-services 和 qt-workspace 分属两个根，b4 必须确定实际公共根及其下所有 manifest/安装定位，不能用 ../ 或样例字符串拼接伪造来源。现配置编辑尚无保留数字原文的模块 manifest 定位编辑；需要该实际调用者时补到原不可变 YAML 编辑机制，不经 QJsonDocument 重写一般业务数字。

另有真实 UI 保护缺口：ManagementController::attachConfiguration 只登记一次固定 RuntimeId 集合，替换产生新 RuntimeId 后可能重新暴露手动 Stop/Retry/Load 或重复卡片。在 b4 接入实际 UI 升级前，须从原 Session/Driver 的当前归属派生保护或兑现同源原子更新；不能逐轮累加旧 ID、反复 attach 或重建整个管理器。b1/b2 的无 UI 装配不扩张该入口。

## b1 实施设计

装配库只负责当前两个实际服务模块、实际 Runtime/配置目录、Driver/Session 及有序退出。运行器负责 QCoreApplication、参数与 deadline、场景 YAML 变体、业务/磁盘/故障断言和打印/进程退出。库的异步方法返回 Task/Result，不调用 QCoreApplication::exit，也不持运行器的栈引用；原五场景调用同一库，关闭示例时仍使用原 probe 入口。

manager、Kernel、QtScheduler 和 Session 均由真实 shared_ptr 拥有，不能给原值对象包空 deleter。当前模块槽明确持有 Generation、NativeModule 与 Runtime；当前槽/原配置定义是工厂的必要拥有者，不预留未使用候选槽、升级策略接口或第二目录。可用性门禁弱持原 Generation 对应的稳定实际记录，不复制 NativeModule，也不在未来 current 槽更新后改查另一代；原服务声明、realm、schema、32 实例上限、发现/业务版本、resident 策略及五个 YAML 场景保持。

装配与关闭的资源顺序显式固定：接纳关闭后禁止新配置操作；等待 Session 关闭，再关闭 Kernel，随后请求两个 Native 会话完整关闭。即便原清理失败，也保留各阶段真实结果并按现有安全路径尝试关闭，不能把 Session 错误当成所有资源已经结束。异步链只捕获装配共享拥有者；调度投递失败须返回原错误并保留未确认拥有者，不能因回调没运行报告静止。

关闭结果仅复制 Session/Kernel/两个会话及排空的值诊断，供运行器保留原逐阶段断言，不把 Task、Runtime、工厂或模块会话装进历史。模块关闭回执不是工厂释放证明：关闭后释放装配所持 Session、Driver、Kernel、定义与 Runtime，再按原 manager.drain 核对实际账目。原 cleanup_failure 的不静止/保留记录继续成立，不改成正常静止；正常路径仍为 Resident/PIN，b1 不批准或自动尝试物理卸载。

进程/调度器仍由运行器拥有：装配返回关闭结果后，运行器检查真实队列尾部屏障、queued/discarded/callbackErrors，再关闭 Scheduler 并退出。独立装配不能关闭调用者还在观察结果的队列，也不能把业务断言或磁盘测试迁进库来扩大职责。

b1 只改实际示例宿主与构建接线，不修改 SDK/Core/Loader/Module 的公开契约或复制替换算法。验证应覆盖同一 Release/Debug/ASan 中原 normal、multi、realms、start_failure、cleanup_failure 全部场景，核对真正业务、磁盘保存与逐阶段退出；无 Qt/Core 不编译该示例，不重复运行未改变的矩阵。检查格式、来源/当前二进制和实际 ASan 对象后独立审计提交同步。

具体宿主 API 为 native_services::Pipeline::open(root, sessionFile, scheduler)，队列必须由调用方提前真实共享拥有并显式传入，构造前检查非空/owner/开放；不在装配中建立不可交还的私有队列。getters 返回实际 shared scheduler/kernel/manager/driver/session，当前槽是宿主能力视图而非纯历史。close 返回 Task<PipelineCloseReport>，逐项保留 Session、Kernel、两个 Native 会话、服务索引和两次 drain 的原 Result；完成时释放定义/Runtime/Kernel等拥有者，报告不含可执行能力。close 期间自保留，未确认的回执故障保留原拥有者；普通外部工厂借用导致的 drain 拒绝不额外永久 pin 整个装配。原五场景继续调用同一库，新增直接装配检查覆盖空关闭/幂等、外部 Runtime 排空拒绝及解除、错误线程拒绝、初始化失败后原队列仍可排空复用，以及真实队列丢弃关闭回执后保留未确认拥有者。

首版宿主初始化聚合缺失一个花括号，首轮构建已准确报错，修正后 8/8 联合通过。随后独立审计指出：QtScheduler 析构不关闭 Application 持有的 Receiver，原内部造队列的 open 失败路径无法交还给调用者关闭；因此先调整为上述注入队列的拥有者模型，再验证，不以首版 8/8 替代最终收口。gate 也改为弱持原代稳定记录，不能因未来 current 槽变更而使旧定义指向另一代或无条件失效。

## b1 收口结论

最终保留一个实际装配/关闭拥有者，运行器只处理场景及进程控制，删除原混合装配和推进路径。可用性门禁只弱持原代稳定记录，未复制 NativeModule 或新建发布/升级目录。队列由运行器/测试先拥有并显式注入；构造前拒绝空队列、错误线程和已关闭队列。manager/Session 等使用真实共享控制块，当前槽/定义为有实际用途的工厂拥有者，关闭结果不持能力。没有改业务 DLL、SDK/Core/Loader/Module 契约、resident 策略或名单。

最终 5 项源码/构建输入冻结，4 项 C++ 格式检查通过；MSVC Release、Debug、ASan 及静态 Core 关闭示例的 probe 分别完成相同受影响专项 10/10。实际唯一通过名称与各自完整选择集合一致，报告 whole_stage=false；这是本段完整受影响验证，不宣称重新跑平台全量。三个实际变更 ASan 对象的插桩和运行前后哈希一致。未变的 Core/SDK 与无 Qt GCC 不重跑，完整平台基线仍归 M5-a2 b1f7d4c。

原五个 CLI 场景继续验证真实业务、realm/多实例、磁盘保存、启动失败与清理不静止。五项直接装配检查覆盖空关闭/幂等、外部 Runtime 导致真实 drain 拒绝且释放后再排空、错误线程关闭、缺 root 的初始化前置失败后同队列排空复用，以及实际 Qt 丢弃关闭回执后的错误 Task/实际拥有者/ledger 保留。缺 root 检查不扩写为逐个中间装配步骤均已故障注入；其余半装配收尾按同一源码路径审查。普通 drain 拒绝不额外永久 pin 装配，真实未确认回执保留原 pin；两者没有混同。

独立结构、所有权、线程/失败及最终源码审查未发现剩余提交阻断项。原编译错误、首版 8 项过程证据及最终来源/专项/对象记录保存在 build/p5-m5b1-*。本段审计提交并同步、工作区干净后才进入 b2 实际 Native A/B 与状态契约；b3/b4 及 M5-c/d/e 均未交付，不以宿主重构代替业务升级或物理名单资格。

## b2 业务契约取舍

原 resident counter/reporter 两个目标和五场景保留。四个资格产物由同一业务源码编译实际 A/B（例如 1.1.0/1.2.0）并独立生成 manifest/digest，发现描述与业务表共用同一编译期版本。WriteModuleManifest 仅补实际所需版本/策略可选参数，默认 1.0.0/resident 不变。explicit 发现严格按既有 minor0/Resident、minor1/Explicit 协商，运行/业务 ABI 仍为 1.0，不修改原 C 表布局。

counter 是真实运行状态，采用 schema id=org.cordis.example.counter、version=1 的 canonical unsigned JSON 十进制，A/B 兼容；to_chars/from_chars 全量解析 uint64，不经 DOM/double，拒绝负数、前导零、浮点、指数、空白/尾随和溢出。capture 读真实实例，validate 无业务资源，restore 在 start 前验证完再赋值；失败不部分写入，缓冲区错误 size 为零，不 PENDING。reporter 保持无状态，其缓存由启动时的真实 counter.next 重建，不为了覆盖而增加无必要的状态扩展。格式转换能力沿 M3 的有限真实 DLL 用例验证，业务 A/B 没有格式变化，不新增无业务需求的第三 schema/产物。

counter 冻结捕获 N 后，旧 reporter.cleanup 的递增不改变 checkpoint，新 counter 导入 N，随后 m 个 reporter.start 得到 N+m。reporter 单模块升级借用活跃 counter，旧 cleanup 和新 start 通常合计 +2。边界导入检查可沿原状态协议/Scope，明确不计入完整 ReplacementOperation 的压力次数；不添加测试 setter 或伪造运行值。

## b2 具体实施边界

原 counter/reporter 目标继续是 version=1.0.0、resident，且不向原 counter 增加状态导出；新的资格产物是 counter-a/counter-b、reporter-a/reporter-b 四个固定目标，moduleId/PluginId 保持业务身份，版本分别 1.1.0/1.2.0，清单请求 explicit，默认应用白名单仍空。使用编译参数 CORDIS_EXAMPLE_MODULE_VERSION、CORDIS_EXAMPLE_EXPLICIT_UNLOAD、CORDIS_EXAMPLE_COUNTER_STATE，后两项默认零，只有两个 counter 资格目标启用状态。发现与业务表共用编译期版本常量，不复制四套业务代码。

严格遵守既有 module_query.h：接受的返回 header minor 必须等于请求，minor0 仅 RESIDENT，minor1 仅 EXPLICIT_UNLOAD，未知 minor 或非 1.0 runtime ABI 拒绝；原 resident 目标拒绝 explicit 请求，不能返回与请求不符的 minor0 描述。资格目标可按 minor0 提供驻留兼容描述，实际显式装载由宿主 exact approval 后请求 minor1。descriptor/table/manifest 的 moduleId、version 与 PluginId 完全一致，WriteModuleManifest 仅新增 MODULE_VERSION/UNLOAD_POLICY 可选参数，默认保持。

counter 的四个同步状态槽沿原 Instance.counter 工作，不建立镜像状态表。schema id 与 version 分开编码；canonical 标量最长 20 个 ASCII 字节，不允许其它 JSON 数值写法或空白。describe/validate/capture/restore 共用必要的 buffer/schema 与 uint64 codec，输入借用不保存；有效可写 buffer 调用的失败 size 归零，不写不足结构容量之外。实际未知 PluginId 不冒称无状态；只有 reporter/原 legacy 缺扩展才是既有无状态路径。已激活实例导出，恢复只对未启动的新实例赋值，不通过测试setter或业务回调改变数据。

资格测试仅针对上述固定四目标及其实际 SHA256/moduleId 传临时允许集合，不从任意所选 manifest 自动授权，不写生产名单。先验证真实 DLL 发现/状态表、大小边界、非法 schema/十进制与 uint64 最大值，再通过原 Native Runtime/Core 状态接入验证导入先于 start、真实 next/捕获、Reporter 仍无状态及原业务不变；有限直接状态检查不计完整升级或 100 次。b4 才接实际 ReplacementOperation 与共同配置提交。

## b2 收口结论

四个实际 Native A/B DLL 已从同一业务源码生成，分别为 1.1.0/1.2.0，发现、业务表和 manifest 共用版本；原 1.0.0/resident 两产物和五个 CLI 场景保留。严格按原发现 minor0/Resident、minor1/Explicit 协商，生产名单仍为空。counter 的原实例字段是唯一运行状态，新资格版本通过独立状态表读写 canonical uint64；解析/编码不经 double，失败先验证再赋值，完整可写失败 buffer.size=0，短结构不越界。startedOnce 阻止已启动过的实例再次导入；reporter 保持无状态，由真实 next 调用重建派生缓存。

新增八项用例分为标准导出的纯 ABI 检查和原 Native/Core 接入。前者持续固定实际文件来源并核映像身份，不伪造 HostTable；后者用实际 Context.inject 导入，再以真正 ReplacementScope.freeze 和原 InstanceStateAccess 捕获，不增加测试 setter/公开入口。实际 reporter start、cleanup、重建从 9007199254740993 依次得到精确 +1；另覆盖独立 realm 的 0/uint64 最大值导入捕获与非法数值/schema 清理。这里只验证最大值导入/捕获，没有额外调用最大值 next 拒绝、report/read 或逐一直接注入 live restore/HostTable 故障，不扩写证据范围。

七项源码/构建输入冻结、五项 C++ 格式检查通过。当前最终断言重新编译后，MSVC Release/Debug/ASan 各受影响专项 30/30，静态 Core 关闭示例 probe 18/18；完整选择集合与唯一 Passed 名称一致，whole_stage=false。30 项含新增八项、原 Native 十项和因共享 manifest writer 受影响的 Notebook 十二项。八个实际 ASan 对象（同源各三种 Native 编译及两个测试 TU）插桩和运行前后哈希一致。SDK/Core/Module/Loader/Replacement/Ela 未改，原 M5-a2 平台全量基线保持，无 Qt GCC 不重复执行。本段日志、冻结输入、实际摘要和清单保存在 build/p5-m5b2-*。

三路交叉审查覆盖职责、ABI/缓冲区、来源/精确权限、数值、生命周期和测试真实性，未发现剩余提交阻断项。只有固定四个目标的明确身份及实际摘要进入测试临时允许集合；drain 不称为物理卸载，独立 A/B 导入不称为整体 A→B ReplacementOperation 或 100 次压力。随本次提交审计同步、工作区干净后才进入 b3；Notebook 草稿、同平台替换和 M5-c/d/e 仍待完成。

## b3 具体实施方案

现 Notebook 的编辑文本只存在 QPlainTextEdit，原配置 text 仅是初值；原工厂/实例没有独立 state IID。状态必须归业务实例，页面只是投影：同一 Notebook 源码保留 legacy 1.0.0 和无状态接口兼容，再编译 A=1.1.0、B=1.2.0 两个实际 resident DLL，只有新两目标声明现有 IQtStatePluginV1/IQtStateInstanceV1。各目标使用独立生成目录、moc 和嵌入 metadata，条件宏同时传给 C++ 与 moc，manifest 由原 writer 生成。公共 SDK/IID、原 Workspace 装配与十二场景不扩张。

Notebook 只新增唯一 QString draft_，不保留第二份编辑缓存。restore 在新实例 activate 前验证完毕并复制 draft；activate 仅在未导入时取配置 text，再创建显示该 draft 的 editor。捕获读取 draft，不读取 QWidget 或配置。实际页面 textChanged 必须同步更新 draft：原 Context::identity 内部复用 business(epoch) 的 owner/冻结/退役门禁；private eventGuard 不开放给插件。门禁拒绝时用 QSignalBlocker 将显示还原到原 draft。不能使用始终排队的 QtConnection 再把它当成同步编辑：用户编辑后立即 freeze 会拒绝尚未交付的回调而漏掉最后输入。原 QObject 连接归页面 receiver，页面 effect 完整退出后原 Module QObjectResource 才销毁 Notebook；不新造 scheduler、连接目录或清理入口。

新状态 schema 为 org.cordis.example.notebook.draft、version=1，JSON 为仅有 draft 字符串的对象。纯 codec 供真实工厂 validate 和实例 restore/capture 共用，检查 schema、完整 UTF-8、JSON 形状和字符串 UTF-16/UTF-8 可往返，不能默默替换无效编码或孤立 surrogate。迁移文本至多 65536 个 UTF-8 字节，与配置 text 上限一致；正常编辑仍保留原行为，超过迁移预算时 capture 明确 E_LIMIT，不截断或改写用户当前草稿。所有状态方法同步、无页面/命令/业务资源创建、不 PENDING、不保存借用输入；完整可写失败 buffer.size=0，容量不足不写 JSON，短结构不碰容量外。activate 已开始后不再 restore；失败导入不部分赋值。

编码取舍明确为本 codec 的 canonical compact JSON：解码后重新编码必须与输入逐字节一致，因而拒绝重复 draft 键、额外字段以及非规范空白/转义，不额外复制 JSON 解析器。A/B 共用相同 schema/codec 和精确 Qt 工具链，未声明接纳任意等价 JSON。QPlainTextEdit 可能规范化换行和分隔符；初次 setPlainText 后才连接 textChanged，不从编辑器回读覆盖已恢复的 draft。纯状态保留原字符串，只有真实后续编辑才以编辑器文本更新模型；配置仍是独立初值，不随草稿自动写盘。

测试使用实际 DLL、UiPages/Command/Output providers 和原 Context/Scope 状态桥。必须覆盖编辑后不推进事件队列立即 freeze/capture、冻结期间程序信号不能改变草稿、解除后可继续编辑；导入先于 editor 创建且覆盖不同配置初值；分别验证 A/B 的同一 schema 导入/编辑/捕获，旧页/命令完整销毁且旧页面 key 无法重新导航，多实例草稿隔离。业务私有 PageHandle/CommandHandle 不为测试新增导出；已有泛型句柄门禁回归保持，本段用实际旧 key、撤销记录及控件 QPointer 核对退出。相同 moduleId 的驻留 A/B 不能在同进程被普通 load 两次；实际同进程 A→B 必须由 b4 原 stage/ReplacementOperation 负责，不能手动复制提交算法或篡改模块身份。另验证 Unicode/边界/非法输入和 legacy 无状态；错误导入/启动失败沿原资源屏障退出。Qt 仍 resident，不尝试卸载或用于累计 100 次。先交叉审查，再完成当前 Release/Debug/ASan 的新专项与原十二项；静态 Core/无 UI 不编译该示例，不重跑未变平台矩阵。

## b3 收口结论

Notebook 现在以实例 draft_ 作为唯一运行草稿，编辑器只是投影，输出与捕获都读取该模型。真实 textChanged 同步通过原 Context.identity 门禁，冻结时拒绝并恢复显示；未使用异步连接掩盖输入窗口。恢复在原 Core/Qt 状态桥内、activate 前完整校验并复制，初次建页不回读规范化后的显示覆盖原文，空稿不退回配置。capture 超限明确拒绝且不改变草稿；缩短后可以再次捕获，原输出预算独立保留。

实际 A/B 为 1.1.0/1.2.0，同源编译并声明原独立状态 IID，schema/codec 按上述取舍一致。legacy 1.0.0 继续无状态接口；三个目标各自独立 metadata/moc，宏与实际元数据/版本/清单一致，全部 resident。原单目标 CMake 路径被一个有三个实际调用者的 helper 替换，没有另造工具链契约、事务或清理目录。公共 SDK 0.1.5、原 IID/C 表与平台 Core/Module/Loader/Ui/Ela 未改，Native 产物摘要保持 b2。

首轮新测试构建因 Qt 5.15 QStringList 比较实例化旧 checked iterator，在 /WX 下失败；改为直接核插件数量与首项，未屏蔽警告。首次联合 20/23 中三项退出断言失败，真实账目均为 qt.loader.root 加 runtime.definition.example.notebook。原因是测试自身已 release 的 Scope 仍持 selection.inputs 工厂直至句柄析构；改用词法块在 close 前结束，保留严格账目断言和失败诊断，没有改生产 release 或放宽资源要求。另补完整失败 buffer payload 不写、嵌入 NUL 及解冻后无迟到输出检查。

九项源码/构建输入最终冻结，八项 C++ 格式通过；当前最终版本 MSVC Release/Debug/ASan 各受影响专项 23/23，十一项新增状态加原十二项工作台场景，唯一 Passed 名称与完整选择集合一致，whole_stage=false。七个实际 ASan 对象（Notebook 三种编译和四个测试 TU）插桩与运行前后哈希一致，九份实际 DLL/manifest/嵌入元数据/MOC 对应核对通过。静态 Core、无 Ui 与无 Qt 不编译这些目标，未重复未变平台全量；原过程失败和当前证据保存在 build/p5-m5b3-*。

三路交叉复查覆盖设计、构建/实际生成物、业务状态、ABI/Unicode/缓冲区、同步输入、真实 UI 生命周期及证据范围，未发现剩余提交阻断项。实际检查包括不泵队列的编辑后立即捕获、CRLF 原状态与显示分离、程序/键盘/按钮冻结拒绝及解除后恢复、0/65536/65537 字节边界、多实例、失败导入/启动、旧 key 撤销与完整 QWidget/editor/button 删除。业务私有句柄不新增测试接口，沿既有泛型门禁；本段不直接探测所有 live restore/OS 故障。最终只剩正常驻留 Qt root，不称物理排空/卸载。独立进程 A/B 状态契约不是同进程 A→B ReplacementOperation；提交同步、工作区干净后才进入 b4。

## b3 与 b4 的后续约束

Notebook 的 draft_ 是实例唯一运行草稿；未导入时才取配置 text 作为初值。恢复只复制纯值，activate 才创建 editor，textChanged 在 Context 输入/冻结门禁内更新草稿。Factory/Instance 实现现有独立 Qt state IID，无需扩新 SDK；新 A/B 声明相同 draft schema、各自真实版本与 DLL/嵌入元数据/manifest。旧 1.0 DLL 没有 state 接口，旧 SDK normal 兼容不代表其草稿可迁移，保留草稿的升级入口必须核对来源具备该契约。Qt resident，不复活 Retired generation/root/同一旧安装来源，也不计为 100 次可回收 Native 循环。

b4 只复用原安装、目录、配置共同提交与事务。配置顶层实例使用原 Driver reconstruction，无需为它们另造 IReplacementOwner。counter 轮重建 reporter，reporter 轮借用活跃 counter，Notebook 保持独立活跃；一次操作只替换一个模块。宿主按真实 report.committed、角色和保留状态投影结果，不能仅按 outcome==Applied 更新槽：通知失败可能已 committed 但 Failed/resourcesRetained，此时 current 必须指向真实候选，旧槽作为保留前驱，不能留旧 current 或回退元数据。原 releaseClaims 可能已在通知前释放预约/claims，只保留原 Execution 与实际仍存在的预约/资源，不能补建或声称已解除的预约仍被占用。recovered=true 同样按真实旧 Current 投影；settled Task 本身失败时读取原 operation.snapshot、目录角色及 Driver 来源，不默认 committed=false，也不执行成功回收。

成功后先替换当前实际槽，再关闭旧 Native 会话、释放旧 Runtime/定义/请求/安装凭据，跨 owner 队列屏障后检查来源、预约/readers/ledger，再显式 tryUnload。临时资格只批准明确四产物的 moduleId+SHA256，生产名单仍空；物理失败与逻辑提交分别报告。64 重代次/128 纯历史、Qt 常驻与最终名单资格不放宽。
