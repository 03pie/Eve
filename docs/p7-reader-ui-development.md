# P7 文献阅读页开发记录

日期：2026-11-24。范围：独立 `plugins/literature-reader/` 的阅读页、页面/命令贡献、Qt 工厂及外部 CMake。数据模型和持久化由同目录 `ReaderLibrary` 负责，平台工作台由独立宿主负责。本记录先约定职责再实现，不改变 P6 运行包、Core 状态机或 Native 批准名单。

## 实际问题与后端选择

现有 Qt 5.15.2 kit 提供 QPdfWriter，但未安装 QtPdf/QtWebEngine；QPdfWriter 不能用于阅读 PDF。宿主的 Windows SDK 10.0.26100.0 包含 C++/WinRT `Windows.Data.Pdf` 与 WindowsApp.lib。采用系统 PDF 内核逐页渲染，不额外分发 Poppler 或启动外部阅读器。SDK 内的 Windows.Data.Pdf 页数、GetPage 和 RenderToStreamAsync 是唯一 PDF 解析/绘制来源；书签、笔记和书库元数据不交给渲染器。

已在独立 MTA probe 实际装载一页 PDF，渲染得到 900×1165 PNG，并检查真实图文内容，原件位于 `build/p7-reader-winrt-probe-20261124-1/`。这只是本机后端可用性验证，不替代外部插件或完整阅读流程验收。系统接口资料：[PdfDocument](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfdocument?view=winrt-26100)、[PdfPage.RenderToStreamAsync](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfpage.rendertostreamasync?view=winrt-26100)。

## 插件与依赖契约

- moduleId：`org.cordis.literature-reader`；pluginId：`literature.reader`；输出 `literature_reader.dll`，由 SDK helper 生成实际 `module.json` 与 `cordis_plugin_metadata.json`。
- 页面 id：`literature-reader`，分类 `literature`，默认标题“文献阅读”。命令 `literature.reader.open` 复用 PageHandle.navigate；保存命令为 `literature.reader.save`。宿主不访问 ReaderPage 或渲染对象。
- 配置只含绝对 `dataRoot` 与可选 `title`。库路径由部署明确给出，不依赖 cwd 或写入应用目录。
- 工厂只消费 `Cordis::QtUiPluginSdk` 的匹配共享 Core/Qt/Ui 与现有 `ui.pages`、`ui.commands`、`ui.output`；worker 复用平台现有 QtValueWorker，并由宿主通用 `qt.scheduler` 服务交付同一 `shared_ptr<QtScheduler>`。不能自行创建第二个调度器身份或关闭宿主调度器。依赖未提供时由平台保持 Waiting，不用插件轮询。
- 工厂返回无 parent 的全新业务 QObject；页面工厂返回无 parent 的 QWidget。PageHandle 与 CommandHandle 仍由原贡献 effect 管理，Qt 模块保持 resident。

## 单一数据源与线程所有权

ReaderLibrary 是业务数据唯一来源。UI 只在应用线程调用实例方法，展示文献列表、活动文献、零基页码、缩放、滚动、笔记、按页附注和书签；用户页码显示为一基。原 PDF 不修改。页数与像素是实际 PDF 的派生值，不写第二份权威目录。

大文件导入分成 worker 的 prepareImport（流式复制、SHA、取消）和 owner 的 adoptPreparedImport（收纳元数据），阅读前通过 worker 的 verifiedFilePathFor 核对正文。后台仅捕获根路径、Document 值副本和取消标记，不借用 ReaderLibrary 或 QWidget。渲染 worker 在 MTA 初始化 WinRT，完成文件装载、页对象、流与解码后只返回 QImage/页数等值；所有 WinRT 对象在 MTA 内释放。

每个页面仅允许一个在途后台工作。导航或缩放期间控件显示忙状态，不为每次输入创建并发解析器；完成后按请求序号和文献 id 检查是否仍当前，过期结果不触碰界面。图像尺寸与输出流字节有上界，不全书栅格化或缓存整本文献。失败在阅读区保留明确错误，不用占位文本伪装已渲染 PDF。

QtValueWorker 的 effect 确认原生线程退出、捕获释放和调度交付。页面关闭可先失效 QPointer，完成回调只访问仍有效的原 Context/页面；普通清理请求取消，平台继续等待原 worker 屏障，不能在析构中阻塞 UI、分离线程或伪造退出。所有按钮输入重查 Context 身份，服务命令沿原 CommandHandle 门禁。

显式“保存书库”调用 ReaderLibrary.save。失败保留未保存标识和内存内容；重载必须由用户明确选择丢弃。关闭不偷偷覆盖磁盘，也不把运行中草稿称作跨进程持久化。位置、笔记与标记的保存状态在同一页明确呈现。

## 界面与两个真实版本

1.0.0 先完成 PDF 文件选择/可输入导入路径、书库选择、真实页图、前后页/页码定位/缩放、页笔记编辑和显式保存。实际外部构建及其完整源码单独保存在仓库外，验明后冻结，不把后续源覆盖为历史版本。

1.1.0 在已冻结 1.0 基础上增加书签列表、添加/删除/点击定位与笔记导出，形成可操作、可验证的差异。原数据 schema 预先可保留书签字段；旧 UI 不提供新功能，不能仅改版本号冒充升级。最终源码只维护当前实现，历史源和二进制以明确快照保留。跨版本验收先保存旧版业务数据，再经原 Session/ModuleManager 装载新版本，并核正文、位置、笔记及新增功能；不绕开平台事务。

1.1 同时提供明确的“按页附注”页签，用户手动录入引文及注释，添加到当前页，可删除或双击定位；不声称系统 PDF API 提供了文本选择/抽取。导出由 ReaderLibrary.exportNotes 原子生成 UTF-8 Markdown，拒绝库内路径、链接与 PDF 目标，不清除尚未保存的书库标识。

外部第一份 1.0 二进制实际 GUI 测试发现 Windows.Storage 不接受 Qt 规范化的正斜杠绝对路径，真实 HRESULT 为 0x800700a1。保留失败树后，仅在 `1.0.0-native-path` 新外部树修复适配边界的 QDir::toNativeSeparators；逐文件确认只改对应 include/调用两行，再以修正版本参加真实验收。原失败二进制不标为通过，也不让 1.1 功能混入修正 1.0 来源。

## 验证边界

插件在 `D:/WorkSpace/P7LiteratureReaderExternal/` 的独立源码快照消费安装 SDK 构建，不借用仓库私有头。验证真实多页 PDF 图像、不同页面的像素/页码、导入失败、中文路径、大文件取消、保存冲突后内存保留、保存重开、多实例根隔离，以及 worker 在加载/渲染时关闭的完整退出。端到端测试由 `tests/p7/` 通过真实控件和宿主执行，不创建第二份业务存储或假 PDF 解析器。
