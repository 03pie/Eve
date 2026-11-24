Cordis 文献阅读器（Windows x64 技术预览）

将整个 ZIP 解压到一个普通目录，双击 cordis_plugin_workbench.exe。
无需安装 Visual Studio、Qt 开发包、Python 或源码 SDK。请保留 plugins/current、
plugins/platforms、plugins/imageformats、licenses 和相邻 DLL 的相对布局，不要只复制 EXE。

进入“文献阅读”页面，导入本机 PDF。导入会复制 PDF 到用户工作区，原文件不改动。
可以翻页、调整缩放、添加按页批注和书签、编辑笔记，使用“保存”显式提交。
关闭后再次打开同一工作区，可恢复文献目录、阅读位置和已保存内容。
默认工作区由宿主选择在当前普通用户的本地应用数据位置 CordisPluginWorkbench。
需要单独工作区时，可运行：
  cordis_plugin_workbench.exe --workspace "D:\我的文献工作区"
工作区必须可写；它与本运行包分开。请自行备份整个工作区，包括 library 下的
library.json 与 objects。不要把未保存的编辑当作已经写入磁盘。

同时打开多个实例时建议使用不同工作区。同库出现冲突时，程序不会覆盖另一实例
的提交；未保存内容保留在当前窗口。确认需要丢弃本窗口编辑后再明确重新加载。
保存失败时请保留窗口，修复可写条件后重试。不要删除 objects 中看似未引用的文件。

首版支持 PDF 和按页批注；引用文本由用户录入，不声称从 PDF 自动选取或精确高亮。
PDF 解码与渲染使用系统 Windows.Data.Pdf，需要兼容的 Windows 10/11 x64。
加密、损坏或系统不能解码的 PDF 会显示明确错误。单篇导入上限 512 MiB。
现版本不提供云同步、格式转换、OCR、文献检索或自动对象清理。

校验：交付 ZIP 的 SHA256 见相邻校验摘要。解压后可用 Windows PowerShell 执行：
  powershell -NoProfile -File .\verify-reader.ps1 -ExpectedManifestSha256 <交付摘要中的manifestSha256>
校验脚本只读比较完整文件集合、长度和 SHA，不启动阅读器。SHA 一致性不是数字签名。
SDK、业务插件版本和运行包分别管理；此包只含已选定的 resident Qt 阅读插件。
第三方许可和取得资料在 licenses；本包范围为内部技术预览。
