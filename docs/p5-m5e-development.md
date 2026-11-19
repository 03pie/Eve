# P5-M5-e 最终资格与交付

日期：2026-11-19。M5-d基线d47210a已审计同步，e0设计5fb3f0b已审计提交同步、8提交blob匹配且clean。e1按[ADR-0056](adr/0056-qualified-native-product-catalog.md)完成产品接线、公开资格、名单审核与相关审查，1188b58已审计提交同步；e2最终矩阵/兼容/资格与审查已随fdf099c审计提交同步；e3桌面补充及整P5出口现已齐备，最终文档随本次审查提交同步。

## 分段与提交屏障

| 段 | 出口 | 状态 |
|---|---|---|
| e0 | 核名单来源/产品入口/拥有者、资格更新及最终矩阵/人工出口；审查设计 | 已审计提交同步，5fb3f0b |
| e1 | 受审精确档案、显式候选导出、私有产品名单与原Workspace接线、正确桌面启动项；相关验证/审计 | 已审计提交同步，1188b58；结论见下 |
| e2 | 最终五配置完整构建/全量清单与实际执行、兼容/产物/ASan审计、FR-10/H01/H02/S09等完整映射 | 已审计提交同步，fdf099c；首轮失败保持 |
| e3 | 新Release产品桌面升级/保存/退出人工回执、整P5代码与架构审计、正式发布档案及状态收口 | 验收/证据齐备，最终文档审计后随本次提交同步 |

每段先设计再实现，完成审查修正和必要验证后精确提交同步，clean才进下一段。发现实质缺口先修正承担该职责的实现，不堆叠旁路；不以新增代码量或测试数代表完成。

## e0 只读核对

ModuleManager默认空NativeUnloadWhitelist、首次策略、来源/ledger/预约、不可清PIN与Qt resident保持。现产品名cordis_platform_workbench_demo仍由CORDIS_PLATFORM_QUALIFICATION宏进入openQualification，bootstrap按本次固定六种产物临时批准；必须与真实产品消费受审名单分开。普通Native Pipeline仍resident 1.0，无需改造成第二升级宿主。现desktop.ps1 run启动配置工作台，也不是本次三模块升级人工入口。

名单源只能包含三个共享资格profile各Counter/Reporter A/B四份。当前十二DLL及十二manifest实际摘要仍等于d2b归档。原normal_artifacts保持报告共36条DLL/manifest配对记录（72文件），含其他用途/配置及旧故障Notebook，不可整体晋升。D1终报不包含全部工具链/产物字段，需关联原JSONL的真实来源、实际编译命令/产物和归档证据链；d2b whole_platform=false、passed单值不构成正式批准。

实际当前CTest注册清单（仅登记，不是新执行）：

| 目录 | 全部注册 | 其中P5 | 配置边界 |
|---|---:|---:|---|
| build/ela-msvc-release | 1475 | 759 | 共享Core/Qt/Ela，Release |
| build/ela-msvc-debug | 1475 | 759 | 同范围Debug |
| build/p3-q3-asan | 1475 | 759 | 同范围RelWithDebInfo、O1与实际ASan |
| build/qt-msvc-release | 1002 | 491 | 静态Core，Ela/示例关闭 |
| build/core-gcc-debug | 459 | 198 | 无Qt，静态Core |

五清单命令文件均存在，DISABLED=0；只有三共享各有d1/d2六项原生工作台入口。最终e1改变后必须重新取清单，不套用上述数；静态/无Qt不声称运行Windows工作台。e2全量含d1/两个seed/三个proof，不将d2b选择5项当全量。

## 历史与最终证据

M1–M4、M5-a/c的架构/状态/故障与d1/d2实际字节资格保留原范围。d2b沿用未变D1的理由已记录，不是M5-d缺口；最终全量仍新执行所有适用项。e2运行前归档d1/d2a固定输出，d2b每次run目录独立保留；输入、清单、实际执行名/返回码、最终exe/模块/名单SHA、编译对象/依赖与失败历史分别记录，不拼重跑计数。

SDK0.1.5八件旧实物/provenance保持；最终用保存的Release旧宿主/插件两个normal进程见证当前Core/Ui实际加载与摘要，范围仅该同工具链实物，不泛化ABI。ASan记录最终实际对象、真实依赖和插桩/mtime/SHA，不固定套用历史51/174数字。

验收表需补d2a/d2b及最终FR-10/H01/H02/S09、状态恢复/物理卸载/失败保留映射。全量和库存审计都不能替代新增桌面人工；使用最终Release产品exe及独立yaml/store，核Native升级/消费者业务、Notebook草稿和页/命令换代、应用与保存区别、报告/驻留原因及关闭真实退出。原P3/UI人工通过继续保留其历史范围，不冒充本次通过。

最终正式发布档案关联受审名单源、相同已测DLL/manifest/依赖、最终产品exe和矩阵/人工回执；未完成全部出口前不宣布M5/P5完成。

## e1 实施设计

5fb3f0b审计提交同步且clean后，名单档案/导出与CMake生成、Workspace装配、具体产品验证三个职责分开。受审源为qualification/native-business.json；显式tools/export_native_qualification.py只输出候选，核固定三profile的四Native及原资格链/实际字节，不能把normal_artifacts全部或passed布尔提升为批准。档案记录资格/工具链/依赖/生产来源，最终发布证据另行关联，不回填改变已测业务字节。

cmake/NativeProductCatalog.cmake只消费受审源，按真实本配置生成productNativeApprovals()私有纯值；声明在examples/qt_workspace/native_catalog_p.hpp，不属于安装SDK。仅产品exe和实际产品测试编译该生成源，原Native/Notebook DLL不依赖名单生成。缺损/非法档案配置失败；无匹配profile生成空名单。普通build和用户候选选择都不能更新受审源。

现有ASan目录没有专门ASan CMake选项，实际由有效CMAKE_CXX_FLAGS及配置flags中的/fsanitize=address确定。profile读取这些真实flags和实际CRT/标准生成表达式，不引入虚构选项、不更改原编译命令；档案同时保存实际生产命令摘要。Release/RelWithDebInfo模式及address状态分别匹配，未知CRT或不支持tuple不命中。

实现前核对发现，直接读取本机历史ignored的p5-m5c3/d1/d2运行封装并不能让新clone重新取得资格。公共工具改为只支持一种当前capture格式：显式--qualify在三个已构建共享目录串行执行固定原9个upgrade_fault、12个platform_upgrade、endurance及两个seed/三个proof，排除依赖受审产品名单的product测试；普通build不调用。保存真实清单、退出/原日志、来源JSONL/proof、Native前后字节、生产来源/编译环境及固定输出旧档案，形成独立run的qualification.json。--evidence只读验证该格式和原文件/当前打包字节后导candidate；不保留多段历史schema适配旁路。初始已资格字节的历史导入仅为ignored一次性引导，审核后由公共流程形成可复现新候选，不作为长期实现或自动批准。

Workspace新增openPlatform(sourceBin, storeRoot, file, scheduler, NativeUnloadWhitelist)，参数为宿主内部纯值，由产品main调用嵌入函数取得；不作为插件服务、CLI或环境批准输入。内部用Notebook/Qualification/Platform三个有限来源模式替换原含混qualification布尔；Notebook不加载Native，Qualification按原固定来源临时批准，Platform冻结传入名单。后两者共享原安装/initial/定义/realm/Session/升级/关闭以及唯一approvals_，不复制装配路径。产品main改用平台宏/入口，保留实际资格API调用者，删除产品资格默认路径和不合适的内部旧命名。

具体产品测试用真实Scene/Workspace、原回执及映像判据：本配置恰四项、A/B往返和Notebook保持、空/错批准拒绝及已接受目标不变、候选真实字节变化即使更新manifest也不能取得批准、保存后新进程按同一定位重开、Qt/原resident保持。CMake生成另核三profile不会并集、Release/ASan选择不同、缺损/身份/格式拒绝及无匹配空名单；不是测试镜像一份policy算法。普通resident、SDK和ModuleManager实际政策不改。

桌面脚本仅增加固定业务工作台启动选择和独立store参数，旧配置工作台仍有用途；不提供名单文件或批准开关。所有代码冻结后统一构建和串行运行，保留原d1/d2固定输出、失败与原实物；按实际对象/依赖和源/产物SHA审计后精确提交同步。e2仍须最终五配置全量和完整映射，e3仍须实际Release人工。

编译与独立Python工具按实际依赖分别冻结：C++/CMake/受审名单先固定并构建真实消费者，期间不得修改这些输入；公共capture工具不参与C++构建，可继续完成，冻结后再运行其测试和显式资格。构建记录仅声明实际冻结的编译输入，不把尚未完成的工具源码记作已验证。

## e1 产品接线验证（初始受审源）

首轮产品测试编译因测试helper `module` 与命名空间歧义失败，三配置原日志及当时输入35件保存于build/p5-m5e1-before-qualified-test-call；仅将三次调用限定为platform_upgrade_test::module，未改生产准入或放宽警告。最终编译输入28件（本段改变11件），8项改变C++格式已核；三配置十个实际消费者目标均构建通过。

Release、Debug、ASan各实际所选71/71通过，包含新增6产品场景、原Workspace/模块/升级视图、九故障、d1及五项d2。Release首探6项不重复累加。restart实际父子PID分别15312→17684、2888→19448、2884→4028；不把重开后初始业务状态称作跨进程状态迁移。roundtrip复用原applied helper会保存，应用与保存分离仍由d1和后续人工核，不冒称该新增case覆盖其顺序。

独立审计逐项核原选择/退出/Passed、exe及输入摘要、每profile实际四行生成名单、原d1逐108次实际业务/物理/资源事实、两seed及三个proof所有子进程/回放/缩减链。三配置固定D1/D2a输出18件在下一次公开资格前已保存到build/p5-m5e1-runtime-fixed-evidence；原49件旧证据与35件首编失败归档保持。报告build/p5-m5e1-evidence-audit-final.json对应本轮原始运行，不回填成之后工具执行的证据。

实际88个ASan C/C++对象、223项仓库编译依赖及8个冻结头的真实消费者已核；RC对象不计ASan。36项正常DLL/manifest配对共72文件、12项metadata/moc、Core三份、64项SDK及旧SDK八件与provenance保持；旧SDK未在本段新执行。见build/p5-m5e1-asan-objects.json和build/p5-m5e1-artifacts.json。这些记录对应初始受审源3cfef70b…；后续改为current-capture证据源时另核源摘要与生成CPP/实际对象/消费exe关系，不将旧源摘要写成新源未变。

结构复审：原ModuleManager政策、唯一目录/ledger、SDK/C ABI及NativePipeline未改，名单只move进原approvals_；SourceMode仅私有初始化选择。openQualification仍有真实Scene/故障调用，产品main已删除原资格路径。两实际生成源消费者为产品exe和产品测试，业务DLL没有反向依赖。公开工具测试最初8项通过，但真实freshness检查发现Ninja预演对目录glob重验误报，且CTest应显式-j 1；修正与最终资格见下。当前71项不替代工具正确、e2全量或e3新增桌面人工。

### 公开资格的构建新鲜度边界（修正前设计）

Ninja -n不能执行VerifyGlobs并取得restat结果，因此不能将其预测的Re-running CMake直接当作业务目标过期；也不能简单忽略所有重新配置。显式--qualify允许刷新原Ninja的build.ninja元数据目标，并保存原命令与日志，但不执行被测producer/consumer的编译或链接。随后以run目录内只含subninja build.ninja的一行查询入口引用原完整依赖图，工作目录仍是原构建目录；由原Ninja及其原日志/头依赖执行-n，任何实际编译/链接待办都拒绝。没有另一份裁剪/复制的依赖图、手算命令hash或构建状态表。

先用独立小fixture证明clean、源码/头文件/编译命令变化以及CMake/glob变更后的拒绝，再用于实际三个配置。刷新后、运行前重核真实profile，运行前后源/编译环境/被测exe/DLL及依赖摘要必须一致。公开指南明确这一步元数据刷新，普通build仍不批准产物，工具仍不替用户构建业务目标；最终原始capture另列元数据命令、查询命令、退出与日志，不把旧工具8项或旧71项转称修正验证。

元图修正后的工具02a753b6…和针对性5项已通过，独立审查后公开capture启动。运行中再审实际归档发现Windows的write_text生成CRLF22字节，而export要求LF21；fixture用read_text会规范换行，未检出。原始capture/source snapshot保持不改，待该轮结束保留实际导出拒绝。修正只将入口写入改为write_bytes固定LF，fixture改为read_bytes精确断言；不放宽导出合同或source稳定性，不为让旧报告通过而改旧entry/归档。新工具需重新冻结、针对性验证，并用新run目录重跑公开资格；原轮真实业务事实及失败原因分开保存，不累计通过数。

## e1 公开资格、名单采用与收口

初始8项工具测试对应原工具SHA14a01348…；元图修正后的5项对应02a753b6…，两次记录均保留。02a完整三配置27项实际通过后，公开export真实exit1且未创建candidate，原CRLF入口/归档及源码副本保持，见build/p5-m5e1-crlf-export-failure.json。不是插件资格失败，也不计作最终工具成功。

最终只两行修正为LF字节写入/字节断言，工具SHAadcb6709…、测试60abd1d5…冻结；针对性5/5实际通过（2.095秒）。未变CMake生成器/初始数据的四类值级场景沿用原8项中的实际记录，不将不同输入的重复名称相加。最终公开--qualify在新build/p5-m5e1-public-lf-capture目录运行：Release、Debug、ASan各27/27，真实exit0，源码、被测程序/Native/依赖、运行期间原图/log/deps稳定；再用公开--evidence/--output实际export，exit0且reviewedSourceUpdated=false。原参数/日志见build/p5-m5e1-public-lf-cli-run.json、build/p5-m5e1-current-export.json。

独立审查逐607条归档摘要、939项源码输入与实际27项清单/原stdout、深D1资源/隐藏HWND/108次事实、两seed及全部proof子进程/删除链。每次Ninja入口精确21字节LF，元图/全target查询/串行CTest命令均核。报告build/p5-m5e1-public-lf-capture-audit-final.json不调用公开export实现作结论；候选三profile各四份Native，toolchain/flags/CRT/产品/producer全部与初始受审数据相同。依赖证据由4项扩至7项：原Core及三Qt按resolve路径保持原SHA，新增实际测量Qt适配/UI/Ela三库，无二进制变化。

经独立审查和root复核，采用candidate原字节为qualification/native-business.json，SHA6c0c5324e0e064667c79ae10ff9cf2d48754cbffd49f376d338d4e8cd91786ec。只有原28编译输入中的数据源一项改变，最终输入另冻于build/p5-m5e1-catalog-compiled-inputs.json；历史bootstrap仅保留于ignored归档，不在公共工具保留适配实现。三个原目录分别真实配置并构建同十个消费者，均exit0/no-work；日志另名p5-m5e1-catalog-*，未覆盖原记录。

采用前195份实物独立归档，采用后/再次check的逐项关系表见build/p5-m5e1-catalog-adoption-after/snapshot.json及before目录：三generatedCPP、六实际catalog对象及六产品/test exe的字节、mtime与Ninja编译/链接输出记录全部不变；原72业务文件、metadata/moc、Core、SDK和旧实物保持。71项与88/223旧源记录仍按其历史scope保留，因实际消费exe未变而沿用，不声称新数据源未变或新跑71；e2最终全量仍须新执行。

e1实现、原错误与失败归档、结构/ABI和实际证据无剩余审计阻断，随本次精确提交同步，clean后才能开始e2。正式最终发布档案另绑定名单及相同产品/插件字节；该资格档案的pendingStages是取证时尚未完成e2/e3的事实，不冒称整P5或新增桌面人工已完成。

## e2 实施前核对与验证方案

e1提交1188b58032c8e774670010c27d59dfd4a8da0f30已同步，22提交blob与审计相同，HEAD/origin差异0/0且clean后开始。现有职责、唯一政策/目录和源码边界合理，本段首先验证完整支持矩阵，不增加生产接口、第二状态或新构建目录。发现真实代码问题时先记录其职责与必要修正，冻结新输入后重建受影响目标，不靠放宽判据收口。

沿原五目录重新配置并默认全目标构建：三共享Qt/Ela Release/Debug/ASan、静态Core关闭Ela/示例、无Qt GCC。记录真实cache/flags/编译器及源码输入，重新取得完整CTest json清单、disabled/命令文件存在性；实际串行执行全部名字，逐项与原退出/Passed核对，不拼专项数。三共享含d1/seed/proof和六产品检查，固定输出先保存，proof只用本次原stdout路径。不同配置runtime也串行，避免原生窗口/句柄基线互相干扰；构建可独立并行。

完整运行之后核最终真实ASan生产/产品对象、原Ninja依赖/命令及输入/产物摘要；范围按真实消费者和生产目标取，不照抄旧88/223。受审十二Native、原72正常文件和当前Core/Qt/Ui/Ela字节必须与名单/资格对应，任何变化先查原因和重新资格，不自动改批准SHA。旧SDK八件/provenance保持，只在全量之后串行运行原Release旧host+旧plugin两个normal探针，保存实际进程和Toolhelp所见当前Core/Ui完整路径/摘要，不能用新重编探针假称旧实物兼容或扩大到其他工具链。

对照原只读说明书/开发指南，把FR-10、H01/H02、S09、状态/恢复/物理失败保留等要求映射到本次真实用例/原报告及支持范围，剩余新增Release桌面人工明确归e3。全量、兼容、源/产物和需求证据审计通过后精确提交同步，clean才开始e3；本段不冒称人工已完成或整P5已发布。

### e2 首轮Release资源门禁与诊断边界

五原目录默认全目标构建均exit0，真实新清单三共享各1481/P5 765、静态1002/P5 491、GCC459/P5 198；独立登记/构建审计通过。939源码冻结，原固定输出/LastTest23件已保存。最终对象初次record为189实际ASan对象/372仓库依赖，含六生产目录92对象及真实产品/宿主消费者，原批准12Native/各7依赖/72正常文件保持；这不是runtime全量完成。

首轮Release完整1481项真实exit8、1480项Passed，唯一workspace_endurance_native失败；原13件记录保存于build/p5-m5e2-release-failure-original/provenance.json，未覆盖为成功。前25轮handles恒279，第26轮Reporter B/phase1/原Op81实际295，GDI20/USER21及QObject形状、catalog/深ledger/队列都等于原基线；该轮原业务Applied/value40，但没有通过完整资源门禁。程序实际字节与此前三次e1成功相同，未改源或预算。其它四配置runtime尚未开始。

历史去重成功证据中三次initial至108轮恒295，更早一次initial/round1为279、warmup round2起295且保持；不能因此将295当新允许值。现有记录只有总数与相对请求窗口，没有内核对象类型/身份、线程库存和采样绝对时点，尚不能判为延迟初始化或泄漏。只读源码核对表明Workspace此处用同步writer，QtScheduler仅postEvent；未启用Ela Dxgi/Mica，不能把未调用路径当原因。

先使用同一未改exe/原argv和新的独立prefix，由外部观察进程取得目标PID的句柄总数、系统句柄类型/值/可用对象地址、线程及模块，并按原JSONL flush的round/phase关联单调观测区间。不得向目标注入代码、attach debugger，或在运行期间duplicate/query-name/read/hash目标动态Native文件/Section，避免观察者妨碍原share0物理检查；模块摘要在启动前或目标退出后取。线程创建时间取真实OS值，普通Event/File只记first-seen区间，未知名称/来源明确未知，不虚构精确内部时间。诊断运行不冒称资格修复，待证实拥有者/生命周期后再记录必要修正；不盲目重跑到通过、不抬阈值、不改名单，也不先堆生产分支。

第一次外部诊断使用原exe/清单三参数，仅改新prefix，实际PID14996；观察器完成且无自身错误，目标原门禁仍失败。该进程预热第7轮已275→291，第9轮phase0用旧275基线拒绝291；相邻完整系统表净增仅Event16，另File增3/减3。Event全部type21/access0x1f0003，object地址被系统置零；句柄0x5b0可见File→Event复用，不把数值当永久对象身份。原JSONL flush偏移逐项匹配；最后线程空/模块299在进程退出竞态内，不能倒推创建者或无短命线程。源/程序/Native字节保持；独立小报告build/p5-m5e2-endurance-observed-1-independent-audit.json。GCC另已完整459/459、exit0，同939输入/53实际主程序及wrapper叶exe保持；它不证明Windows门禁完成。

下一步仍限私有诊断：先在独立临时Python进程自校验Windows进程HandleTracing开关/查询，以已知CreateEvent/CloseHandle见证真实open/close用户栈；再用自行创建的轻量挂起子进程验证parent对该进程的设置/查询与恢复执行，保存实际NTSTATUS/结构长度/权限和原始记录。仅对本次自建诊断进程启用，不改系统/注册表、不安装调试驱动、不注入或改被测源；若不支持/无权限如实记录。不把准备自检当D1运行。验证成功后才另行记录并由root协调对新建D1诊断子进程在初始执行前启用，捕获Event创建栈，目标退出后再符号/模块关联；该模式对运行时有扰动，仍不能算资格或调整原限值。

HandleTracing自检实际能取得OPEN/CLOSE及准确PID/TID，但own/挂起轻量child的16个栈槽均为0，Flags0/1同样；已禁用并正常退出，记录build/p5-m5e2-handletrace-api-3及flags1原件。不将API成功称为栈成功，不为已知空栈再跑D1，不改权限/系统设置来绕过；仍未定位原创建者。

另一具体候选经官方Qt5.15.2源码和当前同SHA Qt5Core.dll反汇编核实：QMutex首次非零timeout争用的慢路径，使用全局QFreeList首块16项；Windows构造每项创建一个Event，归还只回池，进程级池析构才关闭。timeout0在分配前返回。它与Event16相容，但还不是原D1归因。先以独立Core-only小程序三组fresh child对照stdthread-control、held QMutex.tryLock(0)、held QMutex.tryLock(1)；仅public QMutex/atomic握手，不使用GUI/QThreadPool或人为sleep，worker在相同存活阶段采样，后续unlock/析构/第二次争用/join分别核计数及Event类型。不会改正常产物或安装SDK。

如需把候选与真实D1关联，私有外部诊断可在精确固定Qt5Core.dll SHA及PE/结构自校验后，用只读进程访问观察其已核实的Qt私有freelist存储：对比首块未分配→已分配，以及16项内Event HANDLE是否恰为本次系统表新增的16值。只读可读/已提交的Qt模块数据与其池用户态内存，不读写目标代码/Native DLL文件，不改目标状态、不注入；失效/竞态/不可读必须标未知。固定RVA/布局仅存在ignored诊断，不进入SDK/生产代码/长期验收算法。先验证独立对照与布局，再由root一次协调新D1诊断；未证实前仍不改原prepare/阈值或宣布修复。

Core-only三fresh child对照实际完成，control/try0 Event增量0且池未分配，try1增量16恰等双读池16值，unlock/析构/新Mutex第二次争用/join均保持，第二次新增0；独立逐phase/线程创建时间/原记录复核通过。实际D1池观测PID13600完成108轮，但分配发生在startup，邻OS表新增23项Event（池16及另7项），不足以满足原late exact16判据。随后减少startup全局枚举的单次诊断PID15220仍在第22轮失败；最后成功池读取2.062–2.156秒为0，失败/退出表2.187–2.250秒不可读，增长后缺测，既非支持也非反证。两次均只诊断，不算修复/最终通过，原件保持。

为缩小缺测窗，下一诊断先在外部独立低成本采样线程按约1ms读取缓存Qt基址的池字段，不等重型全局表/线程/模块枚举。必要时仅对本次自行启动的D1、在initialized后已见池0→完整发布时，短暂暂停该诊断进程的执行，取得稳定系统表与池值后无条件恢复；先用自行创建的轻量子进程自检实际权限/状态码及恢复。它改变执行时序而非目标代码，因此明确属于有扰动的诊断，不能冒充资格；不得触及用户其他进程、注入、改代码或文件，固定上界和finally恢复/自建child异常回收必须先实现。没有有效冷表/完整发布/稳定快照就标unknown，不为了结果重复或调预算。长期实现和原门禁仍不作任何调整。

快速单次诊断PID7380在初始化前已发布Qt池，没有合格冷表，暂停次数0；原108通过仍仅诊断，不能归因或修复。继续增加采样器不能保证赶在退出前读取，故不再堆观察变体。改用一个明确不同于正常测试的隔离诊断构建：复制原round源码到ignored目录，只在原8轮baseline冻结后增加冷态握手，以及在原资源比较表达式已判失败、原throw/失败日志/Scene析构之前增加握手。原源码、样本、8+100、比较/物理/业务判断全部保持，两个门只通过既有stdin/stdout做Win32字节通信，不创建目标同步对象；在存活的相同Scene/Qt上下文读取池和系统表，然后放行继续原失败。GUI等待会改变时序，记录为诊断而非资格；只将因故障出口太短而丢失的观测保留至parent确认，不改生产接口或为普通运行开放开关。

隔离构建沿原实际compile/link命令只替换复制源、对应单对象和新输出路径，全部.obj/.pdb/.exe/.manifest等写入新的ignored目录，原构建/批准DLL/SDK不重建不覆盖。正常程序在诊断前后按SHA核保持。父进程在冷门/失败门逐次读取原baseline及当时Qt池/系统Event集合，目标EOF/超时按失败清理；控制脚本不等到成功、不把诊断通过作为原测试通过。先静审精确源差异和输出隔离再编译运行，仍没有任何长期门禁修正。

### e2 框架准备契约修正方案（实施前）

限定的两次隔离gated诊断已完成，输出另存endurance-gated-2/3；cold门均已分配Qt池，无资源失败门，归因false。至此停止观察变体与诊断重跑，不把任何diagnostic108成功计作资格。旧首轮及后续失败中具体16个Event的ownership仍未知；已独立证明的是Qt全局QMutex慢路径缓存延迟分配机制和现有D1 prepare未显式准备此框架状态。

修正只属于tests/qt/workspace_endurance_windows.cpp的私有prepare职责：主线程持有一个public QMutex，临时std::thread对同锁执行一次正timeout tryLock并返回false，主线程保持锁直到join完成；释放并析构所有临时锁/线程后，才沿原流程采initial与四phase基线。真实调用完成由join/结果验证，不用sleep、增加预热轮、私有Qt布局或常数16，不向collector/Workspace/SDK引入初始化副作用。不改8+100、任何比较式/容差、物理卸载/业务/保存判断。

这个方案修正独立证实的准备顺序缺口，不宣称旧失败已定位。实际绝对基线将包含合法进程级Qt缓存；相对比较和零增长容差保持。之后额外持久净增长仍被原门禁拒绝；池内槽复用、净增净减相互抵消原本就超出总量采样的覆盖，不将其夸大为全部泄漏证明。Qt池归Qt进程生命周期，不由Native关闭，生产资源所有权保持。

修正后冻结新源码，三共享目录重建并分别完整串行执行1481项；保留首轮失败。静态/GCC既有全量按实际不消费该Windows专用源的边界复核。公开资格工具重新取原27项证据，若批准Native/工具链/依赖字节改变先重新评估，不能自动批准新SHA；最终产物、ASan实际对象/依赖、旧SDK真实探针与需求映射再独立审查。


## e2 最终矩阵与资格证据

准备修正后的939项源码冻结只比首轮改变Windows测试CPP和私有头注释两项；原五目录默认全目标配置/构建全部exit0。各配置重新完整执行，名称/原始Passed/LastTest及真实主程序、wrapper叶程序前后SHA逐项对应，不使用历史专项拼数：

| 原目录 | 本轮完整通过 | P5成员 | 主程序及显式wrapper叶exe |
|---|---:|---:|---:|
| ela-msvc-release | 1481/1481 | 765 | 111 |
| ela-msvc-debug | 1481/1481 | 765 | 111 |
| p3-q3-asan | 1481/1481 | 765 | 111 |
| qt-msvc-release | 1002/1002 | 491 | 71 |
| core-gcc-debug | 459/459 | 198 | 53 |

三共享分别实际完成D1一进程8+100原Applied/108唯一Operation/111代次/最终163、隐藏真实HWND与四相位原资源判据；两个seed及三个proof从各自原stdout路径核child/保存文件回放/InvalidTrace/真实故障删除最小。21项FR-10/H01/H02/S09等具体原断言映射覆盖M1–M5，静态/GCC只取其实际存在的成员。源/清单/运行及独立深审见build/p5-m5e2-final/，其中independent-runtime-audit-final.json；18份本轮D1/D2固定原输出在公开重取之前另存fixed-runtime-evidence，proof独立run目录保持。首轮13件失败原件继续保留，旧16对象ownership未知。

全量前record、后check的实际ASan范围为189对象/372仓库依赖/75目标及链接拥有者，六生产目录92对象。Normal36条DLL/manifest配对共72文件、12项Qt metadata/moc、SDK64头、旧SDK八件及provenance均保持；外部预编译Qt不声称插桩。实际另跑保存的SDK0.1.5 current-host+old-plugin、old-host+old-plugin两个normal，PID16820/13352各exit0；同PID Toolhelp同时见证当前Release Core/Ui和原旧Notebook完整路径/SHA。范围仅MSVC19.44.35222.0/x64/MD Release这两件实物场景，不泛化C++ ABI或桌面人工。原证据build/p5-m5e2-sdk015-runtime/verified.json，两独立审查分别为build/p5-m5e2-sdk015-runtime-independent-audit.json与build/p5-m5e2-sdk015-runtime-command-independent-audit.json；同名输出冲突已按原SHA恢复并分开保存，原probe/log不变，另存collision note。

公开工具在新build/p5-m5e2-final-public-capture实际重新资格，三共享各27/27、qualify及export均exit0，当前capture SHA为cf069ad38a71d30308697d13e5caf792ebee2cf0220dc04feac63f4c1b013198。候选build/p5-m5e2-final-business-candidate.json SHA3e4b5af09fd3b8bca1dd75332166196bf65db0b3737e601d60f9eb6361012cbc，三个完整profile与现受审6c源严格相等；仅证据provenance更新。批准源不重复采用，原产品/名单消费者不重建，避免为刷新证据反复改变已测产物。

后置build/p5-m5e2-post-capture-objects.py实际record及saved比较均exit0：189对象/372依赖/75目标的命令、实际编译/链接记录、源/对象/库/程序SHA及mtime全部等，整个artifacts报告相同。只有query_inputs内.ninja_log变化，旧122272字节是新122478字节的精确SHA前缀；206追加字节仅两条cmake.verify_globs，同命令hash。它对应公开工具的真实元图刷新，不称whole Ninja日志未变。其余五项query输入保持，比较见build/p5-m5e2-post-capture-comparison.json。

独立资格审计首轮exit1是旧LF专项记录的历史scope假设：当时四输入中catalog仍为采用前3cf，而现在为已审计采用6c，另tool/tests/CMake三项保持；不是当前资格或产物失败。保留6df原审计器和首轮失败记录，按原历史before==after、日志/归档及e1采用链核历史；当前6c、939源、607归档、完整profile和真实27项判据不放宽。修正后的最终独立审计与收口另在下段记录。


### e2 收口结论

独立审计v2实际exit0，原报告build/p5-m5e2-final-public-capture-audit-final-v2.json，执行原日志及返回码另存build/p5-m5e2-final-public-audit-v2.log及build/p5-m5e2-final-public-audit-v2-run.json。逐607归档和939源码、真实27项原stdout、每份D1/seed/proof子进程、LF入口与原Ninja新鲜度链、实际export参数/退出/摘要及完整三个profile相等均核通过。旧6df审计器/失败记录原样保留；新v2仅把历史LF专项的catalog采用前后按原归档核验，当前合同没有放宽。该27项资格是全量之外的独立来源，不累加到1481。

结构/所有权及M1–M5关键链复审未发现本段剩余阻断：新代码仅在原Windows/Qt私有prepare准备真实框架状态，collector不产生副作用，原事务、唯一目录/ledger、发布/恢复、卸载和关闭拥有链不扩张；保留资格入口及resident Pipeline有真实消费者，被替代的产品资格默认路径已删除。公开SDK/C ABI和批准策略不变，不引入第二注册表、旁路事务或新生产抽象。审计范围为实际关键路径及上述完整矩阵，不宣称穷举全仓死代码或检测所有泄漏。

本段只提交最小测试修正与设计/验证/审查文档，资格工具、批准源、产品和业务DLL均保持。e2完成代码/格式/证据审查后精确提交同步；清洁且HEAD与origin一致后才进入e3最终Release产品桌面升级/应用保存/正常退出、正式证据索引及整P5收口。e3人工尚未取得，M5/P5不标完成。


## e3 实施前设计与人工边界

前段fdf099c6b585a8db142b7e3d57f5e5d361bf4876已审计提交同步，七提交blob精确相等，fetch后0/0且clean才进入本段。职责检查表明最终发布只需证据索引和原产品桌面回执，无须扩展生产架构或新增通用Native数值RPC控件。继续使用原Release产品/四条嵌入批准和唯一Workspace/Operation/Session；不改SDK、名单、业务DLL或原关闭机制。

桌面使用原build/ela-msvc-release/bin/cordis_platform_workbench_demo.exe、windows QPA及本次新build/p5-m5e3-native/session.yaml和store。只从现有模块窗口执行Counter/Reporter B往返A、Notebook A→B，观察实际影响/报告、配置未保存与显式保存、消费者和Notebook草稿/页/命令换代、Qt驻留及正常关闭；新进程同文件重开核保存定位，再正常关闭。Native精确业务数值/完整物理卸载由e2原product/d1证据承担，不要求人工操作不存在的数值控件；不把运行态草稿泛化为跨进程持久化。

启动记录器仅管理本次自行创建的两个实际进程和原stdout/stderr/退出码、可见窗口/PID/DPI等只读OS信息。所有产物SHA在启动前或进程退出后读取，运行中不读取/duplicate模块文件以免干扰物理独占检查；不自动点击、升级、保存或杀进程，不监视写入中的YAML。第一次正常退出且存在保存文件后同参数重开；保存文件快照也只在进程退出后采，UI的Applied/dirty→Written区别由本次人工核，磁盘中间不变由e2自动证据核。

正式档案复用本工作单/验收映射并新增纯文档证据索引，绑定e2源码commit、6c批准源、相同产品/Native/Qt/依赖、五矩阵/资格/ABI/失败取舍及本次人工原回执。它不是第二准入文件或运行时输入，不为证据刷新重编产物。先完成具体运行和可复查记录，再向用户取得本次明确回执；回执、实际两次正常退出和最终审计齐备后才能标M5/P5完成、提交同步。


### e3 本次启动记录（人工待回执）

按先前设计实际启动最终Release产品，原运行记录器build/p5-m5e3-desktop-run.py SHA d87b6d40d6ad5afef24009ee86da8f0b6b49292b17bb63416c74256d73e3b787。首次实际PID8964，controller9136，命令与独立session/store、windows QPA见build/p5-m5e3-native/；只读OS观察到可见“Cordis 插件平台”窗口，GetDpiForWindow为120，仅属该窗口DPI，不替代Windows系统缩放人工结论。

启动前21份实物摘要已保存run-1-before.json，产品SHA fbece70b8583e006be7199826909b96a574fdc98de97a4cd98cb05248c444f77，与原受审产品归档相同。运行中不读/hash模块文件或YAML，所有后置实物/配置记录等待真实退出。已提供[本次步骤](p5-desktop-verification.md)并异步请求新回执；第一次正常关闭后同配置自动重开，必须再正常关闭。当前不能将已打开窗口、自动全量、原P3/UI回执或尚未取得的用户回复当作本次通过。正式索引与M5/P5仍待本次回执、两正常exit0及最终审查提交同步。


### e3 通过回执与保存定位不一致：聚焦复核方案

用户于2026-11-19明确回复“已完成，全部通过（两次均正常关闭）”，随后澄清Notebook“看到 Applied 且已保存，重开也显示 B”；两份原回复分别独立保存human-receipt.json与human-receipt-clarification-1.json，不修改原recording-only报告。原进程8964/2868确实exit0、21件前后保持，但两退出配置均为6ab8bed2…，实际Notebook locator82c124…仍A1.1.0，store只见三A；不能把用户观察改写成已落盘B，也不能未经证实归因于漏步骤或产品bug。

只读检查显示原Upgrade共同提交按alias写候选hash，显式Save使用同Session，bootstrap存在文件时按所存locator加载，无可定位的成功B静默回退A路径。现有UI用例覆盖Notebook按钮Applied/草稿贡献换代，平台用例覆盖接受B+Session保存，产品新进程case仅Counter/Reporter；组合覆盖缺口与原因未知分别记录。

复核不先改生产代码或重编。沿同一最终Release产品和既有记录器，仅为本次新目录build/p5-m5e3-notebook-recheck另存一份ignored运行脚本；保留旧目录所有原件。用户只需Notebook A→B、显式保存、正常关闭与自动同文件重开；Native往返交互原回执及e2自动证据保留各自范围。新脚本在每次正常退出后实际读取保存配置，明确要求Notebook B的受审6bd37…定位；不匹配保存原件并报未完成，不自动重开A来混同成功。运行中仍不读取模块文件/配置，不自动点击/升级/保存/关闭。若聚焦可复现不一致，再依据实际执行/保存/重开边界先设计最小修复和必要回归；不为解释未知先堆UI或第二状态。


聚焦复核原件也与回执不一致：用户回复重开1.2.0/两次关闭，但实际PID15504仅一次exit0，保存仍A，guard实际exit1且未启动第二进程；原report和第三份human-receipt保持。继续重复口头确认不能补齐物证，下一步由root在同一最终产品通过现有Windows UIAutomation实际驱动菜单、候选输入、升级和Save，再正常关闭/同文件新进程检查；另用新build/p5-m5e3-ui-repro目录，不复用或改写旧配置。自动操作只走产品现有界面，不调用内部Workspace替代界面、不直接写入B定位、不改SDK/生产源码或批准值。记录器保持21件前后/退出后目标locator guard，所有UIA及日志仅ignored诊断。此取证是自动GUI复现，必须与用户原回执分别标明范围，不冒称人工动作；若有真实故障再设计最小修复。


### e3 实际产品界面自动复现（最终体验待回执）

新build/p5-m5e3-ui-repro不修改旧目录；同一fbece70b…产品，记录器仅更换OUT并保留退出后A/A/B精确guard。root通过本机Windows UIAutomation实际打开“文件→模块管理”、写候选输入并调用现有按钮；选择表格行时使用已核PID/窗口下的DPI-aware鼠标及键盘，未调用内部Workspace或直接写配置。最初UIA cell Invoke未实际改变当前行，候选检查报business artifact identity differs；该拒绝和选择过程原快照保持，不能将动作请求返回当作已选Notebook，也不能据此归因旧人工结果。

真实选中Notebook后，PID17032的界面预览明确旧82c124…/1.1.0与候选6bd37…/1.2.0；实际Start后OperationId5为Applied/committed1/retained0，当前表格1.2.0且目标修订2未保存。再实际点击Save，界面明确Written、目标2/已保存2；ui源快照uia-notebook-preview、uia-upgraded-unsaved、uia-saved及action12–17均保留。正常关闭17032后exit0，原退出配置副本精确Notebook B；记录器guard通过才同参数重开16824，实际UI Grid读取CounterA/ReporterA/NotebookB1.2，见uia-reopened-grid.json。这是自动GUI实证，不冒称用户原动作。

随后root在16824通过相同真实UI完成CounterB、ReporterB、CounterA、ReporterA，每轮核原预览别名、不同OperationId5/9/11/15、Applied/committed1/当前版本、未保存状态，再实际Save均Written；完整动作/快照及原回执在native-ui-roundtrip.json。最后uia-final-grid再次为A1.1/A1.1/B1.2。此补充顺序为QtB后Native往返，与e2/原回执范围分别记录，不拼作同一原人工序列。

当前保留16824这个已验证的B主窗口给用户实际体验笔记编辑、页面按钮和命令，再正常关闭；新问题只要求体验并关当前一个窗口，不要求再操作升级/保存。21件前后摘要、第二次真实退出和最终配置等待该进程结束采集。旧两次A配置/聚焦guard失败和用户原陈述全部保留，原因仍未知；没有证据证明生产代码成功B保存后静默回退A，因此未盲目修改生产或放宽判据。


### e3 收口结论

最终用户原话“已体验，通过，并已关闭当前窗口”已独立保存于build/p5-m5e3-ui-repro/human-receipt.json（SHA c6a24029d5a24d0fc83fca0fbf22175d75d65acc36945515d6a02d6ef294eb1e）。它对应已客观核为B1.2.0的16824窗口的笔记编辑、页面输出按钮、Ctrl+Shift+P命令及正常关闭体验；升级/保存/Native往返是前述自动GUI实证，不能合称全部用户亲手操作。

原运行会话31016实际exit0，最终report.recording_completed=true，PID17032/16824分别正常exit0；第二进程确为同一命令/文件/store新OS进程。run1、run2及最终session.yaml字节相同，SHA8f2d745d6cc2a229ff36a34e066698104233af155219b24e07a243b611273ce0，模块精确为Counter A、Reporter A、Notebook B的受审hash定位。四份启动前/退出后21实物摘要全等且当前实物逐项吻合，最终产品仍fbece70b…、6c名单/六A-B模块与七依赖保持。原recording-only报告的human_acceptance_recorded=false原样保留，用户回执单独关联，不回填原录制字段。

早期native目录两次A退出、notebook-recheck一次A/guard拒绝及原三份用户陈述原件全部保持，不改为成功，不猜测历史差异原因。最终结论来自同产品真实UI的Notebook Applied/dirty→Written→B新进程、Native四往返各Written、最终A/A/B原件和本次当前B的人工体验；不把自动动作冒充人工，也不从新成功倒推旧记录。未发现可定位的成功B静默回退A代码，本段没有未经证实修改生产逻辑、扩大白名单或重写配置制造通过。

整P5结构/职责、M1–M5出口、原说明书/指南21映射、五完整配置、旧SDK准确范围、资源/状态/物理/随机证据与本次桌面补充均经复查。正式[证据索引](p5-release.md)只关联源码fdf099c、相同资格字节和原件，不成为第二准入/运行时输入。当前939项编译输入保持，最后检查实际对象/依赖/产物；本段最终变更仅文档，按影响不重复已通过的全量或重新编译业务产品。

M5/P5功能与验收出口已齐备，最终文档/源码边界、引用和精确暂存审计后随本次提交同步；HEAD与origin一致且clean后完成整个目标。不宣称跨工具链C++ ABI、任意第三方Native可卸载、Qt物理卸载、所有泄漏检测或硬时延SLA。源码SDK0.1.5和既有受控策略保持。


最终独立桌面审计build/p5-m5e3-ui-repro-command-final-audit.json（SHA95987135d7f9caea7a150f2108bdb8c8d3650aae8151bc4f0a552ac51d61efe3）核137份原件、全部实际UIA/原生输入、五次升级/保存/重开、fresh回执、两退出/21映射以及store六组manifest与DLL摘要，无剩余阻断。自动与人工分scope、旧原件未知原因均复查通过。

最后重新实物记录并严格比较见build/p5-m5e3-final-physical-comparison.json：189对象/372依赖/75目标及完整ASan报告（含六query输入）与post-capture原记录全等；artifacts只有base_commit从取证时1188b58变为已提交实现fdf099c，其余所有产物/SDK/目录/政策记录相等。直接复用旧whole-report check曾因该HEAD元数据差异exit1，原情况留于p5-m5e3-post-check-history.json；不修改旧报告，也不把元数据变化称为二进制变化。没有放宽任何实物SHA/源/编译链接判据。source939、引用、LF/空白和最终精确提交关系再核后同步。
