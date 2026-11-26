# Cordis Windows 平台独立包

## 交付边界

本轮交付 Windows x64 本机内部预览。工作台入口支持在空工作区启动。运行包只携带平台、日志查询工具、Qt/Ela 及应用随附的运行依赖和许可材料。用户工作区由工作台在可写位置创建。业务模块在独立目录交付，可由宿主的 `--module` 显式加载。

## 构包结构

构包工具消费同一 Release 目录内的工作台、日志查询、Core、Qt 封装、UI 和 Ela 六个二进制。工具记录输入文件的身份、长度和 SHA-256，在复制与包闭合后重验。工作台支持 WebP 背景，因此同一 Qt 开发套件的 WebP 图像格式组件和对应的 Qt 官方许可材料纳入包。Qt Core、Gui、Widgets、基础图像格式组件及 CRT 来源为 P6 的固定部署文件。三个主要 Qt DLL 与当前 Qt 开发套件逐一核对 SHA-256。旧包仅作为固定的运行依赖来源。

工具使用已有的 Windows 安全路径校验、PE 导入分析、文件清单和逐字节核验。输出为全新目录，其中包含 ZIP、包内容的实际展开、ZIP SHA-256 和独立校验回执。PE 检查验证应用和依赖为 x64，非系统导入均在包内，并记录实际声明的系统依赖。完整解包验证文件集合和 SHA-256。命令中指定 Qt Image Formats 5.15.2 的官方源码原件，用于提取 WebP 组件的版权和许可。[Qt 源码与原始校验值](https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtimageformats-everywhere-src-5.15.2.tar.xz.mirrorlist)

构包命令从仓库根目录执行。目标目录须为新目录。

```powershell
$preview = 'D:/WorkSpace/delivery/Cordis-preview3-20261123-1/Cordis-0.1.5-preview.3-windows-x64.zip'
$imageFormats = 'build/platform-closeout-deps/qtimageformats-everywhere-src-5.15.2.tar.xz'
$output = 'D:/WorkSpace/delivery/Cordis-Platform-20261126-1'
python tools/package_platform.py --host-root build/ela-msvc-release/bin `
    --qt-root build/Qt/5.15.2/msvc2019_64 --deployment-archive $preview `
    --imageformats-source $imageFormats --output $output
```

## 运行验收

静态记录和本机运行记录分别保留。本机使用不含开发套件的进程环境，在独立的新目录中启动实际解压工作台并检查日志查询工具。记录程序和依赖来源、进程退出、空工作区及含已有配置的工作区行为。发布仍属于当前的内部预览范围。
