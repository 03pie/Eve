# P7 文献阅读运行包

日期：2026-11-24。此交付与 P6 preview.3 和源码 SDK 分开；不覆盖或重新打包 P6 原产物。

## 输入与职责

`tools/package_reader.py` 仅收集已经构建的五个宿主文件：`cordis_plugin_workbench.exe`、`cordis_core.dll`、`cordis_qt.dll`、`cordis_ui.dll` 和 `ElaWidgetTools.dll`；来源为独立 `build/p7-reader-release/bin`。插件来自仓库外使用匹配 SDK 构建、经过实际测试的最终 1.1 目录，只取其真实 `module.json` 和所指 DLL 到 `plugins/current/`。最终输入路径、大小和 SHA 在执行前以外部锚定清单冻结；工具不 configure/build、重链或变更批准源。

Qt 5.15.2、四份 CRT、五个 Qt 插件以及原许可材料从固定 SHA `41cd2aa9…` 的 P6 preview.3 ZIP 读取，并与其固定 `fdff0203…` manifest 的逐项长度/SHA 对照；保持实际 `plugins/{platforms,styles,imageformats}` 与 `qt.conf` 的路径，不混入另一开发机的 Qt 或依赖。新包不用 P6 Native 资格来批准文献插件，也不携带旧业务示例模块、Python、SDK、头文件、编译器或测试程序。PDF 后端使用系统 Windows.Data.Pdf，不分发 Poppler 或额外 PDF 二进制。

## 输出与验证

新目录下可直接双击 `cordis_plugin_workbench.exe`，默认模块相对应用目录为 `plugins/current/module.json`。业务 workspace 使用宿主普通用户数据位置，或用户显式传入的 `--workspace` 可写目录；运行包不含用户文献、旧 session、个人偏好或预填凭据。中文运行说明放在包根，写明导入 PDF、显式保存/重开、默认数据位置、冲突重载及现有限制。

复制前后验证每个源身份与 SHA。PE 检查仅解析静态导入和 delay-import，要求全部非系统依赖在新包中闭合，记录系统依赖；不通过加载 DLL 来检查。新 manifest 保存每项相对路径、长度、SHA、角色和真实来源，不赋予 Native 卸载或签名信任。包内校验脚本只读校验清单和完整文件集合，不启动程序。

只创建不存在的新目录/ZIP/旁置校验摘要，路径拒绝重解析点及输入/输出重叠。ZIP 完整展开到独立新目录后逐文件核对集合、长度、SHA，前后源再次核对。最终记录只表示静态包校验；实际 PDF 阅读、文献状态重开和自然退出由主流程对这个新包验证，不提前写通过。

## 已执行构包与验包

宿主五件按 `build/p7-reader-acceptance/host-file-hashes.json` 冻结，EXE 为 `e985c0f8…`；四个 shared runtime 与 SDK `58e0a1e0…` manifest 完全相同。插件来自仓库外 `P7LiteratureReaderExternal/1.1.0/build/module/literature_reader`，DLL 为 `024ddff4…`，manifest 为 `fd150e2b…`；该外部构建和十项模型回归均实际退出 0。

新包位于 `D:/WorkSpace/delivery/Cordis-P7-literature-reader/Cordis-literature-reader-1.1.0-windows-x64-2.zip`，14,993,597 字节，SHA256 `42944e8dcc4bb6d68114259c6c9649277140442783eabfa5c0a12c53a60f797c`。包内 120 文件，展开长度合计 35,910,952 字节；全部展开后逐集合/长度/SHA 核对通过，PE 静态依赖闭合，源身份/SHA 前后不变。`reader-manifest.json` SHA 为 `f5bf67706fc9be70c4519ff47f0716a277a6adc29d020a86f614b0ddaf1c7bcf`。包内实际 PowerShell 校验进程 19196 正常退出 0，核 118 payload 加两份元数据，未执行产品。

原命令、真实打包进程 29464/退出 0、stdout/stderr、输入清单和校验入口回执保存在 `build/p7-reader-runtime-packaging-20261124-2`。四项打包回归覆盖冻结源拒绝、真实 Qt 部署路径、ZIP 危险条目和包内 PowerShell 的正确/篡改路径。

首轮路径假设错误（`qt-plugins` 对实际 `plugins`）的失败原件和 partial 目录保留，不改写为成功；修正沿原 `qt.conf`，增加成员集合预检，使用全新 suffix-2 输出。准备脚本初次版本根定位失败也留在首轮 control 的原件中，发生在创建输入清单或启动打包程序之前。

这份静态构包回执保留当时的 `runtimeAcceptancePending` 值。随后在同字节 ZIP 解压物上完成的实际用户流程、两次产品桌面启动、依赖来源和退出记录见 [P7 验收](p7-acceptance.md)。
