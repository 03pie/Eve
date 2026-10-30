# Eve 软件图标

采用用户提供的 `Eve_Studio_完整设计合集` 中的最终纯色蓝版本，主色为 `#2864C5`。

- `eve-master.svg`：原样复制 `01_最终主稿_纯色蓝/eve-solid-blue.svg`，保留正式矢量母版供追溯。
- `eve-16.png` 至 `eve-256.png`：原样复制 `05_不同尺寸_透明PNG` 提供的七档透明 PNG。
- `eve.ico`：将上述七个 PNG 原始字节封装为 Windows ICO，未重新采样、着色或绘制。
- `provenance.json`：原始相对路径及 SHA-256，复制后的资源已与原稿逐项核对。
- `eve_brand.qrc`：Qt 内嵌 PNG，用于默认应用图标、窗口/任务栏图标和 Ela 导航品牌位。
- `eve.rc.in`：桌面入口 EXE 的 Windows 图标资源模板，统一由 cmake/EveBrand.cmake 接入。

来源归档位于只读 `sources/Eve_Studio_完整设计合集`；正式构建仅使用本目录的已复制资源，不依赖该本地归档存在。原稿、字体组合、历史渐变和动画均未更改。

需要重新封装 ICO 时，在仓库根目录运行：

```powershell
python tools/package_eve_icon.py
```

脚本只使用 Python 标准库；常规 CMake 构建使用已入库的 ICO，不需要 Python。UI 库显式注册静态链接的 Qt 资源，新增的公开 SDK 依赖为零。宿主已显式设置应用图标时沿用宿主选择；默认桌面入口采用本套 Eve 图标。图标替换不修改应用名、配置路径、插件业务图标或已保存目标。

当前三个入口为 cordis_ela_demo、cordis_config_workbench 和 cordis_qt_workspace_demo。可用以下只读检查确认 PE 的 RT_GROUP_ICON/RT_ICON 资源与正式 ICO 七帧逐字节相同，不需要启动程序：

```powershell
python tools/check_eve_icons.py build/ela-msvc-release/bin/cordis_ela_demo.exe build/ela-msvc-release/bin/cordis_config_workbench.exe build/ela-msvc-release/bin/cordis_qt_workspace_demo.exe
```
