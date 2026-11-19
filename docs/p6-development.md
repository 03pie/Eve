# P6 发布与交付工作单

日期：2026-11-19。目标：完成[后续路线](post-p5-roadmap.md)定义的 P6，而非只在本机产生一个 ZIP。原 P5 基线 1f79cba，实现 fdf099c；SDK 0.1.5。合同见[ADR-0057](adr/0057-portable-preview-delivery.md)，逐要求出口见[P6验收表](../tests/acceptance/p6-delivery.md)。

## 当前状态

P6-0 已以 dbe8add、P6-1 已以 9af5d4d 审查提交同步并核 clean；P6-2 本机验证与交接工具已完成，独立机器 D11 尚待执行，P6-3–5 尚未实施。用户明确选择内部技术预览、暂不公开发布，并可在另一台干净 Windows 电脑验收。包可测试后再衔接机器执行，不将本机清 PATH 当作另一台机器证据。

| 小阶段 | 状态 | 当前证据/剩余工作 |
|---|---|---|
| P6-0 交付契约 | 已完成 | dbe8add；D01 通过，独立审查及提交同步/clean 已核 |
| P6-1 同字节运行包 | 已完成 | 9af5d4d；134 文件/16.64 MB、24+9 工具测试、独立实际验包和整段审计；运行闭包仍属 P6-2 |
| P6-2 包级验收 | 本机通过，跨机待执行 | 本机四进程/加载路径/负例/只读与恢复已核；交接包及准确命令就绪，D11未完成 |
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

## P6-2 实施前设计

继续使用普通入口，不借测试 API 改 Workspace/Session 或预写 B 配置。运行包和数据的副本均位于新的专用验收目录；所有破坏性负例只作用于该副本。当前 token 实测 Medium、管理员组 deny-only，可验证本机普通权限，不创建用户/更改系统功能。本机不充当干净机器。

记录层与交互层分开：随包的 PowerShell5.1 记录器只启动本轮明确 exe、采 OS/token/环境、窗口/加载路径、独立 stdout/stderr、退出后文件和保存定位，两个入口各首次/重开共四进程串行；它不自动点击、关闭或杀进程，不因用户回执或最终 B 推断中间操作通过。有限共享 Windows 观察函数由实际记录器/负例消费者复用；宿主侧 OS-UI 驱动另用 Python/PS 操作本轮 PID，重找实际控件和前台/焦点，保存 Applied/dirty/Save、冲突/rebase 与草稿/贡献换代原件。用户操作与自动动作分别登记。

子进程仅保留所需 Windows 用户环境、系统 PATH，清理 Qt/QML/开发路径污染；不靠设置 QT_PLUGIN_PATH 掩盖 qt.conf。加载期间只观察真实路径/base/size，退出后校包/store字节。记录器有总时限和记录预算；超时/不可观察只写未完成并保留活进程，不据此重复启动或算正常关闭。业务首退未保存准确 A/A/B 即不重开；配置首退必须有 formatVersion1 原文件，同参数新 PID 后仍需用户按原语义应用。

配置 `--file` 只隔离 YAML；其偏好仍写当前用户 KnownFolder 的 CordisConfigWorkbench/ui-preferences.json。环境变量改名不能声称 Qt KnownFolder 已隔离：记录实际路径与前后摘要，允许该明确用户数据位置，不删除/覆盖用户原偏好，不与其他配置实例并行。业务入口 shell 无偏好文件。`--smoke` 会写应用目录截图且不接受 --file，不能用于只读/指定数据验收。

只读应用副本用真实 DACL 赋当前 token RX，保存原 SDDL 并以同 token 实际 create/write/delete 拒绝证明有效；退出后仅恢复本轮副本 ACL，不清理原件。不可写数据测试必须观察真实 Save 失败、旧配置保持与窗口/dirty保留，恢复 ACL 后再通过原 Save/Close，强杀不算成功。启动失败用例继承受控 ErrorMode 抑制系统错误框；超时仍未确认。缺件/篡改/声明摘要错配/ABI major2各留独立副本、准确退出及旧数据摘要。

干净机器不安装 Python，D07 的副本创建/登记/保护/探测/恢复也必须能通过随包 PowerShell 完成。ACL 工具只保留一个规范实现，放在 `verification/preview-acl.ps1`，用明确 Prepare 动作验外部 manifest 锚后创建新范围和应用/数据副本；开发机负例工具调用它创建登记，删除原 Python 的重复 ACL 登记实现，不保留第二份脚本。记录与日志仍放可写范围，不能把整个 DataRoot 设只读；脚本始终从原验证来源运行，只对其新建副本改 DACL，不更改原包或用户数据。

另发现配置入口使用 narrow argv/fromLocal8Bit，而业务入口使用 Qt Unicode arguments。当前系统 ACP936，只凭普通中文成功不能泛化其他字符/地区；先对有效扩展中文路径做真实参数验证。若确认宿主缺陷，先记录最小 Qt CLI 统一及新的宿主来源/产物证据方案，只修具体入口、复验并保持原 Native/七依赖；不能为了保持旧冻结脚本而绕过缺陷，也不能默许新字节使用旧宿主证据。

实际 PID18788 的原包 Unicode 探针已退出1，stderr 为 configuration read failed，输入文件未变。实施前修复与证据取舍见[ADR-0058](adr/0058-reviewed-configuration-host-fix.md)；只将配置入口统一到已有 Qt Unicode 参数机制，新宿主单独重验，P5 原件/6c 不回填。

新 app-local CRT 环境另以原已测 D1 exe 作私有 QA 组合验证：只把该测试 exe 放入独立的当前包运行库副本，不进入运行 ZIP；原测试继续原 8+100、完整 MEM_IMAGE/独占文件及零新增阈值，source 指当前六 A/B。测试不重建、不补批准，实际观察其 Core/Qt/Ela/CRT 加载路径以确认环境；此证据补强本次部署，不把组合目录或单一 D1 等同完整公开资格流程。

## P6-2 早期实测与失败历史（原件保留）

配置宿主已按 ADR-0058 修复，Release/Debug/ASan 仅重建该目标，各原 smoke 两例通过；57 项产品/模块/运行库前后实际 SHA 完全相同。扩展中文路径下，8516 完成实际 OS UI 编辑/检查/应用 R1/显式保存并 exit0，1432 同文件新进程读到标记并 exit0；三份保存原件同 SHA。历史观察器选择器、异步控件/暂态 UIA 错误保持未通过原件，在同8516续做，不把它们当应用缺陷或虚构重启。详见[实际聚合](../build/p6-2-host-fix/unicode-completion.json)和[采用前独立审查](../build/p6-2-host-fix/independent-host-audit.json)。受审[新宿主输入](../delivery/windows/verification/configuration-host.json)只绑定该源/exe，原P5与6c不改。

新 CRT 下原 D1 也已实际通过：PID7332，原 test exe `9a399b13…`，8+100、108个唯一操作、111代次、最终163及原零新增阈值保持；每轮实际断言完整 MEM_IMAGE 离开与独占文件，50份同PID样本同时见七DLL/四CRT/qwindows均来自QA副本，实际CRT14.44.35211.0。134项旧包载荷及QA副本前后逐SHA相同。独立[审计](../build/p6-2-physical/independent-audit.json) SHA `63bc3af2169faefb526ec3ecd30de03f9244ce1027e496b1a6ac770d82566cae`；此组合中旧Config未执行，不冒称新版正式包/完整27资格/产品UI或干净机器通过。

随包 stock PS ACL Prepare/Protect/Probe 在真实139件候选副本完成，普通 token 实际 create/write/delete 权限均拒；原包未改。首轮记录器启动16896后，旧 Toolhelp 路径把有效非BMP字符转成字面 `??`，PS5.1的严格GetFullPath因此正确拒绝。记录器保留未完成、未杀目标，随后[正常关闭原件](../build/p6-2-records/failed-recorder-product-normal-exit.json)确认16896 exit0，副本DACL实际恢复；这轮不能算桌面流程通过。

采集边界已用受控无GUI cmd 双路路径/代码点证明，按同PID/同模块句柄读取真实宽路径，保持原wrong/missing/四Native等门禁，不替换字符串或拼预期来源。原386a ASCII D1原件保留其范围。当时决定下一候选只采用独立复审及边界测试后的采集实现；此前候选与权限恢复原件保留，不用旧SHA冒新源码。当时完整四窗口流程、不可写保存与独立机器仍待验证，后续本机结果及当前D11缺口见下节；P6-3–5未开始。


五个独立部署故障已实际拒绝，见[故障原件](../build/p6-2-failures-1/report.json)（SHA `fd7f13af1de998d2652537df907940515b44aaf6bde998ebaa87d054eb718f3e`）：缺 Counter B、篡改 Counter B、manifest 摘要错配和 ABI 不兼容均 exit1，缺 Core 实际退出 `0xC0000135`；各副本配置/store 前后逐件相同，原 P5 已接受 A/A/B 数据不变。该批使用第三候选，未执行其中录制器；后续版本须逐件核业务入口/七库/六 A/B/CRT 相同后才可关联此范围，不将旧整包摘要冒作新包实测。

第三候选的配置 PID9460 已由真实 UI 完成保存/冲突/采用基线，但记录器因 stock PS5.1 把 `File.Replace` 的普通 null 绑定成空备份路径而提前失败。UI 关闭请求后进程已消失，但原观察句柄已释放，退出码未知；保留未完成，不能以 UI 驱动 exit0 推断产品 exit0。实际 BMP/非BMP八组绑定诊断定位后，状态写入改为 typed null，并只清理本次 CreateNew 成功拥有的临时文件；已有临时文件不删除，原错误与二次状态错误分开保留。独立复审与四项实际临时故障测试通过，原失败记录不改写。

记录器的资源拥有者还须在报告/日志写失败时无条件释放观察句柄并保留原始错误；状态文件仅用于进度，最终完成只认最终 recording 原件与记录器实际 exit0。两份文件不构造虚假的共同原子提交。桌面 UI 驱动使用明确父菜单和实际 UIA 作用域；Ela 搜索的自绘候选可无 accessible Name，只有精确查询、焦点及对应弹出列表的唯一实际子项才允许 Enter，进入后继续核 Notebook 编辑器和命令所属代次，不修改冻结的 Ela/产品 DLL 来迁就验证器。


实测又发现 Windows 路径分类说明采用区分大小写的相对字符串比较，导致 Win32 返回的大写 CRT 文件名被标作 unlisted-package-path；关键十二件运行库门禁原本就用不区分大小写的完整路径核对，未漏掉 CRT。本次只将位置分类统一为既有 Windows OrdinalIgnoreCase 语义，保留所有关键 missing/wrong 检查；用同份实际模块快照和临时大小写/越界负例复核。此为观测说明修正，不改模块装载、批准、路径来源或冻结运行库。


## P6-2 本机验证与跨机交接

本机实际完整四流程使用第五候选，ZIP `eb13fde7…`、manifest `27a06e46…`，stock PS5.1先解压、移动安装目录后，再通过原ACL工具建立有效RX副本。四个产品进程 `17416 / 7176 / 19060 / 8216` 与记录器 `6076` 均实际exit0；[完整控制记录](../build/p6-2-records/desktop-controller-5.json)持独立句柄交叉确认，产品原件在[记录目录](../build/p6-2-只读验收5𠮷/桌面记录%20中文𠮷/recording.json)。配置V1→V2、外部冲突→采用基线→显式保存与同路径重开，以及五步升级各Applied/未保存/Written、Notebook草稿/新命令代次/按钮与命令输出、A/A/B重开均由真实UI记录；无测试API设置业务状态，也无强杀。本轮4次UI驱动均一次完成，未借用此前第四候选的续做结论。此前候选的失败历史、第四候选的三次UI选择器Unknown与显式同PID续做原件保留；第五候选无续做。

不可写数据用独立同字节宿主PID11852：有效DACL探针返回创建/写入/DELETE权限拒绝5；R1应用后Save实际失败，原磁盘、编辑目标与修订保持。原登记来源恢复DACL后，同一R1显式Save成功、正常关闭且Popen实际exit0；seed、宿主、原139件包与helper保持。见[实际报告](../build/p6-2-readonly-data-evidence-1/report.json)与[独立审计](../build/p6-2-readonly-data-evidence-1/independent-audit.json)，审计SHA `f5030db47f1f536f04a313325f301ce7d4b65c19ebd614213811a14e5bf30dbc`。UI的保存错误优先显示而遮住dirty文案，记录直接的保存前未保存、保存后旧值/目标/修订/活进程及同修订恢复保存；不假称采到内部布尔。测试副本的应用/数据DACL均已恢复，不更改用户原偏好或旧P5数据。

最终交接第六候选只修正上述CRT大小写分类说明，运行门禁与生命周期不变；与第五候选仅录制脚本和派生manifest/SHA两件不同，32件运行产品/模块/声明逐SHA相同。没有重建或替换运行库，亦不将第五候选整包SHA冒作第六候选完整实跑。实际19份完整加载快照以最终函数静态重放，分类改正且原verified/missing/wrong保持；另7份启动未齐样本明确不计通过。见[重放原件](../build/p6-command-recorder-case-label-replay-1.json)、[最终字节关联](../build/p6-2-records/runtime-association-6.json)。原D1的30件运行库/模块/声明、五启动负例的业务输入也逐SHA关联，证据各保原适用范围。第六候选已另用stock PS5.1真实解压/搬迁及独立验包通过，见[验包](../build/p6-2-解压搬迁6𠮷/verification.json)。

当前交接物位于[交接目录](../build/p6-2-clean-machine-handoff-final/)，包版本`0.1.5-preview.2`，139文件、ZIP 16,667,548字节，SHA `d236f5965bccf124eabde8fa0f1539fd910e2eff33a432affbea0174d18c7168`；manifest SHA `06a5bbbf766f88a1872b452f7777a468ad5afc4db83855009ec70b7af154a3df`。包外[验收说明](../build/p6-2-clean-machine-handoff-final/验收说明.txt)SHA `d740bd0415900d129286de0bd8708fd776ade3a8aee6ca59471e2df4c8a8a522`，给出普通用户无需Python的三段PS命令、四窗口顺序、配置冲突与模块候选选择、权限恢复及完整回传目录。`handoff.json`仍明确cleanMachineVerified=false/P6Stage2Complete=false；不在说明或清单中宣称人工验收已经发生。

工具验证：当前打包34/34通过，[源码/命令/日志绑定](../build/p6-2-records/package-current-units-1.json)；最终UI驱动15/15纯用例通过，见[当前原件](../build/p6-2-preview-ui-output-scope-units-1.json)。ACL19例针对同代码的编码前版本，最终stock PS5.1 UTF8-BOM/CRLF helper另实际Prepare→Protect→Probe→Restore通过，保留原范围而非冒称19例在新SHA重跑。桌面记录器最终PS用例与阶段独立审计见下方补充记录。P6-2独立机器的当前包四进程、加载/保存/退出原件及新人工回执仍缺；只提交本机/交接子段，不将本段提交当P6-2或P6完成。D12更新恢复、D14分层CI、D15独立原件归档、D16最终交付均未实施。


最终录制器`99d1aab5…`及当前三份PS/测试源before=after，stock PS5.1完整14/14通过，[命令/摘要](../build/p6-2-records/desktop-final-units-1.json)及对应原stdout/stderr保留。第五候选[独立桌面审计](../build/p6-2-records/independent-desktop5-audit.json)SHA `b861924c206380a217fd926dd6c6fb34fc626826d347e7309bfcd482d355e5f7`，201项检查及277份原件SHA通过；第四候选审计另存历史，最终本机结论采用第五候选的无续做四流程。配置host、打包新例外与九项资产另经非作者交叉审；旧938源与6c未改。最终大小写修正只变分类表达式，逆替后整文件精确回到已完整实跑的`8e9db86c…`；没有因此重复无关P5矩阵或重建DLL。后续首次跨机验收使用第六候选准确摘要，完成后另审D11并收口P6-2。
