# P5-M5-b4-2 实际业务替换接线

日期：2026-11-18。基线 2036670，b4-1 已审计提交同步且工作区干净。本段架构、实现、独立审查和当前风险相称验证已完成，随收口提交同步后才进入 M5-c。

## 问题与边界

基线 2036670 已能在同一 Workspace 运行三模块，但没有实际候选选择、整体操作及旧 Native 回收。Loader 当时没有只改模块 locator 的值变换；管理器固定旧 RuntimeId，在换代后会误露手动动作，Held 候选也会显示随后被 Core 拒绝的操作。复用原目录、安装、事务和模型门禁解决这些具体缺口，不增加第二执行器、插件 RPC 桥、IReplacementOwner 或卸载策略框架。

本段交付宿主值预览/执行 API 与真实业务有限场景；管理升级按钮、完整 H01/S09、100 次/随机、最终生产名单和新增原生桌面验收仍归 M5-c/d/e。新建/已保存 B 的装配不计为代码替换。

## locator 的唯一值编辑

在原 yaml.hpp/yaml.cpp 增加 setModuleManifest(source, alias, manifest, limits)，只替换既有非 builtin alias 的 manifest 字符串。沿原 readTree→string Node（非 plain 标量）→encode→parseConfiguration，不经 Qt/浮点 DOM、不 IO、不直接接受 Session 目标；其余字段、数字原文、配置负载、子树、域、inject 和 disabled 保留。原注释/排版本来不保留，不把规范化称为保留注释。

它是宿主实际事务调用的自由函数，不改变 ParsedConfiguration/其他公开类布局、vtable、既有符号、业务 C 表或 Qt IID。SDK 维持 0.1.5；已在任何本段改动前保存真实 .5 legacy Notebook DLL，SHA256 4a16e0bfc6792f4574e881c4369a1c87da893cb96bcef4e6173a57c21e00cdcf，位置 build/p5-m5b42-sdk015-legacy，后续验证其在当前宿主 normal 运行，不用改元数据的新版替代。

## 管理保护的权威与重入

Kernel 注册名唯一、无 rename/remove/name 复用 API；原 Core prepareCommit 固定 name→旧 RuntimeId，commitPrepared 在无回调窗口原子交换同名槽。attach 时把已验证初始 IDs 对应的名字固定，后续从同一次 KernelSnapshot.names 临时派生当前保护集合。保留最初 IDs 仅兼容已有 stopped 缓存，不累加中间代 ID、不重 attach、不从配置插件名猜 Core 注册名。

capture、动作接纳、排队执行和 Home 兜底共用同一解析/门禁；优先提取必要的宿主私有值策略，移除散布的固定集合判断。一次 capture 只解析一次当前集合；原 dependencyGraph.publicationHeld 同次提供候选输入门禁，不建 candidate 表，不把所有未命名 Runtime 都当配置。Held 可保留只读诊断，不能提供手动 Stop/Retry/Load。异常/缺固定槽失败关门，Home 值投影读取失败时禁止贡献兜底。

配置 provider.snapshot 是可重入外部回调，不能先取旧 Core 权限后调用它再使用旧值。先完成 provider 读取，再取同一次当前 Core/graph/保护集合；attach 回调后重查开放状态、当前槽和待执行手动请求，避免在回调期间换代/关闭/排队请求后继续接管。不新增 UI→Loader 依赖或公开 snapshot 字段/vtable。

交叉审查发现 Home 的逐贡献 hide 回调在 Workbench entries 遍历中重新读取 provider；同步升级/关闭会使该容器迭代器失效。修正为一次宿主私有 Home 值投影：同一次 Authority 生成诊断卡片输入、隐藏实例集合和 fallback 接纳标记，Workbench 在回调完成并复查开放状态后才遍历贡献，仅查询值集合。公开 DiagnosticsSnapshot 保持原布局；原逐项回调及独立 unpublished 查询没有其它调用者，删除被替代路径，不为旧遍历叠加第二套可重入权限机制。普通未绑定 Kernel 的 page-only 外壳仍可显示其合法贡献；绑定配置但读取失败则关闭 fallback，实际动作仍重新读取同一权威来源。

## 宿主预览与执行接口

Workspace 增加纯 UpgradePreview 和升级报告/进度值接口。previewUpgrade(alias, sourceRoot, manifestPath) 只读当前代/实际注册名、Session 当前修订、闭包与候选文件/Qt 元数据；不复制安装、不映射/创建工厂、不占用/停止旧业务。预览只含身份/版本/hash/路径/修订/影响值，无 Runtime、NativeModule、安装凭据或插件闭包。三个 alias 固定对应原业务身份/单 PluginId/backend，不能切换成另一插件。Native explicit 仅沿已冻结四资格身份/摘要的临时名单；resident/Qt 仍沿原准入，不把任意选择自动批准为 explicit。

upgrade(preview) 在同一 owner 重新检查开放状态、单个执行门禁、当前代/Runtime、目标修订及原文件完整身份/hash/路径；陈旧或同产物请求在破坏旧业务前拒绝。只有实际准备/执行拥有者持候选凭据和代码能力，不把它们藏进预览/历史。现固定产物资格来源校验与装配复用必要的具体 helper，避免复制身份/版本/临时名单逻辑；每次装载仍正常重新固定实际文件，旧预检值不是永久授权。

当前 generation/counter/reporter/notebookRuntime 查询须反映真实共同提交，即使正在通知或任务回执已经失败。它们改为 owner 的 Result 查询，先沿原 operation.snapshot、目录角色和 Driver 来源核对/投影，再返回实际记录/能力；旧的直接读缓存 getter 路径删除并更新全部示例测试调用者，不为私有示例 API 留重复包装。只读 getter 不执行插件、关闭/卸载或补建声明；投影只更换当前记录指针，原执行仍强持两代，不能靠异步最终回调才修正已对外可见的 current。

## 唯一整体事务及结果投影

准备流程为原 installModuleArtifact→stage/stageQtModule→真实 Native/Qt Runtime→当前 Driver.configurationDefinitions().replacing→setModuleManifest/目标校验→ReplacementOperation.begin。Request 真实共享 scheduler/Kernel/manager/Session/Driver，pluginRoot 为同一安装 store，definitions 只含本次一个模块的 old RuntimeId→候选 Runtime，configurations 使用当前原目录/目标与修订。显式配置根由原 Driver/default Builder 重建；不手动 freeze/stage实例/commit 或伪造拥有者协议。

counter 轮由原闭包重建 reporter，checkpoint=N 先于旧 reporter.cleanup 的递增，候选导入 N 后实际 reporter.start 得 N+m；reporter 轮借用活跃 counter，cleanup+start 通常 +2。两轮 Notebook 实例/编辑器/草稿保持；Notebook 轮才完整销毁并重建页/命令、导入运行草稿，Native 服务保持。Qt 保持 resident，不复活 Retired lineage，不把循环加载 Qt 计为 100 次。

每次操作只有一个实际执行拥有者，持 previous/candidate 稳定记录、临时请求、原 Operation 与当前所需凭据。report.committed/recovered 及真实目录角色/Driver 来源决定 current；Failed/resourcesRetained 不等于未提交，settled Task 错误也不能默认 committed=false。通知失败时真实 claims 可能已经释放，只保留原执行和真正剩余资源，不补造占用、不重跑提交/恢复。跨线程回执拒绝只标记原子诊断/结算 Promise；后续 owner 查询重验原源，不能跨线程触碰 UI/目录或留旧 current。

正常事务终结后清除本执行的旧目标/定义/Request/Scope 临时强引用，再关闭目标旧 Native 会话、释放本宿主旧 Runtime/记录/安装凭据，跨 owner FIFO 检查实际 RuntimeOrigin、ledger、预约/readers 后显式 tryUnload。Applied、配置/角色提交与后续 unloadFailure 分别报告，物理失败不回滚；外部借用/正常策略拒绝由真实持有者/目录负责，不为历史 pin 整个平台。Qt 仅业务资源退出，root/映像照常驻留。begin 失败或回执未知时先核实际占用/角色，未确认保留原执行/候选，不假定准备已自动撤销。

历史有界且仅含纯报告/身份/错误/阶段；不保存 Runtime、回调、Widget、NativeModule、InstalledArtifact 或执行对象。真实不静止/RecoveryFailed/通知或回执故障保留单个原执行，阻止新升级；不清 PIN、不解除别人的 claims。目录 64 重代次/128 纯卸载历史预算不变。

Workspace 报告的 retired 字段仅指本次退役/撤销候选的清理目标，并不伪造目录角色。原 Operation 恢复提交保留 old Current、未提交 candidate Candidate；正常换代才把 old 置 Retired。恢复用例按原角色断言，Qt 候选的真实 root 继续驻留。

关闭先关升级接纳，取消并等待原在途操作安全终结/恢复再沿 b4-1 保存/Session/Kernel/模块/UI 链；未知原回执和保留资源不能被强行清空。UI 字体/布局或通用停止开关不承担代码替换职责。

## 有限实际验证

使用原单一 Workspace 和未加测试导出的实际 DLL：counter A→B 后 reporter 真重建并继续计数、reporter A→B 借活 counter、Notebook A→B 当前编辑草稿导入和旧控件/命令完整退出；反向 Native 可另做有限一次真实回切。每次核原 OperationId/report、角色、注册名、Driver/Session/Store locator 共同提交及单独 Native 回收，不只看版本标签。模块名保护、Held 手动拒绝、过期预览/源改变/修订冲突、重复请求和关闭在途另用真实 API 验证。

恢复使用同一 Notebook 业务源、相同状态 IID 编译的 tests-only 1.3.0 resident 故障产物：候选 activate 走原 failStart 失败分支，正常 A/B 和旧配置均不改。原 Operation 在真实停止、捕获和候选导入后恢复 old Current/原 locator 和业务，不能拿预检拒绝当恢复，也不为制造失败增加业务 setter 或抢 Session 排队时序。Notebook 状态从 checkpoint 恢复，旧 widget 已删不复活。故障产物不进入默认三模块或 Native explicit 名单。通知/已接受回执被真实 Qt 丢弃另隔离进程，核真实 committed 与已释放 claims，不清 pin 后继续。不是完整 H01/S09 或随机/100 次。

已保存会话的 locator 按同一 reviewBusinessArtifact 身份/backend/单 plugin/Qt 元数据和原目录策略准入；移除只接受六个 A/B 摘要的旧启动检查。否则正常接纳的其它 resident/Qt 候选升级后会保存成功、下次启动却拒绝。新会话默认三 A、启动冻结四个 Native explicit 摘要不变，未给其它 Native explicit 版本自动授权。

测试装配仅在出现第二个真实消费者时提取 platform_workspace 共有 Scene 方法，原九场景和新事务目标各调用同一实现，避免把全部旧用例再次编译进新入口。SDK 自由函数/Ui 保护影响 Loader/UI 路径，按最终源码运行当前支持配置相称检查；整 P5 全量矩阵仍由 M5-e 完成。验证旧 .5 实物兼容、独立公开头、当前协议/格式、实际产物/插桩与完整选中集合；不把未跑配置或设计写成通过。

## 收口结论

本段复用原安装、模块目录、ConfigurationDefinitions、Session/Driver/Store 与 ReplacementOperation 完成同一 Workspace 的实际 Native/Qt 升级。预览是纯值；执行重新核身份/hash/来源、修订和闭包，预览及拒绝请求不把候选写入 store。只有唯一原执行持有两代能力，当前槽在 owner 查询时依据真实 committed、目录角色、规范注册名、Driver locator 和 RuntimeOrigin 投影，不等待最终异步回调。历史只保留最多 128 个纯值报告。

Locator 编辑沿原 YAML 值树，保留非目标字段与数字原文；SDK 只增加自由函数，原类、vtable、C ABI 和 Qt IID 不变。管理保护由固定规范注册名解析本次 Current，Held 候选手动动作关闭，不累加中间代。交叉审查发现逐贡献回调可能使 Workbench 遍历失效，已改为一次 Home 值投影，删除逐项 hide、独立 unpublished 和重复 canAddPlugin 查询。两个真实 provider 回调分别关闭窗口并推进清理/删除、停用后新建实例，核窗口访问屏障、旧页退出、单一卡片及手动拒绝。原普通 page-only、stopped 和配置错误灰显路径保留。

十二项同平台实际 DLL 场景验证 counter/reporter A→B、有限 Native 回切、外借工厂的独立物理拒绝、Notebook 草稿与页/命令替换、真实候选启动失败恢复、Held 手动拒绝、已 committed 后实际丢弃回执、纯预览/伪造/双击、陈旧修订、源篡改和 Prepared 关闭取消。计数通过原 Frozen 状态读取及真实 Reporter 调用核对；Native 回收同时核原映像全范围离开、GetModuleHandle 消失与旧文件可独占打开。没有插件测试导出、虚构 RPC 或手动复制提交算法。恢复保持原 Current/Candidate 角色，retired 报告字段只表示清理目标。提交后 discarded 保留原执行与真实 Current，原 claims 已释放的事实不被改写。

审计修正了测试初始化遗漏 apply、恢复角色误判、预览未安装的磁盘证据缺口和取消断言过弱；生产预览/取消算法未为测试另开路径。最初缺 Qt PATH 的运行未进入程序，随后统一开发环境运行 11/12，恢复断言修正与 Home 收敛后十二项全部通过，原失败记录保留。本段没有声称所有准备失败、Stopping/Building 关闭点或完整 H01/S09 故障矩阵已跑。

最终 32 项源码/构建输入冻结、30 项 C++ 格式通过。MSVC Release、Debug、ASan 各受影响专项 219/219；静态 Core/关闭 Ui 为 Native 18+YAML 11 共 29/29，无 Qt GCC YAML 11/11。每份唯一 Passed 名称与完整选中集合逐项一致，whole_stage=false；不是平台全量。31 个实际 ASan 对象（含四种 Notebook 和两种 main）核 flags 与运行前后哈希；36 份实际产物为原 Native 24 份摘要保持与新 Notebook 12 份清单/metadata/moc/宏一致。两编译器独立 YAML 公开头、七项原契约和 Core/Module/Replacement/Qt/Ela 未变核对通过。

基线 2036670 保存的真实 SDK 0.1.5 legacy DLL，SHA256 4a16e0bfc6792f4574e881c4369a1c87da893cb96bcef4e6173a57c21e00cdcf，已在当前 Release 宿主 normal 场景运行成功，未重建或改元数据替代。两个实际入口三配置共六次坏文件输入，均正常失败收尾并退出 1；这些独立检查不计入 219 项或人工桌面验收。证据保存在 build/p5-m5b42-*。

独立源码、所有权、UI 重入和最终证据复查无剩余阻断，随本次收口提交同步。M5-b 的有限实际升级出口完成，产品升级交互、完整 H01/S09、100 次/随机压力、最终精确名单和新增桌面验收仍归 M5-c/d/e；生产 Native 名单仍空，临时四产物批准不扩张，Qt 始终驻留。
