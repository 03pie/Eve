# P4 原生插件模块开发

日期：2026-10-29。当前 **P4 进行中，M1/M2 已完成，M3 请求/资源桥进行中**；P1/P2/P3 及 UI 专项的既有收口不撤销。M1 为 59b7c76，M2a 为 249a425，M2b 为 fed5a51，均已审核提交；P5 未开始。

M3a 请求组件已审核提交为 1ffa0ff，M3b 生命周期已审核提交为 f32191b；当前 M3c1 dispatch/effect 资源桥已审核修复并完成最终验证，随后 M3c2 服务、M3d 联合验收按顺序审核提交，见 [M3 记录](p4-m3-development.md)。每段确认无未解决问题后提交，再继续下一段；M4 UI DLL 仍是 P4 必需交付。

此前 M2b 交付：完整业务表签名、所有权/能力规则、无 Qt 协商器及纯 C/真实 DLL 夹具；最终 MSVC Debug/Release 各 497/497、GCC 223/223、ASan 与关闭示例 P4 各 38/38，见 [M2b 记录](p4-m2b-development.md)和 [ADR-0031](adr/0031-business-abi.md)。该段是 M2b 历史，不覆盖本页当前 M3 状态。

此前 M2a 交付：真实装载、只读描述 ABI 与默认驻留。审查修复过早发布 Loaded 及异常选项处理；MSVC Debug/Release 各 490/490、GCC 217/217，ASan 与关闭示例的 P4 专项各 31/31，见 [M2a 记录](p4-m2-development.md)。下文交付和验证保留 M1 的历史证据，未实施描述均指当时状态。

## 阶段与完成条件

| 工作包 | 内容 | 当前状态 | 完成条件 |
|---|---|---|---|
| P4-M1 | 声明、路径/摘要预检、宿主引用账目 | 已完成 | 真实文件、越界、预算、租约/线程/排空测试及工具链回归通过，见下表 |
| P4-M2 | 原生装载、ABI 声明与握手 | 已完成（装载/协商层） | M2a 驻留发现 + M2b 完整签名/能力/所有权、C/C++ 布局及真实 DLL 握手；生产桥见 M3 |
| P4-M3 | 请求/资源桥与服务 DLL | 进行中（M3c1 完成，M3c2 待实施） | M3a/M3b 已提交，M3c1 dispatch/effect 已完成；M3c2/M3d 服务与联合桥、两个服务 DLL、多实例及 Kernel/Session 联合闭环、失败驻留 |
| P4-M4 | 同工具链 UI DLL 与 P4 收口 | 未开始 | 工具链检查、UI 工厂完整退出、模块诊断、A/S06/S07 与原生验收映射 |

M1 独立可用，但不替代设计说明书对整个 P4 的服务 DLL、UI DLL 和 ABI 验收要求。架构及限制见 [ADR-0029](adr/0029-module-preflight-ledger.md)。

## 本轮交付

- sdk/include/cordis/module/manifest.hpp、src/module/manifest.cpp：严格 JSON 声明与只读预检。
- sdk/include/cordis/module/ledger.hpp、src/module/ledger.cpp：十类执行引用、唯一租约、排空及永久驻留原因。
- examples/qt/module_inspect.cpp：命令行诊断，不打开 GUI、不加载 DLL、不修改插件配置。
- tests/unit/module_ledger.cpp、tests/qt/module_manifest.cpp、tests/module_junction.ps1：行为测试及原生路径回归；manifest 公开头同时进入无 Qt 构建的独立包含检查。

没有修改 sources/、research/ 或第三方源码；Core/UI/Loader 的既有行为不变，模块目标独立接入。尚未提供 ModuleManager、ABI HostTable、插件安装、服务 DLL 或物理卸载。

## 复现与修复

第一轮 MSVC 严格警告发现指针宽度检查使用普通常量 if，已改为 C++17 if constexpr；未关闭警告。第一轮 MSVC Release 新增 11 项及无 Qt GCC 5 项通过。

随后真实 Windows junction 复现发现，仅使用 Qt canonicalFilePath 会误接受 plugins/escape → plugins-other 的根外文件。修正为 Windows 句柄最终路径，并增加永久回归：根外 junction 拒绝、显式根目录别名和根内 junction 接受。修复前诊断工具错误返回 0 的证据保留于 build/p4-junction-check.log。

提交前审查另发现并修复缺盘符根路径误接受、最终路径校验后按字符串重开来源，以及关闭示例时 junction 检查缺失。新增 root_guards/sharing 在修复前 0/2、修复后 2/2；现在路径/字节/元数据来自同一个仅允许读取共享的句柄，持有到函数返回，并保留原生错误编号。中文、扩展路径、普通只读共享及写入冲突解除后的重试均通过。详情见 [审查记录](code-review.md#p4-m1-模块预检与宿主账目提交前审查)。

## 验证

| 检查 | 最终结果 | 日志 |
|---|---|---|
| 原目录 Ela/MSVC Release 全量 | 473/473 通过 | build/p4-review-release-tests.log |
| 原目录 Ela/MSVC Debug 全量 | 473/473 通过 | build/p4-review-debug-tests.log |
| 无 Qt GCC Debug 全量 | 215/215 通过，含 5 项账目及 manifest 公开头独立包含 | build/p4-review-gcc-tests.log |
| MSVC ASan P4 专项 | 14/14 通过，六个新增编译单元的 ASan 参数已核对 | build/p4-review-asan-tests.log |
| 关闭示例的 Qt/MSVC Debug | P4 专项 14/14，包括独立探针的 junction；检查后恢复原示例开关 | build/p4-review-no-examples-tests.log |
| 编码、接口与依赖边界 | 遵循仓库编码/换行规则、文档链接/锚点及 git diff --check 通过；Core/账目不引入 Qt/Ela，公开头不引入 Qt/Windows | build/p4-review-audit.log |
| 最终源快照 | 13 个源/构建/脚本文件冻结；审查修改四项源/构建文件，另规范化一项脚本格式 | build/p4-review-source-sha256.json |

当前工具链为 Windows x64、Qt 5.15.2 msvc2019_64、MSVC 19.44/v143；GCC 13.1 用于无 Qt 路径。这些结果不声明 DLL ABI、物理卸载、Qt 二进制或外部 UI 插件兼容性已经通过。此次没有桌面界面改动，不重复索取此前 UI 人工确认。

测试名称：p4.ledger_categories / ownership / pins / guards / threads，p4.manifest_schema / duplicates / library_names / files / paths / limits / root_guards / sharing / junction。使用真实临时文件，包括跨块读取、单字节篡改、文件缺失/空文件/目录、精确预算边界、非法 UTF-8、转义重复键、租约转移及跨线程释放。文件内容可为合成字节，因为该步骤只验证声明和摘要，不验证可执行文件格式。

逐项映射见 [P4 验收单](../tests/acceptance/p4-module.md)。原生 junction 已由 CTest 在 Debug/Release/ASan 三套构建运行，脚本用 .NET SHA256 避免依赖特定 PowerShell 模块；失败退出仍必须属于路径阶段，不能被别的错误冒充拒绝成功。

首轮 471/471、ASan 12/12 及源快照保留在 build/p4-*-tests.log / p4-source-sha256.json，仅代表审查前版本；本表的 p4-review-* 才是提交版本依据。

末次规范检查按 [.editorconfig](../.editorconfig) 将 PowerShell 脚本改为 UTF-8 BOM/CRLF，其余文件保持 UTF-8/LF。解码后的脚本与全量测试时逐字一致；随后 junction 在 Release/Debug/ASan 各补验 1/1，关闭示例的探针也通过，见 build/p4-review-script-*.log。首次 ASan 定向调用缺少 VS 运行库路径，返回 DLL 未找到，失败日志保留于 p4-review-script-asan-missing-runtime.log；加载与全量检查相同的 VS 环境后通过，没有修改代码或放宽断言。

## 本机使用

仍使用原 Release 目录。目标构建后：

```powershell
$env:PATH = 'E:/Qt/5.15.2/msvc2019_64/bin;' + $env:PATH
./build/ela-msvc-release/bin/cordis_module_inspect.exe D:/plugins example/module.json
```

第一个参数是明确的插件根，第二个可相对根目录。成功输出模块、版本、产物最终路径、SHA256、大小和插件声明；失败退出 1，参数缺失退出 2。成功文字明确显示 library not loaded。

声明格式沿用 [设计说明书 §11.1](../Cordis插件平台-软件设计说明书.md#111-manifest)，sha256 必须替换为真实文件摘要。该入口不向当前 Home 注册插件。
