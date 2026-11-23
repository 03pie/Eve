# P6 发布与交付工作单

建立：2026-11-19；更新：2026-11-23。目标：完成[后续路线](post-p5-roadmap.md)定义的 P6，而非只在本机产生一个 ZIP。原 P5 基线 1f79cba，实现 fdf099c；SDK 0.1.5。合同见[ADR-0057](adr/0057-portable-preview-delivery.md)，逐要求出口见[P6验收表](../tests/acceptance/p6-delivery.md)。

## 当前状态

P6-0 已以 dbe8add、P6-1 已以 9af5d4d、P6-2 已以 `37d1cf4878966d3d007572fb29b04160257a4d9a` 审查提交同步并核 clean。P6-3按[ADR-0059](adr/0059-fresh-qualification-and-offline-recovery.md)完成新资格、明确采用、完整矩阵及准确新包，离线演练及最终closed经独审并由根采用，D12/D13通过；P6-3已以56c47c1提交同步并核clean，独立总包和包外关闭回执已归档。P6-4分层CI四层技术验收通过，独立归档和最终提交闭环中；P6-5尚未实施，P6未完成。用户确认19份关键历史资格原件没有备份，目前无法恢复，要求重新采证；旧SHA继续标缺，新证据不回填旧报告或自动扩大Native批准。范围仍为内部技术预览、暂不公开发布；物理机或独立Windows虚拟机均可承载验收，当前证明仅覆盖已记录的具体环境与准确候选包。

| 小阶段 | 状态 | 当前证据/剩余工作 |
|---|---|---|
| P6-0 交付契约 | 已完成 | dbe8add；D01 通过，独立审查及提交同步/clean 已核 |
| P6-1 同字节运行包 | 已完成 | 9af5d4d；134 文件/16.64 MB、24+9 工具测试、独立实际验包和整段审计；运行闭包证据见 P6-2 |
| P6-2 包级验收 | 已完成 | 37d1cf4；客体四流程、人工第五进程、拒写恢复及accepted QA13件五失败保护通过，独审/独立总包/同步clean已核 |
| P6-3 更新恢复 | 已完成 | 56c47c1；D12/D13、新资格/5904矩阵/41cd包、68b独立总包及be8e关闭回执，提交同步clean已核 |
| P6-4 分层 CI | 技术验收通过，阶段闭环待核 | 最终963/dde四层已实跑和独审；独立总包、最终文档提交同步clean待核 |
| P6-5 最终交付 | 未开始 | 待最终包/原件归档、独立干净环境人工验收与整体审查 |

## 2026-11-23 P6-2 提交收口与P6-3启动

P6-2精确15文件完成整体23项及暂存11项独立审查，提交 `37d1cf4878966d3d007572fb29b04160257a4d9a` 已推送master，远端refs及全量untracked clean实核通过。[外部阶段关闭回执](../../../../delivery/Cordis-P6-2-acceptance-20261123-1.closure/phase2-complete.json) SHA `1f9022dc2e444cb4a6f85cd539dba899a197619807130c2f39b7d9d4f783dad4`记P6Stage2Complete=true、P6Complete=false。独立总包 `D:/WorkSpace/delivery/Cordis-P6-2-acceptance-20261123-1.zip`（73,737,401字节，SHA `d9e74fb114a155ba7c8c8bc294c90013b86c45470417eb84bb7c3f26089b34dc`）254外层文件及8个嵌套ZIP逐件解压复核；提交前快照中的阶段false作为历史保持，由外部关闭回执补足实际提交事实。

P6-2关闭后依ADR-0059开始新campaign；其实施事实、旧/新证据边界和当前剩余项集中见下节，不以启动时的准备状态覆盖后续真实结果。

## 2026-11-23 P6-3 新资格与运行包（已完成）

按ADR-0059在五个全新目录实际默认全目标构建。三共享profile原公开资格各27/27，589份归档及960项源快照闭合；[候选独审](../build/p6-native-candidate-review-20261123-1/result-1/review.json) SHA `872a9ecb4cd7568c2acf47e1da557ae988a44cea89afc3c992ff787792d7ba5b`通过并独立重导同字节，明确采用新源 `44758309aede7a215874d4d8307fd3421e20da2befbdd5f288516de5b7382246`。原6c保持；采用后961源与capture仅有CMake固定路径/硬SHA、导出器双受信保护/源规则、新JSON和值级测试四项明确差异。[采用独审](../build/p6-native-adoption-independent-review-20261123-1/review.json) SHA `08eb5778ec65a27c12762fe4a48a0cb7f64dd78e51b63e9e9e69b1bdb2c287ee`通过；10项值级测试及采用后55项打包fixture实际通过，首普通shell缺CMake PATH/短TEMP测试夹具错误另保留，不放宽生产归档检查。

仅重建两名单消费者，各profile的19份生产模块/manifest/依赖前后保持、生成名单各恰4条；三个profile各6项产品测试通过。随后完整串行矩阵实际为Release/Debug/ASan各1481/1481、静态Qt1002/1002、无Qt GCC459/459，选择名与Passed逐项相同。[矩阵独审](../build/p6-adopted-support-matrix-review-20261123-1/result-1/review.json) SHA `019fe955134a905a5c33c1c65668c90410c3a2e81fe2c4b12aedb46688ea215b`核5904次全矩阵执行，公开27和产品6不累加；19×3最终物理字节及源961保持。ASan另核实际flags/命令及三个真实COFF样本，不冒称逐个对象全覆盖审计。

新严格打包基线经[根独审](../build/p6-fresh-baseline-root-review-20261123-1/audit.json) SHA `b58098f9135b76a692ed836c1f87e6e3b6531dde45e90941335fc338fe5bbcc8`及真实strict loader核验，固定资产SHA `40ee2c4807a43022096611fa6cdf14181c7c462db9f53dbb3a18c49e965ff85e`，不替代旧19份原件或旧legacy门禁。实际只复制已测字节生成 `D:/WorkSpace/delivery/Cordis-preview3-20261123-1/Cordis-0.1.5-preview.3-windows-x64.zip`，17,134,381字节、138文件，ZIP SHA `41cd2aa93b23f0780bd1f7fb6a934a82933556bf49303b43e360997ca640702f`，manifest SHA `fdff0203a649922a43b3668574b30c0eec09313f5d038065467515072e45b13b`。[实包独审](../build/p6-preview3-independent-review-20261123-1/review.json) SHA `921e5bc0983612ec7abc2631d15077a4ac8f5d070e572d61aec3dae9c9dba6e9`确认完整解压、stock PS验包及26项x64 PE闭包；新包GUI/cleanMachine此时仍未验证，打包原件中的false保持。

离线工具经24项真实文件夹具测试及非作者审查，修正了输出与原输入重叠的提前拒绝、相对路径外部宿主保守阻断及回退journal精确绑定。工具仅在普通QA新controller操作并列应用与完整数据副本。真实旧包在客体通过工具正常运行两入口：配置596实际Apply/Save R1（UI仅可观察Saved）、业务6952实际A/A/B/按钮/命令/显式Save Unchanged R1，两产品及其owner/driver均正常退出0，原13业务/102字节配置不变。651文件closed导出、648件客体源前后闭合，[旧运行基线独审](../build/p6-3-old-runtime-independent-review-20261123-1/audit.json) SHA `f45fdf74735299fc462be6c80179c6acbf06770147fd6e78cc4d4aa3d5acbcc2`通过1842项核对，并另生成外部批准回执。该旧运行基线是后续迁移的前驱；迁移/中断/恢复/回退实际结果见下节及最终闭合段，该旧运行基线生成时尚待后续迁移和阶段审查；最终收口见下节。

### 迁移、新候选运行与首次实际中断

[显式映射独审](../build/p6-migration-map-independent-review-20261123-1/result-1/review.json) SHA `13a9d8c79006a2175ee9d8aebb7b21c25ea7a1a241f4b3bafca4f32f7a1d5f49`通过：真实旧A/A/B到新包同身份/版本/ABI/状态schema的三个定位映射，映射SHA `df76e7bd95dfe820f05e45405b7425d319d3124bca182d7988608585adf07624`；实际纯函数只改三个manifest定位，plugins/config等语义不变，不按同版本自动批准。新Native仍只有Release四条，Qt仍resident。

独立工具目录扩展只承载普通QA的有界验收；第一版已传入146文件但未运行任何next任务。审查发现启动后证据写入/第二日志读取器构造失败可能留下未接管管道，故该版本原样保留并另建suffix2。新版本先取得日志所有权，失败时将未接管流排空到EOF、保留原错并标不完整，不杀产品、不伪报成功。[实际两失败块窄补审](../build/p6-next-wrapper-resource-review-20261123-1/result-3/review.json) SHA `c21bc85de287ba070e495d26c090008a019dfb0feb235c63597e5228c3aec485`绑定新wrapper8ebf9301；受控双流flood进程自然退出，stdout240000字节、stderr兜底排空。测试观察器自身末尾取值错误及前两夹具失败全部保留，不能将该有限检查称为整套GUI通过。

在新managed旧数据副本中CreateNew加入明确自有的opaque/PIN/空目录保留夹具，14原文件不变、旧active仍 `d3d7d4693277737ffa416d27b81af2088f4188fc8ea96e72ec5e6c68d2999ddf`。第一次真实prepare在完整备份后owned checkpoint自退77，事务 `fd290279925741c385e7992797ec10f7`未提交；原recover实际返回old-selection-not-committed/filesDeleted=false，旧数据16件及备份保持。此为真实文件部署进程中断，不是产品kill或运行状态注入，PIN是验收自有夹具，不冒称真实quarantine产生的PIN。

新事务 `b5a0921858cb4832a4495988a9581801`另行准备，00-plan SHA `8a13055f05c29145e64c96155c12c3acdd951149e7df97de0ea56d205de8ce62`、10-prepared SHA `1772bbc231ae5d5dd53a43e186ef8ddfe54fc67d39c0bd0fa9b0ddbab5cff5be`。新配置5080实际Saved R1、新业务6468实际加载新A/A/B、按钮/命令及Written R1，两产品和driver均自然exit0；业务文件变为新定位对应的 `d472d1ab4173700e1d78232e56e87d7de896c517171f8b350060585036c07c20`。1472文件/1469客体原件及275源目录在closed后导出，完整目录身份/属性/SDDL及文件SHA前后不变，宿主真实重建空目录。[新运行独审](../build/p6-new-runtime-independent-review-20261123-1/result-3/review.json) SHA `f6055e239a05df24ceb1418b72df576902bc3e7e0200664a087cfca2dc150045`通过18101项核对，并由根另采用准确tuple `94c6db921e918413b35d35390bb1ba2ef4a187c18e28ef951d2bcfdb1da2c2cf`的[最终运行批准](../build/p6-new-runtime-final-approval-20261123-1/runtime-validation.approved.json) SHA `5990e2b35c802c071d3ec0a7005a4cb34b136015eb77264f0b4f4a3174964f05`。审计器前两次接口适配失败原件保留；该1472导出时旧active仍未切换。后续selector边界中断、恢复提交和版本重开/回退另留原件，不回写该次快照或据候选通过提前关闭P6-3。

当前阶段源码已独立双份保存：capture960与product961各按原SHA复制，三份采用前文件来自准确37d1cf4 Git blob，其余源码复制前后稳定。外部 `D:/WorkSpace/delivery/Cordis-P6-3-source-snapshots-20261123-1.zip`含1926文件，4,656,849字节、SHA `0beed4d4f4a6f20ed851ecf5fc5aba319bbea1be67489ad6ebab9f11e68f8121`；根另逐文件集合/长度/SHA及ZIP完整复核。该源码快照不包含凭据/运行二进制，不等于找回缺失19份历史报告。

### 最终D12技术与提交闭合

在原候选外部批准后，激活前selector边界的owned updater实际自退77，经原recover验证后恢复提交到94c6；新active配置6180实际Saved R1、业务9372实际Unchanged R1并正常退出。rollback子进程9948实际exit0；回退后的旧配置10096实际Saved R1、旧业务7276实际Unchanged R1并正常退出。最终selector SHA `0864026579d8a5926a05c021328e7821d3a06ea853ab5014d6537a473acd292b`指向原旧release/data `3941e5f215b2440297b9d61921aad2e3`，previous为94c6、transaction仍为b5；这是带新selectionId/前驱的回退tuple，不是原d3 selector字节复原。

最终[recovery closed导出](../build/p6-3-vm-next-20261123-2/recovery-closed-export-20261123-101207-043/receipt.json) SHA `4b4565f8a163ca35f5eeef9d161f3c5e471031e2c48737d9de4bffa009321d05`含2411件客体文件及3件导出核验原件，host共2414文件、306目录；其中298为真实源目录；[外侧索引](../build/p6-3-vm-next-20261123-2/recovery-closed-export-20261123-101207-043.sha256-index.json) SHA `537c86bd4394507193c2dcab41552805a94acd89d1f8bb4117b98de71e1c09f8`，[执行摘要](../build/p6-3-vm-next-20261123-2/recovery-closed-export-20261123-101207-043.execution-summary.json) SHA `7fb1fd3395699338fa26689c8448d82865cca4ca7d33d95e9dd4c52501d5a227`。旧初始、新候选、新active、回退旧版共8个持句柄产品及driver均exit0，13个任务均Ready/LastTaskResult0，0个活产品/owner/driver。两次77现场、旧16/新22文件、两份backup及自有PIN/opaque/空目录保留，不删除或重放迁移。

[最终post独审](../build/p6-new-runtime-independent-review-20261123-1/postrun-result-2/review.json) SHA `5cce12fe5ac1ce675b1eda6075b1b1ad12d29ff2d00e58ee3d07c08ded4b5a8d`的20009项检查、0失败通过；2423件[输入索引](../build/p6-new-runtime-independent-review-20261123-1/postrun-result-2/source-sha256.json) SHA `69b47da2ca01cecca3541ec894ccb20d034490b9fe975396af5f742dc37c8341`闭合。根[明确采用D12/D13](../build/p6-3-acceptance-archive-20261123-1/root-d12-adoption.json) SHA `1531fd34c772fd655fc7dbc745fd3b98ed84ae7846fb639eadf550b9fe952e54`，故本轮P6-3技术验收通过。首post checker将pretty JSON误用于tuple摘要而失败，保留其原件；改用原updater.value_sha的一行修正后通过，未修改产品、实际UI或放宽断言。实际中断范围是after-backup及before-selector两次77，未将after-selector夹具覆盖冒称现场实测。

以上为P6-3提交前原件生成时的状态：原导出、执行摘要及独审中的阶段false保持，不回填。独立总包仅消费已明确通过的实际原件，打包时P6Stage3Complete/P6Complete保持false；后续精确提交、同步和clean通过下节独立关闭回执补足。当时P6-4/5尚未实施，当前状态以顶部及后续关闭记录为准。

## 2026-11-23 P6-3 归档与提交关闭

P6-3精确15项源码/文档及暂存内容独审通过（429e3401），提交 `56c47c1b98ee2d37ce992e753e2a65322fd20fb3` 已推送master；独立HEAD/父提交/远端/clean及15个提交blob复核通过。外部[关闭回执](../../../../delivery/Cordis-P6-3-acceptance-20261123-2.closure/phase3-complete.json) SHA `be8e50ad7fd34d8e5b536b57dc93439227fd6e74de47351aa67a635875b8eeef` 记P6Stage3Complete=true、P6Complete=false。一次远端只读查询的TLS失败保留，正常重试成功，未重复push或改变TLS设置。

独立总包 `D:/WorkSpace/delivery/Cordis-P6-3-acceptance-20261123-2.zip`，293,851,735字节、18,571文件，SHA `68b106cb1bc2d73936d8744a8bbd9bc813ced5b40be3b37ce5f5fb295b7d682e`；成品独审 `1bc63550f867fae1647350ddc0ebc0bd07615ee9e27d3cde9674ea1a3f1b9212` 逐项核清单/SHA/白名单及三个固定嵌套ZIP（1926源快照、138新包、139旧包）。其它原始ZIP仅外层逐SHA保存，不夸称全部内部展开。第一份归档checksum误纳自身空摘要，被原exact-set校验拒绝；仅修正创建前取清单的顺序，新副本通过，失败现场/原工具/原断言保留。归档内提交前false保持，由关闭回执补足后续Git事实。

## 2026-11-23 P6-4 分层CI启动

P6-3关闭后按[ADR-0060](adr/0060-layered-ci-and-artifact-promotion.md)实施薄入口 `tools/ci.py`；源码验证使用独立构建树，资格候选走原公开27项及独立导出，受审产品只消费固定447及对应字节，发行层只校验已冻结41cd。初版新增CI脚本和测试后，该轮源图为963项、SHA `ee5f53d58425a0744490adb5fc332939937da08436b6a7cfbae37823edea2c21`，原961逐SHA保持，旧包来源不回填。该版本17项状态/输出/持有进程/ZIP门禁测试实际通过，初轮15项原件保留；非作者静审 `f281d872dba25ef2708894091482b743d140efd49a8574179fbd0ee82c968b01` 核当时四份编排输入。以下首轮结果保留其实际版本，日志名称修订另记。

资格层已实际调用公开入口，三profile各27项通过并独立导出、重导字节相同；本次capture SHA `3f06c008183b0e49eb4c2a6da511f780cb9a23c643971f11a64bf322c73cea38` 实际含607归档、651精确引用，不能与旧589份campaign混写。候选SHA `14ce10b4b2cacda94ebe61eaae6b4cd7675167bfcdafc6efc2fc3c159d792207` 保持PendingReview；[候选独审](../build/p6-ci-candidate-independent-review-20261123-1/review.json) SHA `64906fe92875793366fe7ce150a60131da47e877859d8a6953eb826d92d61a20` 核963源、原深层证明及准确三profile。根另读取完整profile与现受审447逐值相同，明确允许复用已受审字节进行产品验证（[回执](../build/p6-ci-product-root-review-20261123-1/review.json) SHA `c2c0852fcacc62d9a03ac9fc5eef06d67d1e1f2750a636871be5be5861c85e96`）；没有将候选写入批准源或授予新Native批准。随后product层三个profile各6项实际通过、ProductVerified，不重建、不把18项加到旧5904矩阵。

缺环境负例实际exit2/PendingEnvironment，未创建缺失的profile根或启动子命令；源码前后不变。发行层错误外部ZIP摘要实际exit2/Failed，准确41cd/fdff包实际exit0/StaticVerified并使用仓库stock验包器；源和ZIP保持。原件见[两次实际结果](../build/p6-ci-release-20261123-1/result.json)。StaticVerified不代替GUI或cleanMachine；最终包完整四流程及新人工体验仍留P6-5。首轮本机GCC实际全构建175/175、CTest459/459，但新入口用测试通配符作为日志目录名，Windows拒绝含星号目录，三个Python套件未启动，整个CI正确保留Failed/exit2（[原件](../build/p6-ci-source-gcc-20261123-1/stage/result.json)）。不将C++子步骤通过改写为整个入口通过。

### CI日志目录窄修与最终同图复核

修订仅将三个套件的日志label设为固定合法名称，测试pattern、选择及C++代码不变。新增实际Windows目录回归在旧代码真实exit1，在修后18项测试实际exit0；[修复原件](../build/p6-4-source-suite-label-fix-20261123-1/source-963-ee5f/receipt.json)保留旧963项完整源码，首轮Failed、候选和产品记录不改写。最终 `ci.py` SHA `15d02df9adc7b591c72ae5d7199e338f980627e88488544200e469185c9378de`、测试SHA `4a4d9d9763a948200a5342f5ef9a5f8b0f90915fcc67e2a9284207220d90c1f4`，新963源图为 `dde49628e913f7492c6e3b4f580a9337b4729caa52ea6ba310fd2d13e60d56c0`，原961保持。最终资格/产品/发行按此图复核，源码层由真实GitHub Windows2022 MSVC完整入口验证，保留首轮GCC失败而不重复构建与标签无关的C++。

最终图发行层已实际复核：错误摘要PID23820/Failed/exit2，正确41cd/fdff包PID43788/StaticVerified/exit0，stock验包、138项库存及前后源图保持，见[新版结果](../build/p6-ci-release-20261123-2/result.json)。最终图候选为d0496e57、capture2bd518b8，仍607归档/651引用/三profile各27项，独立重导同字节；[最终候选审查](../build/p6-ci-candidate-independent-review-20261123-2/review.json) SHA `e626661cf3efdd89bee415b3750b7bd72cc126328489f9bdb57cd1d49127c039` 保持PendingReview、不采用新候选。沿既有根授权复用现受审447字节，最终product2三个profile各6项通过，[审查](../build/p6-ci-product-independent-review-20261123-2/review.json) SHA `2fefc9c6f65c0762340af24fdda5327afa1fd1175bfd6d4b7bd1fad393a9b214`；原9项消费者/generated字节仍同，无重建。缺环境负例和最终三本地层由[组合审查](../build/p6-ci-final-local-layers-review-20261123-1/review.json) SHA `7b71e483cc87b0a3b472e8cf16f28657da5f012cd143e50eaeb81cfff01a3422` 核完整dde源图与Envelope前后保持。

初步CI提交 `8ebfe9672c6f610369d1fe90129a29050078a8a7` 实际推送、远端同且clean，触发[真实源码run45172242089](https://github.com/03pie/Eve/actions/runs/45172242089)，job137684433373及全部五步success。实际Windows2022/MSVC19.44.35230.0/x64、Python3.12.10、CMake/CTest3.31.6、Ninja1.13.2；全构建175步、源码CTest459项及Python55/24/18各通过。此MSVC版本只支持本次NoQt源码回归，不扩大原19.44.35222/Qt5.15.2的Native资格。候选采集源仍是其实际前驱56c47，不回写为后续提交。

[最终D14技术独审](../build/p6-ci-source-independent-review-20261123-1/review.json) SHA `ce8084ae4be6c17649e93e965b6453d6a8d7f28c7e9bfc00560f16342744f312` 通过：官方API成功、唯一无label前缀的外层SourceVerified、10个实际子命令0、459逐名一致及真实55/24/18，四层均使用最终963/dde；根[明确采用](../build/p6-ci-d14-root-adoption-20261123-1/review.json) SHA `7fe7d2f3df68531692f411f268caec89f0da32ec740f40cd209f69b0b9e475ab`。保存的是官方connector decoded日志，不声称下载或逐SHA核对runner原raw/编译文件；报告内这些SHA仅按CI声明保留。各层、首GCC失败及旧/新源快照分开保存，不混入5904矩阵或最终GUI。D14技术通过，独立总包及最终文档提交同步clean待闭环，P6-5未实施、P6未完成。

## 2026-11-23 P6-2 分项验收收口

以下记录各份P6-2验收原件生成时的状态；其中false、Unknown和失败不回写，当前阶段关闭以本页顶部37d1cf4及独立关闭回执为准。

用户本轮明确反馈“显示和操作正常，已关闭窗口”，同时确认历史原件“没有备份，需要重新采证”。[正确UTF-8人工回执v2](../build/p6-d11-completion-20261123-1/human-receipt-v2.json) SHA `a85292848c96c0d7e87de3d1a4cd66ff7c63748fad5aebf25421f4f584607dd9`引用本对话实际回答；首版派生文书因PowerShell管道转码把中文写成问号，作为错误版保留并由v2明确替代，不改任何客体原件。[D11组合v2](../build/p6-d11-completion-20261123-1/d11-completion-v2.json) SHA `fc295de2c1a7d53d8dbe25d86b7cac53ee3140eacd32660ae4ddd20e0ccdaea2`绑定原19项技术审与新15项人类会话结束审；原2516持句柄实际exit0、wrapper任务0、139件包未变、A/A/B保存保持，故当前准确候选D11通过。原guest summary中的human/D11=false不回写。

不可写保存已在独立客体普通QA/session2执行：第一次launcher用Groups查询完整性标签而启动前误拒，0个产品/未调用owner；改用已锚定原`Windows.Token()`后，新root/task/case一次执行成功，失败原件保留。实际产品9896、UI7324、owner10096均正常退出0。唯一Apply得到实际R1，拒写后精确错误为“磁盘：保存失败”及“configuration write lock is unavailable”；C0字节、编辑目标、实际notes实例2和R1保持。原Restore仍exit1/false；另核3对象身份/Owner/Group/完整有序DACL，仅0x0400状态差异，existing-file写/DELETE访问0后，同一PID同R1显式Save成功，C0的374字节变为实际R1的129字节。UI只显示“磁盘：已保存 R1”，不虚称UI暴露Written枚举。源端149份/host150份及139应用/seed/44工具闭合，[实际Save独审](../build/p6-d10-actual-audit-20261123-1/audit.json) SHA `1ac33d824e386b2a48b418ce14f9348b0dcfa02ac356b46deac513894a077176`的87项核对通过。

人类和D10产品全部结束后，仅从固定QA路径导出真实已接受业务基线：session.yaml和六组DLL/manifest共13文件、258,973字节，QA SID/profile、无活产品/Ready0、源前后fileID/长度/SHA及host字节均核。[基线独审](../build/p6-business-baseline-20261123-1/baseline-independent-audit.json) SHA `b02ccd3ce2a10410fd1d97051bbce94c4a38c37bd2aa03299c5cf39ba86d2274`的36项通过。随后普通host按原工具四件及同包139字节，对五种故障各克隆实际13文件，不改相对hash定位；五个真实退出为1/1/1/1/0xC0000135，每案及原基线/原包前后全同，[本轮五失败独审](../build/p6-d10-accepted-data-startup-audit-20261123-1/audit.json) SHA `21a080ae78aadcfe1a8e2df5f6b907a04ab424b2502fb10419fcc7e1e78a420f`的69项通过。4条原模块partial-copy保留，不用于DLL闭包或host健康重开；这是本轮真实QA数据保护，不是恢复丢失的旧P5用户数据。

[P6-2分项聚合](../build/p6-2-closeout-20261123-1/technical-acceptance.json) SHA `f893304b3b443ace0c6d4c5c5353b255c6de41b2122e0647f63d1d6a536ed026`据此记D10/D11通过，仍明确P6Stage2Complete/P6Complete=false，待整体审查、独立总包及精确提交同步clean。补充包 `D:/WorkSpace/delivery/Cordis-P6-D10-accepted-data-supplement-20261123-1.zip` SHA `0fc4dd80bf7bf6fc01e58f55ad1a1da710d3bcd243e555d75e27e1590b20aa22`含106文件/2,088,111展开字节，完整解压复核通过；整体总包另纳原D11技术ZIP、ready工具ZIP、人类/Save当前原件与相对索引，不能借该窄补充声称全P6交付。

19份旧资格原件缺失事实保留。用户已明确要求新采证，[新分析](../build/diag-p6-d15-revalidation-plan-20261123-1.json) SHA `145441fb490de0a86d576e6c2fe4f967ac267caee72d34afe6144b685c8d90b5`确认当前MSVC19.44.35222/Qt5.15.2及ASan材料可用，但旧runner/producer登记及匹配Debug/ASan批准模块缺失，不能把旧cache当可直接qualify。后续须新campaign真实构建/资格候选、独立审查、受信装配及新数据迁移证据；不回填旧19个SHA，不自动采用CI候选或把三个profile的12项并集编入一个产品。原6c、SDK/生产源码和冻结运行DLL在本阶段未改。许可/材料结论仍限内部技术预览，第一方和公开再分发的既有未决条件没有因此消失。

## 2026-11-19 验收环境调整（历史准备记录）

按用户要求，P6-2/D11 及 P6-5 最终包验收允许使用独立 Windows 虚拟机，无需不同物理主机。架构复查确认变化仅在验收环境：客体拥有应用副本、配置/store 与产品进程；宿主只传入交付物、接收证据，不提供运行依赖。继续复用随包验包器、ACL 工具和桌面记录器，无需改动生产代码、SDK、批准或记录格式。干净基线、普通权限、共享目录边界、虚拟机补充信息和最终包复核要求见[ADR-0057补充](adr/0057-portable-preview-delivery.md#2026-11-19-补充允许虚拟机验收)及[验收表](../tests/acceptance/p6-delivery.md#独立干净环境d11)。

本轮环境复核为 Windows 11 专业版 25H2、构建 26200.8655、x64，Hyper-V 已启用，Windows 沙盒未启用。首次普通会话预检因无 Hyper-V 管理权限无法取得虚拟机清单，当前 build 也缺少19份原冻结输入记录；[预检原件](../build/p6-vm-preflight-20261119-1/preflight.json)保持当时范围。后续通过独立提权只读脚本取得[虚拟机清单](../build/p6-vm-preflight-20261119-1/hyperv-inventory.json)，仅有已关闭的 DockerDesktopVM，无 Windows 验收客体。这些准备条件不构成 D11 通过。

### D11 实施准备

已在 `D:/WorkSpace/test/Cordis-0.1.5-preview.2-windows-x64.zip` 找回第六候选原包，ZIP 大小及 SHA 与下文冻结记录一致。将同字节副本恢复到[本轮交接目录](../build/p6-2-clean-machine-handoff-restored-20261119-1/)，安全检查全部 ZIP 路径后使用系统 PowerShell 解压，再执行包内验包器及外部 manifest 锚；[验包原件](../build/p6-2-clean-machine-handoff-restored-20261119-1/verification.json)确认137载荷、总139文件完整，[恢复记录](../build/p6-2-clean-machine-handoff-restored-20261119-1/recovery.json)保存来源、命令、脚本摘要及 exit0。未重新编译或重链；当前开发目录配置 exe 与受审字节不同，继续使用找回的准确包。原冻结输入记录的缺口仍需 P6-5/D15 归档解决。

按现有职责新增独立 `Cordis-P6-D11` Windows x64 虚拟机，文件限定在 `build/p6-vm-install-20261119-1/`。微软官方 Windows 11 Enterprise Evaluation 26H2 简体中文 x64 ISO 已下载，8,548,628,480字节、SHA `b37a06d5121eee017826c0b133250bbd21bec694c2d607c60edd29090e667a84` 与[官方公布值](https://support.microsoft.com/en-us/servicing/os/windows/docs/2026/09/verify-the-authenticity-of-a-windows-11-enterprise-evaluation-iso-file)一致，见[来源](../build/p6-vm-install-20261119-1/iso-source.json)及[实际校验](../build/p6-vm-install-20261119-1/iso-verification.json)。宿主准备脚本经独立审查，准确 SHA `629978681d67605dc2a0dec44ca2b01d61b6048327f4cb536320310be449bfe5`；仅创建全新 VM/VHDX，安装分区操作只由客体 Windows Setup 处理，不修改宿主磁盘或已有虚拟机。

[准备回执](../build/p6-vm-install-20261119-1/vm-preparation.json)确认 VM ID `0203941d-7fc9-419f-9f8d-2f96f7ded618`，Gen2、Secure Boot/vTPM、4个虚拟处理器、4–6 GB动态内存、64 GB新动态磁盘、无网卡且状态Off。[启动前复核](../build/p6-vm-install-20261119-1/vm-console.json)再次核唯一文件磁盘、准确安装介质及该VM对种子ISO的读取权限，并打开对应 VMConnect 窗口，未启动虚拟机。安装答案配置独立普通用户 `cordisqa` 首次自动登录，临时客体管理用户只用于安装后管理；安装答案/凭据所在私有目录使用受限ACL及DPAPI保护，不纳入验收证据导出。

宿主提权仅用于新虚拟机管理，安装后产品在客体非提权普通用户桌面运行；正式采证前由客体管理路径清理AutoLogon/安装答案缓存并撤下安装介质，传入的准确包及应用/数据均在客体本地，不映射宿主开发目录。客体系统、应用/数据及记录分别由 Windows、既有随包工具拥有，复用现有验包/ACL/桌面流程，不增加生产入口或另一状态机。

当前桌面控制入口两次初始化均返回 `failed to write kernel assets: 系统找不到指定的路径。 (os error 3)`，备用桌面入口也返回同一错误；临时目录与工作区存在，未使用自制UI注入替代。VMConnect 窗口已打开，等待用户在窗口启动虚拟机并处理初次DVD启动按键。客体安装、产品四进程与当前人工回执尚未执行，D11仍待执行，P6-3–5状态不变。

用户随后已打开并启动虚拟机，再次授权自动操作。[本轮只读状态](../build/p6-vm-install-20261119-1/runtime-state-20261119-132131.json)显示VM为Running，但当时心跳/键值交换未连接，VHDX仍4 MiB，尚未观察到系统安装写入。桌面入口经重建会话及备用入口复试仍在执行任何UI动作前返回相同错误，见[重试记录](../build/p6-vm-install-20261119-1/desktop-control-retry-20261119-1.json)。本地运行时路径及当日日志检查未定位可明确修复的缺失资产路径，未猜测修改配置或重建目录；当前仍未取得桌面画面和客体验收原件，D11保持待执行。

### 2026-11-20 安装启动修复设计

用户截图显示新虚拟机停在 UEFI Boot Summary：原安装DVD报告 `The boot loader failed`，空磁盘与数据DVD均无操作系统。[只读检查](../build/p6-vm-install-20261119-1/boot-inspection-20261120-1.json)确认光驱路径正确、VHDX仍4 MiB、原ISO内EFI文件签名有效，未观察到已安装系统；仅凭该汇总不认定原盘损坏或关闭Secure Boot。桌面入口仍在初始化时失败，无法直接处理启动按键。

复用原ISO内 `efi/microsoft/boot/efisys_noprompt.bin`，通过Windows IMAPI生成本轮专用UEFI免按键派生安装ISO。仅改变光盘启动目录指向；原盘全部安装文件保持相同路径、长度和SHA，原官方ISO及Cordis包不修改。派生盘另存来源、引导文件及新ISO摘要，明确它不是官方原盘字节；第二张私有答案/数据ISO保持独立。方案沿用[微软BootOptions接口](https://learn.microsoft.com/en-us/windows/win32/imapi/adding-a-boot-image)与[EFI平台ID定义](https://learn.microsoft.com/en-us/windows/win32/api/imapi2fs/ne-imapi2fs-platformid)，由安装介质处理无人值守启动，不使用替代桌面输入工具。

生成与逐文件复核通过后，仅对已登记VM ID及唯一准确VHDX执行重启安装；重启前再次确认盘仍为空且无客体心跳，保留失败原件。文件输出限定在本轮新建 `boot-repair-20261120-1/`，不操作宿主磁盘、其他VM或生产模块批准。安装完成与D11产品验收分开计，当前D11仍未通过。

### 启动修复实测与客体预备

[派生介质记录](../build/p6-vm-install-20261119-1/boot-repair-20261120-1/receipt.json)确认新ISO SHA `9ebfb6abf00871d5b9159e16b68c6083cd073c0f67c5e81157f6410d17b3c164`；1066项原盘文件在派生盘中逐路径/长度/SHA相同，实际El Torito目录为EFI239、无仿真，所指引导镜像与原盘noprompt文件同SHA。构建脚本 `a406e943…` 及启动脚本 `5cacf996…` 均独立审查通过。仅在准确VM、唯一64 GB Dynamic空盘4 MiB、无快照/父盘、NoContact及原固件/隔离条件复核后重试；[实际启动](../build/p6-vm-install-20261119-1/boot-repair-20261120-1/installation-start.json)采用硬盘→派生DVD→私有答案DVD顺序，避免安装后重复从免按键介质重装。

[进度原件](../build/p6-vm-install-20261119-1/boot-repair-20261120-1/installation-progress.jsonl)记录磁盘开始写入，约210秒首次见客体心跳；[客体管理探测](../build/p6-vm-install-20261119-1/guest-probe-20261120-022106.json)进一步确认Windows 11企业评估版26H2、26300.9457、x64，Setup/OOBE均为0，`cordisqa` 的Explorer桌面会话已建立。此为系统安装及管理连接证据，未冒充普通用户产品运行或D11通过。

客体预备沿用管理与验收分离：临时客体管理员仅把准确ZIP和原`Prepare-Guest.ps1`复制到客体专用新目录、清理本轮AutoLogon/答案缓存并撤下两张安装介质；随后通过Interactive、RunLevelLimited任务在已登录的`cordisqa`桌面执行原预备/验包脚本，独立保留权限/退出原件。预备任务不启动产品、不执行UI动作、不写人工结论；正式产品四进程及人工回执另按原记录器流程完成。

客体预备已实际完成。[首次管理记录](../build/p6-vm-install-20261119-1/guest-stage-20261120-1/receipt.json)保留了复制及本轮AutoLogon/答案缓存清理成功、弹盘后即时观测误拒绝的历史；[新进程复核](../build/p6-vm-install-20261119-1/guest-stage-media-inspection.json)明确两DVD路径均为空、客体MediaLoaded均false，未重做复制/清理/弹盘。续做脚本`587bba86…`绑定原失败记录与`ee323467…`受审源，只复用未执行的QA任务段，经独立审查后运行；[完成记录](../build/p6-vm-install-20261119-1/guest-stage-20261120-2/receipt.json)确认任务仅启动一次并在结束后注销。

[实际QA环境](../build/p6-vm-install-20261119-1/guest-stage-20261120-2/environment.json)为`cordisqa`、session2、64位、UserInteractive=true、非管理员且完整性RID8192；原预备子进程PID7620实际exit0，独立stdout/stderr与[退出记录](../build/p6-vm-install-20261119-1/guest-stage-20261120-2/completion.json)已回传核摘要。[客体独立验包](../build/p6-vm-install-20261119-1/guest-stage-20261120-2/package-verification.json)通过137载荷及准确外部manifest锚，包位于客体`C:/Users/cordisqa/Cordis-P6-D11/application/Cordis-0.1.5-preview.2-windows-x64/`。此次只完成安装与普通权限完整性校验；桌面控制入口复试仍报初始化路径错误，产品/记录器均未启动，四进程UI及当前人工回执仍缺，D11保持待执行。

### 2026-11-20 评估客体正常激活准备

[客体探测](../build/p6-vm-install-20261119-1/guest-probe-20261120-041813.json)观察到唯一 Windows EnterpriseEval 许可为 `LicenseStatus=5`、剩余宽限期0，1074事件明确由`wlms.exe`因许可证期限发起关机；此为客体环境问题，尚不能开展稳定的四进程验收。[微软评估说明](https://www.microsoft.com/en-us/evalcenter/evaluate-windows-11-enterprise)说明评估版无需产品密钥，未激活或到期会每小时关机。

先按正常 Windows 激活机制准备环境：仅对已登记 VM ID，通过临时客体管理会话核实计算机身份和唯一 EnterpriseEval 许可；确认原网卡数为0后，只为该 VM 临时新增一张连接宿主既有 `Default Switch` 的网卡，调用该许可的[Activate](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/sppwmi/activate-softwarelicensingproduct)，必要时正常刷新许可状态。临时网卡由本轮脚本唯一拥有，无论激活成功与否均在 finally 删除，并用独立新进程复核该 VM 网卡再次为0；不修改虚拟交换机、全局网络或其他 VM。

管理日志仅记录调用结果、最少许可状态及清理证据，不记录凭据或产品密钥；不采用换密钥、rearm、改时钟、禁用许可服务或其他绕过。网络或激活失败保留真实原件。以上为实施前边界；实际两次结果见下方记录。正常激活与 Cordis 验收分开，D11 保持未通过；恢复隔离后再重新采集客体环境与产品流程。

### 桌面控制恢复与权限边界

原会话仍在执行JS前返回kernel-assets路径错误；全新控制子会话已能通过正规`@oai/sky`列窗、读取UIA和截图，未以自制输入替代。[本轮诊断整理](../build/p6-vm-install-20261119-1/diag-desktop-control-20261120-1.json)明确它源于实际工具回执、不是原始trace或产品验收原件。High VMConnect中正常显示设置窗口的一次关闭按钮点击未观察到生效，不能把API未报错当作输入成功。

[专用连接准备](../build/p6-vm-install-20261119-1/console-access-repair-20261120-2.json)只给当前宿主账号授予本VM的ConnectAccess并启动当时已关机的准确VM，未加入Hyper-V管理员组；随后普通权限VMConnect按名称和GUID均拒绝。普通token对WMI命名空间已有Rights19，查询准确VM却为0条，未扩大命名空间权限。微软[委派说明](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2012-r2-and-2012/dn741279(v=ws.11))区分控制台授权与标准VMConnect工具的权限需求。已请求用户将承载对话的程序以管理员身份重开；未响应前不重复注入High窗口、不操作认证/UAC界面、不启动计时录制器。

这条单VM授权未能使普通权限VMConnect可用，不保留无实际用途的权限路径。[清理记录](../build/p6-vm-install-20261119-1/console-grant-cleanup-20261120-1.json)已绑定原授权前为空、准确当前用户及本VM，仅撤销本轮新增项，复核授权列表再次为空；其他权限、组和运行中的客体均未改动。

### 激活首次实测与有界续做

受审脚本 `f2b827be…` 已执行。[首次激活原件](../build/p6-vm-install-20261119-1/evaluation-activation-20261120-1/receipt.json)确认临时网卡取得有效地址/默认路由，但许可服务返回“正在尝试为此Sku执行另一个激活操作，请等待后重试”；当次仍为通知状态5、剩余0，finally移除本轮网卡，独立新进程确认网卡0。首次源文件与失败原件保持不变。

续做保留同一 VM/磁盘/唯一许可和临时网卡的资源边界，只增加正常在线激活的等待：接网后最多观察90秒，若尚未Licensed则最多显式调用一次原Activate，再最多观察120秒；随后无条件撤去本轮网卡并独立复核。续做脚本另存并另记第二次原件，不循环重置或延长评估期，不以API无错误代替实际Licensed状态；尚未得到Licensed前不启动QA恢复或产品录制。

续做脚本 `c4c97e35…` 经独立只读审查和PS5.1解析后已实际执行。[第二次原件](../build/p6-vm-install-20261119-1/evaluation-activation-20261120-2/receipt.json)确认接网后的第3次正常观察已为 `LicenseStatus=1`、剩余129600分钟（90天），因此未再显式调用Activate；正常刷新后状态保持。finally删除本轮唯一网卡，独立新进程确认网卡0、VM Running，许可与隔离均成功。此项修复客体环境，不构成产品四进程或D11通过。

### 普通 QA 桌面恢复边界

评估许可正常恢复为 Licensed 后，才准备恢复因许可关机而丢失的普通 QA 桌面。沿用原 Windows 安装时的 bootstrap 管理边界：仅管理已登记 VM、唯一磁盘及现有 `cordisqa`/`cordisadmin` 账户；确认无网卡、DVD介质、业务/录制进程及 QA Explorer，且 Winlogon 自动登录字段全部不存在后，由客体管理会话使用既有 QA 凭据设置一次临时 AutoLogon，并请求一次正常客体重启。凭据仅从受限 DPAPI 文件取得，通过 PowerShell Direct 的凭据对象传递，不写命令行、原件或日志。

该动作由 Windows 自身处理有效本地账户的启动登录，不操作认证对话框，不创建账户、修改密码/组、改动业务数据或启动产品。宿主只作有界的管理连接/唯一 QA Explorer 观察，并在 finally 通过管理路径核本轮账户身份、精确清除本轮 AutoLogon 字段；无法完成清理须保留未完成证据。后续界面由获授权的原桌面工具另行接管，普通用户产品验收仍走原录制器；桌面恢复不计为 D11 通过。

脚本 `3c1b63bc…` 经PS5.1解析及独立审查后已实际执行。[恢复原件](../build/p6-vm-install-20261119-1/qa-desktop-restore-20261120-1/receipt.json)确认仅一次正常重启，新boot `2026-11-20T04:43:26.7813910Z`、唯一QA Explorer PID5360/session1、同一普通QA SID及Licensed状态；本轮临时AutoLogon字段和所有权标记均已清除，remainingKeyNames为空。未启动产品、未写业务数据或人工结论。

正式录制启动脚本 `691760bd…` 已完成独立审查，复用原ACL/录制器并动态绑定实际QA桌面会话；目前未执行。待用户将控制端以管理员身份重开并实际验证窗口输入后，再在普通QA桌面启动四进程录制。当前仍缺四进程UI、加载/保存/退出原件及新人工回执，D11未通过，P6-3–5不推进。

用户随后回复已以管理员身份重开，并提供QA账户密码页截图。认证页面交由用户手动登录，暂停桌面输入，不重启或再次配置自动登录；录制器仍未启动。为使用户能够取得安装时生成的QA临时凭据，准备独立的本地查看脚本：只解密本任务现有DPAPI凭据，在用户自行运行时显示QA用户名/密码，不写入新文件、命令行或对话，不操作登录页面或修改VM/账户。用户关闭查看窗口并进入QA桌面后再恢复控制测试。

### 增强会话的普通用户登录准备

用户随后提供远程桌面登录权限不足的截图。增强会话使用远程登录，现有QA账户仅按本地普通用户创建；修复属于客体显示/会话准备，不改变产品职责或验收权限模型。先由既有PowerShell Direct管理路径核准确VM/磁盘、原QA SID、非管理员、工作组与现有组/允许及拒绝远程登录权；只有内建Remote Desktop Users组本来具有允许权、QA不存在有效拒绝项时，才将准确QA加入该客体组（SID555）。保留前后原件，不修改本地安全策略、宿主权限、网络/NLA、防火墙、密码或管理员组；拒绝策略不自动绕过。用户重新手动登录后再验证普通会话，录制器仍保持未启动。

[首次修复原件](../build/p6-vm-install-20261119-1/qa-enhanced-access-20261120-1.json)观察QA仅属于Users组545；安全模板导出未匹配远程登录权，脚本按门禁停止，未增加成员。[独立导出复核](../build/p6-vm-install-20261119-1/qa-rdp-policy-inspection-20261120-1.json)表明合并导出也仅114字节、没有PrivilegeRights段，不能据此把有效允许权认作空。[直接LSA只读观察](../build/p6-vm-install-20261119-1/qa-lsa-rights-inspection-20261120-1.json)使用仅0x801查询权限，确认允许权状态0含555/544，拒绝权状态0x8000001A表示无账号；没有Add/RemoveAccountRights调用。

续做替换权限观测来源，复用准确SHA绑定的已执行LSA观察器，重新读取有效权利并核原门禁后，仅补QA的555成员身份。旧安全模板导出路径因不能证明有效权利，不再用于续做授权判断；原失败源码和记录仅作为历史证据保留。续做仍不更改策略，也不自动操作登录页，成功结论须分别核成员状态和用户手动重新登录。

续做脚本 `2e3bff54…` 已完成解析及独立审查并实际执行。[第二次权限原件](../build/p6-vm-install-20261119-1/qa-enhanced-access-20261120-2.json)确认仅将原QA加入555、成员状态true、管理员状态false，策略和密码均未改动；该成员身份用于后续增强会话，保留至验收环境使用结束。用户随后明确回复已登录并提供QA桌面截图；[新管理探测](../build/p6-vm-install-20261119-1/guest-probe-20261120-094011.json)确认同一客体26H2/26300.9457、唯一QA Explorer PID5136/session2、许可状态1。登录环境已恢复；当前正在重新验证正规控制工具的实际点击，录制器尚未启动，D11未通过。

用户登录后再次以全新正规Sky会话复测：列窗/截图成功，窗口60426126仍提示目标完整性高于控制helper；仅一次任务栏Explorer点击，立即及延迟观察均无窗口变化，未证实输入送达。未启动产品或计时录制。已向用户发出执行方式选择：继续修复宿主桌面控制，或在客体普通QA桌面使用仓库既有`tools/preview_ui.py`/`preview_ui_action.ps1`真实UI验收驱动；后一方式需另置便携Python及隔离验收工具目录，复用原随包UI观察模块，不改产品字节/记录器。当前仅核对依赖及摘要，尚未部署或执行该驱动，等待用户选择。

### 客体内项目 UI 验收驱动

用户已明确选择“改用项目UI验收脚本”，并将本轮目标设为完成P6。按原阶段门禁先完成D11，再推进更新恢复、CI及最终交付；不把工具准备计为阶段通过。切换后使用原项目驱动在客体实际QA桌面执行，不依赖宿主Sky点击。原记录器继续唯一拥有产品启动/退出、包及加载路径证据；原ACL工具继续拥有只读应用副本与恢复。新增协调器仅按记录器登记的四个PID/入口/首次重开序号依次运行原驱动一次，任何Unknown/非0即停止，不重放输入、不杀产品、不设置内存业务状态。配置外部冲突由原驱动负责，先前单独准备的冲突协调脚本不同时运行。

使用与宿主相同版本的[官方CPython3.14.8便携包](https://www.python.org/downloads/release/python-3148/)，实际12,601,679字节，SHA `a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310` 与官网公布值一致，解出的python.exe为Python Software Foundation有效签名；原件见[下载记录](../build/p6-vm-install-20261119-1/ui-tools-20261120-1/download-receipt.json)。只部署到独立`C:/CordisP6Tools/ui-20261120-1`，Admin/System拥有、QA仅RX，Python以绝对路径和`-I -B`运行；不安装pip/SDK、不更改全局PATH或产品包。工具来源清单SHA `9fb5a08890da619ae437c43d30dd2a9c6efb951a4df503890aa9efc1ca0a00aa` 绑定便携ZIP、原驱动/动作脚本/随包UI观察模块和协调器五项源，客体清单另绑定完整解包文件、单次launchToken及实际QA会话。

协调器 `43626e81…` 与宿主部署启动器 `258fad83…` 已解析并独立审查。部署只创建全新工具目录和Limited/Interactive无触发器任务；原录制器由独立已审启动器随后启动。自动UI记录明确automated/incomplete，原录制器字节和默认报告不改；宿主另核原记录器实际exit0及四个正常退出。当前人工体验回执仍须单独取得，不能由脚本补写。

### D11 配置冲突的外部文件协调边界

配置冲突沿用原 UI 验收的 `config_value('PreviewUI-V1'/'PreviewUI-VExternal'/'PreviewUI-V2')` 值与 JSON 序列化。界面控制端完成 V1 显式保存、V2 应用并保留未保存状态后，由根执行一次外部文件写入；辅助脚本不操作窗口、不推断或改写内存状态，也不启动、关闭产品。辅助脚本只使用准确 VM 的管理会话及本轮录制器登记的 `configuration/cordis.yaml`，同时核 scope、DataRoot、包摘要锚、登记路径，以及写入/冲突核对时的活动配置进程；禁止传入其他配置、session 或 store 路径。

WriteExternal 先要求实际磁盘 JSON 等于原 V1 值，保存原始字节及摘要后，以独占写入句柄重新核前值，再写同 schema 的 VExternal 并核实际字节。VerifyConflict 只读确认磁盘仍为 VExternal，VerifyFinal 只读确认已由原 UI 保存为 V2；每次模式使用新的本轮宿主证据目录及 CreateNew 原件，拒绝已有输出、重解析路径或路径逃逸。运行记录明确 recordingOnly/automatedControl，界面操作证据与磁盘证据分别保留，任何模式都不构成 D11 或人工验收通过。

### D11 环境管理侧盘点

在产品录制前补充一次独立只读环境盘点：绑定准确VM、原QA SID和已登记磁盘，宿主只记录OS/Hyper-V文件版本、虚拟资源、介质/网卡/集成服务及已知会话配置；客体通过既有管理连接读取OS、QA组与Explorer所属会话、许可和显示适配器，以及机器和已加载QA注册表中固定的PATH/Qt/VS/SDK变量、相关安装项与标准开发目录存在性。不读取凭据值、全量环境或用户日志，不启动用户任务/产品、不重启或改变客体状态，输出仅写本轮新JSON。管理连接不能证明QA进程token、实际产品渲染/加载路径或全部RDP重定向状态，无法确定的共享/重定向明确标为unknown，不把缺少管理会话映射当作无共享或D11通过。

盘点脚本 `7e3a1220…` 经PS5.1解析后已实际运行，[本轮JSON](../build/p6-vm-install-20261119-1/d11-baseline-inspection-20261120-1.json) SHA `23df88679394b23aaea5b2f07eff2e5a988e55760be928ce4835524534cdcf51`，error为空。宿主为Win11专业版26200.8655、vmms10.0.26100.9949；VM配置12.0/VMBus、4CPU、动态内存4–6GiB、网卡0、DVD介质0、快照0。客体同一QA SID、非管理员、Explorer5136/session2、许可状态1；所读机器与QA注册表PATH为系统目录/WindowsApps，固定Qt/VS/SDK变量无值、所查相关安装项及标准开发目录未发现开发环境。显示适配器分别报告Hyper-V Video1024×768及Remote Display1366×768；实际产品renderer与QA会话的驱动器/剪贴板/打印机重定向仍为unknown。以上仅覆盖明确列出的管理侧查询范围，不替代QA token、实际加载路径或人工体验原件。

### 客体原 UI 驱动的序列协调边界

按用户选择，在客体独立只读验收工具目录使用便携 Python 和原 `preview_ui.py`/`preview_ui_action.ps1`，随工具复制的 UI 观察模块与原包逐 SHA 相同，不修改原驱动、产品或录制器。薄协调脚本只绑定部署清单/本轮 token、原 QA SID、实际交互会话及 Medium 非管理员权限，读取固定 DataRoot 的原登记和状态，依次把配置首次/重开、业务首次/重开的准确 PID 交给原驱动；同一轮只调用一次，须观察新 PID 与对应 entry/ordinal 后才进入下一轮。

协调脚本只拥有便携工具进程及新的自动操作原件，不拥有产品启动、退出或 ACL；这些仍由原 `record-desktop.ps1` 和已审启动包装脚本处理。原驱动保留240秒预算和实际 UI/冲突流程，协调端最多等待录制开始600秒、总计1200秒；Unknown、非0或超时保留未完成及活进程，不重放输入、不强制关闭或改业务内存。原驱动已含外部配置冲突写入，因此本轮不执行前述独立冲突协调脚本，避免重复外写。

四轮驱动的独立 stdout/stderr、真实进程/退出与原录制文件分别保存。即使四轮 UI 返回0且录制文件声明四产品正常退出，协调结果也保持 automated/incomplete，等待宿主另外核原记录器实际退出原件；不写 D11 或人工通过结论，不把自动操作等同用户体验回执。

### 独立 UI 工具的转移和协调启动边界

按用户选择使用项目UI验收脚本，工具部署与受审产品保持分开：宿主以外部SHA锚定的sources manifest核唯一官方Python嵌入ZIP、原三脚本及本轮协调器，安全枚举ZIP并推导完整载荷摘要；只向准确VM的新`C:/CordisP6Tools/ui-20261120-1`转移、解压及逐文件复核。工具根显式限定Administrators/System完全控制、原QA读取/执行，不沿用旧授权；不改全局PATH、注册表、网络或安装pip/Qt/SDK，协调器的输出由QA在自身profile新建。

管理端核原QA SID、非管理员、当前唯一QA Explorer会话、无产品/录制进程及隔离条件后，仅注册无trigger的Limited/Interactive协调任务并手动启动一次。协调器绑定本轮token、真实session和工具manifest摘要，先等原录制器登记；宿主只在45秒窗口观察真实QA启动日志便返回，不代替根启动录制器/产品、不等待完整UI。失败保留目录/任务/进程和真实日志，不重启、强杀或删除活文件；完成后由根核实际结果再注销任务，D11和人工结论仍独立。

### 本轮客体录制的只读证据导出

宿主快照辅助脚本只通过精确 VM ID 的管理会话读取本轮 UI 协调器、录制器和 DataRoot 的状态与日志。每次导出使用新的宿主时间戳目录；普通日志按白名单复制，拒绝 reparse 路径。最终取证覆盖本轮 scope 顶层 ACL/验包 JSON、完整 DataRoot 的普通 JSON/YAML/日志（含配置、业务会话和 store 明细）及驱动保存的 external-original.bin；不遍历应用整包副本，不复制 DLL、工具载荷或凭据。运行中的日志可能在快照期间增长，导出仅记录该次实际可读字节和读取错误，不宣称跨文件原子一致。

监控不拥有客体产品或输入生命周期；只观察任务和相关进程的实际 owner/session/PID。Unknown 或非零退出只上报原始证据，由主流程决定后续，不重放输入、不重启、不关闭或终止任何客体进程，也不散列运行中 DLL。显式单次业务重开续作后，监控只读范围增加该次 resume 控制目录、任务与 continuation-1 证据目录，保留原 Unknown；完成后导出全部普通日志并核关闭后的 stdout/stderr，无客体写入或任务注销。

### 第四轮前台激活的最小修正与显式续作

前三轮原 UI 驱动退出0，第四轮业务重开只完成首次 Read，随后原生输入 guard 发现 foreground/focus 为 QA Explorer PID5136，未发送业务输入，原 Unknown 与活产品 PID5116 原样保留。续作只允许用户已授权的单次业务 reopen，不重复前三轮、不重新启动产品，不改变原记录器的生命周期或 ACL 所有权。

驱动仍先正常 SetForegroundWindow 并执行原 PID guard；仅失败后重新读取 UI 快照，核唯一 ElaWindow、HWND 和登记 PID，再用当前线程的 PeekMessage 创建消息队列，临时 AttachThreadInput 到前台/目标线程，调用 SetForegroundWindow/SetFocus。连接对去重，所有成功连接在 finally 逆序解除，解除失败或解除后原 guard 不满足均 Unknown；激活返回值、线程及前后事实独立留证。该分支不发送业务按键/鼠标，不修改全局 foreground lock、UAC、权限或产品状态。[API 约束](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput)及[焦点约束](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus)均由 Windows 执行，返回成功也不能替代最终实际 PID 检查。

可选 evidence-label 仅允许1–48位 ASCII 字母数字及下划线/连字符，首位为字母数字，只给独占新建的证据目录追加后缀；默认目录、phase、run root、登记和 PID 规则不变。因此宿主可显式给同一活 PID 建新续作证据，不覆盖或删除原 Unknown，也不把标签当作重试许可。

### 同一业务重开进程的显式续做边界

原四轮UI协调器已结束，前三个driver实际exit0；第四轮业务重开PID5116的首次Read后，实际前台/焦点仍为QA Explorer，激活守卫报Unknown，原录制器和产品保留运行。续做仅面向同一PID/ordinal2，先核当前注册/status、原生父子进程/普通QA会话、旧Unknown/原始Read摘要及真实客体目录完整五件Read证据；不从StatusOnly宿主副本推断没有其他输入。旧工具集和失败原件保持不动，新driver仅修正受控激活资源释放及独占证据后缀，不能授权重放其他阶段或改业务状态。

续做工具放入新Sibling目录`C:/CordisP6Tools/ui-resume-20261120-1`，复用原摘要锚定的37件Python运行时及原action/module，新driver按执行外参SHA冻结；新清单和Limited/Interactive任务只运行一次business/reopen并写`continuation-1`独占证据。管理预检与QA启动前复用同一只读状态校验；状态、已保存文件或原进程身份变化则停止。只保留stdout/stderr、实际driver PID/退出及新UI摘要，不重启/强杀原产品或录制器，不写store/内存，不执行ACL恢复；原wrapper仍拥有四进程完成后的恢复与最终采证。

### Restore 后 ACL 比较的 Windows 状态位边界

本轮198个对象恢复后的 Owner、Group 和有序 ACE 均与原状态相同，Windows 仅增加 SE_DACL_AUTO_INHERITED（0x0400），原完整 SDDL 字符串相等检查因而误报失败。仓库修正仅替换 Restore 完成后的核验，不改变恢复写入、顺序、原 state、producer path、token 或对象 identity 门禁，也不修改冻结包或本轮客体 ACL。

比较以真实 RawSecurityDescriptor 为输入：Owner/Group SID 精确一致，DACL 的 null 与空值严格区分，非 null DACL 逐完整有序 binary 相等；ControlFlags 的 XOR 只能为0或0x0400。Protected、AutoInheritRequired、ACE flags/order/masks/SIDs/GUID、ACL revision 和所有其它控制位均不忽略。报告保存规则、允许掩码、仅该状态位规范化的对象数量及具体不匹配维度，不能用字符串替换或 AccessRule 排序掩盖差异。

随后独立只读客体核验复用受审比较函数及原工具 Native.Identity，绑定本轮 registration/state SHA、QA token 和198对象，核包字节139文件与登记初始快照；不再执行 Restore、SetACL 或其它权限写入。临时访问探针如需执行由主流程另行决定，不能把只读 ACL 核验伪装成新的恢复操作或覆盖原失败报告。

### 独立人类体验窗口的启动边界

技术录制四进程正常退出后，人类体验另开第五个业务进程，不重跑原四轮录制，也不从自动序列推断人类接受。启动器只在原recording完整/四exit0及原recorder-exit0、同一原包139件、普通QA会话和独立只读ACL恢复验证全部通过后使用；ACL验证结果由根在执行时提供完整SHA锚，不改旧失败报告或再写DACL/owner/group。

单次Limited/Interactive任务仅沿原`New-PreviewProcessStartInfo`打开相同business文件、store和独立cwd，清理仅子进程环境。QA新HumanReview目录记录token、真实产品PID/句柄观察、前后包摘要及最少业务/store原件；只读runtime/UI快照最多两次，日志用原有4MiB上限reader排空至用户正常关闭。任何错误都不自动输入、强杀、关闭或修改内存/store；humanAcceptanceRecorded和D11Passed保持false，实际体验陈述与实际退出由根后续独立核对。

### D11 本轮技术结果与人工待验

本轮使用准确ZIP `d236f5965bccf124eabde8fa0f1539fd910e2eff33a432affbea0174d18c7168`，未重建或替换产品、Native批准源或六A/B。普通QA/session2/medium权限下，配置首次6536、配置重开9604、业务首次2676、业务重开5116均由原记录器持句柄确认正常exit0，原记录器6468也exit0。配置完成V1应用/显式保存、V2未保存、外部VExternal冲突、采用基线后仍保留V2、显式保存及同文件重开；业务五次升级均有Applied/dirty/Written原件，Notebook草稿与贡献换代、按钮/命令输出及A/A/B同文件/store重开均已核。

第四轮原driver4308在首次只读快照后因前台/focus为Explorer而Unknown，未发送业务输入。原五件失败证据和旧协调器exit1保留；受审新driver5684只续做同一活产品5116，实际exit0，随后原产品和记录器正常退出。续做的8次激活均由正常SetForegroundWindow成功完成，临时AttachThreadInput分支只由纯单测覆盖，不声称在客体实走该分支。当前26项UI纯单测另捕获[真实命令/日志](../build/p6-vm-install-20261119-1/ui-final-check-20261120-1/result.json)，exit0且前后源码SHA相同。

原wrapper的Restore核验曾返回false/exit1，保留原件；独立只读[QA核验](../build/p6-vm-install-20261119-1/acl-verification-start-20261120-1/qa-result.json) SHA `c600458dbd74b480e1764bd53627193549a623b4c7d691c616a85e29264bc637`确认198对象仅增加0x0400状态位，Owner/Group/有序完整DACL未变，139包文件与登记初始字节相同，原有文件WRITE_DATA/DELETE句柄访问均成功。核验未写删文件、未再写DACL。比较修正验证为1个unittest覆盖34个真实RawSecurityDescriptor用例，另有6个既有ACL回归测试通过，命令和stdout/stderr均保留，不能称为34个unittest。

[完整客体导出](../build/p6-vm-install-20261119-1/d11-snapshot-20261120-115939-828/)含1114文件、30,550,003字节，复制错误/缺失/长度差异均为0；其中六个store DLL只列排除元数据，不能声称已复制其原始字节。[第三版独立审计](../build/p6-vm-install-20261119-1/d11-independent-audit-20261120-3/audit.json) SHA `1e265f93819a84e0b8a80eaf29c8109dbb2b27f2b43df1b36099872bc56d3840`逐SHA复核旧输入后补入恢复核验，19技术项通过、0失败、0技术待定；[1280文件索引](../build/p6-vm-install-20261119-1/d11-independent-audit-20261120-3/source-sha256-index.json) SHA `15265942dd5fac3c7f9b876550e7f5da1362b9e2954ae00405bc286a4e1a8602`。审计保持human=false/D11=false。

人类体验第五产品2516已在同一普通QA桌面打开，使用相同应用、business文件、store和cwd；[启动回执](../build/p6-vm-install-20261119-1/human-review-launch-20261120-1/receipt.json)只证明启动，不证明体验或退出。应由用户核Counter/Reporter 1.1.0、Notebook 1.2.0及笔记按钮/命令，正常关闭后再交叉核真实退出与包字节，方可登记本轮人工结果。当前不借用原P5人工回执。

### 客体会话资源与历史原件边界

补充只读QA会话资源及Shell盘点：当次QA Shell只显示C盘及两个光驱，未显示重定向文件系统资源；所查网络映射成功返回空，rdpclip进程存在但未读取剪贴板。固定tsclient路径查询失败不能证明所有通道均不存在；剪贴板/设备重定向和实际GPU renderer仍不作覆盖承诺。该有限盘点不替代实际产品加载路径证据，原件位于本轮`session-resources-20261120-2/`及`session-shell-resources-20261120-3/`。

D15所缺19份原P5/配置宿主资格记录保留原路径和SHA，不伪造或以新报告回填。[本地盘点](../build/p6-evidence-inventory-20261120-1.json)及[GitHub补充盘点](../build/p6-evidence-inventory-github-supplement-20261120-1.json)均未找到准确原件，用户备份位置仍待反馈。[恢复分析](../build/diag-p6-d15-evidence-recovery-analysis-20261120-1.md)将同字节新采证与历史原件明确区分；新采证也不能自动改6c批准源或绕过打包输入门禁。本轮技术证据另作可搬迁归档，状态保持技术通过/人工待验，不冒作P6最终交付。

### 本轮可搬迁技术归档与续做入口

归档已实际生成在 `D:/WorkSpace/delivery/Cordis-P6-D11-technical-20261120-1.zip`，60,570,404字节，SHA256 `0a9133130e7511c20b63fd634e13b8f42c64bd63de283a52916239e993dccd31`，旁置`.zip.sha256`及完整复核JSON。1407原件及11份控制/索引文件共1418条目，展开共131,071,964字节；包含准确冻结ZIP及展开包、完整本轮快照、工具/测试原件、第三版审计、独立恢复核验、有限环境盘点、历史缺件清单及当次源码/文档快照。完整解压后按相对路径核文件集合、长度和SHA通过；主流程又以独立只读代码逐条复核ZIP与源归档目录，未执行归档载荷，结果见同父目录`.root-verification.json`。MANIFEST SHA `15bf5c28f3ecc98577f91001f980cd7896462f7184df5f73d8b0b51b3e333fc8`。

归档状态为`TECHNICAL_VERIFIED_PENDING_HUMAN`，human/D11/P6/D15均为false；原1280索引中的3个人工启动引用仅列排除映射，未把正在运行的人类体验输出纳入已完成原件。六store DLL未导出字节、19份关键历史原件缺失、原Restore=false及首次reopen Unknown均明确保留。归档不含凭据、答案文件、Windows ISO或虚拟磁盘，也未外部发布；不能据此宣称D15全部历史证据已齐备。

2026-11-21T12:37Z的[新鲜独立只读观察](../build/p6-vm-install-20261119-1/human-review-export-20261121-123716-407/receipt.json) SHA `fc60d0b683d58f705ff57b7ab7db1d5ad8f1e54a121eeb9665b1b309c65ce429`确认原体验产品2516和wrapper8236仍为原QA/session2及原创建时间，任务Running，尚无product-exit或session-summary。观察只导出7份已完成元数据，0复制错误，不读活DLL、不复制正在写入的stdout/stderr、不操作窗口或权限；此前12:05观察原件也保留，当前不连续轮询，仍待用户体验和正常关闭。

P6-3仅完成[只读架构预研](../build/p6-3-architecture-readiness-20261120-1.md)，SHA `24350f2a1d3647b814a91867f33391ab793cf0fac48b6ef2a9dd9209cef6ea7a`，未实施。候选路径为恢复真实preview.1后与已验证preview.2演练并列应用/成套数据备份、原Session/Store显式迁移及失败恢复；preview.1实物当前仍缺，不将历史摘要当作已持有旧包。配置`--file`不重定向用户偏好文件，须单独考虑备份/CAS恢复。D11、阶段审查及提交同步clean门禁保持。

2026-11-21进一步逐路径核实旧`p6-2-failures-1/report.json`、`p6-2-readonly-data-evidence-1/report.json`及其独立审计、`p6-2-physical/independent-audit.json`当前均缺失。它们的历史摘要保留，不作为当前可核原件；D10也不能仅因旧文字已通过而收口。五启动负例优先复用未改的`verify_preview_failures.py`重采：因当前仓库ACL比较修正与冻结包内旧helper不同，须将未改driver及冻结包原ACL/observer同字节放入独立工具布局，保持原Prepare的selfhash门禁。未持有真实完整旧数据baseline时不提供该参数，当前采证只能证明新case目录不变，不能宣称保存了旧P5用户数据。不可写Save真实UI原件仍另待恢复或重采。

新[五启动负例原件](../build/p6-d10-host-recapture-20261121-2/startup-negatives/report.json) SHA `e0081835c05817c910136d80cea7fcfbdc342899c5cf91b3ce4d6585ae108f7d`：普通host token/medium，缺Counter B16348、篡改39016、声明摘要39332、ABI36896均实际exit1且stderr符合；缺Core39676实际`0xC0000135`。原139包文件、各case变更后应用和新空data目录均前后不变，无强杀、超时或活产品残留。首批因遗漏并列原verify-package依赖而在Prepare门禁前失败，0产品/0cases，失败目录保留；第二批补齐四工具同字节布局后driver36620 exit0。

[限定独立审计](../build/p6-d10-host-recapture-audit-20261121-1/audit.json) SHA `0e043f0b5a32d5496eca485723e189d7ad85857ca1f1a9ceff29226db1844ea0`的46项核对通过，含真实退出、原两路输出、精确单项故障、源包及每case实际文件、staging来源和非提权token；1051输入摘要另存同目录。每个短寿命进程都有一条原始Modules `ERROR_PARTIAL_COPY`，不能把外层`observationErrors=[]`当作所有观察均无错；本次只支持启动拒绝与这些新副本保持，不支持完整DLL闭包、旧数据保护、不可写Save、GUI或D11通过，`oldDataPreservationClaimed=false`保持。成功客体流程的加载证据仍按原范围单独使用。

该限定补充已单独归档为 `D:/WorkSpace/delivery/Cordis-P6-D10-startup-supplement-20261121-1.zip`，386,441字节、89文件，SHA `2272de20e65d13360a854cff9eb1a61c7c03347091ce965c92e3dc66abe3dfab`。包含两批准备/运行原件、限定审计、三件仍存在的实际故障变更文件及1051输入的相对覆盖映射；未变应用字节复用主技术包的冻结139件绑定，不宣称复制了五套完整应用。完整解压及主流程独立逐entry长度/SHA/目录集合复核通过，同父目录保留`.archive-receipt.json`和`.root-verification.json`；主技术ZIP原SHA不变，未覆盖早先归档或扩大验收结论。

不可写Save的[最小设计](../build/p6-d10-readonly-save-design-20261121-1.json) SHA `968c492c45cc5a672861fef36f4c563c7b62d56abdb1be58ac3a824750828825`先完成只读预研：待当前人工产品2516/wrapper8236正常结束、退出后原件导出冻结，再在同一VM普通QA会话独立采证，不争当前人工窗口前台或触碰宿主KnownFolder。复用原UI/action/observer/ACL producer，单产品记录器和有限阶段适配器分工；新证据/握手在拒写data之外，同R1 Apply→有效DACL拒绝→Save失败且C0/目标/修订/活进程保持→原producer恢复→同R1显式Save成功→真实exit0。该记录产生时仅有设计，后续实施见下节；不能用旧desktop registration伪装新场景。

### D10 不可写保存工具的实施合同

人工窗口仍存活时可先完成工具代码与审查，GUI执行门禁不变。[最小实施合同](../build/p6-d10-readonly-save-contract-20261121-1.json)在改代码前固定两项职责：`record-readonly-save.ps1`唯一拥有单产品句柄、原producer权限变更/恢复及磁盘/退出原件；`preview_readonly_ui.py`复用既有纯UI helper，只通过实际UI完成有限阶段，不借旧四轮Driver登记。使用新的不可变登记、nonce、创建FILETIME与同QA进程身份，以及位于拒写data之外的七份CreateNew阶段收据和逐次摘要链；未知结果不重放输入。

原action helper目前只核PID/进程名，不能排除PID重用；新增可选的实际target/wrapper身份前置核验，由本场景强制提供，旧调用合同兼容。核验实际创建时间、会话、路径、SID及父进程，不能把登记字段本身当证明；不改产品/Native或公共observer。原ACL工具仍是唯一writer，恢复责任在Protect调用前进入，原Restore false单独保留。仅0x0400状态位的已审只读比较可证明语义恢复；成功Save可能换文件ID，已恢复责任必须先清除，finally不得再次套旧state。若ACL writer仍活，不强杀或并发Restore；若产品仍活，保留真实句柄等待正常关闭，不遗失唯一退出观察者。

实施已完成在[单产品记录器](../tools/record-readonly-save.ps1)（SHA `d0e067f116159f364b97e9732938c01203bf7d4bd8d5a26609f8fa2ac1739951`）与[有限UI适配器](../tools/preview_readonly_ui.py)（SHA `a1da2b0c931344f22a5c536349bc128dd4fb6a9a5a5f1e7c1d95a7d530df6c42`）。原action的可选身份核验（当前SHA `17b728eb071b59dfa1a1603f3b81bd2f18cc0ab42cad708d98978b02b6d620c6`）同时绑定实际普通QA actor、Python caller与父wrapper；新调用强制核验，旧Driver/原D11工具副本未改。观察worker不新建控制台，焦点仅有界重读等待，无未知输入重放。第一次预备遗漏source verifier的历史失败和此前已冻结包均保持。

收口复查发现打包器会枚举`delivery/windows`并严格拒绝白名单外素材，因此该自动QA记录器源码按[位置取舍](../build/p6-d10-readonly-save-placement-20261121-1.json)移至`tools`，未改任何字节或扩大`SUPPORT_ASSETS`。已验证的独立工具包保持原内部`delivery/windows/verification`布局；owner用显式UiToolRoot查找依赖，并把实际PSCommandPath与工具清单绑定，不依赖仓库放置位置。既有测试/审查原件保留当时源码路径和同一SHA，不回写旧证据。

[UI纯检查](../build/p6-d10-readonly-ui-units-20261121-3/result.json)20项及隔离CLI help通过；[原生身份检查](../build/p6-d10-action-identity-check-20261121-1/result.json)为2项unittest，含14种错误descriptor、4种顶层PID强转拒绝及持有的无UI子进程自然退出拒绝；[owner预检](../build/p6-d10-owner-preflight-check-20261121-2/result.json)为5次真实坏输入CLI拒绝且Scope未创建，以及47项提取纯函数检查，均有命令/原始stdout/stderr及source before=after。它们未启动Cordis产品或调用ACL producer。不能把47个纯函数case称为47个GUI测试。

[非作者实现审查](../build/p6-d10-independent-review-20261121-1/implementation-review-final.json) SHA `7a7a236ee8493852c1632341d5906170c6f60275135a3cb146a60d8375339fef`绑定最终源及原日志，无剩余实现阻断；另以旧D11真实R1快照纯解析核对匿名四列表格，未执行新GUI。审查确认原包137载荷+2清单先逐SHA通过才执行原ACL源、Protect部分失败仍承担恢复责任、正常Close与最后阶段收据的先后竞态，以及原子保存后不能重复旧state恢复。审查保持actualGui/D10/D11/P6/human=false。

[host-only工具准备](../build/p6-d10-readonly-tools-20261121-1/result.json)含44份工具/运行时载荷及自身清单，清单SHA `7ff69bb7385b015a4a88d8aece63f50dfc9a781aa59b3a24284b6a01efb9a4e4`。七份已审脚本/模块与原官方CPython ZIP的37份运行时逐字节复制；实际embedded Python3.14.8以`-I -B`运行CLI help及版本检查exit0，payload/源前后不变。清单准备给原QA SID/session2，未来执行前仍须重新核真实QA会话和人工退出/after-state前置，不把宿主help当客体UI通过；未向客体部署本工具或改权限。

本轮收口[15:17Z只读观察](../build/p6-vm-install-20261119-1/human-review-export-20261121-151719-402/receipt.json) SHA `0f31b38b869b1e8b0e57a1e932c5b3993ddb5f132e9d9b2e2f2dcd2aa55178ae`仍确认原2516/wrapper8236及原创建时间、普通QA/session2，任务Running，无session-summary。仅复制7份已完成元数据、0错误、未读活DLL，未连续轮询。新D10执行前置仍不满足；当前继续等待用户真实体验后正常退出虚拟机内的Cordis应用、保持虚拟机运行，不能自动代填人工结论或强关窗口。

工具准备已独立归档为 `D:/WorkSpace/delivery/Cordis-P6-D10-readonly-tools-ready-20261121-1.zip`，12,519,851字节，SHA `90e46e62e92291aaf140c0dda8fc239b0bd3073a73375a4b7f2fba5be533c1cb`，113原件及10控制/索引文件共123条目，展开24,954,174字节。完整提取及主流程独立逐entry长度/SHA/目录集合均通过；payload严格44项+manifest自身，总24,608,141字节。外侧`.verification.json`与`.root-verification.json`保留完整核验；后补12/12工具闭包窄审另作`.bundle-review` sidecar，首次审查的字节计数口径误读单独保留，不改原包。归档actualGUI/D10/D11/P6/human均为false，只有工具准备与纯检查通过。

源码后续位置调整仅用新的`.source-placement` sidecar补充同字节映射、资产边界检查及当前开发记录，不覆盖已冻结归档。当前`tools/record-readonly-save.ps1`与归档内部owner逐字节相同；已核运行包素材105件均在原支持/许可范围，没有因QA工具引入新运行包素材。此为静态资产边界检查，不等于打包或资格原件缺口已解决。

连续第三轮goal审计的[15:32Z新鲜进程原件](../build/p6-vm-install-20261119-1/human-review-export-20261121-153237-854/receipt.json)确认原2516/8236及原创建时间、普通QA/session2仍运行，无product-exit/session-summary，0复制错误且未读活DLL。[阻塞审计](../build/p6-goal-blocked-audit-20261121-1.json)区分前两轮工具/证据实质进展与本轮verified wait；同一人工体验/正常退出前置已连续三轮未满足，独立审查也确认无必须继续新增的准备工作。完整P6目标受阻而非完成；不强关原产品、不伪造人工回执、不越过roadmap126/128的阶段门禁。用户体验后正常退出虚拟机内的Cordis应用、保留虚拟机运行并反馈结果后，再核真实after-state并继续D10及后续完整阶段。

## P6-0 架构盘点

现 `examples/qt_workspace/main.cpp` 与配置入口已分离应用目录和用户数据；业务入口的 `businessArtifacts()` 每次验证六 A/B。`ModuleManager`、嵌入名单、Session/Store、升级及关闭链保持唯一权威，打包没有理由修改生产源码或 SDK。当前配置入口持久化外观，业务入口 shell 未传偏好文件；按现行为写准，不承诺不存在的跨进程草稿/设置能力。

第一方源许可证未选，内部预览不替用户决策；Ela/LibYAML 上游 MIT 原件存在。Qt 5.15.2 kit 的运行库/插件存在，尚未找到 windeployqt；应以实际依赖固定闭包同字节复制，并补当前 Qt/渲染/图像/CRT 材料，不能拿另一个 Qt6 文档目录冒充 Qt5 材料。

已读 PE imports：两个入口、四 Cordis/Ela 库、三 Qt 库、六业务 DLL 均为 x64。插件候选为 qwindows、windowsvistastyle、qjpeg/qgif/qico；GL/ANGLE 动态路径仍须核实。至少需要 MSVCP140、MSVCP140_1、VCRUNTIME140、VCRUNTIME140_1，正式 VC143.CRT 候选实际版本 14.44.35211.0，与本机系统 14.50.35719.0 不同，列为 P6 新环境。品牌和 Ela 资源已嵌入 qrc，不复制松散图片；ElaAwesome 字体及两嵌入图片的来源/单列许可材料尚待整理，不提前称全部第三方材料齐备。

P6-0 当时记录本机注册表标为家庭版、构建 26300，未发现可用 WindowsSandbox/已配置虚拟机，当时选择用户提供的第二台机器，未启用系统功能或重启。此为历史环境记录，当前环境与虚拟机选择见上方调整记录。已查官方 Qt5 部署和 Microsoft CRT/Sandbox 文档；事实与来源链接列于 ADR，最终仍以实际文件/运行证明。

CI 只有现 presets/dev/desktop/公开资格工具，没有 workflows；P5 最终静态 Qt 目录用了显式 `CORDIS_SHARED_CORE=OFF`、关闭 Ela/示例，不能仅凭同名 preset 声称重现。ASan 无现成 preset。P6-4 需要薄编排和准确矩阵，不搬运 ignored 历史脚本当公共工具，也不更改业务/卸载语义。

P6-0 独立复核通过，无剩余架构阻断；六份文档 UTF-8/LF/末尾换行、369 个本地链接目标及 `git diff --check` 通过。此段只有文档，没有生产源码/SDK/批准/产物修改，无需重建或重跑 P5。下一动作：精确提交同步并核 clean，随后进入 P6-1 最小同字节包工具、实际闭包和许可材料。

## P6-1 实施前边界

新增单用途 `tools/package_preview.py`：显式消费 build/Qt/CRT 目录、P5 实物证据和受审源，校 profile/实际 PE 依赖及复制前后摘要，只在新输出目录生成包/清单/ZIP；失败不替换旧包，不调用构建器。PE 只读解析有真实包消费者和负例测试。运行包静态材料集中 `delivery/windows/`，不混入生产库或公开 SDK。

包清单 schema 为 `cordis.preview.package/1`；记录 `deliveryVersion`、`applicationVersion`、`scope`、`platform`、`profile`、`qualification`、`files`。每项文件有 `path`、`role`、`bytes`、`sha256` 和 `source`；PE 项另含实际导入及机器类型，来源路径用于追溯而非运行。`files` 排除清单及其旁置 `SHA256SUMS.txt`，后者覆盖所有载荷和 build-manifest，自身摘要由外部 ZIP 摘要承担。`verification/verify-package.ps1` 是无 Python 的独立 .NET/PowerShell 校验器，核路径、重复、完整文件集合、长度及 SHA；不读取外部源文件或提供批准。它与打包工具分别校验，便于在干净机器使用。

第一版既有受审产品/六 A/B/七依赖逐 SHA 锚定到 P5 原件；额外 Qt 插件、渲染/图像/CRT 和许可材料是明确新增部署文件，不能冒称 P5 全环境不变。原资格 raw bundle 与后续验收分别归档，运行包清单不成为第二批准源。包级行为、机器验收和更新恢复仍是后段出口，P6-1 不提前宣布这些通过。

## P6-1 实现与验证（审查通过）

交付冻结索引只引用四份原 P5 记录及摘要，不重列批准行。工具核原 939 项来源保持、21 件产品记录、配置入口、匹配四 Native/六 A/B、实际 CMake/Ninja/入口 /MD Release 及 PE 搜索闭包；复制前后核文件身份/时间/大小/SHA，归档前后核准确集合、元数据和逐项解压内容。源/目标/证据变化、混 profile、未解析依赖、路径逃逸、联接和重复均拒绝；新目录失败保留未完成标记，不替换旧包。已删除审查发现的全局 DLL basename 匹配及写后重新定义预期摘要的错误路径。

运行材料包含 Qt5 原 LGPL/GPL 及第三方说明、Ela/LibYAML MIT、官方 CRT 材料及准确 Qt5 源码获取方式，共 95 件材料和一个 sources 索引；逐文件集合/长度/SHA 已核。原文按 `.gitattributes` 保留原字节/空白；第一方工具仍执行规范检查。四项未决边界见[材料说明](../delivery/windows/licenses/README.txt)和[来源清单](../delivery/windows/licenses/sources.json)，内部预览不替代公开发行许可决定或来源安全认证。

实际第二次候选位于 `build/p6-1-preview-2/Cordis-0.1.5-preview.1-windows-x64/`，134 文件，ZIP 16,641,343 字节；[pack回执](../build/p6-1-preview-2/packaging.json)与[命令记录](../build/p6-1-records/pack-2.json)明确 runtime/cleanMachine 尚未通过。ZIP SHA `f57d830a0853133c2f567a9df4dfb2c6323ea2696e374141115be003840f8a5c`，manifest SHA `965e8c37704834216068672724086d6fdfa784864a8e435bd346dc6ca614af03`。第一候选 pack-1 原件保留；审查修正通用负例缺口后以 pack-2 重做，不将旧工具的成功当新版证明。

根工具最终 24/24 临时用例通过，实际原件见[独立命令与源码摘要](../build/p6-closeout-agent-packaging-units-final-1.json)及[原日志](../build/p6-closeout-agent-packaging-units-final-1.log)。PS5.1 校验器最终 9/9 通过并归档[命令/输入摘要](../build/p6-1-records/verifier-units-final.json)和[原日志](../build/p6-1-records/verifier-units-final.stdout)。同工具 SHA 的实际 pack-2 再使用包内复制的校验器、外部准确 manifest 锚点，独立验证 132 payload+2 清单通过，见[真实验包](../build/p6-1-records/verify-real-2.json)及[命令](../build/p6-1-records/verify-real-2-command.json)。临时 PE fixtures 不加载库，负例不改 P5 原件，不启动产品，不重建。

复现打包（开发机，Python 3.10+）：

```powershell
python tools/package_preview.py --qt-root "E:/Qt/5.15.2/msvc2019_64" --crt-root "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT" --delivery-version 0.1.5-preview.1 --output build/p6-preview-new
```

`--output` 必须尚不存在；非本机来源用显式 `--build-dir`、`--p5-root` 指向同一冻结实物及原证据。该入口不 freshbuild、不补批准，不以此宣称位级可重复构建。干净机器只需运行包内 PowerShell 校验器，不需要 Python 或原来源路径。

[独立整段审计](../build/p6-1-records/independent-stage1-audit.json) SHA `de5b21196cb3d3c834ae33f90d9754b031ff6c3159f4176c62b079afc597440a`，实际重核 ZIP/清单/源件/许可与 24/9 原日志，无代码或证据阻断。D02/D04/D05 通过；D03 仅静态部署闭包及材料部分，实际运行留 P6-2；D06–D16 未提前通过。生产代码、SDK、批准源及既有 DLL 均未修改，无需重建 P5。文档/精确 stage 检查和同步后才进入 P6-2。

提交前核 110 个精确文件，第一方编码/换行、Python AST、JSON、429 个文档本地目标及 staged diff 通过；许可原件 index 与工作区字节相同，PS 按既有 LF index/CRLF checkout 契约核对。未 stage 生产、SDK、qualification 或只读 sources 文件。

## P6-2 实施前设计

继续使用普通入口，不借测试 API 改 Workspace/Session 或预写 B 配置。运行包和数据的副本均位于新的专用验收目录；所有破坏性负例只作用于该副本。当前 token 实测 Medium、管理员组 deny-only，可验证本机普通权限，不创建用户/更改系统功能。本机不充当干净机器。

记录层与交互层分开：随包的 PowerShell5.1 记录器只启动本轮明确 exe、采 OS/token/环境、窗口/加载路径、独立 stdout/stderr、退出后文件和保存定位，两个入口各首次/重开共四进程串行；它不自动点击、关闭或杀进程，不因用户回执或最终 B 推断中间操作通过。有限共享 Windows 观察函数由实际记录器/负例消费者复用；宿主侧 OS-UI 驱动另用 Python/PS 操作本轮 PID，重找实际控件和前台/焦点，保存 Applied/dirty/Save、冲突/rebase 与草稿/贡献换代原件。用户操作与自动动作分别登记。

子进程仅保留所需 Windows 用户环境、系统 PATH，清理 Qt/QML/开发路径污染；不靠设置 QT_PLUGIN_PATH 掩盖 qt.conf。加载期间只观察真实路径/base/size，退出后校包/store字节。记录器有总时限和记录预算；超时/不可观察只写未完成并保留活进程，不据此重复启动或算正常关闭。业务首退未保存准确 A/A/B 即不重开；配置首退必须有 formatVersion1 原文件，同参数新 PID 后仍需用户按原语义应用。

配置 `--file` 只隔离 YAML；其偏好仍写当前用户 KnownFolder 的 CordisConfigWorkbench/ui-preferences.json。环境变量改名不能声称 Qt KnownFolder 已隔离：记录实际路径与前后摘要，允许该明确用户数据位置，不删除/覆盖用户原偏好，不与其他配置实例并行。业务入口 shell 无偏好文件。`--smoke` 会写应用目录截图且不接受 --file，不能用于只读/指定数据验收。

只读应用副本用真实 DACL 赋当前 token RX，保存原 SDDL 并以同 token 实际 create/write/delete 拒绝证明有效；退出后仅恢复本轮副本 ACL，不清理原件。不可写数据测试必须观察真实 Save 失败、旧配置保持与窗口/dirty保留，恢复 ACL 后再通过原 Save/Close，强杀不算成功。启动失败用例继承受控 ErrorMode 抑制系统错误框；超时仍未确认。缺件/篡改/声明摘要错配/ABI major2各留独立副本、准确退出及旧数据摘要。

干净机器不安装 Python，D07 的副本创建/登记/保护/探测/恢复也必须能通过随包 PowerShell 完成。ACL 工具只保留一个规范实现，放在 `verification/preview-acl.ps1`，用明确 Prepare 动作验外部 manifest 锚后创建新范围和应用/数据副本；开发机负例工具调用它创建登记，删除原 Python 的重复 ACL 登记实现，不保留第二份脚本。记录与日志仍放可写范围，不能把整个 DataRoot 设只读；脚本始终从原验证来源运行，只对其新建副本改 DACL，不更改原包或用户数据。

另发现配置入口使用 narrow argv/fromLocal8Bit，而业务入口使用 Qt Unicode arguments。当前系统 ACP936，只凭普通中文成功不能泛化其他字符/地区；先对有效扩展中文路径做真实参数验证。若确认宿主缺陷，先记录最小 Qt CLI 统一及新的宿主来源/产物证据方案，只修具体入口、复验并保持原 Native/七依赖；不能为了保持旧冻结脚本而绕过缺陷，也不能默许新字节使用旧宿主证据。

实际 PID18788 的原包 Unicode 探针已退出1，stderr 为 configuration read failed，输入文件未变。实施前修复与证据取舍见[ADR-0058](adr/0058-reviewed-configuration-host-fix.md)；只将配置入口统一到已有 Qt Unicode 参数机制，新宿主单独重验，P5 原件/6c 不回填。

新 app-local CRT 环境另以原已测 D1 exe 作私有 QA 组合验证：只把该测试 exe 放入独立的当前包运行库副本，不进入运行 ZIP；原测试继续原 8+100、完整 MEM_IMAGE/独占文件及零新增阈值，source 指当前六 A/B。测试不重建、不补批准，实际观察其 Core/Qt/Ela/CRT 加载路径以确认环境；此证据补强本次部署，不把组合目录或单一 D1 等同完整公开资格流程。

## P6-2 早期实测与失败历史（原件保留）

配置宿主已按 ADR-0058 修复，Release/Debug/ASan 仅重建该目标，各原 smoke 两例通过；57 项产品/模块/运行库前后实际 SHA 完全相同。扩展中文路径下，8516 完成实际 OS UI 编辑/检查/应用 R1/显式保存并 exit0，1432 同文件新进程读到标记并 exit0；三份保存原件同 SHA。历史观察器选择器、异步控件/暂态 UIA 错误保持未通过原件，在同8516续做，不把它们当应用缺陷或虚构重启。详见[实际聚合](../build/p6-2-host-fix/unicode-completion.json)和[采用前独立审查](../build/p6-2-host-fix/independent-host-audit.json)。受审[新宿主输入](../delivery/windows/verification/configuration-host.json)只绑定该源/exe，原P5与6c不改。

新 CRT 下原 D1 也已实际通过：PID7332，原 test exe `9a399b13…`，8+100、108个唯一操作、111代次、最终163及原零新增阈值保持；每轮实际断言完整 MEM_IMAGE 离开与独占文件，50份同PID样本同时见七DLL/四CRT/qwindows均来自QA副本，实际CRT14.44.35211.0。134项旧包载荷及QA副本前后逐SHA相同。独立[审计](../build/p6-2-physical/independent-audit.json) SHA `63bc3af2169faefb526ec3ecd30de03f9244ce1027e496b1a6ac770d82566cae`；此组合中旧Config未执行，不冒称新版正式包/完整27资格/产品UI或干净机器通过。

随包 stock PS ACL Prepare/Protect/Probe 在真实139件候选副本完成，普通 token 实际 create/write/delete 权限均拒；原包未改。首轮记录器启动16896后，旧 Toolhelp 路径把有效非BMP字符转成字面 `??`，PS5.1的严格GetFullPath因此正确拒绝。记录器保留未完成、未杀目标，随后[正常关闭原件](../build/p6-2-records/failed-recorder-product-normal-exit.json)确认16896 exit0，副本DACL实际恢复；这轮不能算桌面流程通过。

采集边界已用受控无GUI cmd 双路路径/代码点证明，按同PID/同模块句柄读取真实宽路径，保持原wrong/missing/四Native等门禁，不替换字符串或拼预期来源。原386a ASCII D1原件保留其范围。当时决定下一候选只采用独立复审及边界测试后的采集实现；此前候选与权限恢复原件保留，不用旧SHA冒新源码。当时完整四窗口流程、不可写保存与独立机器仍待验证，后续本机结果及当前D11缺口见下节；P6-3–5未开始。


五个独立部署故障已实际拒绝，见[故障原件](../build/p6-2-failures-1/report.json)（SHA `fd7f13af1de998d2652537df907940515b44aaf6bde998ebaa87d054eb718f3e`）：缺 Counter B、篡改 Counter B、manifest 摘要错配和 ABI 不兼容均 exit1，缺 Core 实际退出 `0xC0000135`；各副本配置/store 前后逐件相同，原 P5 已接受 A/A/B 数据不变。该批使用第三候选，未执行其中录制器；后续版本须逐件核业务入口/七库/六 A/B/CRT 相同后才可关联此范围，不将旧整包摘要冒作新包实测。

第三候选的配置 PID9460 已由真实 UI 完成保存/冲突/采用基线，但记录器因 stock PS5.1 把 `File.Replace` 的普通 null 绑定成空备份路径而提前失败。UI 关闭请求后进程已消失，但原观察句柄已释放，退出码未知；保留未完成，不能以 UI 驱动 exit0 推断产品 exit0。实际 BMP/非BMP八组绑定诊断定位后，状态写入改为 typed null，并只清理本次 CreateNew 成功拥有的临时文件；已有临时文件不删除，原错误与二次状态错误分开保留。独立复审与四项实际临时故障测试通过，原失败记录不改写。

记录器的资源拥有者还须在报告/日志写失败时无条件释放观察句柄并保留原始错误；状态文件仅用于进度，最终完成只认最终 recording 原件与记录器实际 exit0。两份文件不构造虚假的共同原子提交。桌面 UI 驱动使用明确父菜单和实际 UIA 作用域；Ela 搜索的自绘候选可无 accessible Name，只有精确查询、焦点及对应弹出列表的唯一实际子项才允许 Enter，进入后继续核 Notebook 编辑器和命令所属代次，不修改冻结的 Ela/产品 DLL 来迁就验证器。


实测又发现 Windows 路径分类说明采用区分大小写的相对字符串比较，导致 Win32 返回的大写 CRT 文件名被标作 unlisted-package-path；关键十二件运行库门禁原本就用不区分大小写的完整路径核对，未漏掉 CRT。本次只将位置分类统一为既有 Windows OrdinalIgnoreCase 语义，保留所有关键 missing/wrong 检查；用同份实际模块快照和临时大小写/越界负例复核。此为观测说明修正，不改模块装载、批准、路径来源或冻结运行库。


## P6-2 本机验证与跨机交接

以下为2026-11-19本机交接时的历史记录；所列部分旧build原件（包括打包/UI/录制器单测及独立本机审计报告）尚未在当前镜像找回，不作为本轮客体技术通过证据。本轮客体原件及当前待办以本文“当前状态”和“D11本轮技术结果与人工待验”为准；保留历史摘要不表示旧链接可用或历史原件已恢复。

本机实际完整四流程使用第五候选，ZIP `eb13fde7…`、manifest `27a06e46…`，stock PS5.1先解压、移动安装目录后，再通过原ACL工具建立有效RX副本。四个产品进程 `17416 / 7176 / 19060 / 8216` 与记录器 `6076` 均实际exit0；[完整控制记录](../build/p6-2-records/desktop-controller-5.json)持独立句柄交叉确认，产品原件在[记录目录](../build/p6-2-只读验收5𠮷/桌面记录%20中文𠮷/recording.json)。配置V1→V2、外部冲突→采用基线→显式保存与同路径重开，以及五步升级各Applied/未保存/Written、Notebook草稿/新命令代次/按钮与命令输出、A/A/B重开均由真实UI记录；无测试API设置业务状态，也无强杀。本轮4次UI驱动均一次完成，未借用此前第四候选的续做结论。此前候选的失败历史、第四候选的三次UI选择器Unknown与显式同PID续做原件保留；第五候选无续做。

不可写数据用独立同字节宿主PID11852：有效DACL探针返回创建/写入/DELETE权限拒绝5；R1应用后Save实际失败，原磁盘、编辑目标与修订保持。原登记来源恢复DACL后，同一R1显式Save成功、正常关闭且Popen实际exit0；seed、宿主、原139件包与helper保持。见[实际报告](../build/p6-2-readonly-data-evidence-1/report.json)与[独立审计](../build/p6-2-readonly-data-evidence-1/independent-audit.json)，审计SHA `f5030db47f1f536f04a313325f301ce7d4b65c19ebd614213811a14e5bf30dbc`。UI的保存错误优先显示而遮住dirty文案，记录直接的保存前未保存、保存后旧值/目标/修订/活进程及同修订恢复保存；不假称采到内部布尔。测试副本的应用/数据DACL均已恢复，不更改用户原偏好或旧P5数据。

最终交接第六候选只修正上述CRT大小写分类说明，运行门禁与生命周期不变；与第五候选仅录制脚本和派生manifest/SHA两件不同，32件运行产品/模块/声明逐SHA相同。没有重建或替换运行库，亦不将第五候选整包SHA冒作第六候选完整实跑。实际19份完整加载快照以最终函数静态重放，分类改正且原verified/missing/wrong保持；另7份启动未齐样本明确不计通过。见[重放原件](../build/p6-command-recorder-case-label-replay-1.json)、[最终字节关联](../build/p6-2-records/runtime-association-6.json)。原D1的30件运行库/模块/声明、五启动负例的业务输入也逐SHA关联，证据各保原适用范围。第六候选已另用stock PS5.1真实解压/搬迁及独立验包通过，见[验包](../build/p6-2-解压搬迁6𠮷/verification.json)。

当前交接物位于[交接目录](../build/p6-2-clean-machine-handoff-final/)，包版本`0.1.5-preview.2`，139文件、ZIP 16,667,548字节，SHA `d236f5965bccf124eabde8fa0f1539fd910e2eff33a432affbea0174d18c7168`；manifest SHA `06a5bbbf766f88a1872b452f7777a468ad5afc4db83855009ec70b7af154a3df`。包外[验收说明](../build/p6-2-clean-machine-handoff-final/验收说明.txt)SHA `d740bd0415900d129286de0bd8708fd776ade3a8aee6ca59471e2df4c8a8a522`，给出普通用户无需Python的三段PS命令、四窗口顺序、配置冲突与模块候选选择、权限恢复及完整回传目录。`handoff.json`仍明确cleanMachineVerified=false/P6Stage2Complete=false；不在说明或清单中宣称人工验收已经发生。

工具验证：当前打包34/34通过，[源码/命令/日志绑定](../build/p6-2-records/package-current-units-1.json)；最终UI驱动15/15纯用例通过，见[当前原件](../build/p6-2-preview-ui-output-scope-units-1.json)。ACL19例针对同代码的编码前版本，最终stock PS5.1 UTF8-BOM/CRLF helper另实际Prepare→Protect→Probe→Restore通过，保留原范围而非冒称19例在新SHA重跑。桌面记录器最终PS用例与阶段独立审计见下方补充记录。P6-2独立干净环境的当前包四进程、加载/保存/退出原件及新人工回执仍缺；只提交本机/交接子段，不将本段提交当P6-2或P6完成。D12更新恢复、D14分层CI、D15独立原件归档、D16最终交付均未实施。


最终录制器`99d1aab5…`及当前三份PS/测试源before=after，stock PS5.1完整14/14通过，[命令/摘要](../build/p6-2-records/desktop-final-units-1.json)及对应原stdout/stderr保留。第五候选[独立桌面审计](../build/p6-2-records/independent-desktop5-audit.json)SHA `b861924c206380a217fd926dd6c6fb34fc626826d347e7309bfcd482d355e5f7`，201项检查及277份原件SHA通过；第四候选审计另存历史，最终本机结论采用第五候选的无续做四流程。配置host、打包新例外与九项资产另经非作者交叉审；旧938源与6c未改。最终大小写修正只变分类表达式，逆替后整文件精确回到已完整实跑的`8e9db86c…`；没有因此重复无关P5矩阵或重建DLL。后续首次独立干净环境验收使用第六候选准确摘要，完成后另审D11并收口P6-2。
