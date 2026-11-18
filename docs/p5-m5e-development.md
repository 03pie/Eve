# P5-M5-e 最终资格与交付

日期：2026-11-19。M5-d基线d47210a已审计同步，e0设计5fb3f0b已审计提交同步、8提交blob匹配且clean。e1按[ADR-0056](adr/0056-qualified-native-product-catalog.md)完成产品接线、公开资格、名单审核与相关审查，随本提交归档同步；最终矩阵/人工/整P5尚待完成。

## 分段与提交屏障

| 段 | 出口 | 状态 |
|---|---|---|
| e0 | 核名单来源/产品入口/拥有者、资格更新及最终矩阵/人工出口；审查设计 | 已审计提交同步，5fb3f0b |
| e1 | 受审精确档案、显式候选导出、私有产品名单与原Workspace接线、正确桌面启动项；相关验证/审计 | 已完成，随本次审计提交同步；结论见下 |
| e2 | 最终五配置完整构建/全量清单与实际执行、兼容/产物/ASan审计、FR-10/H01/H02/S09等完整映射 | 待e1提交同步且clean |
| e3 | 新Release产品桌面升级/保存/退出人工回执、整P5代码与架构审计、正式发布档案及状态收口 | 待e2提交同步且clean |

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
