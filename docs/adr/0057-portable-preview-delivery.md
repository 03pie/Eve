# ADR-0057：同字节 Windows 技术预览交付

日期：2026-11-19。状态：P6-0 独立审查采纳，随本段提交归档。基线 1f79cba，SDK 0.1.5；P5 实现、资格及产品原件见[P5索引](../p5-release.md)。

## 问题与取舍

当前两个工作台均可从开发目录运行，但没有独立运行包、文件清单、部署验收及离线更新契约。现有 Core/Module/Replacement/Loader/Session/Store/Qt/UI 的职责和关闭拥有链能够承载现需求；缺口在交付层。P6 不改动业务 producer、扩大 Native 批准、复制配置状态机或建立通用安装器。

用户已明确选择“内部技术预览，暂不公开发布”，并允许使用另一台干净 Windows 物理机或独立 Windows 虚拟机验收；虚拟机可运行于本开发主机，隔离与证据边界见下方补充。第一方开源许可证保持未决定；本阶段不选择许可证或公开发行。第三方实际随包材料仍必须整理，未验证材料不得标为已齐备。

选择 Windows x64、MSVC 19.44.35222.0、动态 CRT /MD、Qt 5.15.2、共享 Cordis Core/Qt/UI、Ela 2.0.0 项目维护基线、Release。首包消费 P5 已审字节，而非执行 freshbuild。应用/SDK 版本为 0.1.5；交付修订使用独立 `0.1.5-preview.N`，不借打包修改应用内版本或 DLL。目标为 Windows 10/11 x64，支持记录只列实际验证的系统版本/构建，不泛化全部组合。

## 入口与最小布局

便携 ZIP 根目录保留如下布局；用户可直接运行 exe，无需 Python、CMake、VS 或 Qt 开发 kit。用户不必运行开发用 `desktop.ps1`。

```text
Cordis-0.1.5-preview.N-windows-x64/
  cordis_config_workbench.exe
  cordis_platform_workbench_demo.exe
  cordis_core.dll / cordis_qt.dll / cordis_ui.dll / ElaWidgetTools.dll
  Qt5Core.dll / Qt5Gui.dll / Qt5Widgets.dll
  platforms/qwindows.dll
  <实际核实的额外 Qt 插件、图像/渲染/CRT 运行依赖>
  native-upgrade/{counter,reporter}-{a,b}/module.json + 对应 DLL
  qt-upgrade/notebook-{a,b}/module.json + 对应 DLL
  qt.conf
  README.txt / build-manifest.json / SHA256SUMS.txt
  licenses/ / verification/
```

配置入口保留已注册内置插件能力；业务入口是固定 Counter/Reporter/Notebook 三模块升级工作台，不能称为任意插件的通用装配宿主。原独立 Notebook 1.0、其他示例、测试程序、故障夹具、PDB、头文件、开发工具和整个 build 目录不进入运行包。

业务入口使用 `applicationDirPath()`，`businessArtifacts()` 每次初始化检查六份 A/B，即使已有配置/store；保留相对树，不能仅复制初始 A。Qt 平台、图像、渲染与 CRT 的完整闭包由实际 PE 依赖和进程加载位置核对。`qt.conf` 只定位包内 Qt 插件；未知或缺失依赖必须报错，不从开发 PATH 静默补齐。七份资格依赖不是完整部署闭包。

## 数据与资源拥有者

| 数据 | 唯一拥有者与位置 | 更新/删除规则 |
|---|---|---|
| exe、运行库、六份模块源、文档 | 只读应用版本目录 | 同字节复制；新版本并列，不覆盖加载中 DLL |
| 配置工作台配置 | Session/Store；默认 `%LOCALAPPDATA%/CordisConfigWorkbench/cordis.yaml`，或原 `--file` | 原显式保存、CAS 与冲突语义；部署工具不得隐式接受目标 |
| 配置入口外观 | 原 WorkbenchPreferences；同目录 `ui-preferences.json` | 原独立原子保存；不是插件配置 |
| 业务配置与安装 store | 原 Session/Store/ModuleInstaller；默认 `%APPDATA%/CordisBusinessPlatform/session.yaml` 与 `plugins/`，或原两个位置参数 | hash 定位及受审准入不变，旧代保守保留 |
| 业务窗口外观 | 当前 shell 未传偏好路径，为会话内设置 | 不增加跨进程持久化承诺 |
| 插件运行态/Notebook 草稿 | 原实例/epoch、状态迁移协议 | 仅同进程迁移；不是跨进程业务文档备份 |
| 输出/通知 | 原宿主内存纯值历史 | 输出最多 1000 条、64 通道、每条 4096 字节；通知最多 100 条、每条 4096 字节；关闭释放 |
| 验收记录、配置备份 | 验收/更新工具的显式可写目录 | 不写应用目录；不后台自动收集，不自动清旧记录 |

首包不新建持续文件日志。用户通过现有复制/清空输出获取诊断；验收工具每次显式生成一份独立记录，按次归档。无需凭空引入文件轮转服务；未来持久日志必须先定义容量、轮转、敏感信息与保留期限。卸载只删除已退出且确认不用的应用版本，默认保留全部用户数据/store、备份和取证；无自动旧 hash 清理器。

## 清单与资格来源

包工具负责校验输入、复制、清单与归档，不运行链接、不修改受审产物、不生成批准。每项记录用途、来源、相对路径、长度与 SHA；build-manifest 记录应用/交付/配置格式/状态 schema/SDK/ABI、平台、编译器/CRT、Qt/Ela/LibYAML 版本、资格源和证据锚点。清单自身摘要由旁置 SHA256SUMS/包外摘要承担，避免自摘要循环；清单不是运行时政策。

原 [6c 名单源](../../qualification/native-business.json)及历史绝对路径保持；归档映射把原路径关联到包内相对路径与同一 SHA，不能回填历史源。每个 profile 仅四份精确 Native 批准，Qt 保持 resident。新增 Qt 插件/CRT 的部署验证不追授 Native 资格。独立验包检测缺件、多件、路径逃逸、错 profile 或摘要不符；摘要证明一致性，不证明不可信来源安全。

打包同字节原则优先。Qt 官方说明部署工具可能改写 Qt5Core 中的路径；当前 kit 未发现 windeployqt，首轮按核实闭包复制并使用 qt.conf，不下载未知工具替换受审库。任何受审字节变化停止打包并走原资格流程。CRT 来源固定到有来源记录的匹配 Release 可再分发文件，不从任意 System32 抽取或携带 Debug CRT。当前 app-local CRT 候选与 P5 本机 System32 CRT 摘要不同，属于 P6 新部署闭包，必须单独验收；“同字节”只对原受审产物及其明确依赖成立，不能称整个 CRT 环境已与 P5 相同。

## 更新、验证与流水线边界

离线应用更新在两入口均退出后进行：旧/新版本目录并列，先验包、备份、验证兼容/迁移，再启用新版本；失败保留旧版本、原配置和备份。配置 formatVersion 当前为 1；状态 schema 属于各插件，ABI/SDK 与应用版本分别检查。新 Native hash 不因版本字符串相同获批；迁移映射须核模块身份、原/新摘要、版本/schema及现有受审准入。P6-3 再按实际包契约设计最小工具，不新增常驻更新器或另一配置保存状态机。

源码 CI、资格候选、受审产品验证、冻结发行分层。freshbuild 先走资格，不改名单以求通过；待资格的产品正向项明确未完成。资格采用后只重建名单消费者，保持已测模块/依赖字节，再验证产品；冻结后打包不重链。真实桌面/GDI/USER 与干净机器验证不由 offscreen 或开发机清 PATH 替代。

P6-2 先完成包内字节、搬迁、任意工作目录、中文空格路径、只读应用/可写数据、实际加载路径和失败用例；独立干净环境记录无源码/开发依赖、普通权限、OS、PID、版本、保存定位、重开及退出码。P6-5 交付最终包后再次在符合条件的环境进行人工验收。自动操作与人工体验分开，物证不一致仍未通过。

## 2026-11-19 补充：允许虚拟机验收

用户明确允许虚拟机承载 P6-2 的 D11 及 P6-5 最终包验收。复查现有边界：所需证明是脱离开发依赖后的独立部署、真实 Windows 桌面与数据保存/恢复；这些职责可由独立安装的 Windows 客体系统承担，无需修改生产代码、打包工具、插件批准或记录器。另一台物理机仍可用于验收；不再要求使用不同物理主机。

- 客体系统以干净安装或可追溯的干净基线开始，无项目源码树、VS/Qt 开发环境及开发路径污染；产品进程以客体普通用户的非提权 token 运行。
- 交付包传入后先核外部 ZIP/manifest 摘要，再解压到客体本地磁盘。应用、配置和 store 均使用客体路径；共享目录只用于传入交付物、导出证据，不提供宿主开发库或运行中的应用/用户数据依赖。
- 在客体真实 Windows 桌面完成原有两个入口的首次/重开四进程、只读应用区、保存/冲突、模块升级及正常退出流程；实际 DLL/Qt 插件加载路径须来自包内或已声明的客体系统依赖。P6-3 更新/中断/恢复仍按原要求留证。
- 复用随包验包器、ACL 工具和桌面记录器；另随原件记录虚拟化平台及版本、客体 Windows 版本/构建、CPU/内存配置、显示/渲染方式、干净基线及共享目录设置。快照回退或销毁客体前先导出原始记录和失败证据；自动操作与当前人工回执分别保留。

验收结论只覆盖实际记录的客体系统、虚拟化平台和显示/渲染环境，不泛化其他系统或物理硬件。允许虚拟机只调整验收环境选择，不把宿主清 PATH、既有本机自动 UI 原件或新建虚拟机本身计为 D11 通过；P6-2 仍待实际验收，P6-5 仍须使用最终包及准确摘要复核。

许可证、部署与隔离参考：[Qt 5.15 Windows 部署](https://doc.qt.io/archives/qt-5.15/windows-deployment.html)、[Microsoft CRT 再分发说明](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)、[Windows Sandbox 安装条件](https://learn.microsoft.com/en-us/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-install)。这些参考不能代替本包实际依赖及材料审查。

## 阶段门禁

具体要求与证据逐项维护在[P6验收表](../../tests/acceptance/p6-delivery.md)，实施记录见[P6工作单](../p6-development.md)。每段先审架构、实现/删旧、验证、独立审查，再精确提交同步并核 clean；P6-0 仅冻结合同，不能标记打包、跨机器或整 P6 完成。
