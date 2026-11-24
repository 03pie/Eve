# Cordis 文献阅读插件

在 Cordis 的「文献阅读」页导入本地 PDF，可翻页、缩放、继续上次阅读，并保存每篇文献的笔记。1.1 增加书签、按页附注和 Markdown 导出。引文由读者自行录入。点击「保存书库」将当前文献库的改动写入磁盘。

## 阅读与记录

1. 从工作台导航进入「文献阅读」。点击「选择 PDF…」，或在路径框输入本地 PDF 的完整路径，再点击「导入书库」。原文件保持不变，书库保存独立副本。
2. 在左侧书库选择文献，用「上一页」「下一页」、页码框和百分比缩放控件阅读。右侧「文献笔记」记录整篇笔记。
3. 在「书签」页签输入可选名称，点击「为当前页添加书签」。单击列表条目跳到对应页；选中后可删除。
4. 在「按页附注」页签手动填写引文和附注，点击「添加当前页附注」。双击列表条目跳到对应页。此功能不提供 PDF 文本选择或自动抽取。
5. 在导出路径框填写书库之外的绝对 `.md` 或 `.txt` 路径，再点击「导出笔记」。导出包含当前内存中的笔记、书签和附注，不修改 PDF，也不代替「保存书库」。库内文件、链接或 PDF 目标会被拒绝。

关闭或升级前点击「保存书库」，并确认状态栏显示已保存。失败时更改仍留在当前窗口；不要以关闭窗口代替保存。「重新载入」会询问是否丢弃尚未保存的更改。

## 在仓库外构建

将本目录单独复制或解压到开发目录。在与交付 SDK 匹配的 x64 Visual Studio Developer PowerShell 中构建。以下命令假设 Cordis SDK 已解压到 `D:/CordisSdk`，Qt 5.15.2 开发包位于 `D:/Qt/5.15.2/msvc2019_64`。Qt 需有 Widgets 私有开发头，Windows SDK 需有 C++/WinRT 的 Windows.Data.Pdf 头和 WindowsApp.lib。SDK 使用的 MSVC 版本记录在其 CMake 配置中。

```powershell
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL "-DCMAKE_PREFIX_PATH=D:/CordisSdk;D:/Qt/5.15.2/msvc2019_64"
cmake --build out
$env:PATH = "D:/Qt/5.15.2/msvc2019_64/bin;" + $env:PATH
ctest --test-dir out --output-on-failure
```

同一次构建会在 `out/module/literature_reader/` 生成 DLL 与 `module.json`。用交付的工作台打开这组文件。为开发验证选择单独的可写工作区，示例命令如下。

```powershell
& 'D:/CordisReader/cordis_plugin_workbench.exe' --module "$PWD/out/module/literature_reader/module.json" --workspace 'D:/ReaderTest'
```

## 数据与退出

工作台的 `session.yaml` 描述实例。每个实例的 `dataRoot` 指向文献库。保存后，`library.json` 记录阅读位置、笔记、按页附注和书签，`objects/` 保存导入的 PDF 副本。备份和恢复时复制整个文献库。遇到写入冲突，在放弃本窗口编辑前先保留笔记内容，再重新加载另一实例保存的版本。

从旧版迁移时，先保存文献库、关闭工作台并备份工作区，再启动新版，选用原工作区。插件为进程驻留的 Qt UI 模块。业务数据格式版本与 DLL 版本分别维护。
