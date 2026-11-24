# P7 文献阅读插件交付与验收

2026-11-24，Windows x64 内部技术预览。P7 采用两个独立版本的本地文献阅读插件验证 SDK、宿主和业务数据的完整接入流程。P6 的发布基线为 `2733827`。P7 是本轮路线图的最后一个已规划阶段。[路线图](post-p5-roadmap.md) · [架构决策](adr/0062-external-literature-reader.md)

## 安装与阅读

将 Windows 运行 ZIP 完整解压，双击 `cordis_plugin_workbench.exe`，在导航里打开「文献阅读」。选择本地 PDF 后点击「导入书库」，用页码框或翻页按钮阅读。可以给整篇文献写笔记，为某页添加书签、手动录入引文与附注，也可以把笔记导出为 Markdown。关闭前点击「保存书库」。再次打开同一工作区会恢复已保存的文献、阅读位置与标注。[操作说明与源码构建](../plugins/literature-reader/README.md)

默认工作区位于当前用户本地应用数据目录的 `CordisPluginWorkbench` 下。指定独立目录可使用 `--workspace "D:\我的文献工作区"`。备份时复制整个工作区，其中 `session.yaml` 记录实例配置，`library/library.json` 保存业务记录，`library/objects/` 保存导入的 PDF 副本。业务编辑由「保存书库」提交，源 PDF 保持原字节。

## 五项要求的实测

| 路线要求 | 结果 |
|---|---|
| 实际业务与持久化 | 导入和渲染使用 Windows.Data.Pdf。用一篇 12 页的本地论文测试翻页、缩放、编辑和新进程重开，恢复到第 2 页、125% 和原笔记。可分发的回归使用程序生成的三页 PDF。 |
| 仓库外 SDK | SDK 0.1.5 含公开头文件、共享库、CMake 配置和最小 UI 插件模板。搬迁 SDK 后在外部目录构建模板。独立解压交付的 SDK ZIP 和文献插件源码 ZIP 后重新构建，10 项模型测试全部通过。 |
| 接入、诊断与生命周期 | 7 项工作台集成测试涵盖依赖等待及恢复、两实例独立书库、失败诊断、重开、两版升级、后台作业中关闭和外部模板实际装载。5 种无效 SDK 消费配置均给出预期诊断。 |
| 宿主边界 | 宿主按 manifest 加载一个受信任的驻留 Qt UI 模块，并可配置多个实例。沿用 ModuleManager、Kernel、ConfigurationSession 和原 Qt/UI 生命周期。宿主与外部 SDK 的 Core、Qt、Ui、Ela 四个动态库逐项 SHA-256 相同。 |
| 升级、持续使用与恢复 | 1.0.0 保存后退出，1.1.0 打开同一书库，添加书签和按页附注、保存并导出，再用第三个进程打开核对。测试覆盖写入冲突、锁竞争、拒写后重试、损坏数据和取消中的关闭。 |

源文件与资源所有权、数据冲突和渲染诊断分别记录在 [SDK 开发记录](p7-sdk-development.md)、[业务数据](p7-reader-data-development.md)、[阅读界面](p7-reader-ui-development.md) 和 [运行包构建](p7-reader-delivery.md)。实际集成测试的复执行方式见 [tests/p7](../tests/p7/README.md)。

## 交付物与适用范围

本地交付目录为 `D:/WorkSpace/delivery/Cordis-P7-literature-reader/`。

| 交付物 | SHA-256 |
|---|---|
| `Cordis-literature-reader-1.1.0-windows-x64-2.zip`，120 个文件，14,993,597 字节 | `42944e8dcc4bb6d68114259c6c9649277140442783eabfa5c0a12c53a60f797c` |
| `Cordis-LiteratureReader-1.1.0-source.zip`，10 个源码、文档及测试文件 | `81d09a2b37ae47d79fb122ab1c4db3458a6421452cb26aa011d4203c5aab7373` |
| `Cordis-0.1.5-windows-x64-msvc-release-sdk.zip`，含模板及使用说明 | `58e0a1e0ea1aa9ed02b5c2d6bccc1f6d8b6187dcc57a485ede9d79f7562a3cfd` |

在 Windows 11 专业版 x64、系统构建 26200.8655 上，逐文件核对最终 ZIP 的解压目录后，从独立工作目录启动其中的正式 GUI 程序，使用只含 Windows 系统目录的 PATH。两个进程（43728、26180）分别创建产品窗口，加载包内阅读 DLL、Cordis、Qt、平台插件及 CRT，经正常关窗流程退出 0。模块记录显示其余加载项均来自 Windows 系统目录。同一批包内运行库和 1.1 模块另经 Windows 平台真实 Qt 控件完成三进程升级、笔记导出和再次重开。测试程序和 QtTest 由包外验收目录单独提供。

运行 ZIP 自带运行库，开发者使用 SDK 时需要匹配的 Qt 5.15.2 开发包、MSVC 19.44.35222.0 x64、动态 CRT 和 Windows SDK。支持本地 PDF，单篇上限 512 MiB。引文由读者录入。此次桌面实测环境为开发主机，P6 的干净 Hyper-V 客体验收对应此前的技术预览包。发布范围为内部预览。

过程中的 WinRT 路径错误和首次打包对 Qt 插件目录的错误假设均保留原失败记录，修正后分别用真实 PDF 和最终 ZIP 验证。交付目录内的 `Cordis-P7-reader-acceptance.zip` 汇集原始测试记录与可分发的合成 PDF 界面截图，包外 `p7-complete.json` 记录最终源码身份与各交付物摘要。
