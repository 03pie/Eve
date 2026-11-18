# P5-M5-b4 单一业务平台与整体替换

日期：2026-11-18。基线 3e74d52，b1/b2/b3 已完成审计、提交同步且工作区干净。先核对实际代码和拥有者，再实施以下分段；每段完成审查、修正、相称验证和提交同步后才进入下一段。

| 小段 | 具体出口 | 当前状态 |
|---|---|---|
| b4-1 | 原 Native 装配的必要复用；单一 Workspace 装配三模块；显式队列拥有、初始化失败收尾和有序关闭；保留全部原示例 | 已完成，2036670 已审计提交同步 |
| b4-2 | 同源 locator 编辑和管理保护；原安装/stage/ReplacementOperation/配置共同提交；实际 Native/Qt 替换、恢复、结果投影及旧 Native 回收 | 实际业务接线与有限验证已完成审计，随收口提交同步，见 [具体结论](p5-m5b42-development.md#收口结论) |

b4 不交付管理升级按钮、完整 H01/S09、100 次/随机压力、生产名单或新增原生桌面验收。这些仍归 M5-c/d/e；本段不用各后端独立平台或候选并存冒充整体事务。

## b4-1 职责与依赖

当前 Pipeline 和 Workspace 各自创建 Kernel/manager/Driver/Session，直接并置会产生两套平台；任一 close 也会误停共享业务。保留一个实际 Workspace 拥有者，复用原 ElaShell、UI providers、配置页、模块诊断和退出；不复制 MixedWorkspace 或泛型插件框架。

从 native_services 提取两个实际服务共用的具体装配：稳定代次记录、NativeModule/Runtime 构建、固定依赖/输出/schema 和弱记录可用性门禁。由原 Pipeline 与 Workspace 两个真实调用者使用，后续候选也复用相同构建契约。组件只接受调用方实际 manager、scheduler、ServiceRegistry 和代次，不创建 Kernel/Driver/Session、加载器策略或进程循环。记录在打开会话前由宿主真实拥有，半装配错误仍可沿该记录关闭。原 Pipeline 继续负责原 Session→Kernel→两个会话→释放工厂→drain；其五 CLI、五直接装配和 b2 业务契约不变。

审查发现旧 Pipeline 在返回故障关闭报告时也释放自身 Session/Kernel/定义 facade。这是保留的 CLI 语义：终结后只读纯阶段报告与 manager/Native 诊断，不提供失败后的修复/重建入口，实际未确认资源仍由原共享执行控制块与 Native PIN 保留；Core 不借用 Kernel facade 地址。没有实际资源丢失/UAF 证据，本段不为 facade 存活引入新的永久 Pipeline 环或改写原五场景。原 18 项检查不宣称证明故障 facade 存活。Workspace 继续承载 UI 诊断及后续恢复，故必须保留更完整的拥有链；该差异有具体用途，不复制关闭算法到 Native 组件。

每个 Native 记录只对应一代，session/runtime 归记录，available 只弱持该原记录；新 current 以替换 shared_ptr 表示，不能改写旧记录身份。Qt 记录持实际 Generation 和 Runtime，门禁弱查原 manager/固定 Generation，不造 Native 会话或卸载接口。页面/editor 仍由原 Context effect/QObjectResource 顺序释放。

Workspace 接收调用方预先真实拥有的 QtScheduler，构造 UI/Kernel 前验证应用线程、开放 owner 队列与 QApplication。原私有队列 open 路径删除，更新现有 main/tests 两个实际调用者，不保留无人调用的兼容包装。open 返回 Result，初始化异常按同一关闭链收尾；只有关闭/未确认回执阶段自保留。调用方负责队列最终屏障和关闭，失败不产生无法交还的私有 Receiver。

原单 Notebook open 入口和 legacy 默认定位保留其业务；另增加明确的三业务资格装配入口，共用同一 Workspace 初始化/关闭路径。它只创建一份 scheduler/Kernel/ModuleManager/Driver/Session/ElaShell，先发布原 ui.pages/commands/output，再准备两个 Native Runtime 和 stateful Notebook Runtime，最后一次 ConfigurationDefinitions::create 和 configured Driver/Session。顶层配置实例归原 Driver，不增加 IReplacementOwner。原注册名保持稳定，宿主页仍归独立内置 Runtime。

## 产物、根目录与配置来源

资格入口的来源是实际 bin 中固定四个 Native A/B 和两个 Qt A/B；核对既定 moduleId、PluginId、loader、版本、策略，Native 仅将这四个目标的实际 SHA256 传临时精确允许集合，生产名单仍空。Qt 始终 resident。原 legacy 示例不自动获得显式批准，不提供任意所选 manifest 自动授权入口。

通过原 installModuleArtifact 把实际当前产物复制到同一个存在的绝对 storeRoot/hash/module.json；统一平台 pluginRoot 就是该 storeRoot，不拼 ../，也不依赖构建目录可写。新 session 使用 A 的三个安装定位；已存在 session 先按原 YAML 解析其 modules，按实际 locator 固定并核身份/Native 精确批准/Qt 工具链和状态 schema，再构建对应当前记录，不能无条件按 A 覆盖已保存的 B 目标。默认配置文本只在首次无文件时生成，现有文件不被 bootstrap 改写。

会话、定义与 Runtime 使用同一原模块定位；配置中的 manifest 是安装根内的真实路径，三个模块与候选共享同一 pluginRoot。安装凭据只在 bootstrap 的局部准备阶段跨安装至三代加载，不由 Workspace 全运行期保留。原加载器长期持有实际 DLL 文件保护、manifest 值快照及执行/驻留来源；manifest 文件和祖先目录 guard 仅在加载栈固定，后续操作须正常重新预检，不能把旧值当永久文件授权。已保存配置分支本来不创建安装凭据，新建 A 与已有 B 不应存在无用途的长期强权限差异；返回/抛出后未加载产物仅作为不可变 store 文件保留，不自行回滚安装。AppData/session 与安装 store 使用新资格演示的独立目录，不改原 Notebook 或配置工作台文件。

三模块初始化完成后再建立完整不可变定义目录；不向旧两项目录追加第三项，因为 replacing 只替换已有身份。原 Native 的 second realm、schema 和依赖预算由实际调用者保留；统一配置仍能按原机制处理多实例和 realm，不在宿主复制解析/求解算法。

## 关闭、失败与诊断

保留原 close() 的 Task<void> 行为，另给有实际检查调用者的纯关闭快照，记录实际 Session、Kernel、两个 Native 会话、Shell 及 drain 阶段原 Result；未装配/未执行阶段为空，不伪造成功回执。Task/工厂/Context/模块对象不进入该快照。

UI 关闭先检查/保存当前接受目标；磁盘冲突发生在 Session/Kernel 关闭之前，返回可重试失败并保留完整窗口/业务。开始破坏性关闭后关门，等待 Session，再 Kernel，再依次尝试所有已打开 Native 会话；即使前段报错也按安全路径尝试下一段，保留原结果，不把清理失败写成静止。实际未确认或不静止时保留原 Session/Kernel/模块/定义和自保留，Shell 留诊断入口；不提前释放仍可能调用业务的拥有者。

全部确认静止后关闭 Shell，退役/释放 UI providers，释放宿主页句柄、配置适配器、Session/Driver/Definitions/Runtime/Kernel，跨原 owner FIFO 屏障再记录 Native drain。Qt loader root 正常驻留；drain 不是物理卸载。调用方单独保留 Runtime 引起的 drain 拒绝单独记录，不永久 pin 整个已经确认关闭的 Workspace；全局残余引用不冒称已经消失。accepted 回执 discarded/投递失败只在观察器内原子标记诊断/结算 Promise，未确认拥有者继续留存，不跨线程修改 UI 或释放资源。

## b4-1 验证出口

原 Native 五 CLI、五装配、八项状态及 Notebook 十二工作台、十一项状态继续通过；新增直接复用同一 Workspace 的三模块装配测试，核实际三个来源、counter→reporter 业务、Notebook 编辑/输出/页面归属、同一 Kernel/ServiceRegistry/Session/Driver，以及正常关闭、保存冲突/重试、半装配失败、外部工厂 drain 拒绝和真实回执丢弃保留。资格默认生产名单不变，不尝试自动物理卸载。

变化只在具体宿主装配/示例构建与测试，预计无 SDK/Core/Module/Loader/Ui/Ela 修改；按最终改动执行 Release/Debug/ASan 的受影响专项，静态 Core 关闭 UI 的 Native probe 验证同一组件，无 Qt 不编译该部分。检查当前冻结输入、格式、实际 DLL/元数据/来源与 ASan 对象，再独立审计提交同步。

## b4-1 收口结论

Native 两服务的实际合同、schema、弱稳定记录门禁已从 Pipeline 提取为一个具体组件，原 Pipeline 与 Workspace 两个真实调用者共用；旧重复装配代码删除，未复制关闭器或事务。记录身份 kind/generation 不可变，宿主先拥有再 prepare，部分打开的会话保留在原记录。Workspace 只建一份 Kernel/manager/Driver/Session/ElaShell，三个实际 Runtime 和 UI providers 准备后一次冻结目录；顶层实例仍归原 Driver。现有三 B 定位启动已实测，不强行覆盖为 A。原 legacy、五 CLI、五装配和两后端状态场景保持。

旧私有队列 open 路径已删除，两个实际调用者改为显式共享队列；App/owner/关闭准入发生在 UI 分配前。bootstrap、宿主/UI 接线、异步关闭分工明确，两个 exe 同源 main，不复制 ready/关闭/队列退出逻辑。安装凭据从无人使用的 Workspace 长期强成员缩为 bootstrap 局部，成功/异常后实际映像由原目录/记录负责；未加载 store 文件不擅自回滚。ready/apply 在关闭后明确拒绝，close 幂等，新增用例锁住该生命周期出口。

关闭只在全部实际回执确认静止后释放拥有者；保存冲突先于任何 Session/Kernel 破坏且可重试，Session 错误仍安全尝试 Kernel 与两个 Native 会话。未确认回执/不静止保留真实拥有链，普通外部 Runtime 仅造成独立 drain 拒绝，不永久 pin 已确认关闭的 Workspace。实际关闭快照只有值；Qt root 正常驻留，不称物理排空。原 Pipeline 的 CLI 故障 facade 释放差异及保留理由按上文限定，未把它的资源安全证据扩写为 facade 保活。

新增九场景覆盖三模块同源/同平台的真实业务、realm/多实例、已有三 B 来源、保存冲突、错误队列/线程、前置及 schema 半装配失败、外部 Runtime、真实 discarded 与 counter/Notebook 清理失败。首轮 50/50 后补强失败初始化的新窗口 QPointer 完整销毁检查，2/2 通过；最终安装凭据收窄后重新构建并执行下面完整受影响集合。半装配检查仅到达三模块实际准备后的 schema 拒绝，不泛称所有构造步骤或同一已 drain Native 代可复活。

十七项源码/构建输入最终冻结，十五项 C++ 格式通过。MSVC Release/Debug/ASan 各受影响专项 50/50（新平台九项、原 Native 十八项和 Qt 二十三项），静态 Core 关闭 Ui 的 Native probe 18/18；唯一 Passed 名称与完整选择集合一致，whole_stage=false。两个共享 main 的实际 exe 各在三配置运行一次坏文件输入，均按预期退出 1 且完成新增窗口/队列/资源收尾，共六次独立入口检查，不计入 50 项或桌面人工验收。十四个实际 ASan 对象（含同源 main 的两种编译）插桩与运行前后哈希一致。三十三份原 Native/Qt 产物、清单摘要及 Qt 元数据保持，SDK 0.1.5、Core/Module/Loader/Ui/Ela 未改；无 Qt 不编译这些具体宿主，不重复未变平台全量。

三路结构、所有权、源码与测试真实性复查没有剩余提交阻断项；最终清单、哈希、入口与专项日志在 build/p5-m5b41-*。产品入口是明确的固定产物资格演示，临时 Native 批准只含四个既定身份/实际摘要，生产名单仍为空。本段没有 ReplacementOperation、物理卸载、100 次/随机或新桌面验收。随本次审计提交同步、工作区干净后才进入 b4-2 的 locator、动态管理保护和实际事务接线。

## b4-2 已核对的前置边界

管理固定 RuntimeId 在换代后会过时。无需新增 Session 接口或 SDK 字段：Kernel 注册名唯一且不可重命名/移除，原 prepareCommit 固定 name→旧 RuntimeId 基准，commitPrepared 在无回调窗口原子换同名槽。attach 时固定这些已验证名字和初始 ID（后者仅兼容既有 stopped 缓存），随后从同一次 KernelSnapshot.names 临时派生当前保护集合，不累加历史 ID、不重 attach。接受、排队执行、诊断及 Home 用同一门禁；Held 候选复用现 graph.publicationHeld 只读标志禁用手动操作，不能仅等 Core 拒绝后显示失败。provider.snapshot 可重入，调用后必须重查当前状态，异常/缺槽保守关门。

Loader 尚无 locator 编辑。仅补实际调用所需 setModuleManifest(source, alias, manifest, limits)，复用原 YAML 值树→encode→parse 校验，只改既有 alias 的 manifest，保留其余数字原文/负载/子树。添加自由函数不改类布局、vtable、原导出或业务 ABI；保持 SDK 0.1.5 的依据须随具体实现/兼容验证记录。不得全局字符串替换、经 double DOM 重写通用配置或绕开原 Session/CAS；共同提交复用现 mergeReplacementTarget。

实际执行沿安装→stage/stageQtModule→真实 Runtime→当前 Definitions.replacing→目标校验→原 ReplacementOperation.begin。配置根由 Driver 默认重建，counter 轮闭包重建 reporter，reporter 轮借用活跃 counter，Notebook 各自独立。报告 committed/recovered 和真实角色是 current 槽投影依据，Failed/resourcesRetained 或 Task error 不假定未提交；保留原执行和真正尚存的资源，不补造已解除的 claims。结束请求/定义/Scope 等临时强拥有后才能关闭旧 Native、释放本宿主工厂/凭据、跨屏障检查来源/ledger，再显式 tryUnload；物理失败独立于业务提交。Qt 不复活 Retired lineage，不用于累计 100 次。
