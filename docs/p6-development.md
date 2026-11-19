# P6 发布与交付工作单

日期：2026-11-19。目标：完成[后续路线](post-p5-roadmap.md)定义的 P6，而非只在本机产生一个 ZIP。原 P5 基线 1f79cba，实现 fdf099c；SDK 0.1.5。合同见[ADR-0057](adr/0057-portable-preview-delivery.md)，逐要求出口见[P6验收表](../tests/acceptance/p6-delivery.md)。

## 当前状态

P6-0 已以 dbe8add 审查提交同步并核 clean；P6-1 同字节包与独立验包已通过审查，随本段提交归档，P6-2–5 尚未实施。用户明确选择内部技术预览、暂不公开发布，并可在另一台干净 Windows 电脑验收。包可测试后再衔接机器执行，不将本机清 PATH 当作另一台机器证据。

| 小阶段 | 状态 | 当前证据/剩余工作 |
|---|---|---|
| P6-0 交付契约 | 已完成 | dbe8add；D01 通过，独立审查及提交同步/clean 已核 |
| P6-1 同字节运行包 | 审查通过，随本段提交 | 134 文件/16.64 MB、24+9 工具测试、独立实际验包和整段审计；运行闭包仍属 P6-2 |
| P6-2 包级验收 | 未开始 | 待候选包本机负向/搬迁/ACL与独立机器验证 |
| P6-3 更新恢复 | 未开始 | 待实际包基础上的迁移设计、真实升级/中断/恢复验证 |
| P6-4 分层 CI | 未开始 | 待可执行编排和实际运行记录，普通构建不自动批准 |
| P6-5 最终交付 | 未开始 | 待最终包/原件归档、独立机器人工验收与整体审查 |

## P6-0 架构盘点

现 `examples/qt_workspace/main.cpp` 与配置入口已分离应用目录和用户数据；业务入口的 `businessArtifacts()` 每次验证六 A/B。`ModuleManager`、嵌入名单、Session/Store、升级及关闭链保持唯一权威，打包没有理由修改生产源码或 SDK。当前配置入口持久化外观，业务入口 shell 未传偏好文件；按现行为写准，不承诺不存在的跨进程草稿/设置能力。

第一方源许可证未选，内部预览不替用户决策；Ela/LibYAML 上游 MIT 原件存在。Qt 5.15.2 kit 的运行库/插件存在，尚未找到 windeployqt；应以实际依赖固定闭包同字节复制，并补当前 Qt/渲染/图像/CRT 材料，不能拿另一个 Qt6 文档目录冒充 Qt5 材料。

已读 PE imports：两个入口、四 Cordis/Ela 库、三 Qt 库、六业务 DLL 均为 x64。插件候选为 qwindows、windowsvistastyle、qjpeg/qgif/qico；GL/ANGLE 动态路径仍须核实。至少需要 MSVCP140、MSVCP140_1、VCRUNTIME140、VCRUNTIME140_1，正式 VC143.CRT 候选实际版本 14.44.35211.0，与本机系统 14.50.35719.0 不同，列为 P6 新环境。品牌和 Ela 资源已嵌入 qrc，不复制松散图片；ElaAwesome 字体及两嵌入图片的来源/单列许可材料尚待整理，不提前称全部第三方材料齐备。

本机注册表标为家庭版、构建 26300，尚未发现可用 WindowsSandbox/已配置虚拟机；不启用系统功能或重启来代替用户提供的第二台机器。已查官方 Qt5 部署和 Microsoft CRT/Sandbox 文档；事实与来源链接列于 ADR，最终仍以实际文件/运行证明。

CI 只有现 presets/dev/desktop/公开资格工具，没有 workflows；P5 最终静态 Qt 目录用了显式 `CORDIS_SHARED_CORE=OFF`、关闭 Ela/示例，不能仅凭同名 preset 声称重现。ASan 无现成 preset。P6-4 需要薄编排和准确矩阵，不搬运 ignored 历史脚本当公共工具，也不更改业务/卸载语义。

P6-0 独立复核通过，无剩余架构阻断；六份文档 UTF-8/LF/末尾换行、369 个本地链接目标及 `git diff --check` 通过。此段只有文档，没有生产源码/SDK/批准/产物修改，无需重建或重跑 P5。下一动作：精确提交同步并核 clean，随后进入 P6-1 最小同字节包工具、实际闭包和许可材料。

## P6-1 实施前边界

新增单用途 `tools/package_preview.py`：显式消费 build/Qt/CRT 目录、P5 实物证据和受审源，校 profile/实际 PE 依赖及复制前后摘要，只在新输出目录生成包/清单/ZIP；失败不替换旧包，不调用构建器。PE 只读解析有真实包消费者和负例测试。运行包静态材料集中 `delivery/windows/`，不混入生产库或公开 SDK。

包清单 schema 为 `cordis.preview.package/1`；记录 `deliveryVersion`、`applicationVersion`、`scope`、`platform`、`profile`、`qualification`、`files`。每项文件有 `path`、`role`、`bytes`、`sha256` 和 `source`；PE 项另含实际导入及机器类型，来源路径用于追溯而非运行。`files` 排除清单及其旁置 `SHA256SUMS.txt`，后者覆盖所有载荷和 build-manifest，自身摘要由外部 ZIP 摘要承担。`verification/verify-package.ps1` 是无 Python 的独立 .NET/PowerShell 校验器，核路径、重复、完整文件集合、长度及 SHA；不读取外部源文件或提供批准。它与打包工具分别校验，便于在干净机器使用。

第一版既有受审产品/六 A/B/七依赖逐 SHA 锚定到 P5 原件；额外 Qt 插件、渲染/图像/CRT 和许可材料是明确新增部署文件，不能冒称 P5 全环境不变。原资格 raw bundle 与后续验收分别归档，运行包清单不成为第二批准源。包级行为、机器验收和更新恢复仍是后段出口，P6-1 不提前宣布这些通过。

## P6-1 实现与验证（审查通过）

交付冻结索引只引用四份原 P5 记录及摘要，不重列批准行。工具核原 939 项来源保持、21 件产品记录、配置入口、匹配四 Native/六 A/B、实际 CMake/Ninja/入口 /MD Release 及 PE 搜索闭包；复制前后核文件身份/时间/大小/SHA，归档前后核准确集合、元数据和逐项解压内容。源/目标/证据变化、混 profile、未解析依赖、路径逃逸、联接和重复均拒绝；新目录失败保留未完成标记，不替换旧包。已删除审查发现的全局 DLL basename 匹配及写后重新定义预期摘要的错误路径。

运行材料包含 Qt5 原 LGPL/GPL 及第三方说明、Ela/LibYAML MIT、官方 CRT 材料及准确 Qt5 源码获取方式，共 95 件材料和一个 sources 索引；逐文件集合/长度/SHA 已核。原文按 `.gitattributes` 保留原字节/空白；第一方工具仍执行规范检查。四项未决边界见[材料说明](../delivery/windows/licenses/README.txt)和[来源清单](../delivery/windows/licenses/sources.json)，内部预览不替代公开发行许可决定或来源安全认证。

实际第二次候选位于 `build/p6-1-preview-2/Cordis-0.1.5-preview.1-windows-x64/`，134 文件，ZIP 16,641,343 字节；[pack回执](../build/p6-1-preview-2/packaging.json)与[命令记录](../build/p6-1-records/pack-2.json)明确 runtime/cleanMachine 尚未通过。ZIP SHA `f57d830a0853133c2f567a9df4dfb2c6323ea2696e374141115be003840f8a5c`，manifest SHA `965e8c37704834216068672724086d6fdfa784864a8e435bd346dc6ca614af03`。第一候选 pack-1 原件保留；审查修正通用负例缺口后以 pack-2 重做，不将旧工具的成功当新版证明。

根工具最终 24/24 临时用例通过，实际原件见[独立命令与源码摘要](../build/p6-closeout-agent-packaging-units-final-1.json)及[原日志](../build/p6-closeout-agent-packaging-units-final-1.log)。PS5.1 校验器最终 9/9 通过并归档[命令/输入摘要](../build/p6-1-records/verifier-units-final.json)和[原日志](../build/p6-1-records/verifier-units-final.stdout)。同工具 SHA 的实际 pack-2 再使用包内复制的校验器、外部准确 manifest 锚点，独立验证 132 payload+2 清单通过，见[真实验包](../build/p6-1-records/verify-real-2.json)及[命令](../build/p6-1-records/verify-real-2-command.json)。临时 PE fixtures 不加载库，负例不改 P5 原件，不启动产品，不重建。

复现打包（开发机，Python 3.10+）：

```powershell
python tools/package_preview.py --qt-root "E:/Qt/5.15.2/msvc2019_64" --crt-root "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC/14.44.35112/x64/Microsoft.VC143.CRT" --delivery-version 0.1.5-preview.1 --output build/p6-preview-new
```

`--output` 必须尚不存在；非本机来源用显式 `--build-dir`、`--p5-root` 指向同一冻结实物及原证据。该入口不 freshbuild、不补批准，不以此宣称位级可重复构建。干净机器只需运行包内 PowerShell 校验器，不需要 Python 或原来源路径。

[独立整段审计](../build/p6-1-records/independent-stage1-audit.json) SHA `de5b21196cb3d3c834ae33f90d9754b031ff6c3159f4176c62b079afc597440a`，实际重核 ZIP/清单/源件/许可与 24/9 原日志，无代码或证据阻断。D02/D04/D05 通过；D03 仅静态部署闭包及材料部分，实际运行留 P6-2；D06–D16 未提前通过。生产代码、SDK、批准源及既有 DLL 均未修改，无需重建 P5。文档/精确 stage 检查和同步后才进入 P6-2。

提交前核 110 个精确文件，第一方编码/换行、Python AST、JSON、429 个文档本地目标及 staged diff 通过；许可原件 index 与工作区字节相同，PS 按既有 LF index/CRLF checkout 契约核对。未 stage 生产、SDK、qualification 或只读 sources 文件。
