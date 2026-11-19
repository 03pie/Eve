# P5 正式证据索引

日期：2026-11-19。实现源码基线为 `fdf099c6b585a8db142b7e3d57f5e5d361bf4876`（M5-e2已审计提交同步）。e2自动化与产物审计、本次e3自动GUI升级/保存/新进程重开、最后B笔记人工体验及两次正常退出已闭环通过，支持范围见下文。原两轮回执与A快照不一致的原件保留。最终文档审查、精确stage和远端同步随本次收口完成；e3属于后续文档提交，不把本次人工记录追记为fdf099c已包含的证据，也不改变已测实现基线。

本文件把最终源码、已资格字节和验收原件关联起来。产品准入仍只消费[受审名单源](../qualification/native-business.json)，遵循[ADR-0056](adr/0056-qualified-native-product-catalog.md)；本索引不参与CMake、批准或运行时装配。阶段职责、取舍和原始错误见[M5-e工作单](p5-m5e-development.md)，21项要求映射见[最终验收表](../tests/acceptance/p5-replacement.md#m5-e2-最终五配置全量与需求映射)。

## 源码、名单与Release产品

最终构建及公开资格共用939项源码输入，快照SHA-256为 `414087f615295820bf4e890a2a58a6edafb925c072b9ef326ff02b639833be7f`，见[final源码清单](../build/p5-m5e2-final/source-inputs.json)与[capture源码清单](../build/p5-m5e2-final-public-capture/source-inputs.json)。文档后续收口不改变这些已测编译输入。

受审源SHA-256为 `6c0c5324e0e064667c79ae10ff9cf2d48754cbffd49f376d338d4e8cd91786ec`。它包含三个独立profile，每份恰四条Native及七项实际依赖；产品只嵌入匹配本构建的四条，不并集。普通构建、用户所选manifest和本次新证据均不能批准新字节；实际字节改变须按[资格指南](native-qualification.md)重新资格及审核。

本次桌面入口固定为 `build/ela-msvc-release/bin/cordis_platform_workbench_demo.exe`，MSVC 19.44.35222.0/x64/MD Release、Qt 5.15.2、SDK0.1.5。首轮人工取证的[run-1启动前21件摘要](../build/p5-m5e3-native/run-1-before.json)记录产品SHA-256 `fbece70b8583e006be7199826909b96a574fdc98de97a4cd98cb05248c444f77`，同时绑定相同6c源、下列四Native/manifest、Qt A/B/manifest及七项依赖。[run-1退出后](../build/p5-m5e3-native/run-1-after.json)、[run-2启动前](../build/p5-m5e3-native/run-2-before.json)和[run-2退出后](../build/p5-m5e3-native/run-2-after.json)的21项映射与首份完全相等，四份JSON原件SHA均为 `5c2778b46ff413b772933616f2330c9f130232d4b772d321689ddc76ba7b7c7f`；运行与保存范围见下段。

Release Native完整身份如下，均为c-abi/explicit。实际相对library路径、manifest SHA、producer源码/命令和依赖完整SHA见[6c源的ela-msvc-release profile](../qualification/native-business.json)及[最终产物原清单](../build/p5-m5e2-final-artifacts.json)的normal_artifacts，不另维护准入表。

| 产物 | moduleId / pluginId | 版本 | DLL SHA-256 |
|---|---|---|---|
| Counter A | org.cordis.example.counter / example.counter | 1.1.0 | a80db76f9112f5890959b02751ef4062a69c6c75a5179acbeb0a0246020cc9fa |
| Counter B | org.cordis.example.counter / example.counter | 1.2.0 | 41c334eaa710945d219639b3aace40b79028d3c9557ee423f921736733cd056c |
| Reporter A | org.cordis.example.reporter / example.reporter | 1.1.0 | 86d58a1c94cf28c31eefde1417d4f5d398a2ba9dcb88256455948ff5f8565f0c |
| Reporter B | org.cordis.example.reporter / example.reporter | 1.2.0 | fde6f56659f8e55274261692dbdc434d9c23b70bba1c45fb6d67f140b3b93f3b |

同一Release环境的Qt Notebook为 `org.cordis.example.notebook` / `example.notebook`：A/1.1.0 SHA `82c124424a5cadfb7bdbfc973fd286db2ba80095b11c54e83c8f6b1f9fb48daf`，B/1.2.0 SHA `6bd37afe627bab4c5c072b7b4213f7ab378b8459d676337fd9cf75756754b5c1`。两份Qt产物固定resident，不列入Native批准；路径、manifest及metadata/moc摘要同见最终产物清单。

Release七项依赖是实际 `cordis_core`、`cordis_qt`、`cordis_ui`、`ElaWidgetTools`、Qt5Core/Gui/Widgets。6c profile的dependencies保存完整路径/SHA，[最终产物清单](../build/p5-m5e2-final-artifacts.json)的qualified_current_environment逐项记录相同实际字节。外部Qt为预编译库，不宣称已插ASan。

## 最终验证入口

五个原目录均重新默认全目标构建并完整串行执行；不是将专项通过数相加。原构建、真实清单、运行返回码/日志/LastTest和实际主程序/显式wrapper叶程序摘要分别保存在[final目录](../build/p5-m5e2-final/)，汇总与逐要求原断言见[最终独立矩阵审计](../build/p5-m5e2-final/independent-runtime-audit-final.json)。

| 配置目录 | 完整通过 | P5成员 | 支持范围 |
|---|---:|---:|---|
| ela-msvc-release | 1481/1481 | 765 | MD Release，共享Core/Qt/Ela |
| ela-msvc-debug | 1481/1481 | 765 | MDd Debug，匹配Debug业务产物 |
| p3-q3-asan | 1481/1481 | 765 | MD RelWithDebInfo，实际/O1与/fsanitize=address |
| qt-msvc-release | 1002/1002 | 491 | 静态Core，Qt开启，Ela/示例关闭 |
| core-gcc-debug | 459/459 | 198 | GCC Debug，静态Core，无Qt |

三共享配置均含本次D1原8+100成功/108唯一Operation/111代次/最终163、两个seed及三个fresh-child proof和六项产品检查；静态/无Qt只按自身实际用例范围成立。18件固定输出在公开重取前另存[fixed-runtime-evidence](../build/p5-m5e2-final/fixed-runtime-evidence/provenance.json)，proof使用各次原stdout所列独立run目录。

最终189个实际ASan对象、372项仓库依赖、75个目标及链接拥有者见[对象/依赖清单](../build/p5-m5e2-final-asan-objects.json)，含六生产目录92对象。Normal36条DLL/manifest配对共72文件、12项Qt metadata/moc、Core、SDK64头、原SDK0.1.5八件及provenance见[产物清单](../build/p5-m5e2-final-artifacts.json)。公开capture后[只读比较](../build/p5-m5e2-post-capture-comparison.json)确认对象/依赖/目标与整个产物报告保持；Ninja日志仅追加206字节两条verify_globs，对应真实metadata refresh，不能称整个Ninja日志未变。

公开工具在[final capture](../build/p5-m5e2-final-public-capture/qualification.json)实际各跑27/27，共607件归档/939项源码；qualify和export均exit0，原参数/退出/日志摘要见[运行记录](../build/p5-m5e2-final-public-cli-run.json)及[六键export记录](../build/p5-m5e2-final-current-export.json)。[新候选](../build/p5-m5e2-final-business-candidate.json)的三个完整profile（共12条Native、各七依赖）严格等于6c源，仅provenance新；未再次采用，也未为证据刷新重编产品。名单及候选的pendingStages保留各自取证时点，本索引关联后续e2/e3完成证据，不回填批准源。逐原件深审见[v2实际审计](../build/p5-m5e2-final-public-capture-audit-final-v2.json)和[独立交叉复审](../build/p5-m5e2-final-public-capture-qualification-independent-review.json)，不将这27项累加到全量。

## ABI、失败与解释边界

C/C++布局、旧SDK拒绝及M1–M5接口要求的实际用例在上述[21项映射](../tests/acceptance/p5-replacement.md#m5-e2-最终五配置全量与需求映射)列名。原SDK0.1.5实物另有本轮[current-host+old-plugin及old-host+old-plugin记录](../build/p5-m5e2-sdk015-runtime/verified.json)，实际PID16820/13352、两次normal均exit0，同PID Toolhelp见证当前Core/Ui及原旧Notebook路径/SHA；[独立审查](../build/p5-m5e2-sdk015-runtime-independent-audit.json)与[第二审查](../build/p5-m5e2-sdk015-runtime-command-independent-audit.json)分别保存。范围仅同MSVC19.44.35222.0/x64/MD Release这两件实物场景，不泛化跨工具链C++ ABI或新增API行为。

首轮Release的1480/1481、exit8及D1 +16句柄失败13件原件继续保留于[失败provenance](../build/p5-m5e2-release-failure-original/provenance.json)。Qt public QMutex慢路径进程缓存机制与D1准备顺序缺口有独立证据，修正只在私有prepare；原8+100、比较式和零新增容差未改。旧失败具体16个Event的ownership仍未知，诊断108成功不计资格，不将后续成功倒推归因。详细选择见[工作单](p5-m5e-development.md#e2-首轮release资源门禁与诊断边界)。

历史LF专项审计器把采用前3cf错当当前名单的首轮exit1也[保留原记录](../build/p5-m5e2-final-public-audit-first-failure.json)。最终v2按原历史归档及e1采用链核验；当前6c、939源、607归档和完整profile合同保持，未改公开工具或批准源。

## e3桌面证据与最后人工体验（通过）

使用同一最终Release产品，按[桌面验收步骤](p5-desktop-verification.md)与[工作单](p5-m5e-development.md)分别登记原人工回执、实际保存/退出原件和本次自动GUI动作，不互相替代。

### 保留的回执与A快照

首轮 `build/p5-m5e3-native` 的[report](../build/p5-m5e3-native/report.json) SHA `b50ba95d1bf8d71fdfc1296b80415f474e11cea02d095e087ea20980e919dd79`记录PID8964/2868各exit0；[run-1](../build/p5-m5e3-native/run-1-session.yaml)和[run-2保存快照](../build/p5-m5e3-native/run-2-session.yaml)同SHA `6ab8bed212051a0329fdb671ef9b4bc9818de18c9500630d1f496310cf3321aa`，实际为Counter A/Reporter A/Notebook A。用户[原回执](../build/p5-m5e3-native/human-receipt.json)为“已完成，全部通过（两次均正常关闭）”，[澄清](../build/p5-m5e3-native/human-receipt-clarification-1.json)为“看到 Applied 且已保存，重开也显示 B”；原件与观察不一致，原因仍未知，不归因于漏步骤或产品缺陷，也不据此推断Native往返未发生。

随后 `build/p5-m5e3-notebook-recheck` 的[聚焦报告](../build/p5-m5e3-notebook-recheck/report.json)只记录PID15504一次exit0；退出后仍A，B定位guard停止，`recording_completed=false`，没有启动第二进程。[该轮用户回复](../build/p5-m5e3-notebook-recheck/human-receipt.json)虽称重开1.2.0/两次关闭，不能把它写成两个实际进程已通过。两目录的原recording-only标志、用户原话及A快照均保持，不用后来的成功覆盖它们。

首轮[窗口观察](../build/p5-m5e3-native/desktop-observation-1.json)的UTC 2026-11-19T00:08:10.587736+00:00及空退出码只属PID8964当时仍运行的历史状态；窗口标题“Cordis 插件平台”和DPI120只证明该窗口范围，不代替升级或系统缩放验收。

### 本次自动UI取证

在新独立 `build/p5-m5e3-ui-repro` 目录，root通过Windows现有UIAutomation及实际DPI-aware鼠标/键盘操作原产品菜单、Notebook行、候选输入、升级与Save，不调用内部Workspace替代界面，不直接写入B定位，不改生产源码/批准值或重编。此段是自动GUI动作证据，不冒称用户执行了这些动作。

PID17032的[Applied/未保存UI快照](../build/p5-m5e3-ui-repro/uia-upgraded-unsaved.json)记录Notebook OperationId5、committed=1、Current1.2.0及目标修订2未保存；[Save快照](../build/p5-m5e3-ui-repro/uia-saved.json)另记录Written/已保存修订2。随后[run-1实际记录](../build/p5-m5e3-ui-repro/run-1.json)为exit0，[退出后保存快照](../build/p5-m5e3-ui-repro/run-1-session.yaml) SHA `8f2d745d6cc2a229ff36a34e066698104233af155219b24e07a243b611273ce0`，实际Notebook locator为受审B的 `6bd37afe627bab4c5c072b7b4213f7ab378b8459d676337fd9cf75756754b5c1/module.json`。

同文件重开的新PID16824由[实际UI Grid](../build/p5-m5e3-ui-repro/uia-reopened-grid.json)读到Counter1.1.0/Reporter1.1.0/Notebook1.2.0。该进程随后又通过原UI完成Counter B、Reporter B、Counter A、Reporter A，四次OperationId分别5/9/11/15，均Applied/committed且各自显式Save Written，保存修订2/3/4/5；逐操作原回执见[Native UI往返](../build/p5-m5e3-ui-repro/native-ui-roundtrip.json)，原件SHA `6adbffc6e74785d14700e896791144fb606741af617a5838f494f8cf84451e57`。[最终UI Grid](../build/p5-m5e3-ui-repro/uia-final-grid.json)仍为A/A/B，Qt显示进程驻留。

用户在[本次最后体验原回执](../build/p5-m5e3-ui-repro/human-receipt.json)明确回复“已体验，通过，并已关闭当前窗口”，范围是已重开Notebook B的笔记编辑、页面输出按钮与Ctrl+Shift+P输出笔记命令；回执SHA为 `c6a24029d5a24d0fc83fca0fbf22175d75d65acc36945515d6a02d6ef294eb1e`。Notebook升级/Save及四次Native往返是上述自动GUI操作，不写成人工重新执行了这些动作。

本轮[最终report](../build/p5-m5e3-ui-repro/report.json) SHA `48b76106af20f13e2c363cfc3b6490104398dcb5ee79b32400c2711205ce8b51`保留recording-only标志且 `recording_completed=true`：PID17032和16824实际各exit0。两份退出后保存快照及最终session同SHA `8f2d745d6cc2a229ff36a34e066698104233af155219b24e07a243b611273ce0`，精确为Counter A/Reporter A/Notebook B；[run-2保存原件](../build/p5-m5e3-ui-repro/run-2-session.yaml)与第一份相等。四份21项[启动前](../build/p5-m5e3-ui-repro/run-1-before.json)/[退出后](../build/p5-m5e3-ui-repro/run-2-after.json)摘要映射全部相同，JSON原件均为上述 `5c2778b4…`，产品/6c/Native/Qt/依赖保持；运行中没有据此读取写入中的配置或模块文件。

自动化中的业务数值、完整MEM_IMAGE离开和文件独占仍由原product/D1证据承担，不把UI回收文字单独扩大为这些物理判断，不把运行态草稿称作跨进程持久化。本次独立成功链补齐Notebook UI→显式保存→新PID重开及最终体验/正常退出，支持本页所列M5/P5验收闭环；原A矛盾原因仍未知，未宣称已定位或修复，旧原件不改写为成功。整P5架构、需求映射与支持边界以工作单、验收表及本次最终文档审查共同归档。

最终[独立桌面审计](../build/p5-m5e3-ui-repro-command-final-audit.json)核137份原件、全部实际UI动作、回执/两进程/21件及store六组，无剩余阻断。[最终实物比较](../build/p5-m5e3-final-physical-comparison.json)核189对象/372依赖/75目标和整个ASan/query报告与post-capture相同；artifacts仅历史base_commit由1188变为已提交实现fdf，其余相同。原whole-report check因此元数据差异退出1的[记录](../build/p5-m5e3-post-check-history.json)保持，不重写旧数据或放宽任何实物判据。

## 原件摘要锚点

以下SHA-256来自已保存JSON原件；每个原件内部继续列出它所引用的实际文件摘要，不复制长历史产物表。

| 原件 | SHA-256 |
|---|---|
| final-artifacts.json | 749a4d0b84fb4e7e00d9d9d20f82886770f5cf1e45899a4354bdff547dd265b1 |
| final/independent-runtime-audit-final.json | c32633af91f51ddeadf86eead825d58eb8c813783046c9872d55322285fd232b |
| final-public-capture/qualification.json | cf069ad38a71d30308697d13e5caf792ebee2cf0220dc04feac63f4c1b013198 |
| final-business-candidate.json | 3e4b5af09fd3b8bca1dd75332166196bf65db0b3737e601d60f9eb6361012cbc |
| final-public-capture-audit-final-v2.json | 9fdbb273c3b9bbe63a33db4f5b9b9c42ae789f604537835f171fe236bee89cf1 |
| sdk015-runtime/verified.json | bf2ef4e30c3dcc82418ede54391a1154a0422b83af437772b6a87d2937bfa07f |
| release-failure-original/provenance.json | e0bd59d0813e480b1564077c9b11a7b1305bebfc2c6e6041056ab49378c16dd1 |

表中原件均位于 `build/p5-m5e2-` 前缀下；具体可点击路径在正文。`build/`取证原件本地保留并应随交付独立保存，不由源码提交自动携带；索引保留摘要以核对所持原件。
