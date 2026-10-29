# CordisCpp 插件平台

基于 Cordis 思路设计的 C++17 插件内核，目标桌面环境为 Qt 5.15.2 + ElaWidgetTools。

当前已完成 **P1/P2/P3 及第一方 Windows UI 专项**，P3 收口及用户确认见 [阶段审计](docs/p3-closeout.md)。配置工作台支持预览、应用、期望启停、保存和磁盘冲突处理；Home 卡片可打开管理/配置窗口，顶部菜单可添加已注册插件的配置实例。Windows 托管 worker 支持类型化结果及完整退出屏障。

**P4 进行中**：M1 预检/账目、M2 原生装载及完整业务 ABI 协商已审核提交，见 [ABI V1](docs/abi-v1.md)。当前 [M3a 请求组件](docs/p4-m3-development.md)、[M3b 原生生命周期](docs/p4-m3b-development.md)及 [M3c dispatch/effect/服务桥](docs/p4-m3c-development.md)已实现，两个服务 DLL 与 Kernel/Session 联合桥、M4 UI DLL 仍待完成，P5 受控替换未开始；当前 C++ 源码 SDK 不构成稳定二进制 ABI。

当前工作区还已实现受限类型化 Qt 信号投递：数值、枚举与标准/Qt 字符串会在发射时冻结，参数保留至异步业务完成，并沿用 Context/epoch 及原生退出屏障。详情见 [类型化连接决策](docs/adr/0026-typed-qt-connections.md)，示例为 cordis_qt_value_connection_demo。

## 从这里开始

在当前目录运行 PowerShell：

```powershell
./tools/dev.ps1 -Action doctor
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
```

已安装 Qt 5.15.2 MSVC kit 后，可在普通 PowerShell 中构建和运行桌面工作台：

```powershell
./tools/desktop.ps1 -Action verify -Preset ela-msvc-release
./tools/desktop.ps1 -Action run -Preset ela-msvc-release
```

入口自动加载本机 VS2022 x64 工具链，默认 kit 在 build/Qt/5.15.2/msvc2019_64；也可用 -QtRoot 指定。环境只在调用期间生效。恢复环境与依赖安装记录见 [开发环境](docs/development.md)。

`verify` 依次配置、编译、运行 CTest。完整工程需要匹配的 C/C++ 编译器，以构建固定 LibYAML C 库和 C++17 模块。CMake 不在 PATH 时可设置进程内的 `CORDIS_CMAKE_BIN` 指向其 bin 目录。脚本不修改系统 PATH。核心及 Qt 预设不构建 Ela；Ela 和 LibYAML 均使用仓库内源码，构建不下载上游。

本机 MinGW GCC 用于验证无 Qt 的 C++17 内核，MSVC/Qt 5.15.2/Ela 组合另行构建验证。环境入口见 [开发环境](docs/development.md)，Ela 构建与交互示例命令见 [P3 桌面适配](docs/p3-development.md#q3a-ela-页面贡献)。

## 当前内容

| 路径 / 目标 | 用途 |
|---|---|
| `sdk/include/cordis` | 源码 SDK 基础类型，不是稳定二进制 ABI |
| `src/core` | 不依赖 Qt/Ela 的内核库起点 |
| `src/loader` | 独立 Cordis::Loader，配置结构校验与目标差异预览，无运行或文件副作用 |
| `Cordis::ConfigYaml` | 独立受限 YAML 解析及内置定义/schema 校验层，SDK 不暴露 LibYAML 类型 |
| `Cordis::ConfigCoordinator` | CAS 目标接受、最新目标/串行动作及关闭协议，宿主运行驱动单独适配 |
| `Cordis::ConfigKernel` | 内置配置树的真实 Kernel 驱动，类型化依赖、realm 继承/覆盖、父恢复后的子树重建及稳定操作/清理屏障 |
| `Cordis::ConfigFileQt` | 可选 Qt 配置文件事务，原文/hash 基线、冲突保留及临时文件提交 |
| `Cordis::ConfigStore` / `Cordis::ConfigStoreQt` | 无 Qt 的保存状态/单写队列，以及可选真实 Qt 文件后端 |
| `Cordis::ConfigSession` | 独占连接候选预览、CAS 接受、期望启停和 Store 跟踪/退出刷新，不依赖 Qt |
| `Cordis::ConfigIoQt` | 可选配置后台串行读写、线程 join 与文件/目录监听，通知不自动应用目标 |
| `Cordis::ConfigUi` | 可选 Ela 配置编辑页及 Home/管理适配器，弱持宿主会话；普通 Ui SDK 不依赖配置层 |
| `src/qt` | 可选 Cordis::Qt，包含 QtScheduler、QtClock、QObjectResource、托管 QtConnection 与 QtWorker |
| `src/module` | Cordis::Module 账目、ModulePreflight 预检及 Windows ModuleNative 只读描述装载；尚不激活外部业务插件 |
| `src/ui` | 可选 Cordis::Ui，包含 Ela 插件状态卡片及电源操作、单击直达的插件导航、菜单/搜索/前进后退、可全部关闭的中央页签、底部工作区、贡献服务和只读诊断；资源树属于具体插件页 |
| `third_party/ela` | 项目内维护的 Ela 组件库及资源，保留上游基线、本地修改和 MIT 许可 |
| `third_party/libyaml` | 固定 0.2.5 C 解析库和 MIT 许可，私有链接至配置文本层 |
| `examples/builtin` | 异步 Task、Effect、EventBus、插件联合生命周期及真实配置协调示例 |
| `examples/configuration` | 基线、候选及拒绝样例，配合 cordis_configuration_demo 打印只读差异 |
| `examples/qt` | Ela 管理页、页面/命令/通知、缺依赖和失败重试及异步退出示例 |
| `tests` | SDK 契约、内核与真实 Qt 循环测试，以及验收规范 |
| `tools/dev.ps1` | 环境检查、配置、构建、测试入口 |
| `docs` | 决策、接口契约、首阶段工作单和准备记录 |
| `sources` | 同步参考资料，只读；不纳入产品构建或 Git |
| `research` | 独立研究副本，不纳入产品构建或 Git |

编译后可运行真实配置示例（不依赖 Qt，其他预设替换构建目录名）：

```powershell
./build/core-gcc-debug/examples/builtin/cordis_reconcile_demo.exe
```

示例接受两份 YAML 父子目标，由 inject 声明建立消费者子项的真实依赖；更新提供者父配置后检查旧子终结、新子身份及最新服务值，最后确认全部自有实例静止。它不写配置文件；完整协议及范围见 [配置子树协调](docs/adr/0019-configuration-subtrees.md)。

父代上下文/子实例的纯 Core 示例：

```powershell
./build/core-gcc-debug/examples/builtin/cordis_child_epoch_demo.exe
```

宿主通过 whenActiveAfter 观察父实例新代次，使用弱 Context 创建带类型化依赖的子实例；父更新等待旧子清理，再挂载新子。该示例单独验证 Core 桥接能力，见 [跨代观察协议](docs/adr/0018-activation-context-bridge.md)；YAML 自动协调由上面的 cordis_reconcile_demo 演示。

运行配置工作台（沿用原 Release 构建目录，入口隔离加载工具链和 Qt）：

```powershell
./tools/desktop.ps1 -Action run -Preset ela-msvc-release
```

默认配置文件位于应用本地数据目录，界面显示确切路径；入口可用 -ConfigurationFile 指定文件，直接执行 exe 时对应 --file。读取原文不会自动应用或保存，先“检查差异”再“应用”，选择条目用左右开关修改期望启停，点击“保存”持久化。保存会规范化 YAML/JSON，未应用编辑不会写盘；退出刷新已应用修改，磁盘冲突先保留窗口供处理。此入口只使用配置目标管理实例，原 cordis_ela_demo 继续演示手动实例管理。直接执行 exe 的明暗自动示例使用 --smoke/--smoke-dark，始终写临时文件。

Qt 信号连接与异步退出示例（只依赖 QtCore，Qt/Ela 预设均构建；直接执行 exe 需为当前 PowerShell 加入所用 kit 的 bin，下面采用本机默认位置）：

```powershell
$env:PATH = "$PWD/build/Qt/5.15.2/msvc2019_64/bin;$env:PATH"
./build/ela-msvc-release/bin/cordis_qt_connection_demo.exe
```

信号作为零参数触发，业务在 owner 排队执行；示例等待真实异步任务、原生连接和时钟排空后退出，见 [托管连接协议](docs/adr/0023-managed-qt-connections.md)。

Windows 插件工作线程与信号联合退出示例（仍沿用原 Release 目录）：

```powershell
./build/ela-msvc-release/bin/cordis_qt_worker_demo.exe
```

QtWorker 支持协作取消，在原生线程和 TLS 退出后释放捕获，随后交付结果。该示例等待 worker、连接和 Kernel 各自确认后退出，见 [工作线程协议](docs/adr/0024-managed-workers.md)。

类型化数据示例（沿用上述当前进程 Qt PATH）：

```powershell
./build/ela-msvc-release/bin/cordis_qt_value_worker_demo.exe
./build/ela-msvc-release/bin/cordis_qt_value_connection_demo.exe
```

## 设计及实施入口

- [开发进度](docs/progress.md)：当前状态、工作包、验证台账及下一任务。
- [P4 原生模块](docs/p4-development.md)：分阶段范围、预检/驻留账目、测试及诊断入口。
- [UI 设计与优化专项](docs/ui-optimization-plan.md)：P3 收口后的工作台/页面/状态/偏好优化、工作包和完成条件。
- [Home 管理窗口与平台添加](docs/home-management-development.md)：卡片管理/配置窗口、顶部添加已注册配置实例及验证记录。
- [UI 专项阶段](docs/ui-special-stages.md)：Ela 基线、VS Code 全结构分析、框架、宿主页和联合验收五个阶段。
- [VS Code UI 对照](docs/vscode-ui-structure.md)：逐结构职责、当前实现、适配决定及后续修改清单。
- [UI 专项验收](tests/acceptance/ui-optimization.md)：明暗主题、窗口/缩放、交互与新增状态/偏好的验收矩阵。
- [P3 验收映射](docs/p3-acceptance.md)：L/Q/U 与联合场景的自动化证据、人工确认及适用边界。
- [P3 收口审计](docs/p3-closeout.md)：完成条件、最终验证、用户确认和原生代理补验。
- [W1 交付与验证](docs/w1-verification.md)：异步基础能力和四配置测试结果。
- [W2 交付与验证](docs/w2-verification.md)：Effect 清理屏障、超时观察及四配置各 60/60 检查。
- [W3 开发记录](docs/w3-development.md)：实例与清理上下文交付，四配置各 64/64 检查及后续边界。
- [W4 交付与验证](docs/w4-development.md)：服务依赖、输出契约、realm 视图及异步调用，四配置各 86/86 检查。
- [W5 交付与验证](docs/w5-development.md)：子实例、父代清理屏障及联合环诊断，四配置各 96/96 检查。
- [W6 交付与验证](docs/w6-development.md)：事件协议、作用域、异步屏障及控制台示例，四配置各 110/110 检查。
- [P3 桌面适配](docs/p3-development.md)：Qt/Ela、配置校验、真实 Kernel 协调、文件事务与 Store，以及各轮验证记录。
- [配置候选与预览](docs/adr/0011-configuration-preview.md)：结构校验范围、限额及预览与执行计划的边界。
- [受限 YAML 与内置校验](docs/adr/0012-restricted-yaml-validation.md)：输入子集、schema/依赖联合、固定库及文件预览用法。
- [目标接受与串行协调](docs/adr/0013-configuration-coordinator.md)：CAS、实际状态再规划、部分失败与关闭协议。
- [Kernel 配置驱动](docs/adr/0016-kernel-configuration-driver.md)：顶层内置配置的真实依赖/realm 映射、执行、退出及剩余范围。
- [实例级配置依赖](docs/adr/0017-instance-inject-contracts.md)：动态 inject 的类型目录、契约联合、实际状态规划和重建边界。
- [宿主跨代观察](docs/adr/0018-activation-context-bridge.md)：弱 Context、父重启后的子实例重建及类型化依赖桥。
- [配置子树协调](docs/adr/0019-configuration-subtrees.md)：真实父子归属、继承域、等待父代、自动恢复及失败/关闭边界。
- [配置会话与页面](docs/adr/0020-configuration-session-ui.md)：不可伪造候选、接受/跟踪顺序、期望启停、冲突决策及可选配置工作台。
- [配置文件事务](docs/adr/0014-configuration-file-transactions.md)：Qt 原始字节版本、协作锁、冲突来源及临时文件提交的范围。
- [配置 Store](docs/adr/0015-configuration-store.md)：已接受目标/历史保存、dirty、单写队列、显式 rebase 与退出刷新。
- [P3 桌面验收单](tests/acceptance/p3-ui.md)：明暗主题、缩放、组件一致性、交互及正常退出；2026-10-25 用户确认完成。
- [W7 联合验收](docs/w7-verification.md)：服务/事件/子树完整闭环、失败残留和 A01–A10 证据，四配置各 113/113 检查。
- [软件设计说明书](Cordis插件平台-软件设计说明书.md)：详细设计与验收依据。
- [开发指南](Cordis-Cpp17-Qt5152-开发指南.md)、[研究证据](research-evidence.md)：研究基线。
- [基础决策](docs/adr/0001-foundation.md)：本轮冻结的约束。
- [首阶段接口契约](docs/core-contracts.md)：实现 Task/Effect/Instance 前的接口语义。
- [第一阶段工作单](docs/p1-work-items.md)：开发顺序与完成条件。
- [验收场景](tests/acceptance/p1-core.md)：待实现行为，不属于当前已通过测试。
- [准备完成记录](docs/preparation-status.md)：实际检查结果与剩余环境条件。
- [开发环境验证报告](docs/environment-verification.md)：GCC/MSVC 四配置实测结果与 Qt 缺项。

第一方代码尚未选定开源许可证；third_party/ela 和 third_party/libyaml 保留各自上游 MIT 许可，该许可不自动适用于第一方代码。
