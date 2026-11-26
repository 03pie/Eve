Cordis 平台 · Windows x64 内部预览

解压整个 ZIP 后双击 cordis_plugin_workbench.exe。首次启动显示空工作区，
默认数据位置为 %LOCALAPPDATA%\CordisPluginWorkbench\platform-workspace。
可在独立目录中打开工作区，例如在解压目录的 PowerShell 中执行

  .\cordis_plugin_workbench.exe --workspace "D:\Cordis 工作区"

工作台的输出面板可筛选、查找和导出日志。关闭工作台后，仍可以用包内的
只读工具列出同一工作区中的运行和查询历史事件。

  .\cordis_log_query.exe --workspace "D:\Cordis 工作区"
  .\cordis_log_query.exe --workspace "D:\Cordis 工作区" --run "<运行 UUID>" --event 2

需要使用独立安装的受信任 Qt 模块时，以 --module 指定其完整清单路径。
打开原来已配置该模块的工作区时，提供原模块清单。旧工作区无需搬入运行包。

  .\cordis_plugin_workbench.exe --workspace "D:\Cordis 工作区" --module "D:\外部模块\module.json"

build-manifest.json 列出包中的运行文件、用途和 SHA-256。SHA256SUMS.txt
覆盖当前目录的包文件和清单。第三方许可及 Qt WebP 补充材料分别位于
licenses 和 licenses-qtimageformats。包内容可与交付目录旁的 ZIP 摘要核对。
